#include "benchmark_data_comparison.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace msf {

namespace {

// Stable ordering for a side, so a pair's left and right are the same on every run and a
// pair is never emitted twice.
//
// The metric, mode and provenance fields all participate, because two sides that differ
// in any of them are different observations rather than the same one counted twice.
//
// Built with make_tuple rather than std::tie: the cast results are temporaries, and tie
// only binds lvalues.
auto sideSortKey(const ComparisonSide& s) {
    return std::make_tuple(s.datasetFingerprint, s.mediaScope, static_cast<int>(s.metricLevel),
                           static_cast<int>(s.metricResolution),
                           static_cast<int>(s.requestedMode), static_cast<int>(s.effectiveMode),
                           s.buildVersion, static_cast<int>(s.gitCommitState), s.gitCommit,
                           s.eligibleSamples);
}

void addRejection(std::vector<RejectedComparison>& out, std::vector<EligibilityTally>& tally,
                  ComparisonDimension dim, ComparisonEligibility reason,
                  const ComparisonSide& left, const ComparisonSide& right) {
    RejectedComparison r;
    r.dimension = dim;
    r.reason = reason;
    r.datasetFingerprint = left.datasetFingerprint;
    r.mediaScope = left.mediaScope;
    r.metricLevel = left.metricLevel;
    r.leftLabel = left.buildVersion + "/" + std::string(msf::gpuBackendKindName(left.requestedMode)) +
                  ">" + msf::gpuBackendKindName(left.effectiveMode);
    r.rightLabel = right.buildVersion + "/" + std::string(msf::gpuBackendKindName(right.requestedMode)) +
                   ">" + msf::gpuBackendKindName(right.effectiveMode);
    r.leftSampleCount = left.eligibleSamples;
    r.rightSampleCount = right.eligibleSamples;
    out.push_back(std::move(r));

    for (EligibilityTally& t : tally) {
        if (t.reason == reason) { ++t.count; return; }
    }
    tally.push_back(EligibilityTally{reason, 1});
    std::sort(tally.begin(), tally.end(), [](const EligibilityTally& a, const EligibilityTally& b) {
        return a.reason < b.reason;
    });
}

// The three journal fields that would make a comparison controlled, none of which exist.
bool sameBuildVersionOnly(const ComparisonSide& a, const ComparisonSide& b) {
    return a.buildVersion == b.buildVersion && a.gitCommit == b.gitCommit &&
           a.gitCommitState == b.gitCommitState;
}

}  // namespace

// ---------------------------------------------------------------------------

const char* comparisonEligibilityName(ComparisonEligibility e) {
    switch (e) {
        case ComparisonEligibility::Eligible: return "eligible";
        case ComparisonEligibility::MismatchedDataset: return "mismatched-dataset";
        case ComparisonEligibility::MismatchedScope: return "mismatched-scope";
        case ComparisonEligibility::MismatchedMetric: return "mismatched-metric";
        case ComparisonEligibility::MismatchedMode: return "mismatched-mode";
        case ComparisonEligibility::MissingProvenance: return "missing-provenance";
        case ComparisonEligibility::SameProvenance: return "same-provenance";
        case ComparisonEligibility::NoComparableSamples: return "no-comparable-samples";
        case ComparisonEligibility::InsufficientData: return "insufficient-data";
    }
    return "unknown";
}

const char* comparisonDimensionName(ComparisonDimension d) {
    switch (d) {
        case ComparisonDimension::BuildProvenance: return "build-provenance";
        case ComparisonDimension::ModeSemantics: return "mode-semantics";
    }
    return "unknown";
}

bool ComparisonLimitations::empty() const {
    return !distanceUnavailable && !resourcePolicyUnavailable && !gpuBackendUnavailable &&
           !controlledEnvironmentUndefined && !processIsolationUncontrolled &&
           !filesystemCacheUncontrolled && !repeatedRunsInsufficient &&
           !runWallDurationOneSecondResolution && !cancellationUnobserved;
}

