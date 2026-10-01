#pragma once
// S6 Data-mining: comparison metrics.
//
// SCOPE. This layer computes numbers. It does not interpret them.
//
//   absoluteDelta       = right - left          (same unit as the metric, milliseconds)
//   relativeDeltaPercent= ((right - left) / left) * 100
//   ratio               = right / left
//
// Three rules decide what this layer refuses to do.
//
// LEFT IS NOT A BASELINE. `left < right` in S6-5 is a typed ordering that makes the pair
// reproducible, nothing more. It does not mean old versus new, previous versus current,
// or good versus bad. Nothing here re-reads the orientation as a baseline, because a
// deterministic ordering that also implied "before" would silently pick a direction.
//
// A NUMBER IS NOT A VERDICT. `relativeDeltaPercent = +50%` is reported as +50%. It is not
// called a regression, an improvement, better, worse or a winner. Whether a difference
// matters is a later decision layer's job, and that layer does not exist yet. No threshold
// is defined here, so nothing here can be passed or failed.
//
// ZERO IS NOT MISSING. A zero is a measurement. A RunWallDuration of 0 ms means the two
// timestamps fell in the same second, which is a real derived value; it is not an absent
// duration. So a zero left side still yields an absolute delta, while the relative delta
// and the ratio are reported as unavailable, because dividing by it has no answer. They are
// NOT replaced with 0, and no NaN or infinity string is ever placed in the output contract.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "benchmark_core.h"                        // GpuBackendKind
#include "benchmark_data_aggregation.h"           // MetricLevel, MetricResolution
#include "benchmark_data_comparison.h"            // ComparisonCandidate, ComparisonLimitations
#include "benchmark_data_mining.h"               // GitCommitState

namespace msf {

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

// Which summary statistic of the aggregate is being compared.
//
// Each is compared independently. A missing value in one statistic never removes another
// statistic's comparison, because a population can legitimately lack one and still have
// another.
enum class ComparisonStatistic {
    Min,
    Max,
    Mean,
    Median,
    P95,
};

const char* comparisonStatisticName(ComparisonStatistic s);

// ---------------------------------------------------------------------------
// Per-side accounting
// ---------------------------------------------------------------------------

// The population behind one side, carried forward rather than reduced.
//
// Observed, eligible and excluded are all preserved because a delta computed from
// populations of very different sizes is not the same kind of statement as one computed
// from equal sizes, and the reader has to be able to see that.
struct ComparisonSampleAccounting {
    std::size_t observed = 0;
    std::size_t eligible = 0;
    std::size_t excluded = 0;

    bool hasEligible() const { return eligible > 0; }
};

// ---------------------------------------------------------------------------
// Why a number is absent
// ---------------------------------------------------------------------------

// Named reasons, so an absent number is never just an absent number.
enum class MetricUnavailableReason {
    NoEligibleSamples,  // the side had no eligible performance sample
    LeftStatisticAbsent,
    RightStatisticAbsent,
    ZeroLeftReference,   // dividing by a measured zero has no answer
};

const char* metricUnavailableReasonName(MetricUnavailableReason r);

// ---------------------------------------------------------------------------
// One statistic of one candidate
// ---------------------------------------------------------------------------

struct StatisticComparison {
    ComparisonStatistic statistic = ComparisonStatistic::Mean;

    // The two aggregate values, as S6-4 computed them. Not recomputed here: this layer
    // consumes the aggregate and never re-reads a sample.
    std::optional<double> leftValue;
    std::optional<double> rightValue;

    // right - left, in the metric's own unit (milliseconds).
    std::optional<double> absoluteDeltaMs;

    // ((right - left) / left) * 100. Absent when left is 0, which is a measured zero and
    // not a missing value.
    std::optional<double> relativeDeltaPercent;

    // right / left. Absent for the same reason as relativeDeltaPercent.
    std::optional<double> ratio;

    // Whether left was exactly zero. Kept separately so "the ratio is absent because the
    // reference was zero" can be told from "the ratio is absent because a value was
    // missing".
    bool leftValueIsZero = false;

    // Why any of the above is absent. Never empty when something is absent, and empty
    // when everything is present.
    std::vector<MetricUnavailableReason> unavailable;

    bool complete() const {
        return leftValue.has_value() && rightValue.has_value() && absoluteDeltaMs.has_value() &&
               relativeDeltaPercent.has_value() && ratio.has_value();
    }
    bool anyAvailable() const { return absoluteDeltaMs.has_value(); }
};

// ---------------------------------------------------------------------------
// The result for one candidate
// ---------------------------------------------------------------------------

struct ComparisonMetrics {
    // The candidate this came from, kept whole: it carries the orientation, the run
    // references, the provenance states and the limitations, and all of those belong with
    // the numbers. Dropping them would detach a delta from what was compared.
    ComparisonCandidate candidate;

    MetricLevel metricLevel = MetricLevel::ModeElapsed;
    MetricResolution metricResolution = MetricResolution::Recorded;

    // Ordered by ComparisonStatistic, so the output order is fixed.
    std::vector<StatisticComparison> statistics;

    // Lookup by statistic. Returns nullptr when the statistic was not computed.
    const StatisticComparison* find(ComparisonStatistic s) const;

    // True when at least one statistic produced an absolute delta.
    bool anyAvailable() const;
};

// ---------------------------------------------------------------------------
// Layering
// ---------------------------------------------------------------------------

// Computes metrics for one candidate.
//
// `aggregation` is the S6-4 result the candidate was generated from. This function reads no
// journal, performs no grouping and computes no statistic of its own: every value comes
// from the aggregate it is given. Passing a candidate built from a different aggregation
// yields a metric with absent statistics rather than a wrong number.
ComparisonMetrics computeComparisonMetrics(const ComparisonCandidate& candidate,
                                            const BenchmarkAggregation& aggregation);

// Computes metrics for every candidate, preserving order.
std::vector<ComparisonMetrics> computeAllComparisonMetrics(const ComparisonAnalysis& analysis,
                                                           const BenchmarkAggregation& aggregation);

}  // namespace msf