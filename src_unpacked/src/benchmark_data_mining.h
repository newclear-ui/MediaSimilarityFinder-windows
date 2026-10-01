#pragma once
// S6 Data-mining: journal discovery, ingestion and normalization.
//
// SCOPE. This layer turns durable S3 journals into an analysis-ready dataset.
// It discovers candidate journals under the benchmark storage root, hands each
// one to S3's own replay, and projects the result into a normalized model that
// the later S6 stages (grouping, aggregation, comparison) will consume.
//
// What this layer deliberately does NOT do:
//
//   * It does not parse or recover journals. Every integrity, idempotency and
//     anomaly decision belongs to msf::replayJournal(), which already
//     implements the S3 contract (truncated tail discard, commitless case,
//     duplicate ignore, conflicting duplicate first-record retention,
//     mid-file corruption fatal). Writing a second, more permissive parser here
//     would fork that contract, which is the single thing S6 must not do.
//   * It does not compare, aggregate, judge a regression, or detect anomalies
//     statistically. Those are S6-3 and later.
//   * It does not add, change or migrate any journal field.
//   * It creates no benchmark status of its own. See IngestRunClass below.
//
// It is library level: no CLI, no report format. That arrives in a later stage.

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "benchmark_core.h"   // GpuBackendKind, BenchmarkStatus, MediaKind, BenchmarkScanSummary

namespace msf {

// ---------------------------------------------------------------------------
// Run classification
// ---------------------------------------------------------------------------
//
// An S6-side classification of whether a run is analysable. It is NOT a
// benchmark status and it never replaces one: BenchmarkStatus continues to carry
// S2's Success / Failed / Cancelled / Skipped, and this enum only answers the
// separate question "may this run be used as analysis input".
enum class IngestRunClass {
    Complete,    // terminal record present, not cancelled, nothing fatal
    Cancelled,   // run_cancelled present: kept, but not a comparison baseline
    Incomplete,  // no terminal record: the run crashed or was still going
    Corrupt,     // S3 replay reported fatal mid-file corruption
    Unavailable, // journal absent, unreadable or empty
};

const char* ingestRunClassName(IngestRunClass c);

// ---------------------------------------------------------------------------
// Exclusion reasons
// ---------------------------------------------------------------------------
//
// Nothing is dropped silently. Every run that is not analysable, and every
// case that cannot be attributed, is counted under a reason so a caller can
// report "42 found -> 37 analysable -> 5 excluded" and name the five.
enum class IngestExclusion {
    None,
    UnreadableJournal,     // could not be opened
    EmptyJournal,          // no records at all
    FatalCorruption,       // S3 replay set fatal; no automatic repair
    IncompleteRun,         // no run_finished / run_cancelled
    CancelledRun,          // present and preserved, but not a baseline
    NoDatasetFingerprint,  // no dataset identity to group or compare by
    TruncatedTail,         // an interrupted final line was discarded by S3
    ConflictingDuplicate,  // same recordId, different payload; first kept
    DuplicateIgnored,      // identical re-emission ignored by S3
    CommitlessCase,        // mode records without a case_complete commit
};

const char* ingestExclusionName(IngestExclusion e);

// ---------------------------------------------------------------------------
// Build provenance
// ---------------------------------------------------------------------------
//
// Three states that must never be conflated. A legacy journal predating the
// field is not the same as a build where git was unavailable, and neither is the
// same as a known commit. S6 never fills a legacy gap with the current git
// value: that would attribute a measurement to a commit that did not produce it.
enum class GitCommitState {
    Known,    // a short commit id was recorded
    Unknown,  // the writer recorded the literal "unknown"
    Legacy,   // the journal predates the field and carries no value
};

const char* gitCommitStateName(GitCommitState s);

// ---------------------------------------------------------------------------
// Dataset identity
// ---------------------------------------------------------------------------
//
// Three states that must stay apart. The journal records the field always, and an
// empty value is a real, meaningful answer: it means the source could not be
// measured (DatasetFingerprint state not_available / failed). Collapsing that
// into "absent" would claim the identity is merely unknown, which is a different
// statement and would let a later stage group an unmeasurable dataset as if it
// were merely undocumented.
enum class DatasetIdentityState {
    Missing,  // the field is not present on the run_started record
    Empty,    // the field is present and empty: the source could not be measured
    Valid,    // a fingerprint was measured
};

const char* datasetIdentityStateName(DatasetIdentityState s);

// ---------------------------------------------------------------------------
// Timestamp resolution
// ---------------------------------------------------------------------------
//
// S3 writes startedAt and completedAt with "%Y-%m-%dT%H:%M:%S": no fractional
// part. Every duration S6 derives from them therefore inherits that resolution.
//
// This is exposed as one value rather than as a per-run field, because it is a
// property of how the journal is written and is the same for every run. It exists
// so a consumer can state the limit instead of quietly reporting a whole number
// of milliseconds that was never measured.
enum class TimestampResolution {
    OneSecond,  // the only value: S3 stamps to whole seconds
};

TimestampResolution benchmarkTimestampResolution();
const char* timestampResolutionName(TimestampResolution r);

// ---------------------------------------------------------------------------
// Normalized model
// ---------------------------------------------------------------------------

// One mode execution of one case.
//
// elapsedMs is optional on purpose: an unmeasured mode has NO elapsed time, and
// storing 0.0 for it would make "instantly fast" and "never ran" print the same
// number. A skipped or not-started mode therefore leaves it empty.
struct IngestModeResult {
    GpuBackendKind requestedMode = GpuBackendKind::Auto;
    GpuBackendKind effectiveMode = GpuBackendKind::Cpu;
    BenchmarkStatus status = BenchmarkStatus::Skipped;
    bool started = false;
    bool completed = false;
    std::optional<double> elapsedMs;
    BenchmarkScanSummary summary;
    std::string errorMessage;
};

// One committed case.
//
// elapsedMs is the case value S2 defines: the sum of its mode elapsed times,
// excluding discovery. It is NOT a run wall duration and the two are never
// compared on the same axis.
struct IngestCase {
    std::string caseId;
    std::string path;
    MediaKind media = MediaKind::Unknown;
    BenchmarkStatus status = BenchmarkStatus::Skipped;
    double elapsedMs = 0.0;
    std::vector<IngestModeResult> modes;
};

// One run, normalized.
struct IngestRun {
    // --- provenance: where this record came from -----------------------------
    std::string sourceJournalPath;
    std::string runId;
    std::string suiteId;

