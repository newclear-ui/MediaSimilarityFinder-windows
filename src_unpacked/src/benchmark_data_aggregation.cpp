#include "benchmark_data_aggregation.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace msf {

namespace {

constexpr double kRoundingScale = 1000000.0;  // 6 decimal places, see aggregationRoundMs

SampleExclusion exclusionForStatus(BenchmarkStatus s) {
    switch (s) {
        case BenchmarkStatus::Skipped: return SampleExclusion::Skipped;
        case BenchmarkStatus::Failed: return SampleExclusion::Failed;
        case BenchmarkStatus::Cancelled: return SampleExclusion::Cancelled;
        case BenchmarkStatus::Success: return SampleExclusion::None;
    }
    return SampleExclusion::Invalid;
}

// Accumulates eligible samples, then summarises them once at the end.
//
// Samples are held in a vector and sorted at the end, so a summary cannot depend on
// the order cohorts happened to arrive in.
class SampleAccumulator {
public:
    void add(double v) { samples_.push_back(v); }
    std::size_t size() const { return samples_.size(); }

    DurationStatistics finish(MetricLevel level, MetricResolution resolution) const {
        DurationStatistics st;
        st.level = level;
        st.resolution = resolution;
        st.sampleCount = samples_.size();
        if (samples_.empty()) return st;

        std::vector<double> s = samples_;
        std::sort(s.begin(), s.end());

        st.minMs = aggregationRoundMs(s.front());
        st.maxMs = aggregationRoundMs(s.back());

        double sum = 0.0;
        for (double v : s) sum += v;
        st.meanMs = aggregationRoundMs(sum / static_cast<double>(s.size()));

        const std::size_t n = s.size();
        st.medianMs =
            aggregationRoundMs((n % 2 == 1) ? s[n / 2] : (s[n / 2 - 1] + s[n / 2]) / 2.0);

        // Nearest rank, 1-based: index = ceil(0.95 * n) - 1.
        auto rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(n)));
        if (rank < 1) rank = 1;
        if (rank > n) rank = n;
        st.p95Ms = aggregationRoundMs(s[rank - 1]);
        return st;
    }

private:
    std::vector<double> samples_;
};

// One cell of the hierarchy, holding every population meaningful at its level.
//
// The same struct serves dataset, scope and build level so a rollup is never
// assembled from statistics afterwards. Summing a mean is meaningless, so each level
// accumulates its own samples instead.
//
// The three levels do not share an eligibility rule, because the model does not:
//
//   mode  elapsedMs is optional. Eligible iff Success AND a duration exists. A
//         Success with no duration is MissingElapsed, never read as 0 ms.
//
//   case  elapsedMs is a plain double in the normalized record, so a duration always
//         exists and MissingElapsed cannot occur here. Status decides alone.
//
//   run   wallDurationMs is optional and derived. Eligible iff Success and a wall
//         duration exists. A run with no terminal record has no status at all, which
//         is Invalid rather than an assumed Success.
struct Cell {
    SampleAccumulator caseAcc;
    SampleAccumulator runAcc;
    SampleAccounting caseAcct;
    SampleAccounting runAcct;
    std::vector<StatusTally> caseStatuses;
    std::vector<StatusTally> runStatuses;

    // Runs whose terminal record never appeared. Their status is unknown, so they are
    // counted here rather than being tallied as any existing status value.
    std::size_t runsWithoutStatus = 0;

    // Populated at build level only.
    std::map<ModeCohortKey, std::pair<SampleAccumulator, SampleAccounting>> modes;
    std::map<ModeCohortKey, std::vector<StatusTally>> modeStatusByKey;
};

void addStatus(std::vector<StatusTally>& t, BenchmarkStatus s) {
    for (StatusTally& e : t) {
        if (e.status == s) { ++e.count; return; }
    }
    t.push_back(StatusTally{s, 1});
    std::sort(t.begin(), t.end(),
              [](const StatusTally& a, const StatusTally& b) { return a.status < b.status; });
}

void admitMode(const IngestModeResult& m, SampleAccumulator& acc, SampleAccounting& acct) {
    acct.observed++;
    if (m.status != BenchmarkStatus::Success) {
        ++acct.excluded;
        acct.addExcluded(exclusionForStatus(m.status));
        return;
    }
    if (!m.elapsedMs.has_value()) {
        ++acct.excluded;
        acct.addExcluded(SampleExclusion::MissingElapsed);
        return;
    }
    ++acct.eligible;
    acc.add(*m.elapsedMs);
}

