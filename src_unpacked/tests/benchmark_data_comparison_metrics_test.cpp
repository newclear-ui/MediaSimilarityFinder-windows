// S6-6 comparison metrics.
//
// S6-5 decided which populations may be compared. This stage computes the numbers for
// those candidates, and the checks concentrate on the ways a computed delta can lie:
//
//   * reading `left` as a baseline and `right` as the thing being tested;
//   * calling a positive relative delta a regression;
//   * turning a measured zero into a missing value, or a missing value into a zero;
//   * dividing by zero and emitting 0, NaN or an infinity string;
//   * recomputing a statistic here that S6-4 had already computed;
//   * comparing numbers from different metric levels or resolutions;
//   * dropping a whole statistic because a different one was absent.

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "benchmark_core.h"
#include "benchmark_data_aggregation.h"
#include "benchmark_data_comparison.h"
#include "benchmark_data_comparison_metrics.h"
#include "benchmark_data_grouping.h"
#include "benchmark_data_mining.h"

namespace {

int gChecks = 0;
int gFails = 0;

void chk(bool ok, const std::string& what) {
    ++gChecks;
    if (ok) std::printf("  [ok] %s\n", what.c_str());
    else { ++gFails; std::printf("  [F] %s\n", what.c_str()); }
}

const double kEps = 1e-6;

msf::IngestModeResult makeMode(msf::GpuBackendKind req, msf::GpuBackendKind eff,
                               msf::BenchmarkStatus st, std::optional<double> elapsed) {
    msf::IngestModeResult m;
    m.requestedMode = req;
    m.effectiveMode = eff;
    m.status = st;
    m.started = (st != msf::BenchmarkStatus::Skipped);
    m.completed = m.started;
    m.elapsedMs = elapsed;
    return m;
}

msf::IngestCase makeCase(const std::string& id, msf::BenchmarkStatus st, double elapsed,
                         const std::vector<msf::IngestModeResult>& modes) {
    msf::IngestCase c;
    c.caseId = id;
    c.path = "/d/" + id;
    c.media = msf::MediaKind::Image;
    c.status = st;
    c.elapsedMs = elapsed;
    c.modes = modes;
    return c;
}

msf::IngestRun makeRun(const std::string& runId, const std::string& journal, const std::string& fp,
                       const std::string& scope, const std::string& build,
                       msf::GitCommitState gs, const std::string& git,
                       std::optional<double> wallMs,
                       const std::vector<msf::IngestCase>& cases) {
    msf::IngestRun r;
    r.sourceJournalPath = journal;
    r.runId = runId;
    r.suiteId = journal;
    r.datasetFingerprint = fp;
    r.datasetIdentity = msf::DatasetIdentityState::Valid;
    r.buildVersion = build;
    r.gitCommitState = gs;
    if (gs == msf::GitCommitState::Known) r.gitCommit = git;
    r.mediaScope = scope;
    r.runStatus = msf::BenchmarkStatus::Success;
    r.wallDurationMs = wallMs;
    r.runClass = msf::IngestRunClass::Complete;
    r.cases = cases;
    return r;
}

// One run whose cases all carry the same measured value.
msf::IngestRun uniformRun(const std::string& id, const std::string& journal, const std::string& build,
                          const std::string& git, double wallMs, double caseMs, int cases) {
    std::vector<msf::IngestCase> cs;
    for (int i = 0; i < cases; ++i)
        cs.push_back(makeCase("c" + std::to_string(i), msf::BenchmarkStatus::Success, caseMs,
                              {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success,
                                        std::optional<double>{caseMs})}));
    return makeRun(id, journal, "fp-1", "images", build, msf::GitCommitState::Known, git,
                   std::optional<double>{wallMs}, cs);
}

struct Pipeline {
    msf::BenchmarkAggregation aggregation;
    msf::ComparisonAnalysis analysis;
    std::vector<msf::ComparisonMetrics> metrics;

    void run(const msf::NormalizedBenchmarkData& d) {
        const msf::BenchmarkGrouping g = msf::groupBenchmarks(d);
        aggregation = msf::aggregateBenchmarks(d, g);
        analysis = msf::findComparisonCandidates(d, aggregation);
        metrics = msf::computeAllComparisonMetrics(analysis, aggregation);
    }
};

