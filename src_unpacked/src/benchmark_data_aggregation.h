#pragma once
// S6 Data-mining: aggregation.
//
// SCOPE. This layer turns the S6-3 cohorts into statistics. It sits above grouping
// and re-derives nothing: cohorts, provenance and identities are taken as given.
//
// Three populations are kept apart and never mixed, because they measure different
// things:
//
//   Mode elapsed    one mode_result.elapsedMs, one mode of one file
//   Case elapsed    one case_complete.elapsedMs, S2's sum of that case's modes
//   Run wall        completedAt - startedAt, derived, ONE SECOND resolution
//
// A "median" of each is a different quantity. They are carried in separate types
// with different resolutions so a later comparison cannot line them up as if they
// were interchangeable.
//
// It computes no regression percentage, no threshold, no anomaly verdict, no ranking
// and no claim of statistical confidence. A single sample produces a mean and a
// median, and the sample count travels with the result so a later stage can refuse
// to over-read it.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "benchmark_core.h"           // BenchmarkStatus, GpuBackendKind
#include "benchmark_data_grouping.h"  // S6-3 cohort keys
#include "benchmark_data_mining.h"    // S6-2 dataset, TimestampResolution

namespace msf {

// ---------------------------------------------------------------------------
// Populations
// ---------------------------------------------------------------------------

// What a sample measures. Present on every statistics block so the metric kind is
// never implicit.
enum class MetricLevel {
    ModeElapsed,
    CaseElapsed,
    RunWallDuration,
};

const char* metricLevelName(MetricLevel l);

// How precisely a metric is known.
//
// Recorded values come straight from S2's per-record timing and keep the
// resolution they were written with. The run wall duration is different: it is
// derived from second-resolution stamps, so every value is a multiple of 1000 ms
// and any run shorter than a second yields exactly 0. That 0 means "started and
// completed within the same second", NOT "no time passed".
enum class MetricResolution {
    Recorded,   // written per record by S2 (mode and case elapsed)
    OneSecond,  // derived from second-resolution timestamps (run wall duration)
};

const char* metricResolutionName(MetricResolution r);

// Why a sample is not a performance sample.
//
// This is about POPULATION membership, not about quality of the run. A Skipped
// mode and a Failed mode are both perfectly recorded facts; they just are not
// timings of the thing being compared. In particular a Skipped mode's zero is
// never rewritten as a zero millisecond measurement and never enters a mean.
enum class SampleExclusion {
    None,
    Skipped,        // never executed: not available, not permitted, cancelled before start
    Failed,         // executed and errored
    Cancelled,      // started, then interrupted
    MissingElapsed, // eligible status, but the duration was not recorded
    Invalid,        // no usable status, so eligibility cannot be decided
};

const char* sampleExclusionName(SampleExclusion r);

struct StatusTally {
    BenchmarkStatus status = BenchmarkStatus::Skipped;
    std::size_t count = 0;
};

struct SampleExclusionTally {
    SampleExclusion reason = SampleExclusion::None;
    std::size_t count = 0;
};

// ---------------------------------------------------------------------------
// Sample accounting
// ---------------------------------------------------------------------------

// How a population was formed.
//
// The invariant observed == eligible + excluded holds at every level, and the
// reason for each exclusion is kept. A count of excluded samples with no reason is
// not an acceptable result, because it cannot be told apart from a bug.
struct SampleAccounting {
    std::size_t observed = 0;   // samples seen, whatever their state
    std::size_t eligible = 0;   // samples that carry a usable measurement
    std::size_t excluded = 0;   // observed - eligible
    std::vector<SampleExclusionTally> exclusions;  // sorted by reason

    void addExcluded(SampleExclusion reason);
    bool balanced() const { return observed == eligible + excluded; }
};

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

// Distribution summary of one metric over one eligible population.
//
// Every field is optional and is absent when there is no eligible sample at all.
// An absent mean is not a zero, and a zero mean is not an absent one.
//
// Rounding: all reported values are rounded to 6 decimal places
// (aggregationRoundMs). The samples are stored as double milliseconds, so raw
// sums are reproducible but a printed mean is not stable across a different
// summation order; rounding fixes the printed form without touching the inputs.
//
// Median: for an odd count the middle sample, for an even count the mean of the two
// middle samples.
//
// p95: nearest-rank on the sorted samples, index = ceil(0.95 * n) - 1. This is
// reported for every population including n = 1 and n = 2, because hiding it would
// be worse than showing it next to its sample count; interpreting it is a later
// stage's job.
struct DurationStatistics {
    MetricLevel level = MetricLevel::ModeElapsed;
    MetricResolution resolution = MetricResolution::Recorded;

    std::size_t sampleCount = 0;

    std::optional<double> minMs;
    std::optional<double> maxMs;
    std::optional<double> meanMs;
    std::optional<double> medianMs;
    std::optional<double> p95Ms;

    bool empty() const { return sampleCount == 0; }
};

// The single rounding rule for every reported millisecond value.
double aggregationRoundMs(double v);

// ---------------------------------------------------------------------------
// Aggregates
// ---------------------------------------------------------------------------
//
// The shape follows the S6-3 hierarchy: dataset, then media scope, then build
// provenance, then mode. Each level nests the next rather than being flattened into
// one composite key, so a scope cohort can be examined without pretending it was
// also a build cohort.

struct AggregatedMode {
    ModeCohortKey key;