void admitCase(const IngestCase& c, SampleAccumulator& acc, SampleAccounting& acct) {
    acct.observed++;
    if (c.status != BenchmarkStatus::Success) {
        ++acct.excluded;
        acct.addExcluded(exclusionForStatus(c.status));
        return;
    }
    ++acct.eligible;
    acc.add(c.elapsedMs);
}

void admitRun(const IngestRun& r, SampleAccumulator& acc, SampleAccounting& acct) {
    acct.observed++;
    if (!r.runStatus.has_value()) {
        ++acct.excluded;
        acct.addExcluded(SampleExclusion::Invalid);
        return;
    }
    if (*r.runStatus != BenchmarkStatus::Success) {
        ++acct.excluded;
        acct.addExcluded(exclusionForStatus(*r.runStatus));
        return;
    }
    if (!r.wallDurationMs.has_value()) {
        ++acct.excluded;
        acct.addExcluded(SampleExclusion::MissingElapsed);
        return;
    }
    ++acct.eligible;
    acc.add(*r.wallDurationMs);
}

// Feeds one run into every level it belongs to.
//
// The walk goes run -> case -> mode over the NORMALIZED data, which already holds one
// record per logical entity. The real store contains suites written more than once,
// and averaging raw journal lines would weight a repeatedly-written suite above an
// identical one written once; this is why the population is logical entities.
void placeRun(const IngestRun& run, Cell& dataset, Cell& scope, Cell& build) {
    Cell* levels[3] = {&dataset, &scope, &build};
    for (Cell* cell : levels) {
        admitRun(run, cell->runAcc, cell->runAcct);
        if (run.runStatus.has_value())
            addStatus(cell->runStatuses, *run.runStatus);
        else
            ++cell->runsWithoutStatus;
    }

    for (const IngestCase& c : run.cases) {
        for (Cell* cell : levels) {
            admitCase(c, cell->caseAcc, cell->caseAcct);
            addStatus(cell->caseStatuses, c.status);
        }
        for (const IngestModeResult& m : c.modes) {
            const ModeCohortKey mk{m.requestedMode, m.effectiveMode};
            auto& slot = build.modes[mk];
            admitMode(m, slot.first, slot.second);
            addStatus(build.modeStatusByKey[mk], m.status);
        }
    }
}

struct BuildNode { Cell cell; };
struct ScopeNode {
    Cell cell;
    std::map<BuildCohortKey, BuildNode> builds;
};
struct DatasetNode {
    Cell cell;
    std::map<ScopeCohortKey, ScopeNode> scopes;
};

DurationStatistics caseStats(const Cell& c) {
    return c.caseAcc.finish(MetricLevel::CaseElapsed, MetricResolution::Recorded);
}
DurationStatistics runStats(const Cell& c) {
    return c.runAcc.finish(MetricLevel::RunWallDuration, MetricResolution::OneSecond);
}

// Adds one accounting block into another, keeping the exclusion tally.
//
// Counts are additive, so merging is exact and the observed == eligible + excluded
// invariant is preserved as long as each part already held.
void mergeInto(SampleAccounting& dst, const SampleAccounting& src) {
    dst.observed += src.observed;
    dst.eligible += src.eligible;
    dst.excluded += src.excluded;
    for (const SampleExclusionTally& t : src.exclusions) {
        bool found = false;
        for (SampleExclusionTally& e : dst.exclusions) {
            if (e.reason == t.reason) { e.count += t.count; found = true; break; }
        }
        if (!found) dst.exclusions.push_back(t);
    }
    std::sort(dst.exclusions.begin(), dst.exclusions.end(),
              [](const SampleExclusionTally& a, const SampleExclusionTally& b) {
                  return a.reason < b.reason;
              });
}