    // Dataset identity, kept in two parts so the three states stay apart.
    //
    // datasetFingerprint is empty AND datasetIdentity is Missing when the
    // run_started record has no such field; empty with datasetIdentity Empty when
    // the field is present but blank, which means the source was not measurable.
    // A missing or empty identity is never replaced with a generated one.
    std::optional<std::string> datasetFingerprint;
    DatasetIdentityState datasetIdentity = DatasetIdentityState::Missing;

    std::optional<std::string> buildVersion;
    std::optional<std::string> gitCommit;
    GitCommitState gitCommitState = GitCommitState::Legacy;

    // The run's own benchmark status, straight from the terminal record, using
    // S2's vocabulary. Absent when the run has no terminal record at all.
    //
    // This is separate from runClass on purpose: runClass answers "may this be
    // analysed", this answers "what did the benchmark record", and neither
    // replaces the other.
    std::optional<BenchmarkStatus> runStatus;

    // Preserved verbatim from the journal. Never converted to UTC and never
    // reinterpreted: the S3 timestamp formatter writes a local time with a
    // literal Z, and this layer does not second-guess that.
    std::optional<std::string> mediaScope;
    std::string startedAt;
    std::string completedAt;

    // --- derived ------------------------------------------------------------
    // completedAt - startedAt, in milliseconds.
    //
    // Derived, and deliberately computed from these two fields ONLY. Both come
    // from the same formatter (local time, no zone suffix), so they are
    // comparable with each other; the journal record timestamp is not, and is
    // never used for a duration or an ordering.
    //
    // RESOLUTION IS ONE SECOND, see benchmarkTimestampResolution(). S3 writes
    // startedAt and completedAt with no fractional part, so this value is always a
    // multiple of 1000 ms and any run shorter than a second yields exactly 0.
    // A stored 0 therefore means "started and completed within the same second",
    // NOT "took no time", and must never be read as a measured zero duration. It
    // is absent only when one of the two stamps is absent, which is the normal
    // state for a cancelled run because run_cancelled carries no completedAt.
    std::optional<double> wallDurationMs;

    // --- fields the journal does not carry -----------------------------------
    // Present as absence, on purpose, so later stages can report them as
    // missing instead of substituting 0 / false / "unknown". They are empty
    // today; if the journal schema grows, this is where the values arrive.
    std::optional<unsigned> distance;
    std::optional<std::string> resourcePolicy;
    std::optional<std::string> gpuBackend;