    // Every mode in this cohort, whatever its status. The 60 real
    // requested=CUDA/effective=CPU/SKIPPED records appear here even though they
    // contribute no elapsed time to modeElapsed.
    std::vector<StatusTally> statuses;

    SampleAccounting accounting;   // over mode results in this cohort
    DurationStatistics elapsed;     // mode_result.elapsedMs, Success only

    // Case-level elapsed restricted to the cases in which THIS mode semantics
    // actually ran.
    //
    // This is a FILTER on the case population, NOT an attribution of the case total to
    // this mode. `case_complete.elapsedMs` is by S2 definition the sum of ALL of that
    // case's modes, so splitting it between modes would mean dividing a measured total
    // by a factor the journal does not record.
    //
    // The consequence is that these populations OVERLAP: a case with two successful
    // modes is counted in both. They are therefore never added across modes, and
    // summing them would double count. The overlap is visible here because
    // caseAccounting reports what was observed and what was excluded per cohort.
    //
    // A mode that was Skipped contributes nothing: a requested=CUDA/effective=CPU
    // /SKIPPED case is not a CPU performance sample at any level.
    std::vector<StatusTally> caseStatuses;
    SampleAccounting caseAccounting;
    DurationStatistics caseElapsed;  // MetricLevel::CaseElapsed, Recorded
};

struct AggregatedBuild {
    BuildCohortKey key;
    bool commitComparable = false;

    std::vector<AggregatedMode> modes;   // sorted by ModeCohortKey

    // Run-level metrics for the runs of this build. Separate from the mode
    // population above and never added to it.
    std::vector<StatusTally> runStatuses;
    SampleAccounting runAccounting;
    DurationStatistics runWallDuration;  // MetricLevel::RunWallDuration, OneSecond

    // Case-level elapsed for the cases of this build, with the same eligibility rule as
    // the scope and dataset levels (case status Success).
    //
    // S6-5 needs this axis. Without it, two builds over the same dataset and scope
    // cannot be compared on case elapsed at all, because the scope and dataset
    // aggregates carry no per-build breakdown. The measurement already exists in the
    // normalized data; this is a projection of it, not a new measurement.
    //
    // The build totals equal the sum of its scopes' contributions, but this block and
    // the scope block are the SAME samples seen from two levels. They are alternative
    // views of one population and are never added together.
    std::vector<StatusTally> caseStatuses;
    SampleAccounting caseAccounting;
    DurationStatistics caseElapsed;  // MetricLevel::CaseElapsed, Recorded
};

struct AggregatedScope {
    ScopeCohortKey key;
    std::vector<AggregatedBuild> builds;  // sorted by BuildCohortKey

    std::vector<StatusTally> caseStatuses;
    SampleAccounting caseAccounting;
    DurationStatistics caseElapsed;  // case_complete.elapsedMs, Success only

    std::vector<StatusTally> runStatuses;
    SampleAccounting runAccounting;
    DurationStatistics runWallDuration;
};

struct AggregatedDataset {
    DatasetCohortKey key;
    std::vector<AggregatedScope> scopes;  // sorted; images / videos / all stay apart

    std::vector<StatusTally> caseStatuses;
    SampleAccounting caseAccounting;
    DurationStatistics caseElapsed;

    std::vector<StatusTally> runStatuses;
    SampleAccounting runAccounting;
    DurationStatistics runWallDuration;
};

// One file across runs. A separate view, because per-file comparison is a different
// question from per-cohort comparison.
struct AggregatedCase {
    CaseCohortKey key;
    std::string path;

    std::vector<StatusTally> statuses;
    SampleAccounting accounting;
    DurationStatistics elapsed;
};

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------

struct BenchmarkAggregation {
    std::vector<AggregatedDataset> datasets;   // dataset -> scope -> build -> mode
    std::vector<AggregatedCase> cases;          // dataset + case

    // Whole-dataset populations, for a top-level sanity check. These must equal the
    // same metric summed down the hierarchy; they exist so a mismatch is visible
    // rather than inferred.
    SampleAccounting modePopulation;
    SampleAccounting casePopulation;
    SampleAccounting runPopulation;

    // Samples the run set could not be placed under any dataset scope, because the
    // run had no usable dataset identity. Counted, never merged into a cohort and
    // never silently dropped.
    std::size_t runsOutsideDatasetScope = 0;
    std::size_t modeRecordsOutsideDatasetScope = 0;
    std::size_t caseRecordsOutsideDatasetScope = 0;

    // Runs whose runId is shared with another journal.
    //
    // runId alone is NOT a safe identity in a real store: the S6 fixtures
    // suite-ORDER-A/B/C each wrote a journal under the same runId, so the accepted
    // set contains more runs than it contains distinct runIds. Aggregation therefore
    // keys per-file populations on (sourceJournalPath, runId), which is unambiguous,
    // and reports the collision count here rather than quietly merging or quietly
    // double counting. Deciding whether those runs are one measurement recorded three
    // times or three separate measurements needs evidence the journal does not carry.
    std::size_t runsWithCollidingIdentity = 0;
    std::size_t distinctRunIds = 0;
};

// Aggregates the cohorts produced by S6-3.
//
// `data` supplies the normalized values and `grouping` supplies the cohort
// structure. Both must describe the same ingestion; passing a grouping built from a
// different dataset is a caller error and is not detected here.
BenchmarkAggregation aggregateBenchmarks(const NormalizedBenchmarkData& data,
                                          const BenchmarkGrouping& grouping);

}  // namespace msf