std::size_t ComparisonLimitations::count() const {
    std::size_t n = 0;
    if (distanceUnavailable) ++n;
    if (resourcePolicyUnavailable) ++n;
    if (gpuBackendUnavailable) ++n;
    if (controlledEnvironmentUndefined) ++n;
    if (processIsolationUncontrolled) ++n;
    if (filesystemCacheUncontrolled) ++n;
    if (repeatedRunsInsufficient) ++n;
    if (runWallDurationOneSecondResolution) ++n;
    if (cancellationUnobserved) ++n;
    return n;
}

bool comparisonCandidateLess(const ComparisonCandidate& a, const ComparisonCandidate& b) {
    if (a.dimension != b.dimension) return a.dimension < b.dimension;
    const auto ka = std::make_tuple(a.datasetFingerprint, a.mediaScope,
                                    static_cast<int>(a.metricLevel), sideSortKey(a.left),
                                    sideSortKey(a.right));
    const auto kb = std::make_tuple(b.datasetFingerprint, b.mediaScope,
                                    static_cast<int>(b.metricLevel), sideSortKey(b.left),
                                    sideSortKey(b.right));
    return ka < kb;
}

// ---------------------------------------------------------------------------

ComparisonEligibility evaluateComparison(const ComparisonSide& left, const ComparisonSide& right,
                                          ComparisonDimension dimension, bool usableForDimension) {
    // Order matters and is deliberate. Identity of the population is checked before
    // anything else, because "different dataset" is the more fundamental reason and a
    // caller should not have to know that.
    if (left.datasetFingerprint.empty() || right.datasetFingerprint.empty()) {
        return ComparisonEligibility::MismatchedDataset;
    }
    if (left.datasetFingerprint != right.datasetFingerprint) {
        return ComparisonEligibility::MismatchedDataset;
    }
    if (left.mediaScope != right.mediaScope) return ComparisonEligibility::MismatchedScope;
    if (left.metricLevel != right.metricLevel || left.metricResolution != right.metricResolution) {
        return ComparisonEligibility::MismatchedMetric;
    }
    if (!usableForDimension) return ComparisonEligibility::InsufficientData;

    if (dimension == ComparisonDimension::BuildProvenance) {
        // requested and effective mode must agree, so a CPU run is never compared
        // against an AUTO run that happened to resolve to CPU.
        if (left.requestedMode != right.requestedMode || left.effectiveMode != right.effectiveMode) {
            return ComparisonEligibility::MismatchedMode;
        }
        if (sameBuildVersionOnly(left, right)) return ComparisonEligibility::SameProvenance;
        // Commit-level comparison needs a Known commit on both sides. Legacy predates the
        // field and Unknown means the writer had it and did not fill it; neither is
        // evidence of a shared or differing binary.
        if (!left.commitComparable() || !right.commitComparable()) {
            return ComparisonEligibility::MissingProvenance;
        }
        if (left.eligibleSamples == 0 || right.eligibleSamples == 0) {
            return ComparisonEligibility::NoComparableSamples;
        }
        return ComparisonEligibility::Eligible;
    }

    // ModeSemantics: the build must be identical, or the mode difference is confounded
    // by a build difference.
    if (!sameBuildVersionOnly(left, right)) return ComparisonEligibility::MismatchedMode;
    if (left.requestedMode == right.requestedMode && left.effectiveMode == right.effectiveMode) {
        return ComparisonEligibility::SameProvenance;
    }
    if (left.eligibleSamples == 0 || right.eligibleSamples == 0) {
        return ComparisonEligibility::NoComparableSamples;
    }
    return ComparisonEligibility::Eligible;
}

// ---------------------------------------------------------------------------

