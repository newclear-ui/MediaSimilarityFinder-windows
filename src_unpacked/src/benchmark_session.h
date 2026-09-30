#pragma once
// S3 Benchmark Session — binds the S2 runner to a suite's durable storage.
//
// SCOPE. This is the wiring layer only. It decides nothing about how a file is
// executed: mode ordering, aggregation, cancellation and result preservation all
// stay in BenchmarkRunner (S2). This class turns those S2 hooks into storage
// calls and owns the lifetime of the lock and the journal stream.
//
// Lifecycle, in this order:
//
//   open()                       acquire suite lock
//                                -> prepare the run's runtime tree
//                                -> open the journal stream
//   attach(request)              install hooks + run id + per-mode index dirs
//   BenchmarkRunner::run()       (S2 executes; hooks fire here)
//     onRunStarted               -> run_started
//     onCaseComplete  (per file) -> mode_result xN, then case_complete
//     onRunFinished              -> run_finished or run_cancelled
//                                -> flush + close
//                                -> replay the journal -> summary.json
//
// Two ordering rules are load bearing:
//
//   1. The lock is taken BEFORE the journal is opened. A run that cannot get the
//      lock must not append anything, otherwise a rejected run would still be
//      able to interleave records into the suite's evidence.
//   2. summary.json is always produced by replaying runs.jsonl, never from the
//      in-memory run. That is what makes the journal the single source and lets a
//      deleted summary be regenerated to the same meaning.
//
// Nothing here is written inside a scanned source folder.

#include <cstddef>
#include <functional>
#include <string>

#include "benchmark_core.h"   // BenchmarkRequest / BenchmarkRun / GpuBackendKind
#include "benchmark_journal.h"
#include "benchmark_store.h"

namespace msf {

struct BenchmarkSessionConfig {
    std::string suiteId;
    // Supplied by the caller, not generated during the run: the lock, the journal
    // path and the per-mode runtime directories are all derived from it.
    std::string runId;
    std::string applicationDataRoot; // "Application data root", not the app dir

    std::string sourceRoot;
    std::string sourceRootLabel;
    std::string sourceRootId;
    std::string datasetFingerprint;
    std::string buildVersion;
    MediaScope mediaScope = MediaScope::All;
};

enum class BenchmarkSessionState {
    Closed,   // not opened, or finished
    Busy,     // another run holds this suite's lock
    Open,     // lock held, journal open, execution may proceed
    Failed    // opened unsuccessfully; nothing was written to the journal
};

class BenchmarkSession {
public:
    explicit BenchmarkSession(BenchmarkSessionConfig config);
    ~BenchmarkSession();
    BenchmarkSession(const BenchmarkSession&) = delete;
    BenchmarkSession& operator=(const BenchmarkSession&) = delete;

    // Acquires the suite lock, prepares the runtime tree and opens the journal.
    // Returns false on failure. If isBusy() is true afterwards, the caller must
    // not start execution and must not touch the journal.
    bool open(std::string& errorOut);

    BenchmarkSessionState state() const { return state_; }
    bool isBusy() const { return state_ == BenchmarkSessionState::Busy; }

    const BenchmarkSuitePaths& paths() const { return paths_; }
    const std::string& runsJsonlPath() const { return paths_.runsJsonl; }
    const std::string& summaryPath() const { return paths_.summaryJson; }

    // Installs the run id, the per-mode index application directories and the
    // three journal hooks onto a request. The session must outlive the run.
    void attach(BenchmarkRequest& request);
    void detach(BenchmarkRequest& request);

    // Writes the terminal event, closes the stream, then regenerates summary.json
    // from the journal. Called automatically by the onRunFinished hook.
    //
    // Idempotent: a second call does nothing, so a run cannot end up with two
    // conflicting terminal records.
    bool finalize(const BenchmarkRun& run);

    // Rebuilds summary.json from runs.jsonl alone. Used by recovery.
    bool regenerateSummary(std::string& errorOut);

    // Replays this session's run out of the suite journal.
    JournalReplay replay() const;

    // Removes the run's runtime tree. Explicit, never implicit during recovery:
    // runtime is execution scratch, but a recovery pass must decide a run's state
    // before anything is deleted.
    bool cleanupRuntime();

    std::size_t recordsWritten() const { return writer_.recordCount(); }
    const std::string& lastError() const { return lastError_; }

private:
    void handleRunStarted(const BenchmarkRun& run);
    void handleCaseComplete(const BenchmarkCaseResult& caseResult);
    void handleRunFinished(const BenchmarkRun& run);

    // Suite paths computed from the config alone, used when the session was
    // never opened (recovery replays a journal without holding the lock).
    BenchmarkSuitePaths derivedPaths() const;

    BenchmarkSessionConfig cfg_;
    BenchmarkSuitePaths paths_;
    BenchmarkJournalWriter writer_;
    BenchmarkSuiteLock lock_;
    BenchmarkSessionState state_ = BenchmarkSessionState::Closed;

    BenchmarkRun activeRun_;      // captured by onRunStarted, used to stamp records
    bool runRecorded_ = false;
    bool terminalWritten_ = false;
    std::string lastError_;

    BenchmarkRequest* attached_ = nullptr;
    std::string savedRunId_;
    std::function<void(const BenchmarkCaseResult&)> savedCase_;
    std::function<void(const BenchmarkRun&)> savedStarted_;
    std::function<void(const BenchmarkRun&)> savedFinished_;
    std::function<std::string(GpuBackendKind)> savedModeDir_;
};

} // namespace msf
