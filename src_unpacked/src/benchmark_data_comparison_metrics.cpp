#include "benchmark_data_comparison_metrics.h"

#include <algorithm>
#include <cmath>

#include "benchmark_data_grouping.h"

namespace msf {

namespace {

// The rounding rule S6-4 already fixed, reused so a number does not change shape on its
// way through another layer.
constexpr double kRoundingScale = 1000000.0;

double roundMs(double v) {
    if (!std::isfinite(v)) return 0.0;
    return std::round(v * kRoundingScale) / kRoundingScale;
}

// The aggregate behind one side, plus the accounting that describes it.
//
// This is the only place that reaches into BenchmarkAggregation. Returning the statistics
// by pointer to the aggregate keeps the layering visible: no value is copied and then
// recomputed, so a metric cannot disagree with the aggregate it came from.
struct ResolvedSide {
    const DurationStatistics* stats = nullptr;
    const SampleAccounting* accounting = nullptr;
    bool found = false;
};

// Locates the aggregate matching one side of a candidate.
//
// The nesting level is decided by what the candidate addresses, not by the dimension
// alone:
//
//   * a mode cohort when the comparison is ModeSemantics, or when the metric is
//     ModeElapsed. A BuildProvenance candidate over ModeElapsed is generated per mode
//     semantics by S6-5, so it names a mode cohort inside each build even though the
//     dimension says "build";
//   * the build itself for CaseElapsed and RunWallDuration, because S6-4 aggregates those
//     per build and no per-mode figure of them exists at build level.
//
// Metric level then selects which of the build's blocks is the right one.
ResolvedSide resolveSide(const BenchmarkAggregation& agg, const ComparisonCandidate& cand,
                         const ComparisonSide& side) {
    const bool addressesModeCohort =
        (cand.dimension == ComparisonDimension::ModeSemantics) ||
        (cand.metricLevel == MetricLevel::ModeElapsed);

    ResolvedSide out;
    for (const AggregatedDataset& ds : agg.datasets) {
        if (ds.key.datasetFingerprint != side.datasetFingerprint) continue;
        for (const AggregatedScope& sc : ds.scopes) {
            if (sc.key.mediaScope != side.mediaScope) continue;
            for (const AggregatedBuild& b : sc.builds) {
                if (b.key.buildVersion != side.buildVersion) continue;
                if (b.key.gitCommitState != side.gitCommitState) continue;
                if (b.key.gitCommit != side.gitCommit) continue;

                if (addressesModeCohort) {
                    for (const AggregatedMode& m : b.modes) {
                        if (m.key.requestedMode != side.requestedMode) continue;
                        if (m.key.effectiveMode != side.effectiveMode) continue;
                        switch (cand.metricLevel) {
                            case MetricLevel::ModeElapsed:
                                out.stats = &m.elapsed;
                                out.accounting = &m.accounting;
                                break;
                            case MetricLevel::CaseElapsed:
                                out.stats = &m.caseElapsed;
                                out.accounting = &m.caseAccounting;
                                break;
                            case MetricLevel::RunWallDuration:
                                // The build's run wall duration is not a per-mode figure,
                                // so a mode comparison of it is not expressible.
                                return ResolvedSide{};
                        }
                        out.found = true;
                        return out;
                    }
                    return ResolvedSide{};
                }

                switch (cand.metricLevel) {
                    case MetricLevel::CaseElapsed:
                        out.stats = &b.caseElapsed;
                        out.accounting = &b.caseAccounting;
                        break;
                    case MetricLevel::RunWallDuration:
                        out.stats = &b.runWallDuration;
                        out.accounting = &b.runAccounting;
                        break;
                    case MetricLevel::ModeElapsed:
                        // Unreachable: ModeElapsed always addresses a mode cohort.
                        return ResolvedSide{};
                }
                out.found = true;
                return out;
            }
        }
    }
    return out;
}

const std::optional<double>& statisticValue(const DurationStatistics& s,
                                            ComparisonStatistic which) {
    switch (which) {
        case ComparisonStatistic::Min: return s.minMs;
        case ComparisonStatistic::Max: return s.maxMs;
        case ComparisonStatistic::Mean: return s.meanMs;
        case ComparisonStatistic::Median: return s.medianMs;
        case ComparisonStatistic::P95: return s.p95Ms;
    }
    return s.meanMs;
}

void noteUnavailable(StatisticComparison& sc, MetricUnavailableReason r) {
    for (MetricUnavailableReason e : sc.unavailable)
        if (e == r) return;
    sc.unavailable.push_back(r);
    std::sort(sc.unavailable.begin(), sc.unavailable.end(),
              [](MetricUnavailableReason a, MetricUnavailableReason b) {
                  return static_cast<int>(a) < static_cast<int>(b);
              });
}

// Builds one statistic's comparison from two already-computed aggregate values.
StatisticComparison compareStatistic(ComparisonStatistic which, const DurationStatistics& left,
                                     const SampleAccounting& leftAcct,
                                     const DurationStatistics& right,
                                     const SampleAccounting& rightAcct) {
    StatisticComparison sc;
    sc.statistic = which;
    sc.leftValue = statisticValue(left, which);
    sc.rightValue = statisticValue(right, which);

    if (leftAcct.eligible == 0) noteUnavailable(sc, MetricUnavailableReason::NoEligibleSamples);
    if (rightAcct.eligible == 0) noteUnavailable(sc, MetricUnavailableReason::NoEligibleSamples);
    if (!sc.leftValue.has_value()) noteUnavailable(sc, MetricUnavailableReason::LeftStatisticAbsent);
    if (!sc.rightValue.has_value())
        noteUnavailable(sc, MetricUnavailableReason::RightStatisticAbsent);

    if (!sc.leftValue.has_value() || !sc.rightValue.has_value()) return sc;

    // A measured zero is a value, not an absence. It is recorded before the division so the
    // absent ratio can be explained rather than merely omitted.
    sc.leftValueIsZero = (*sc.leftValue == 0.0);

    sc.absoluteDeltaMs = roundMs(*sc.rightValue - *sc.leftValue);

    if (sc.leftValueIsZero) {
        // Dividing by a measured zero has no answer. Reporting 0, NaN or an infinity string
        // would each be a fabricated value.
        noteUnavailable(sc, MetricUnavailableReason::ZeroLeftReference);
        return sc;
    }

    sc.relativeDeltaPercent =
        roundMs(((*sc.rightValue - *sc.leftValue) / *sc.leftValue) * 100.0);
    sc.ratio = roundMs(*sc.rightValue / *sc.leftValue);
    return sc;
}

}  // namespace

// ---------------------------------------------------------------------------

const char* comparisonStatisticName(ComparisonStatistic s) {
    switch (s) {
        case ComparisonStatistic::Min: return "min";
        case ComparisonStatistic::Max: return "max";
        case ComparisonStatistic::Mean: return "mean";
        case ComparisonStatistic::Median: return "median";
        case ComparisonStatistic::P95: return "p95";
    }
    return "unknown";
}

const char* metricUnavailableReasonName(MetricUnavailableReason r) {
    switch (r) {
        case MetricUnavailableReason::NoEligibleSamples: return "no-eligible-samples";
        case MetricUnavailableReason::LeftStatisticAbsent: return "left-statistic-absent";
        case MetricUnavailableReason::RightStatisticAbsent: return "right-statistic-absent";
        case MetricUnavailableReason::ZeroLeftReference: return "zero-left-reference";
    }
    return "unknown";
}

const StatisticComparison* ComparisonMetrics::find(ComparisonStatistic s) const {
    for (const StatisticComparison& e : statistics)
        if (e.statistic == s) return &e;
    return nullptr;
}

bool ComparisonMetrics::anyAvailable() const {
    for (const StatisticComparison& s : statistics)
        if (s.anyAvailable()) return true;
    return false;
}

// ---------------------------------------------------------------------------

ComparisonMetrics computeComparisonMetrics(const ComparisonCandidate& candidate,
                                            const BenchmarkAggregation& aggregation) {
    ComparisonMetrics m;
    m.candidate = candidate;
    m.metricLevel = candidate.metricLevel;
    m.metricResolution = candidate.metricResolution;

    // Defence in depth. S6-5 already refuses a candidate whose sides disagree on metric
    // level or resolution, and this re-checks rather than trusting it, because comparing
    // numbers from different metrics is exactly the mistake a delta would hide.
    const bool comparableUnits = candidate.left.metricLevel == candidate.right.metricLevel &&
                                 candidate.left.metricResolution == candidate.right.metricResolution &&
                                 candidate.left.metricLevel == candidate.metricLevel &&
                                 candidate.left.metricResolution == candidate.metricResolution;

    static const ComparisonStatistic kOrder[] = {
        ComparisonStatistic::Min, ComparisonStatistic::Max, ComparisonStatistic::Mean,
        ComparisonStatistic::Median, ComparisonStatistic::P95};

    for (ComparisonStatistic which : kOrder) {
        StatisticComparison sc;
        sc.statistic = which;
        if (!comparableUnits) {
            // Nothing is computed, and every field stays absent. The reasons explain why
            // rather than leaving the reader to guess whether the values were zero.
            noteUnavailable(sc, MetricUnavailableReason::LeftStatisticAbsent);
            noteUnavailable(sc, MetricUnavailableReason::RightStatisticAbsent);
            m.statistics.push_back(std::move(sc));
            continue;
        }

        const ResolvedSide L = resolveSide(aggregation, candidate, candidate.left);
        const ResolvedSide R = resolveSide(aggregation, candidate, candidate.right);
        if (!L.found || !R.found) {
            noteUnavailable(sc, MetricUnavailableReason::LeftStatisticAbsent);
            noteUnavailable(sc, MetricUnavailableReason::RightStatisticAbsent);
            m.statistics.push_back(std::move(sc));
            continue;
        }
        m.statistics.push_back(compareStatistic(which, *L.stats, *L.accounting, *R.stats,
                                                *R.accounting));
    }
    return m;
}

std::vector<ComparisonMetrics> computeAllComparisonMetrics(const ComparisonAnalysis& analysis,
                                                           const BenchmarkAggregation& aggregation) {
    std::vector<ComparisonMetrics> out;
    out.reserve(analysis.candidates.size());
    for (const ComparisonCandidate& c : analysis.candidates)
        out.push_back(computeComparisonMetrics(c, aggregation));
    return out;
}

}  // namespace msf