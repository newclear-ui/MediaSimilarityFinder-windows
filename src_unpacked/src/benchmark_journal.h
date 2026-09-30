#pragma once
// S3 Benchmark Journal — append-only JSONL evidence and recovery.
//
// SCOPE. The journal is the durable, recoverable record of a benchmark run. The
// terminal UI and summary.json are views over it; neither is ever the source.
//
// Record contract
// --------------
// Every line is one flat JSON object terminated by '\n'. Records are appended in
// this order per file (S2's onCaseComplete is the commit point):
//
//   mode_result(AUTO), mode_result(CPU), mode_result(GPU-max), case_complete
//
// A case is COMMITTED only when its case_complete record exists. Mode records
// without a following case_complete are an incomplete transaction and are
// classified as such during recovery. This is the storage-level expression of
// S2's per-case contract; S2 itself is unchanged.
//
// Idempotency
// -----------
// Every record carries a recordId. A deterministic id is used for case commits
// (case_complete:<runId>:<caseId>) so a re-emitted identical record is ignored,
// while a re-emitted record with a DIFFERENT payload is reported as an anomaly
// rather than silently overwriting history.

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "benchmark_core.h"   // BenchmarkModeResult / BenchmarkCaseResult / BenchmarkRun
#include "benchmark_store.h" // kBenchmarkJournalSchemaVersion

namespace msf {

enum class JournalEventType {
    RunStarted,
    ModeResult,
    CaseComplete,
    RunFinished,
    RunCancelled
};

const char* journalEventTypeName(JournalEventType t);
bool journalEventTypeFromName(const std::string& s, JournalEventType& out);

// Wall-clock label stamped onto every journal record.
//
// Deliberately separate from the timestamp helper inside benchmark_core.cpp: the
// core labels a run with local time and no zone suffix, while journal records
// carry the 'Z' suffix. They are not unified here, because doing so would change
// the run metadata S2 already produces.
std::string journalTimestamp();

// One replayed mode result. Fields mirror BenchmarkModeResult so a recovery can
// reconstruct the exact per-mode state, not just the case aggregate.
struct ReplayedModeResult {
    std::string recordId;
    GpuBackendKind requestedMode = GpuBackendKind::Auto;
    GpuBackendKind effectiveMode = GpuBackendKind::Cpu;
    BenchmarkStatus status = BenchmarkStatus::Skipped;
    bool started = false, completed = false;
    double elapsedMs = 0.0;
    BenchmarkScanSummary summary;
    std::string errorMessage;
};

struct ReplayedCase {
    std::string caseId;
    std::string path;
    MediaKind media = MediaKind::Unknown;
    BenchmarkStatus status = BenchmarkStatus::Skipped;
    bool committed = false;          // a case_complete record was seen
    double elapsedMs = 0.0;
    std::string errorMessage;
    std::vector<ReplayedModeResult> modes;
};

enum class JournalAnomalyKind {
    DuplicateIdentical,   // same recordId, same payload: ignored
    DuplicateConflicting, // same recordId, different payload: reported, never overwritten
    TruncatedTail,        // final line had no newline: discarded
    MidFileCorruption     // a complete line failed to parse: fatal, no auto recovery
};

struct JournalAnomaly {
    JournalAnomalyKind kind = JournalAnomalyKind::TruncatedTail;
    std::string detail;
    std::size_t lineNumber = 0;
};

struct JournalReplay {
    bool runStarted = false;
    bool runFinished = false;
    bool runCancelled = false;
    std::string runId, suiteId, buildVersion, startedAt, completedAt, completionReason;

    // Build provenance of the run. Empty means the journal predates the field,
    // which is a normal state for a pre-existing journal and not an error: the
    // replay never invents a value for a record that did not carry one.
    std::string gitCommit;
    std::vector<ReplayedCase> cases;          // committed cases, in commit order
    std::vector<ReplayedCase> incompleteCases;// modes seen without a commit
    std::vector<JournalAnomaly> anomalies;
    bool fatal = false;                       // mid-file corruption: caller must not auto-repair

    std::size_t committedCount() const { return cases.size(); }
    std::size_t incompleteCount() const { return incompleteCases.size(); }
};

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

// Append-only writer. Opens the file in append mode and keeps the stream open for
// the life of a run so that per-case records land as they happen rather than in
// one batch at the end.
class BenchmarkJournalWriter {
public:
    BenchmarkJournalWriter() = default;
    ~BenchmarkJournalWriter();
    BenchmarkJournalWriter(const BenchmarkJournalWriter&) = delete;
    BenchmarkJournalWriter& operator=(const BenchmarkJournalWriter&) = delete;

    bool open(const std::string& runsJsonlPath);

    // run identity / lifecycle
    bool writeRunStarted(const BenchmarkRun& run);
    bool writeRunFinished(const BenchmarkRun& run);
    bool writeRunCancelled(const BenchmarkRun& run, const std::string& reason);

    // Per-case commit. Emits one mode_result per entry of case.modeResults in the
    // given order, then the case_complete marker. The caller is S2's
    // onCaseComplete, so this is the only commit point.
    bool writeCaseComplete(const BenchmarkRun& run, const BenchmarkCaseResult& caseResult);

    // Flush pushes bytes to the OS. This is process-crash safety, not power-loss
    // durability; see the durability note in writeFileAtomic's neighbourhood in
    // benchmark_store.cpp.
    bool flush();
    void close();
    bool isOpen() const { return out_.is_open(); }
    std::size_t recordCount() const { return records_; }

private:
    bool appendLine(const std::string& line);

    std::string path_;
    std::ofstream out_;
    std::size_t records_ = 0;
};

// ---------------------------------------------------------------------------
// Reader / recovery
// ---------------------------------------------------------------------------

// Replays a journal. Never modifies the file.
//
//   * A final line without a terminating newline is a truncated write and is
//     discarded; everything up to the last complete record is used.
//   * A COMPLETE line that does not parse is mid-file corruption, which is
//     reported as fatal. It is deliberately NOT auto-repaired, because silently
//     discarding interior records would destroy evidence.
//   * Duplicate recordIds with identical payload are ignored; with a different
//     payload they are reported as anomalies and the first record is kept.
//
// runIdFilter selects a single run out of a suite journal that several runs have
// appended to. Pass an empty string to replay every run in the file. Cases are
// tracked per (runId, caseId), so runs that examine the same file never merge.
JournalReplay replayJournal(const std::string& runsJsonlPath,
                            const std::string& runIdFilter = std::string());

// Minimal flat-JSON field readers. Exposed for the tests and for callers that
// need to inspect a record without the full replay. They handle only the flat
// object shape the journal writes.
bool jsonFieldString(const std::string& line, const std::string& key, std::string& out);
bool jsonFieldNumber(const std::string& line, const std::string& key, double& out);
bool jsonFieldBool(const std::string& line, const std::string& key, bool& out);

// Builds a summary body from a replay. The summary is always derived; anything it
// omits may still exist in the journal.
std::string buildSummaryJson(const JournalReplay& replay);

} // namespace msf
