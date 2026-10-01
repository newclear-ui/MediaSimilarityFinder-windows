#pragma once
// S6 Data-mining: comparison eligibility and candidate generation.
//
// SCOPE. This stage does NOT decide which side is faster. It decides which two
// populations may be compared at all, and records the ones that may not together with
// the reason. Producing a candidate is not a verdict: no regression percentage, no
// speedup, no threshold, no anomaly and no winner is computed here.
//
// Two rules shape everything below.
//
// First, eligibility here is an ANALYSIS state about comparability. It is not a
// benchmark execution status and shares no vocabulary with S2's BenchmarkStatus, so
// a Cancelled run and a comparison that cannot be made never look alike.
//
// Second, a candidate is POPULATION versus POPULATION, not a matching of individual
// runs. Runs repeat: pairing run 1 of build A against run 1 of build B would throw away
// the rest and imply a correspondence the data does not contain. So each side is a whole
// aggregate with its own sample count, and the run references behind it are kept for
// traceability.
//
// A candidate is never a claim that conditions are equal. distance, resourcePolicy and
// GPU backend are absent from the journal, and the environment is uncontrolled, so every
// candidate carries those limitations rather than silently dropping them.

#include <cstddef>
#include <string>
#include <vector>

#include "benchmark_core.h"            // GpuBackendKind
#include "benchmark_data_aggregation.h" // MetricLevel, MetricResolution
#include "benchmark_data_mining.h"     // GitCommitState

namespace msf {

// ---------------------------------------------------------------------------
// Run identity
// ---------------------------------------------------------------------------

// A run's real identity.
//
// runId alone is NOT unique in a real store: the fixtures suite-ORDER-A/B/C each wrote
// a journal under the same runId, so 31 accepted runs carry only 29 distinct runIds.
// Keying anything on runId alone would keep one of the three and silently drop the
// others. The pair (sourceJournalPath, runId) is what actually identifies a run.
//
// A duplicate of the SAME (journal, runId) is a different question and is already
// resolved by S6-1/S6-2 recovery; this type does not try to re-decide it.
struct RunReference {
    std::string sourceJournalPath;
    std::string runId;

    bool operator<(const RunReference& o) const {
        if (sourceJournalPath != o.sourceJournalPath) return sourceJournalPath < o.sourceJournalPath;
        return runId < o.runId;
    }
    bool operator==(const RunReference& o) const {
        return sourceJournalPath == o.sourceJournalPath && runId == o.runId;
    }
    // Display form, only for diagnostics. Never used as a key.
    std::string label() const { return sourceJournalPath + " :: " + runId; }
};

// ---------------------------------------------------------------------------
// Eligibility
// ---------------------------------------------------------------------------

// Whether two populations may be compared. An analysis state, not an execution status.
//
//   Eligible              the two sides may be compared, with the stated limitations
//   MismatchedDataset     different datasetFingerprint, so not the same population
//   MismatchedScope       different mediaScope; all/images/videos never mix
//   MismatchedMetric      different metric level or resolution
//   MismatchedMode        different requested/effective mode semantics
//   MissingProvenance     at least one side lacks a Known commit, so no commit-level
//                         comparison is possible. Legacy and Unknown never merge, and
//                         Legacy+Legacy is not assumed to be the same binary either
//   SameProvenance        identical build provenance, so there is nothing to compare
//   NoComparableSamples   at least one side has no eligible performance sample
//   InsufficientData      structurally nothing to pair
enum class ComparisonEligibility {
    Eligible,
    MismatchedDataset,
    MismatchedScope,
    MismatchedMetric,
    MismatchedMode,
    MissingProvenance,
    SameProvenance,
    NoComparableSamples,
    InsufficientData,
};

const char* comparisonEligibilityName(ComparisonEligibility e);

// What differs between the two sides.
//
// Build and mode comparisons are separate dimensions, never collapsed into one
// "comparison" type: "build A got slower" and "CPU got slower than AUTO" answer
// different questions and carry different limits.
enum class ComparisonDimension {
    BuildProvenance,  // same dataset, scope, metric and mode; different known commit
    ModeSemantics,    // same dataset, scope, metric and build; different mode semantics
};

const char* comparisonDimensionName(ComparisonDimension d);

// ---------------------------------------------------------------------------
// Limitations carried by every candidate
// ---------------------------------------------------------------------------

// What the data cannot support, kept rather than dropped.
//
// These are recorded so that a candidate is never mistaken for a controlled
// measurement. In particular a missing field is NOT the same condition as a matching
// one: the journal simply does not record these at all.
struct ComparisonLimitations {
    // Not present in the journal. Their absence is unknown-unknown, not agreement.
    bool distanceUnavailable = true;
    bool resourcePolicyUnavailable = true;
    bool gpuBackendUnavailable = true;
    // No controlled environment has been defined. S2-PERF already accepted process
    // isolation and the OS filesystem cache as uncontrolled.
    bool controlledEnvironmentUndefined = true;
    bool processIsolationUncontrolled = true;
    bool filesystemCacheUncontrolled = true;
    // Repeated runs do not exist in sufficient number; a small n is not a
    // statistically sufficient one.
    bool repeatedRunsInsufficient = true;
    // Timestamps are written at one second resolution, so run wall durations are
    // quantised and small differences are not resolvable.
    bool runWallDurationOneSecondResolution = true;
    // No real run_cancelled sample exists, so cancellation behaviour is unverified.
    bool cancellationUnobserved = true;

