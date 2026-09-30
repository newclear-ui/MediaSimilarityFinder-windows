#include "benchmark_session.h"

#include "path_utils.h"

namespace msf {

BenchmarkSession::BenchmarkSession(BenchmarkSessionConfig config)
    : cfg_(std::move(config)) {}

BenchmarkSession::~BenchmarkSession() {
    // Best effort: if a caller abandons the session without finalizing, the
    // journal still gets a terminal record so the run is not left looking
    // unterminated to a later recovery pass.
    if (state_ == BenchmarkSessionState::Open && !terminalWritten_ && runRecorded_) {
        BenchmarkRun partial = activeRun_;
        partial.status = BenchmarkStatus::Cancelled;
        partial.completedAt = journalTimestamp();
        finalize(partial);
    }
    writer_.close();
    lock_.release();
}

bool BenchmarkSession::open(std::string& errorOut) {
    lastError_.clear();

    if (cfg_.suiteId.empty()) {
        lastError_ = "suiteId is required";
        errorOut = lastError_;
        state_ = BenchmarkSessionState::Failed;
        return false;
    }
    if (cfg_.runId.empty()) {
        lastError_ = "runId is required: durable storage must name the run before execution";
        errorOut = lastError_;
        state_ = BenchmarkSessionState::Failed;
        return false;
    }
    if (cfg_.applicationDataRoot.empty()) {
        lastError_ = "applicationDataRoot is required";
        errorOut = lastError_;
        state_ = BenchmarkSessionState::Failed;
        return false;
    }

    paths_ = benchmarkSuitePaths(cfg_.applicationDataRoot, cfg_.suiteId);
    if (!ensureBenchmarkSuiteDir(paths_)) {
        lastError_ = "could not create the suite directory";
        errorOut = lastError_;
        state_ = BenchmarkSessionState::Failed;
        return false;
    }

    // Lock before the journal. A rejected run must not be able to append to the
    // suite's evidence, so nothing above this line may have opened the journal.
    if (!lock_.acquire(paths_.lockFile)) {
        lastError_ = "another run already holds this suite's lock";
        errorOut = lastError_;
        state_ = BenchmarkSessionState::Busy;
        return false;
    }

    // The source label is sanitised here rather than trusted from the caller, so
    // a raw full path can never reach suite.json.
    const std::string label = cfg_.sourceRootLabel.empty()
                                  ? sanitizeSourceLabel(cfg_.sourceRoot)
                                  : cfg_.sourceRootLabel;
    const std::string rootId = cfg_.sourceRootId.empty() ? shortRootId(cfg_.sourceRoot)
                                                          : cfg_.sourceRootId;
    if (!writeSuiteJson(paths_, cfg_.suiteId, label, cfg_.sourceRoot, label, rootId,
                        cfg_.datasetFingerprint, cfg_.buildVersion)) {
        lastError_ = "could not write suite.json";
        errorOut = lastError_;
        lock_.release();
        state_ = BenchmarkSessionState::Failed;
        return false;
    }

    // Runtime tree: execution scratch, never evidence, always inside the suite.
    const std::string runRuntime = benchmarkRunRuntimeDir(paths_, cfg_.runId);
    std::error_code ec;
    std::filesystem::create_directories(path_from_utf8(runRuntime), ec);
    if (ec) {
        lastError_ = "could not create the run runtime directory";
        errorOut = lastError_;
        lock_.release();
        state_ = BenchmarkSessionState::Failed;
        return false;
    }

    if (!writer_.open(paths_.runsJsonl)) {
        lastError_ = "could not open the journal";
        errorOut = lastError_;
        lock_.release();
        state_ = BenchmarkSessionState::Failed;
        return false;
    }

    state_ = BenchmarkSessionState::Open;
    return true;
}

void BenchmarkSession::attach(BenchmarkRequest& request) {
    if (attached_ == &request) return;

    savedRunId_ = request.runId;
    savedCase_ = std::move(request.onCaseComplete);
    savedStarted_ = std::move(request.onRunStarted);
    savedFinished_ = std::move(request.onRunFinished);
    savedModeDir_ = std::move(request.modeIndexApplicationDirectory);

    const std::string runId = cfg_.runId;
    const BenchmarkSuitePaths* paths = &paths_;

    request.runId = runId;

    // Each mode's index lands under <suite>/runtime/run-<id>/<mode>/, so the
    // benchmark index is never the production Search Index and never sits beside
    // it. IndexManager is unchanged: it still writes <applicationDirectory>/Index.
    request.modeIndexApplicationDirectory = [paths, runId](GpuBackendKind mode) {
        return benchmarkModeIndexApplicationDir(*paths, runId, mode);
    };

    request.onRunStarted = [this](const BenchmarkRun& run) { handleRunStarted(run); };
    request.onCaseComplete = [this](const BenchmarkCaseResult& c) { handleCaseComplete(c); };
    request.onRunFinished = [this](const BenchmarkRun& run) { handleRunFinished(run); };

    attached_ = &request;
}

void BenchmarkSession::detach(BenchmarkRequest& request) {
    if (attached_ != &request) return;
    request.runId = savedRunId_;
    request.onCaseComplete = std::move(savedCase_);
    request.onRunStarted = std::move(savedStarted_);
    request.onRunFinished = std::move(savedFinished_);
    request.modeIndexApplicationDirectory = std::move(savedModeDir_);
    attached_ = nullptr;
}

void BenchmarkSession::handleRunStarted(const BenchmarkRun& run) {
    activeRun_ = run;
    if (runRecorded_) return;   // run_started is emitted exactly once per session
    if (!writer_.writeRunStarted(run)) lastError_ = "could not write run_started";
    runRecorded_ = true;
}

void BenchmarkSession::handleCaseComplete(const BenchmarkCaseResult& caseResult) {
    if (!writer_.isOpen()) return;
    // Defensive: the runner always fires onRunStarted first, but a record written
    // without run identity would not be recoverable.
    if (!runRecorded_) handleRunStarted(activeRun_);

    // writeCaseComplete emits one mode_result per entry of caseResult.modeResults
    // in order, then the case_complete commit marker. Mode detail is never dropped
    // in favour of the aggregate.
    if (!writer_.writeCaseComplete(activeRun_, caseResult)) {
        lastError_ = "could not write case records for " + caseResult.caseId;
    }
}

void BenchmarkSession::handleRunFinished(const BenchmarkRun& run) {
    finalize(run);
}

bool BenchmarkSession::finalize(const BenchmarkRun& run) {
    if (terminalWritten_) return true;   // a run gets exactly one terminal record

    bool ok = true;
    if (run.status == BenchmarkStatus::Cancelled) {
        // A cancelled run is recorded as cancelled rather than as a failure. The
        // reason names how far it got, so a later recovery does not have to infer
        // whether the run was interrupted or simply ran out of modes.
        std::string reason = "cancelled";
        if (run.filesRemaining > 0) {
            reason += " before completion: " + std::to_string(run.filesRemaining) +
                      " of " + std::to_string(run.filesStarted) + " files not attempted";
        }
        ok = writer_.writeRunCancelled(run, reason);
    } else {
        ok = writer_.writeRunFinished(run);
    }
    if (!ok) lastError_ = "could not write the run's terminal record";
    terminalWritten_ = true;

    // Flush and close before the summary is built, so the replay reads exactly
    // what the run produced.
    writer_.flush();
    writer_.close();

    // The summary is derived, never authoritative, and is always rebuilt from the
    // journal rather than from the in-memory run.
    std::string regenError;
    if (!regenerateSummary(regenError) && ok) {
        lastError_ = regenError;
        ok = false;
    }

    state_ = BenchmarkSessionState::Closed;
    lock_.release();
    return ok;
}

bool BenchmarkSession::regenerateSummary(std::string& errorOut) {
    // Recovery must be able to regenerate a summary without holding the suite
    // lock, so the paths are derived here if open() was never called.
    if (paths_.suiteDir.empty()) {
        paths_ = benchmarkSuitePaths(cfg_.applicationDataRoot, cfg_.suiteId);
    }
    const JournalReplay r = replay();
    if (r.fatal) {
        lastError_ = "journal replay reported fatal corruption; summary not regenerated";
        errorOut = lastError_;
        return false;
    }
    if (!writeSummaryJson(paths_, buildSummaryJson(r))) {
        lastError_ = "could not write summary.json";
        errorOut = lastError_;
        return false;
    }
    return true;
}

JournalReplay BenchmarkSession::replay() const {
    // Scoped to this session's run: a suite journal accumulates every run that
    // used the suite, and the summary describes the one that just finished.
    // The paths may not have been built by open(), since recovery replays without
    // taking the lock.
    const BenchmarkSuitePaths& p =
        paths_.suiteDir.empty() ? derivedPaths() : paths_;
    return replayJournal(p.runsJsonl, cfg_.runId);
}

BenchmarkSuitePaths BenchmarkSession::derivedPaths() const {
    return benchmarkSuitePaths(cfg_.applicationDataRoot, cfg_.suiteId);
}

bool BenchmarkSession::cleanupRuntime() {
    return cleanupRunRuntime(paths_, cfg_.runId);
}

} // namespace msf