const msf::StatisticComparison* pick(const msf::ComparisonMetrics& m,
                                     msf::ComparisonStatistic s) {
    return m.find(s);
}

// A candidate for CaseElapsed between two Known builds, with the given aggregates.
struct Fixture {
    msf::NormalizedBenchmarkData d;
};

// Build A cases average 110 (100,120); build B cases average 165 (150,180).
Fixture knownKnownFixture() {
    Fixture f;
    f.d.runs.push_back(makeRun("run-a", "jA", "fp-1", "images", "1.0.0", msf::GitCommitState::Known,
                               "commitA", std::optional<double>{1000.0},
                               {makeCase("c1", msf::BenchmarkStatus::Success, 100.0,
                                         {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                                   msf::BenchmarkStatus::Success,
                                                   std::optional<double>{100.0})}),
                                makeCase("c2", msf::BenchmarkStatus::Success, 120.0,
                                         {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                                   msf::BenchmarkStatus::Success,
                                                   std::optional<double>{120.0})})}));
    f.d.runs.push_back(makeRun("run-b", "jB", "fp-1", "images", "2.0.0", msf::GitCommitState::Known,
                               "commitB", std::optional<double>{2000.0},
                               {makeCase("c1", msf::BenchmarkStatus::Success, 150.0,
                                         {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                                   msf::BenchmarkStatus::Success,
                                                   std::optional<double>{150.0})}),
                                makeCase("c2", msf::BenchmarkStatus::Success, 180.0,
                                         {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                                   msf::BenchmarkStatus::Success,
                                                   std::optional<double>{180.0})})}));
    return f;
}

// Finds the metrics for one metric level, so a test never depends on candidate ordering.
const msf::ComparisonMetrics* byLevel(const std::vector<msf::ComparisonMetrics>& v,
                                      msf::MetricLevel lvl) {
    for (const auto& m : v)
        if (m.metricLevel == lvl) return &m;
    return nullptr;
}