    bool empty() const;
    std::size_t count() const;
};

// ---------------------------------------------------------------------------
// One side of a comparison
// ---------------------------------------------------------------------------

struct ComparisonSide {
    // What was measured.
    std::string datasetFingerprint;
    std::string mediaScope;
    MetricLevel metricLevel = MetricLevel::ModeElapsed;
    MetricResolution metricResolution = MetricResolution::Recorded;

    // How the mode was requested and what actually ran. Never merged: AUTO requested
    // with CPU effective is a different observation from CPU requested with CPU
    // effective.
    GpuBackendKind requestedMode = GpuBackendKind::Auto;
    GpuBackendKind effectiveMode = GpuBackendKind::Cpu;

    // Build provenance. gitCommitState is carried beside gitCommit so a Legacy side
    // never looks like a Known one.
    std::string buildVersion;
    GitCommitState gitCommitState = GitCommitState::Legacy;
    std::string gitCommit;

    // Eligible performance samples, taken from S6-4 rather than recomputed.
    std::size_t eligibleSamples = 0;

    // The runs this side's population came from.
    std::vector<RunReference> runReferences;

    // Convenience: whether this side's commit supports commit-level comparison.
    bool commitComparable() const { return gitCommitState == GitCommitState::Known; }
};

// ---------------------------------------------------------------------------
// A candidate
// ---------------------------------------------------------------------------

struct ComparisonCandidate {
    ComparisonDimension dimension = ComparisonDimension::BuildProvenance;

    // Denormalised for convenience; equal to the matching field on both sides.
    std::string datasetFingerprint;
    std::string mediaScope;
    MetricLevel metricLevel = MetricLevel::ModeElapsed;
    MetricResolution metricResolution = MetricResolution::Recorded;

    // left < right by the typed comparison below, so a pair is never emitted twice
    // and left/right are stable across runs.
    ComparisonSide left;
    ComparisonSide right;

    ComparisonLimitations limitations;

    std::size_t totalSampleCount() const { return left.eligibleSamples + right.eligibleSamples; }
    std::size_t smallerSideSampleCount() const {
        return left.eligibleSamples < right.eligibleSamples ? left.eligibleSamples
                                                             : right.eligibleSamples;
    }
};

// Orders a candidate's two sides deterministically.
bool comparisonCandidateLess(const ComparisonCandidate& a, const ComparisonCandidate& b);

// ---------------------------------------------------------------------------
// Rejections
// ---------------------------------------------------------------------------

// A comparison that was considered and refused, with the reason. Losing the reason
// would leave "0 candidates" unexplained.
struct RejectedComparison {
    ComparisonDimension dimension = ComparisonDimension::BuildProvenance;
    ComparisonEligibility reason = ComparisonEligibility::InsufficientData;
    std::string datasetFingerprint;
    std::string mediaScope;
    MetricLevel metricLevel = MetricLevel::ModeElapsed;
    std::string leftLabel;
    std::string rightLabel;
    std::size_t leftSampleCount = 0;
    std::size_t rightSampleCount = 0;
};

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------

struct EligibilityTally {
    ComparisonEligibility reason = ComparisonEligibility::InsufficientData;
    std::size_t count = 0;
};

struct ComparisonAnalysis {
    std::vector<ComparisonCandidate> candidates;
    std::vector<RejectedComparison> rejections;

    // Opportunities considered, and how they ended. opportunities == eligible + rejected.
    std::size_t comparisonOpportunities = 0;
    std::size_t eligibleCandidates = 0;
    std::size_t rejectedCandidates = 0;

    // Every rejection reason with its count, sorted by reason.
    std::vector<EligibilityTally> rejectionReasons;

    // All runs behind the populations considered, as unique (journal, runId) pairs.
    std::vector<RunReference> runReferences;
    std::size_t distinctRunIds = 0;
    std::size_t runsWithCollidingIdentity = 0;

    ComparisonLimitations limitations;

    bool accounted() const { return comparisonOpportunities == eligibleCandidates + rejectedCandidates; }
};

// ---------------------------------------------------------------------------
// Evaluation and generation
// ---------------------------------------------------------------------------

// Judges an arbitrary pair without generating anything.
//
// usableForDimension says whether the comparison kind itself is representable for these
// two sides. One case is structurally impossible: CaseElapsed is aggregated per
// dataset+scope, with no per-build and no per-mode breakdown, so a case-elapsed
// comparison between two builds, or between two modes, cannot be stated from the S6-4
// result at all. Saying so is better than emitting a candidate whose numbers do not
// exist.
ComparisonEligibility evaluateComparison(const ComparisonSide& left, const ComparisonSide& right,
                                          ComparisonDimension dimension, bool usableForDimension);

// Generates every candidate the data supports, and records every refusal.
ComparisonAnalysis findComparisonCandidates(const NormalizedBenchmarkData& data,
                                            const BenchmarkAggregation& aggregation);

}  // namespace msf