ComparisonAnalysis findComparisonCandidates(const NormalizedBenchmarkData& data,
                                            const BenchmarkAggregation& aggregation) {
    ComparisonAnalysis out;
    out.limitations = ComparisonLimitations();

    // --- run references ----------------------------------------------------
    //
    // Grouped by the same cohort keys S6-3 uses, so the runs behind a candidate are
    // traceable without a second grouping scheme. groupingKey() is S6-2's own key
    // material, so nothing is re-derived here.
    std::map<std::tuple<std::string, std::string, std::string, int, std::string>,
             std::set<RunReference>>
        runsByCohort;
    std::set<std::string> runIds;
    {
        std::set<RunReference> allRefs;
        std::map<std::string, std::size_t> idCounts;
        for (const IngestRun& run : data.runs) {
            const RunGroupingKey k = groupingKey(run);
            if (!k.datasetFingerprint.has_value() || k.datasetFingerprint->empty()) continue;
            const std::string scope = k.mediaScope.value_or(std::string());
            const std::string build = k.buildVersion.value_or(std::string());
            const std::string commit =
                (k.gitCommitState == GitCommitState::Known && k.gitCommit.has_value())
                    ? *k.gitCommit
                    : std::string();
            runsByCohort[{*k.datasetFingerprint, scope, build, static_cast<int>(k.gitCommitState),
                          commit}]
                .insert(RunReference{run.sourceJournalPath, run.runId});
            allRefs.insert(RunReference{run.sourceJournalPath, run.runId});
            idCounts[run.runId] += 1;
        }
        out.runReferences.assign(allRefs.begin(), allRefs.end());
        out.distinctRunIds = idCounts.size();
        for (const auto& [id, n] : idCounts)
            if (n > 1) out.runsWithCollidingIdentity += n;
    }

    auto refsFor = [&](const std::string& fp, const std::string& scope, const std::string& build,
                       GitCommitState gs, const std::string& commit) {
        std::vector<RunReference> v;
        auto it = runsByCohort.find({fp, scope, build, static_cast<int>(gs), commit});
        if (it != runsByCohort.end()) v.assign(it->second.begin(), it->second.end());
        return v;
    };

    // --- generate ----------------------------------------------------------
    for (const AggregatedDataset& ds : aggregation.datasets) {
        for (const AggregatedScope& sc : ds.scopes) {
            const std::string& fp = ds.key.datasetFingerprint;
            const std::string& scope = sc.key.mediaScope;

            // ---- 1. run wall duration, build provenance --------------------
            //
            // RunWallDuration is aggregated per build, so a build-vs-build comparison
            // is representable.
            for (std::size_t i = 0; i < sc.builds.size(); ++i) {
                for (std::size_t j = i + 1; j < sc.builds.size(); ++j) {
                    const AggregatedBuild& A = sc.builds[i];
                    const AggregatedBuild& B = sc.builds[j];
                    ComparisonSide a, b;
                    a.datasetFingerprint = fp;
                    b.datasetFingerprint = fp;
                    a.mediaScope = scope;
                    b.mediaScope = scope;
                    a.metricLevel = b.metricLevel = MetricLevel::RunWallDuration;
                    a.metricResolution = b.metricResolution = MetricResolution::OneSecond;
                    a.buildVersion = A.key.buildVersion;
                    a.gitCommitState = A.key.gitCommitState;
                    a.gitCommit = A.key.gitCommit;
                    a.eligibleSamples = A.runAccounting.eligible;
                    b.buildVersion = B.key.buildVersion;
                    b.gitCommitState = B.key.gitCommitState;
                    b.gitCommit = B.key.gitCommit;
                    b.eligibleSamples = B.runAccounting.eligible;
                    a.runReferences = refsFor(fp, scope, A.key.buildVersion, A.key.gitCommitState,
                                              A.key.gitCommit);
                    b.runReferences = refsFor(fp, scope, B.key.buildVersion, B.key.gitCommitState,
                                              B.key.gitCommit);
                    if (sideSortKey(a) > sideSortKey(b)) std::swap(a, b);

                    ++out.comparisonOpportunities;
                    const ComparisonEligibility e = evaluateComparison(
                        a, b, ComparisonDimension::BuildProvenance, true);
                    if (e == ComparisonEligibility::Eligible) {
                        ComparisonCandidate cand;
                        cand.dimension = ComparisonDimension::BuildProvenance;
                        cand.datasetFingerprint = fp;
                        cand.mediaScope = scope;
                        cand.metricLevel = MetricLevel::RunWallDuration;
                        cand.metricResolution = MetricResolution::OneSecond;
                        cand.left = a;
                        cand.right = b;
                        cand.limitations = out.limitations;
                        out.candidates.push_back(std::move(cand));
                        ++out.eligibleCandidates;
                    } else {
                        ++out.rejectedCandidates;
                        addRejection(out.rejections, out.rejectionReasons,
                                     ComparisonDimension::BuildProvenance, e, a, b);
                    }
                }
            }

            // ---- 2. mode elapsed, build provenance -------------------------
            //
            // For each mode semantics, pair the builds that share it.
            std::map<std::pair<int, int>, std::map<std::size_t, const AggregatedMode*>> byMode;
            for (std::size_t bi = 0; bi < sc.builds.size(); ++bi)
                for (const AggregatedMode& m : sc.builds[bi].modes)
                    byMode[{static_cast<int>(m.key.requestedMode),
                            static_cast<int>(m.key.effectiveMode)}][bi] = &m;

            for (auto& [modeKey, perBuild] : byMode) {
                for (auto& [bi, mA] : perBuild) {
                    for (auto& [bj, mB] : perBuild) {
                        if (bj <= bi) continue;
                        const AggregatedBuild& A = sc.builds[bi];
                        const AggregatedBuild& B = sc.builds[bj];
                        ComparisonSide a, b;
                        a.datasetFingerprint = b.datasetFingerprint = fp;
                        a.mediaScope = b.mediaScope = scope;
                        a.metricLevel = b.metricLevel = MetricLevel::ModeElapsed;
                        a.metricResolution = b.metricResolution = MetricResolution::Recorded;
                        a.requestedMode = static_cast<GpuBackendKind>(modeKey.first);
                        a.effectiveMode = static_cast<GpuBackendKind>(modeKey.second);
                        b.requestedMode = a.requestedMode;
                        b.effectiveMode = a.effectiveMode;
                        a.buildVersion = A.key.buildVersion;
                        a.gitCommitState = A.key.gitCommitState;
                        a.gitCommit = A.key.gitCommit;
                        a.eligibleSamples = mA->accounting.eligible;
                        b.buildVersion = B.key.buildVersion;
                        b.gitCommitState = B.key.gitCommitState;
                        b.gitCommit = B.key.gitCommit;
                        b.eligibleSamples = mB->accounting.eligible;
                        a.runReferences = refsFor(fp, scope, A.key.buildVersion,
                                                  A.key.gitCommitState, A.key.gitCommit);
                        b.runReferences = refsFor(fp, scope, B.key.buildVersion,
                                                  B.key.gitCommitState, B.key.gitCommit);
                        if (sideSortKey(a) > sideSortKey(b)) std::swap(a, b);

                        ++out.comparisonOpportunities;
                        const ComparisonEligibility e = evaluateComparison(
                            a, b, ComparisonDimension::BuildProvenance, true);
                        if (e == ComparisonEligibility::Eligible) {
                            ComparisonCandidate cand;
                            cand.dimension = ComparisonDimension::BuildProvenance;
                            cand.datasetFingerprint = fp;
                            cand.mediaScope = scope;
                            cand.metricLevel = MetricLevel::ModeElapsed;
                            cand.metricResolution = MetricResolution::Recorded;
                            cand.left = a;
                            cand.right = b;
                            cand.limitations = out.limitations;
                            out.candidates.push_back(std::move(cand));
                            ++out.eligibleCandidates;
                        } else {
                            ++out.rejectedCandidates;
                            addRejection(out.rejections, out.rejectionReasons,
                                         ComparisonDimension::BuildProvenance, e, a, b);
                        }
                    }
                }
            }

            // ---- 3. mode elapsed, mode semantics ---------------------------
            //
            // Within ONE build, pair two mode semantics. This answers "is AUTO different
            // from CPU here", which is a different question from "did the build change".
            for (const AggregatedBuild& B : sc.builds) {
                for (std::size_t i = 0; i < B.modes.size(); ++i) {
                    for (std::size_t j = i + 1; j < B.modes.size(); ++j) {
                        const AggregatedMode& MA = B.modes[i];
                        const AggregatedMode& MB = B.modes[j];
                        ComparisonSide a, b;
                        a.datasetFingerprint = b.datasetFingerprint = fp;
                        a.mediaScope = b.mediaScope = scope;
                        a.metricLevel = b.metricLevel = MetricLevel::ModeElapsed;
                        a.metricResolution = b.metricResolution = MetricResolution::Recorded;
                        a.buildVersion = b.buildVersion = B.key.buildVersion;
                        a.gitCommitState = b.gitCommitState = B.key.gitCommitState;
                        a.gitCommit = b.gitCommit = B.key.gitCommit;
                        a.requestedMode = MA.key.requestedMode;
                        a.effectiveMode = MA.key.effectiveMode;
                        b.requestedMode = MB.key.requestedMode;
                        b.effectiveMode = MB.key.effectiveMode;
                        a.eligibleSamples = MA.accounting.eligible;
                        b.eligibleSamples = MB.accounting.eligible;
                        a.runReferences =
                            refsFor(fp, scope, B.key.buildVersion, B.key.gitCommitState, B.key.gitCommit);
                        b.runReferences = a.runReferences;
                        if (sideSortKey(a) > sideSortKey(b)) std::swap(a, b);

                        ++out.comparisonOpportunities;
                        const ComparisonEligibility e = evaluateComparison(
                            a, b, ComparisonDimension::ModeSemantics, true);
                        if (e == ComparisonEligibility::Eligible) {
                            ComparisonCandidate cand;
                            cand.dimension = ComparisonDimension::ModeSemantics;
                            cand.datasetFingerprint = fp;
                            cand.mediaScope = scope;
                            cand.metricLevel = MetricLevel::ModeElapsed;
                            cand.metricResolution = MetricResolution::Recorded;
                            cand.left = a;
                            cand.right = b;
                            cand.limitations = out.limitations;
                            out.candidates.push_back(std::move(cand));
                            ++out.eligibleCandidates;
                        } else {
                            ++out.rejectedCandidates;
                            addRejection(out.rejections, out.rejectionReasons,
                                         ComparisonDimension::ModeSemantics, e, a, b);
                        }
                    }
                }
            }

            // ---- 4. case elapsed ------------------------------------------
            //
            // CaseElapsed is aggregated per dataset+scope. S6-4 keeps no per-build and
            // no per-mode breakdown of it, so neither a build comparison nor a mode
            // comparison of case elapsed can be stated from the result at all. That is
            // recorded as insufficient data rather than emitted as a candidate whose
            // numbers do not exist.
            if (sc.caseElapsed.sampleCount > 0) {
                ComparisonSide a, b;
                a.datasetFingerprint = b.datasetFingerprint = fp;
                a.mediaScope = b.mediaScope = scope;
                a.metricLevel = b.metricLevel = MetricLevel::CaseElapsed;
                a.metricResolution = b.metricResolution = MetricResolution::Recorded;
                a.buildVersion = b.buildVersion = "(scope-level only)";
                a.gitCommitState = b.gitCommitState = GitCommitState::Legacy;
                a.eligibleSamples = b.eligibleSamples = sc.caseAccounting.eligible;
                a.runReferences = b.runReferences = refsFor(fp, scope, "", GitCommitState::Legacy, "");

                ++out.comparisonOpportunities;
                ++out.rejectedCandidates;
                addRejection(out.rejections, out.rejectionReasons,
                             ComparisonDimension::BuildProvenance,
                             ComparisonEligibility::InsufficientData, a, b);
            }
        }
    }

    std::sort(out.candidates.begin(), out.candidates.end(), comparisonCandidateLess);
    std::sort(out.rejections.begin(), out.rejections.end(), [](const RejectedComparison& a,
                                                               const RejectedComparison& b) {
        return std::make_tuple(a.dimension, a.reason, a.datasetFingerprint, a.mediaScope,
                               static_cast<int>(a.metricLevel), a.leftLabel, a.rightLabel) <
               std::make_tuple(b.dimension, b.reason, b.datasetFingerprint, b.mediaScope,
                               static_cast<int>(b.metricLevel), b.leftLabel, b.rightLabel);
    });
    return out;
}

}  // namespace msf