// Returns the CaseElapsed metrics, or an empty stand-in when there are none, so a test can
// inspect availability without branching on the candidate count.
const msf::ComparisonMetrics& caseMetrics(const std::vector<msf::ComparisonMetrics>& v) {
    static const msf::ComparisonMetrics kEmpty;
    const msf::ComparisonMetrics* m = byLevel(v, msf::MetricLevel::CaseElapsed);
    return m ? *m : kEmpty;
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("S6-6 benchmark comparison metrics\n");

    // --- 1. the brief's synthetic known-known case --------------------------
    {
        const Fixture f = knownKnownFixture();
        Pipeline p;
        p.run(f.d);
        chk(p.analysis.candidates.size() == 3, "the synthetic known-known pair yields 3 candidates: ModeElapsed, CaseElapsed and RunWallDuration");
        chk(p.metrics.size() == 3, "and three metrics results, one per candidate");

        const msf::StatisticComparison* mean = pick(caseMetrics(p.metrics), msf::ComparisonStatistic::Mean);
        chk(mean != nullptr, "the mean statistic is present");
        if (mean != nullptr) {
            chk(mean->leftValue.has_value() && std::abs(*mean->leftValue - 110.0) < kEps,
                "left mean is 110");
            chk(mean->rightValue.has_value() && std::abs(*mean->rightValue - 165.0) < kEps,
                "right mean is 165");
            chk(mean->absoluteDeltaMs.has_value() && std::abs(*mean->absoluteDeltaMs - 55.0) < kEps,
                "absolute delta is +55 ms");
            chk(mean->relativeDeltaPercent.has_value() &&
                    std::abs(*mean->relativeDeltaPercent - 50.0) < kEps,
                "relative delta is +50%");
            chk(mean->ratio.has_value() && std::abs(*mean->ratio - 1.5) < kEps, "ratio is 1.5");
            chk(mean->complete(), "every field is available when both values are present");
            chk(mean->unavailable.empty(), "and no unavailable reason is recorded");
        }
        chk(caseMetrics(p.metrics).metricLevel == msf::MetricLevel::CaseElapsed,
            "the metric level travels with the metrics");
        chk(caseMetrics(p.metrics).metricResolution == msf::MetricResolution::Recorded,
            "CaseElapsed keeps the Recorded resolution");
    }

    // --- 2. each statistic is compared independently ------------------------
    {
        const Fixture f = knownKnownFixture();
        Pipeline p;
        p.run(f.d);
        const msf::ComparisonMetrics& m = caseMetrics(p.metrics);
        chk(m.statistics.size() == 5, "five statistics are compared: min, max, mean, median, p95");
        for (msf::ComparisonStatistic s :
             {msf::ComparisonStatistic::Min, msf::ComparisonStatistic::Max,
              msf::ComparisonStatistic::Mean, msf::ComparisonStatistic::Median,
              msf::ComparisonStatistic::P95}) {
            const msf::StatisticComparison* c = m.find(s);
            chk(c != nullptr && c->absoluteDeltaMs.has_value(),
                std::string("absolute delta exists for ") + msf::comparisonStatisticName(s));
        }
        const auto* median = m.find(msf::ComparisonStatistic::Median);
        const auto* p95 = m.find(msf::ComparisonStatistic::P95);
        chk(median && median->leftValue.has_value() && std::abs(*median->leftValue - 110.0) < kEps,
            "median left is 110");
        chk(p95 && p95->leftValue.has_value() && std::abs(*p95->leftValue - 120.0) < kEps,
            "p95 left is 120, so p95 is not just the mean repeated");
        chk(p95 && p95->rightValue.has_value() && std::abs(*p95->rightValue - 180.0) < kEps,
            "p95 right is 180");
        chk(p95 && std::abs(*p95->absoluteDeltaMs - 60.0) < kEps, "p95 absolute delta is +60");
        // Ordered by statistic, so the output order is fixed.
        bool ordered = true;
        for (std::size_t i = 1; i < m.statistics.size(); ++i)
            if (m.statistics[i - 1].statistic >= m.statistics[i].statistic) ordered = false;
        chk(ordered, "statistics are in a fixed order");
    }

    // --- 3. negative delta is not an improvement ----------------------------
    {
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(uniformRun("run-a", "jA", "1.0.0", "commitA", 1000.0, 200.0, 2));
        d.runs.push_back(uniformRun("run-b", "jB", "2.0.0", "commitB", 2000.0, 150.0, 2));
        Pipeline p;
        p.run(d);
        const msf::StatisticComparison* mean =
            pick(caseMetrics(p.metrics), msf::ComparisonStatistic::Mean);
        chk(mean != nullptr && mean->absoluteDeltaMs.has_value() &&
                std::abs(*mean->absoluteDeltaMs + 50.0) < kEps,
            "absolute delta is -50 ms");
        chk(mean && mean->relativeDeltaPercent.has_value() &&
                std::abs(*mean->relativeDeltaPercent + 25.0) < kEps,
            "relative delta is -25%");
        chk(mean && mean->ratio.has_value() && std::abs(*mean->ratio - 0.75) < kEps, "ratio is 0.75");
        chk(mean && mean->complete(), "a negative delta is a complete comparison like any other");
        // Nothing in the type vocabulary claims a direction.
        chk(std::string(msf::comparisonStatisticName(msf::ComparisonStatistic::Mean)) == "mean",
            "the statistic name carries no verdict, and no better/worse field exists");
    }

    // --- 4. zero delta -------------------------------------------------------
    {
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(uniformRun("run-a", "jA", "1.0.0", "commitA", 1000.0, 150.0, 2));
        d.runs.push_back(uniformRun("run-b", "jB", "2.0.0", "commitB", 2000.0, 150.0, 2));
        Pipeline p;
        p.run(d);
        const msf::StatisticComparison* mean =
            pick(caseMetrics(p.metrics), msf::ComparisonStatistic::Mean);
        chk(mean && mean->absoluteDeltaMs.has_value() &&
                std::abs(*mean->absoluteDeltaMs) < kEps,
            "identical populations give an absolute delta of 0, which is a real zero");
        chk(mean && mean->relativeDeltaPercent.has_value() &&
                std::abs(*mean->relativeDeltaPercent) < kEps,
            "and a relative delta of 0%");
        chk(mean && mean->ratio.has_value() && std::abs(*mean->ratio - 1.0) < kEps, "and a ratio of 1");
        chk(mean && !mean->leftValueIsZero, "a non-zero reference is not flagged as zero");
    }

    // --- 5. zero reference: absolute present, relative and ratio absent ------
    {
        msf::NormalizedBenchmarkData d;
        // Build A's cases measure 0 ms. That is a measurement, not a missing value.
        d.runs.push_back(uniformRun("run-a", "jA", "1.0.0", "commitA", 1000.0, 0.0, 2));
        d.runs.push_back(uniformRun("run-b", "jB", "2.0.0", "commitB", 2000.0, 100.0, 2));
        Pipeline p;
        p.run(d);
        const msf::StatisticComparison* mean =
            pick(caseMetrics(p.metrics), msf::ComparisonStatistic::Mean);
        chk(mean != nullptr, "the zero-reference candidate has metrics");
        if (mean != nullptr) {
            chk(mean->leftValue.has_value() && std::abs(*mean->leftValue) < kEps,
                "the left mean is a present 0, not an absent value");
            chk(mean->absoluteDeltaMs.has_value() && std::abs(*mean->absoluteDeltaMs - 100.0) < kEps,
                "absolute delta is +100 ms");
            chk(!mean->relativeDeltaPercent.has_value(),
                "relative delta is unavailable rather than 0, NaN or an infinity string");
            chk(!mean->ratio.has_value(), "ratio is unavailable too");
            chk(mean->leftValueIsZero, "the reason is recorded: the left reference was zero");
            bool noted = false;
            for (auto r : mean->unavailable)
                if (r == msf::MetricUnavailableReason::ZeroLeftReference) noted = true;
            chk(noted, "ZeroLeftReference is listed as the unavailable reason");
            chk(!mean->complete() && mean->anyAvailable(),
                "the statistic is partially available: the absolute delta is real");
        }
    }

    // --- 6. both sides zero ---------------------------------------------------
    {
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(uniformRun("run-a", "jA", "1.0.0", "commitA", 1000.0, 0.0, 2));
        d.runs.push_back(uniformRun("run-b", "jB", "2.0.0", "commitB", 2000.0, 0.0, 2));
        Pipeline p;
        p.run(d);
        const msf::StatisticComparison* mean =
            pick(caseMetrics(p.metrics), msf::ComparisonStatistic::Mean);
        chk(mean && mean->absoluteDeltaMs.has_value() &&
                std::abs(*mean->absoluteDeltaMs) < kEps,
            "0 against 0 gives an absolute delta of 0");
        chk(mean && !mean->relativeDeltaPercent.has_value() && !mean->ratio.has_value(),
            "relative delta and ratio stay unavailable, since 0/0 has no answer either");
        chk(mean && mean->leftValueIsZero, "and the zero reference is still recorded");
    }

    // --- 7. run wall duration keeps OneSecond resolution ---------------------
    {
        msf::NormalizedBenchmarkData d;
        std::vector<msf::IngestCase> cs;
        cs.push_back(makeCase("c1", msf::BenchmarkStatus::Success, 10.0,
                              {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success,
                                        std::optional<double>{10.0})}));
        d.runs.push_back(makeRun("run-a", "jA", "fp-1", "images", "1.0.0", msf::GitCommitState::Known,
                                 "commitA", std::optional<double>{7000.0}, cs));
        d.runs.push_back(makeRun("run-b", "jB", "fp-1", "images", "2.0.0", msf::GitCommitState::Known,
                                 "commitB", std::optional<double>{3000.0}, cs));
        Pipeline p;
        p.run(d);
        bool found = false;
        for (const auto& m : p.metrics) {
            if (m.metricLevel != msf::MetricLevel::RunWallDuration) continue;
            found = true;
            chk(m.metricResolution == msf::MetricResolution::OneSecond,
                "a RunWallDuration comparison carries OneSecond resolution");
            const msf::StatisticComparison* mean = pick(m, msf::ComparisonStatistic::Mean);
            chk(mean && mean->absoluteDeltaMs.has_value() &&
                    std::abs(*mean->absoluteDeltaMs + 4000.0) < kEps,
                "7000 against 3000 gives an absolute delta of -4000 ms");
            chk(mean && mean->relativeDeltaPercent.has_value() &&
                    std::abs(*mean->relativeDeltaPercent + 4000.0 / 7000.0 * 100.0) < kEps,
                "the relative delta is computed against the oriented left value");
        }
        chk(found, "the run wall duration candidate exists");
    }

    // --- 8. a zero run wall duration is a measurement -------------------------
    {
        msf::NormalizedBenchmarkData d;
        std::vector<msf::IngestCase> cs;
        cs.push_back(makeCase("c1", msf::BenchmarkStatus::Success, 10.0,
                              {makeMode(msf::GpuBackendKind::Cpu, msf::GpuBackendKind::Cpu,
                                        msf::BenchmarkStatus::Success,
                                        std::optional<double>{10.0})}));
        // Build A finished inside the same second, so its wall duration is a real 0.
        d.runs.push_back(makeRun("run-a", "jA", "fp-1", "images", "0.5.0", msf::GitCommitState::Known,
                                 "commitZ", std::optional<double>{0.0}, cs));
        d.runs.push_back(makeRun("run-b", "jB", "fp-1", "images", "2.0.0", msf::GitCommitState::Known,
                                 "commitB", std::optional<double>{4000.0}, cs));
        Pipeline p;
        p.run(d);
        bool checked = false;
        for (const auto& m : p.metrics) {
            if (m.metricLevel != msf::MetricLevel::RunWallDuration) continue;
            const msf::StatisticComparison* mean = pick(m, msf::ComparisonStatistic::Mean);
            if (mean == nullptr) continue;
            checked = true;
            chk(mean->leftValue.has_value(),
                "a zero wall duration is a present value, not a missing one");
            if (mean->leftValue.has_value() && std::abs(*mean->leftValue) < kEps) {
                chk(!mean->relativeDeltaPercent.has_value(),
                    "relative delta against a measured zero is unavailable, not reclassified");
                chk(mean->leftValueIsZero, "and the zero reference is named");
            }
        }
        chk(checked, "the zero wall duration case was exercised");
    }

    // --- 9. sample accounting travels with the numbers ----------------------
    {
        const Fixture f = knownKnownFixture();
        Pipeline p;
        p.run(f.d);
        chk(caseMetrics(p.metrics).candidate.left.eligibleSamples == 2,
            "the left sample count travels with the metrics");
        chk(caseMetrics(p.metrics).candidate.right.eligibleSamples == 2,
            "and so does the right sample count");
        chk(caseMetrics(p.metrics).candidate.left.runReferences.size() == 1 &&
                !caseMetrics(p.metrics).candidate.left.runReferences[0].sourceJournalPath.empty(),
            "the run references travel with the metrics, so a delta stays attached to what was "
            "compared");
    }

    // --- 10. limitations are carried through unchanged ----------------------
    {
        const Fixture f = knownKnownFixture();
        Pipeline p;
        p.run(f.d);
        const msf::ComparisonLimitations& lim = caseMetrics(p.metrics).candidate.limitations;
        chk(lim.distanceUnavailable && lim.resourcePolicyUnavailable && lim.gpuBackendUnavailable,
            "the unavailable journal fields travel with the metrics");
        chk(lim.controlledEnvironmentUndefined && lim.processIsolationUncontrolled &&
                lim.filesystemCacheUncontrolled,
            "the uncontrolled conditions travel too");
        chk(lim.repeatedRunsInsufficient, "the insufficient repetition travels");
        chk(lim.cancellationUnobserved, "and the absent cancellation sample travels");
        chk(lim.count() == 9,
            "exactly the nine limitations S6-5 defined, with no second limitation system");
    }

    // --- 11. provenance behaviour -------------------------------------------
    {
        // A Legacy side produces no candidate, so no metrics can be computed for it.
        msf::NormalizedBenchmarkData d;
        d.runs.push_back(makeRun("run-a", "jA", "fp-1", "images", "1.0.0", msf::GitCommitState::Known,
                                 "commitA", std::optional<double>{1000.0},
                                 {makeCase("c1", msf::BenchmarkStatus::Success, 100.0,
                                           {makeMode(msf::GpuBackendKind::Cpu,
                                                     msf::GpuBackendKind::Cpu,
                                                     msf::BenchmarkStatus::Success,
                                                     std::optional<double>{100.0})})}));
        d.runs.push_back(makeRun("run-b", "jB", "fp-1", "images", "0.9.0",
                                 msf::GitCommitState::Legacy, "", std::optional<double>{2000.0},
                                 {makeCase("c1", msf::BenchmarkStatus::Success, 150.0,
                                           {makeMode(msf::GpuBackendKind::Cpu,
                                                     msf::GpuBackendKind::Cpu,
                                                     msf::BenchmarkStatus::Success,
                                                     std::optional<double>{150.0})})}));
        Pipeline p;
        p.run(d);
        chk(p.analysis.candidates.empty(),
            "a Legacy side produces no candidate, so no metrics are invented for it");
        chk(p.metrics.empty(), "and the metrics list is empty");
    }

    // --- 12. metric level mismatch is refused defensively --------------------
    {
        // A hand-built candidate whose sides disagree. S6-5 would never emit it, so this
        // checks the second line of defence rather than the first.
        msf::ComparisonCandidate bad;
        bad.dimension = msf::ComparisonDimension::BuildProvenance;
        bad.datasetFingerprint = "fp-1";
        bad.mediaScope = "images";
        bad.metricLevel = msf::MetricLevel::CaseElapsed;
        bad.metricResolution = msf::MetricResolution::Recorded;
        msf::ComparisonSide l;
        l.datasetFingerprint = "fp-1";
        l.mediaScope = "images";
        l.metricLevel = msf::MetricLevel::CaseElapsed;
        l.metricResolution = msf::MetricResolution::Recorded;
        l.buildVersion = "1.0.0";
        l.gitCommitState = msf::GitCommitState::Known;
        l.gitCommit = "commitA";
        l.eligibleSamples = 2;
        msf::ComparisonSide r = l;
        r.metricLevel = msf::MetricLevel::ModeElapsed;   // the mismatch
        r.buildVersion = "2.0.0";
        r.gitCommit = "commitB";
        bad.left = l;
        bad.right = r;

        const Fixture f = knownKnownFixture();
        Pipeline real;
        real.run(f.d);
        const msf::ComparisonMetrics m = msf::computeComparisonMetrics(bad, real.aggregation);
        for (const auto& s : m.statistics) {
            chk(!s.absoluteDeltaMs.has_value() && !s.relativeDeltaPercent.has_value() &&
                    !s.ratio.has_value(),
                std::string("a mismatched metric level yields no numbers for ") +
                    msf::comparisonStatisticName(s.statistic));
        }
        chk(!m.anyAvailable(), "so the mismatched candidate produces no available metric");
    }

    // --- 13. resolution mismatch is refused too ------------------------------
    {
        const Fixture f = knownKnownFixture();
        Pipeline real;
        real.run(f.d);
        msf::ComparisonCandidate bad = real.analysis.candidates.at(0);
        bad.right.metricResolution = msf::MetricResolution::OneSecond;
        bad.metricResolution = msf::MetricResolution::OneSecond;
        const msf::ComparisonMetrics m = msf::computeComparisonMetrics(bad, real.aggregation);
        chk(!m.anyAvailable(), "a mismatched resolution yields no numbers");
        for (const auto& s : m.statistics)
            chk(!s.absoluteDeltaMs.has_value(),
                "no absolute delta is produced across a resolution difference");
    }

    // --- 14. no candidate means no metrics -----------------------------------
    {
        const msf::NormalizedBenchmarkData empty;
        Pipeline p;
        p.run(empty);
        chk(p.analysis.candidates.empty(), "an empty dataset has no candidates");
        chk(p.metrics.empty(), "and therefore no metrics");
        chk(p.metrics.empty() == p.analysis.candidates.empty(),
            "the metrics list mirrors the candidate list one for one");
    }

    // --- 15. determinism ------------------------------------------------------
    {
        const Fixture f = knownKnownFixture();
        Pipeline a, b;
        a.run(f.d);
        b.run(f.d);
        bool same = a.metrics.size() == b.metrics.size();
        for (std::size_t i = 0; same && i < a.metrics.size(); ++i) {
            same = a.metrics[i].metricLevel == b.metrics[i].metricLevel &&
                   a.metrics[i].metricResolution == b.metrics[i].metricResolution &&
                   a.metrics[i].candidate.left.gitCommit == b.metrics[i].candidate.left.gitCommit &&
                   a.metrics[i].statistics.size() == b.metrics[i].statistics.size();
            for (std::size_t j = 0; same && j < a.metrics[i].statistics.size(); ++j) {
                const auto& x = a.metrics[i].statistics[j];
                const auto& y = b.metrics[i].statistics[j];
                same = x.statistic == y.statistic;
                if (same && x.absoluteDeltaMs.has_value() && y.absoluteDeltaMs.has_value())
                    same = *x.absoluteDeltaMs == *y.absoluteDeltaMs;
                if (same && x.relativeDeltaPercent.has_value() && y.relativeDeltaPercent.has_value())
                    same = *x.relativeDeltaPercent == *y.relativeDeltaPercent;
                if (same && x.ratio.has_value() && y.ratio.has_value())
                    same = *x.ratio == *y.ratio;
                same = same && x.leftValueIsZero == y.leftValueIsZero &&
                       x.unavailable.size() == y.unavailable.size();
            }
        }
        chk(same, "the same input produces identical metrics, order and availability");
    }

    // --- real journal read-only ---------------------------------------------
    if (argc > 1) {
        const msf::NormalizedBenchmarkData d = msf::ingestBenchmarks(argv[1]);
        const msf::BenchmarkGrouping g = msf::groupBenchmarks(d);
        const msf::BenchmarkAggregation agg = msf::aggregateBenchmarks(d, g);
        const msf::ComparisonAnalysis an = msf::findComparisonCandidates(d, agg);
        const auto m1 = msf::computeAllComparisonMetrics(an, agg);
        const auto m2 = msf::computeAllComparisonMetrics(an, agg);

        std::printf("LIVE  runs=%zu accepted=%zu\n", d.runsFound, d.runsAccepted);
        std::printf("LIVE  candidates=%zu comparisonMetrics=%zu\n", an.candidates.size(),
                    m1.size());
        std::printf("LIVE  rejection reasons:");
        for (const auto& t : an.rejectionReasons)
            std::printf(" %s=%zu", msf::comparisonEligibilityName(t.reason), t.count);
        std::printf("\n");
        std::printf("LIVE  -- metrics --\n");
        std::size_t available = 0;
        for (const auto& m : m1) {
            std::printf("LIVE    %-18s %-10.10s %-6s %-16s L[n=%zu] R[n=%zu]\n",
                        msf::comparisonDimensionName(m.candidate.dimension),
                        m.candidate.datasetFingerprint.c_str(), m.candidate.mediaScope.c_str(),
                        msf::metricLevelName(m.metricLevel), m.candidate.left.eligibleSamples,
                        m.candidate.right.eligibleSamples);
            for (const auto& s : m.statistics) {
                if (!s.absoluteDeltaMs.has_value()) continue;
                ++available;
                std::printf("LIVE      %-7s abs=%-12.3f rel=%-12.3f ratio=%-8.4f res=%s\n",
                            msf::comparisonStatisticName(s.statistic), *s.absoluteDeltaMs,
                            s.relativeDeltaPercent.value_or(-999.0), s.ratio.value_or(-999.0),
                            msf::metricResolutionName(m.metricResolution));
            }
        }
        std::printf("LIVE  absolute deltas available=%zu (0 is the expected result on the current "
                    "store: there are no comparison candidates)\n",
                    available);

        bool same = m1.size() == m2.size();
        for (std::size_t i = 0; same && i < m1.size(); ++i) {
            same = m1[i].metricLevel == m2[i].metricLevel &&
                   m1[i].statistics.size() == m2[i].statistics.size();
            for (std::size_t j = 0; same && j < m1[i].statistics.size(); ++j) {
                const auto& x = m1[i].statistics[j];
                const auto& y = m2[i].statistics[j];
                same = x.statistic == y.statistic &&
                       x.leftValue.has_value() == y.leftValue.has_value() &&
                       x.absoluteDeltaMs.has_value() == y.absoluteDeltaMs.has_value();
                if (same && x.absoluteDeltaMs.has_value())
                    same = *x.absoluteDeltaMs == *y.absoluteDeltaMs;
                if (same && x.ratio.has_value() && y.ratio.has_value())
                    same = *x.ratio == *y.ratio;
            }
        }
        std::printf("LIVE  determinism: %s\n", same ? "IDENTICAL" : "DIVERGED");
        std::printf("LIVE  limitations carried per metric=%zu\n",
                    m1.empty() ? size_t{0} : m1[0].candidate.limitations.count());
        return same ? 0 : 1;
    }

    std::printf("S6-6 metrics: %s checks=%d fails=%d\n", gFails == 0 ? "ok" : "FAILED", gChecks,
                gFails);
    return gFails == 0 ? 0 : 1;
}