SampleAccounting mergeAccounting(const std::vector<const SampleAccounting*>& parts) {
    SampleAccounting out;
    for (const SampleAccounting* p : parts) mergeInto(out, *p);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------

const char* metricLevelName(MetricLevel l) {
    switch (l) {
        case MetricLevel::ModeElapsed: return "mode-elapsed";
        case MetricLevel::CaseElapsed: return "case-elapsed";
        case MetricLevel::RunWallDuration: return "run-wall-duration";
    }
    return "unknown";
}

const char* metricResolutionName(MetricResolution r) {
    switch (r) {
        case MetricResolution::Recorded: return "Recorded";
        case MetricResolution::OneSecond: return "OneSecond";
    }
    return "unknown";
}

const char* sampleExclusionName(SampleExclusion r) {
    switch (r) {
        case SampleExclusion::None: return "none";
        case SampleExclusion::Skipped: return "skipped";
        case SampleExclusion::Failed: return "failed";
        case SampleExclusion::Cancelled: return "cancelled";
        case SampleExclusion::MissingElapsed: return "missingElapsed";
        case SampleExclusion::Invalid: return "invalid";
    }
    return "unknown";
}

double aggregationRoundMs(double v) {
    if (!std::isfinite(v)) return 0.0;
    return std::round(v * kRoundingScale) / kRoundingScale;
}

void SampleAccounting::addExcluded(SampleExclusion reason) {
    if (reason == SampleExclusion::None) return;
    for (SampleExclusionTally& e : exclusions) {
        if (e.reason == reason) { ++e.count; return; }
    }
    exclusions.push_back(SampleExclusionTally{reason, 1});
    std::sort(exclusions.begin(), exclusions.end(),
              [](const SampleExclusionTally& a, const SampleExclusionTally& b) {
                  return a.reason < b.reason;
              });
}

// ---------------------------------------------------------------------------

BenchmarkAggregation aggregateBenchmarks(const NormalizedBenchmarkData& data,
                                          const BenchmarkGrouping& grouping) {
    BenchmarkAggregation out;

    // Whole-dataset populations, accumulated beside the hierarchy in the same pass so
    // the two cannot drift. These are NOT produced by summing cohort statistics: a
    // mean of means is not a mean.
    Cell global;
    std::map<ModeCohortKey, std::pair<SampleAccumulator, SampleAccounting>> globalModes;

    std::map<DatasetCohortKey, DatasetNode> tree;

    for (const IngestRun& run : data.runs) {
        const bool hasDataset = run.datasetIdentity == DatasetIdentityState::Valid &&
                                run.datasetFingerprint.has_value() &&
                                !run.datasetFingerprint->empty();
        if (!hasDataset) {
            // Counted and excluded. Such a run is already absent from the S6-3
            // cohorts, so there is no cohort to add it to, and inventing one would
            // make "same dataset" a claim the data does not support.
            ++out.runsOutsideDatasetScope;
            out.caseRecordsOutsideDatasetScope += run.cases.size();
            for (const IngestCase& c : run.cases)
                out.modeRecordsOutsideDatasetScope += c.modes.size();
            admitRun(run, global.runAcc, global.runAcct);
            if (run.runStatus.has_value())
                addStatus(global.runStatuses, *run.runStatus);
            else
                ++global.runsWithoutStatus;
            continue;
        }

        const DatasetCohortKey dKey{*run.datasetFingerprint};
        // "all" is carried through as its own scope. It is never decomposed into
        // images+videos, and its elapsed time is never distributed between them,
        // because nothing records how many cases of each a run actually covered.
        const ScopeCohortKey sKey{*run.datasetFingerprint,
                                  run.mediaScope.value_or(std::string())};

        BuildCohortKey bKey;
        bKey.buildVersion = run.buildVersion.value_or(std::string());
        bKey.gitCommitState = run.gitCommitState;
        bKey.gitCommit = (run.gitCommitState == GitCommitState::Known && run.gitCommit.has_value())
                             ? *run.gitCommit
                             : std::string();

        DatasetNode& d = tree[dKey];
        ScopeNode& s = d.scopes[sKey];
        BuildNode& b = s.builds[bKey];

        placeRun(run, d.cell, s.cell, b.cell);

        // The whole-dataset run population is accumulated here too, not only on the
        // no-dataset-identity path below, so it covers every run exactly once.
        admitRun(run, global.runAcc, global.runAcct);
        if (run.runStatus.has_value())
            addStatus(global.runStatuses, *run.runStatus);
        else
            ++global.runsWithoutStatus;

        for (const IngestCase& c : run.cases) {
            for (const IngestModeResult& m : c.modes) {
                auto& slot = globalModes[ModeCohortKey{m.requestedMode, m.effectiveMode}];
                admitMode(m, slot.first, slot.second);
            }
        }
    }

    // --- case-level view ----------------------------------------------------
    //
    // Per-file, across runs. Separate from the cohort hierarchy because per-file
    // comparison is a different question from per-cohort comparison.
    // Keys a case cohort by the pair that is actually unique, then reports how many
// runIds are shared so the caller can see the ambiguity instead of inheriting it.
//
// grouping.caseCohorts supplies the cohort keys, which are (dataset, caseId) and
// therefore unambiguous. The runs behind them are recovered from the normalized data
// rather than from CaseCohort::runIds, because that list is keyed by runId alone and
// runId is not unique in a real store.
{
        std::map<std::string, std::size_t> journalRuns;  // (journal, runId) -> 1
        std::map<std::string, std::size_t> runIds;
        for (const IngestRun& r : data.runs) {
            journalRuns[r.sourceJournalPath + "\x1f" + r.runId] += 1;
            runIds[r.runId] += 1;
        }
        out.distinctRunIds = runIds.size();
        for (const auto& [id, n] : runIds)
            if (n > 1) out.runsWithCollidingIdentity += n;

        std::map<CaseCohortKey, AggregatedCase> cases;
        std::map<CaseCohortKey, SampleAccumulator> accs;
        std::map<CaseCohortKey, SampleAccounting> accts;
        SampleAccumulator globalCaseAcc;

        for (const CaseCohort& cohort : grouping.caseCohorts) cases[cohort.key];  // seed keys
        for (const CaseCohort& cohort : grouping.caseCohorts)
            if (!cohort.path.empty() && cases[cohort.key].path.empty())
                cases[cohort.key].path = cohort.path;

        for (const IngestRun& run : data.runs) {
            const bool hasDataset = run.datasetIdentity == DatasetIdentityState::Valid &&
                                    run.datasetFingerprint.has_value() &&
                                    !run.datasetFingerprint->empty();
            if (!hasDataset) continue;
            for (const IngestCase& c : run.cases) {
                const CaseCohortKey key{*run.datasetFingerprint, c.caseId};
                AggregatedCase& ac = cases[key];
                ac.key = key;
                if (ac.path.empty()) ac.path = c.path;
                addStatus(ac.statuses, c.status);
                admitCase(c, accs[key], accts[key]);
                admitCase(c, globalCaseAcc, global.caseAcct);
            }
        }

        for (auto& [k, v] : cases) {
            v.accounting = accts[k];
            v.elapsed = accs[k].finish(MetricLevel::CaseElapsed, MetricResolution::Recorded);
            out.cases.push_back(std::move(v));
        }
    }

    // --- materialise the hierarchy ------------------------------------------
    for (auto& [dKey, dNode] : tree) {
        AggregatedDataset ds;
        ds.key = dKey;
        for (auto& [sKey, sNode] : dNode.scopes) {
            AggregatedScope sc;
            sc.key = sKey;
            for (auto& [bKey, bNode] : sNode.builds) {
                AggregatedBuild ab;
                ab.key = bKey;
                // Provenance quality, not a comparability verdict. Legacy and Unknown
                // never merge with a Known commit, and Legacy + Legacy is not assumed
                // to mean the same binary.
                ab.commitComparable = (bKey.gitCommitState == GitCommitState::Known);
                for (auto& [mKey, slot] : bNode.cell.modes) {
                    AggregatedMode am;
                    am.key = mKey;
                    auto st = bNode.cell.modeStatusByKey.find(mKey);
                    if (st != bNode.cell.modeStatusByKey.end()) am.statuses = st->second;
                    am.accounting = slot.second;
                    am.elapsed =
                        slot.first.finish(MetricLevel::ModeElapsed, MetricResolution::Recorded);
                    ab.modes.push_back(std::move(am));
                }
                ab.runStatuses = bNode.cell.runStatuses;
                ab.runAccounting = bNode.cell.runAcct;
                ab.runWallDuration = runStats(bNode.cell);
                sc.builds.push_back(std::move(ab));
            }
            sc.caseStatuses = sNode.cell.caseStatuses;
            sc.caseAccounting = sNode.cell.caseAcct;
            sc.caseElapsed = caseStats(sNode.cell);
            sc.runStatuses = sNode.cell.runStatuses;
            sc.runAccounting = sNode.cell.runAcct;
            sc.runWallDuration = runStats(sNode.cell);
            ds.scopes.push_back(std::move(sc));
        }
        ds.caseStatuses = dNode.cell.caseStatuses;
        ds.caseAccounting = dNode.cell.caseAcct;
        ds.caseElapsed = caseStats(dNode.cell);
        ds.runStatuses = dNode.cell.runStatuses;
        ds.runAccounting = dNode.cell.runAcct;
        ds.runWallDuration = runStats(dNode.cell);
        out.datasets.push_back(std::move(ds));
    }

    // --- top-level populations ---------------------------------------------
    //
    // Counts are merged; statistics are never re-derived from counts, because they
    // are already summarised per cohort. The accounting invariant
    // observed == eligible + excluded must survive the merge.
    {
        std::vector<const SampleAccounting*> modeParts;
        for (auto& [k, slot] : globalModes) modeParts.push_back(&slot.second);
        out.modePopulation = mergeAccounting(modeParts);
        out.casePopulation = global.caseAcct;
        out.runPopulation = global.runAcct;
    }
    return out;
}

}  // namespace msf