    // --- payload ------------------------------------------------------------
    IngestRunClass runClass = IngestRunClass::Unavailable;
    std::vector<IngestCase> cases;
    std::vector<IngestExclusion> exclusions;
};

// ---------------------------------------------------------------------------
// Stable key material
// ---------------------------------------------------------------------------

// The comparable identity of a run, gathered in one place.
//
// This is NOT a grouping decision. It is the set of fields a later grouping stage
// may choose from, collected so that grouping cannot invent a key the model does
// not actually carry, and cannot reach into a run for a field that is absent
// without noticing. Which combination of these becomes a group key is S6-3's
// choice, not this stage's.
struct RunGroupingKey {
    std::optional<std::string> datasetFingerprint;
    DatasetIdentityState datasetIdentity = DatasetIdentityState::Missing;
    std::optional<std::string> mediaScope;
    std::optional<std::string> buildVersion;
    std::optional<std::string> gitCommit;
    GitCommitState gitCommitState = GitCommitState::Legacy;
};

RunGroupingKey groupingKey(const IngestRun& run);

// ---------------------------------------------------------------------------
// Measured / Derived / Missing
// ---------------------------------------------------------------------------

enum class ValueOrigin {
    Measured,  // recorded in the journal
    Derived,   // computed by S6
    Missing,   // absent from the source and deliberately not substituted
};

// The run-level fields whose origin is part of the data contract. Case-level and
// mode-level fields are Measured by construction: they are copied straight from
// their record and S6 computes nothing at those levels.
enum class RunField {
    DatasetFingerprint,
    BuildVersion,
    GitCommit,
    MediaScope,
    RunStatus,
    WallDurationMs,
    Distance,
    ResourcePolicy,
    GpuBackend,
};

ValueOrigin valueOrigin(const IngestRun& run, RunField field);
const char* valueOriginName(ValueOrigin o);

// ---------------------------------------------------------------------------
// Exclusion records
// ---------------------------------------------------------------------------

// One exclusion, with enough provenance to identify which run and which journal
// it came from. S3's anomaly names are not renamed here: the reason is carried as
// the S6 exclusion that maps from them, and the mapping lives in one switch.
struct IngestExclusionRecord {
    std::string sourceJournalPath;
    std::string runId;
    std::string suiteId;
    IngestExclusion reason = IngestExclusion::None;
};

// How many runs were excluded for one reason.
struct IngestExclusionTally {
    IngestExclusion reason = IngestExclusion::None;
    std::size_t count = 0;
};

// The whole ingestion outcome, and the analysis dataset S6-2 hands forward.
//
// This is the aggregate the later stages consume: runs, the runs that were kept
// out and why, and the accounting that makes "42 found -> 37 analysable ->
// 5 excluded" reportable. It is analysis input only and replaces no S2/S3
// runtime model.
//
// Presentation is deliberately not modelled here: console, JSON, CSV and Markdown
// are all views over this same structure, so the report format stays a separate
// later decision.
struct NormalizedBenchmarkData {
    // Analysable runs, in a deterministic order.
    std::vector<IngestRun> runs;

    // Every run found, including the excluded ones, so a caller can inspect a
    // rejected run without re-reading the journal. Journals with no run identity
    // at all are NOT here; they are counted in journalsWithoutRuns.
    std::vector<IngestRun> excludedRuns;

    // Every exclusion, flattened, with the journal and run it belongs to. The
    // per-run lists above and this flat view carry the same information; this one
    // exists so a report can enumerate reasons without walking the runs.
    std::vector<IngestExclusionRecord> exclusionRecords;

    std::size_t journalsDiscovered = 0;
    std::size_t journalsUnreadable = 0;

    // Journals that contain no run_started at all: a zero-byte file, or a suite
    // directory that never ran. Counted separately because there is no run
    // identity to exclude, so counting them as excluded runs would break
    // "found == accepted + excluded".
    std::size_t journalsWithoutRuns = 0;
    std::size_t runsFound = 0;
    std::size_t runsAccepted = 0;
    std::size_t runsExcluded = 0;
    std::size_t commitlessCases = 0;

    // Sorted by reason so the report itself is deterministic.
    std::vector<IngestExclusionTally> exclusions;

    // Whether any journal reported fatal corruption. A caller that must not act
    // on partial data can check this before using runs.
    bool anyFatal = false;
};

// S6-1 named this type IngestResult. The name is kept so existing callers and its
// test keep compiling; S6-2 is where the dataset contract was fixed, and
// NormalizedBenchmarkData is that name.
using IngestResult = NormalizedBenchmarkData;

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

// Discovers and ingests every Console journal under `applicationDataRoot`.
//
// Discovery follows the S3 layout already in use and invents no new convention:
// <root>/Benchmark/Console/suite-*/runs.jsonl, built from benchmarkSuitePaths()
// so the path policy stays owned by S3. The GUI snapshot tree and the legacy
// benchmark output are never read.
//
// Only runs.jsonl is treated as an authoritative candidate. summary.json is
// derived and deliberately not read here.
//
// The result is deterministic: journals are processed in sorted path order and
// runs in sorted runId order, so the same tree always yields the same output
// regardless of filesystem enumeration order.
//
// One journal is replayed at a time and released before the next, so peak memory
// stays proportional to the largest single journal rather than to the whole
// tree.
IngestResult ingestBenchmarks(const std::string& applicationDataRoot);

// Convenience wrapper returning the analysis dataset under its S6-2 name.
// Identical to ingestBenchmarks; the distinct name marks the type whose contract
// is now fixed.
inline NormalizedBenchmarkData normalizeBenchmarks(const std::string& applicationDataRoot) {
    return ingestBenchmarks(applicationDataRoot);
}

}  // namespace msf