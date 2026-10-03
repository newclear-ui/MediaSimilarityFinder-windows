#pragma once
// S2 Run/Suite Benchmark Core ??execution model and result model.
//
// SCOPE. This is the core only: how a benchmark run is described, how files are
// executed under each requested mode, and how per-file results are recorded and
// aggregated. It deliberately does NOT implement the final AUTO/CPU/GPU-max
// policy, the durable JSONL journal, storage isolation, a terminal renderer, or
// any public CLI option. Those belong to later stages, and this file must not
// start growing them.
//
// The three-mode set used by the tests is a CONTRACT for verifying execution
// order, requested/effective recording, aggregation and cancellation. It is not a
// statement that the final policy is permanently three modes.
//
// Relationship to the existing product (no duplicated logic):
//
//   BenchmarkRunner
//        ?? BenchmarkExecutor  (boundary, injectable)
//   MediaSearchEngine / Index / VideoCache / ResourcePolicy
//
// The production adapter reuses the ordinary product scan path. There is no
// second search engine anywhere in this file.
//
// Relationship to TelemetryRecorder (src/benchmark.*): the legacy recorder is
// left completely untouched and keeps its own meaning. The structured state
// below is the source of truth; only a minimum of progress is mirrored into the
// recorder when a run finishes, and the recorder is never read back into the
// structured state.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "command_line.h"     // MediaScope
#include "gpu_backend.h"     // GpuBackendKind
#include "resource_policy.h" // ResourcePolicy
#include "scan_pipeline.h"   // MediaKind

namespace msf {

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
//
// FAILED and CANCELLED are deliberately distinct: a failure is a wrong result,
// a cancellation is an execution that was never allowed to finish. Folding them
// together would hide one behind the other.
enum class BenchmarkStatus {
    Success,    // executed and completed as intended
    Failed,     // executed and produced an error
    Cancelled,  // execution was interrupted; not an error
    Skipped     // not executed: not available, not permitted, or cancelled before start
};

const char* benchmarkStatusName(BenchmarkStatus s);

struct BenchmarkModeResult; // aggregateStatus() operates on the vector below

// Aggregate precedence, applied to a case's mode results:
//
//   Cancelled > Failed > Success > Skipped
//
// A cancellation dominates because it means the case did not finish, which
// outranks any completion judgement. A failure that happened alongside is NOT
// lost: every per-mode status is preserved verbatim in BenchmarkCaseResult.
BenchmarkStatus aggregateStatus(const std::vector<BenchmarkModeResult>& modes);

// ---------------------------------------------------------------------------
// Result model
// ---------------------------------------------------------------------------

// Summary of what the product scan reported. Only values the engine already
// provides are copied; no new internal instrumentation is inserted (S2 core does
// not add stage-level timing of its own).
struct BenchmarkScanSummary {
    std::size_t scanned = 0, added = 0, modified = 0, unchanged = 0, removed = 0;
    std::size_t analyzed = 0, candidates = 0, groups = 0;
    std::size_t indexedVideos = 0, videoCandidatePairs = 0;
};

// One execution of one file under one requested mode. This is where
// requested/effective mode lives, because a single file is executed under
// several modes and a single pair of fields on the case could not express that.
struct BenchmarkModeResult {
    GpuBackendKind requestedMode = GpuBackendKind::Auto;
    GpuBackendKind effectiveMode = GpuBackendKind::Cpu; // resolved by existing capability rules
    BenchmarkStatus status = BenchmarkStatus::Skipped;

    bool started = false;
    bool completed = false;

    // Monotonic wall-clock for the execute() call only. Index open/close, file
    // discovery and aggregation are deliberately excluded. CPU time is never
    // mixed in here.
    double elapsedMs = 0.0;

    BenchmarkScanSummary summary;
    std::string errorMessage; // populated for Failed, and for Cancelled where useful
};

// One file.
struct BenchmarkCaseResult {
    std::string caseId;   // stable identifier, unique within the run
    std::string path;     // canonical UTF-8, as produced by the scanner
    MediaKind media = MediaKind::Unknown;
    std::size_t sizeBytes = 0;

    BenchmarkStatus status = BenchmarkStatus::Skipped;
    bool started = false;
    bool completed = false;

    // Sum of the mode results' elapsedMs. Does not include discovery.
    double elapsedMs = 0.0;

    // Always preserved verbatim. Aggregation never rewrites or drops an entry.
    std::vector<BenchmarkModeResult> modeResults;
    std::string errorMessage; // first failing mode's message, for a quick read
};

// One run: one mode's measurements over a source root.
struct BenchmarkRun {
    std::string runId;
    std::string suiteId;
    std::string sourceRoot;
    std::string sourceRootLabel;
    std::string sourceRootId;
    std::string datasetFingerprint;

    MediaScope mediaScope = MediaScope::All;
    bool scanImages = true, scanVideos = true;
    unsigned distance = 8;

    // Injected from the generated build version header. Never hardcoded.
    std::string buildVersion;

    std::string startedAt;
    std::string completedAt;

    // Build provenance, alongside buildVersion and for the same reason: it is
    // run metadata, not execution state.
    //
    // This is the value the binary was built with, supplied by the caller from
    // the generated MSF_BUILD_GIT. The runner never shells out to git, so the
    // journal always records the provenance of the binary that actually ran.
    // Empty means the caller did not supply one; the journal writer emits the
    // literal "unknown" in that case rather than an empty provenance claim.
    std::string gitCommit;

    BenchmarkStatus status = BenchmarkStatus::Success;
    std::size_t filesStarted = 0, filesCompleted = 0, filesRemaining = 0;

    std::vector<BenchmarkCaseResult> cases;
};

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

struct BenchmarkRequest {
    std::string sourceRoot;
    std::string applicationDirectory; // where the index lives
    MediaScope mediaScope = MediaScope::All;
    unsigned distance = 8;
    std::string suiteId;
    std::string buildVersion;

    // Build provenance for the run record. Optional and additive: leaving it
    // empty preserves the original S2 behaviour exactly, and the execution
    // algorithm never reads it. It exists so BenchmarkRun can carry the
    // provenance of the binary that is running, without the runner having to
    // invoke git.
    std::string gitCommit;

    // Optional caller-supplied run identity. When empty the runner generates one,
    // which is the original S2 behaviour.
    //
    // S3 needs this because a durable run must be nameable BEFORE execution: the
    // suite lock, the journal path and the per-mode runtime directories are all
    // derived from the run id. Generating the id inside the runner would make the
    // storage layout unknowable up front. Supplying it changes no ordering,
    // aggregation or cancellation behaviour.
    std::string runId;

    // Cancellation boundary only. Restart/recovery is out of scope; this exists
    // so a future interactive caller can stop a run and still get an accurate
    // Cancelled result for the in-flight mode.
    std::function<bool()> isCancelled;

    // Optional resource policy for this run. PURELY ADDITIVE.
    //
    // When empty (the default) the executor behaves exactly as S2 originally did:
    // it starts from the engine's own policy and adjusts only gpuEnabled from the
    // requested mode. Every existing S2/S3 caller leaves this unset and is
    // unaffected.
    //
    // When set, the executor starts from this policy instead, so a caller that
    // already has a resolved policy does not have to build a second one. The GUI
    // passes the very same ResourcePolicy its ordinary scan uses.
    //
    // gpuEnabled remains mode-derived in both cases: that is the S2 rule, and the
    // policy preset and the benchmark mode stay separate axes.
    std::optional<ResourcePolicy> resourcePolicy;

    // Called once per completed case. This is the seam a journal writer uses:
    // case.modeResults is complete at this point, so the journal can write the
    // mode records followed by the case commit marker.
    //
    // Note the callback fires for a case whose aggregate is Cancelled as well.
    // A case is only withheld when cancellation struck before the file was even
    // started, and then no mode record exists either, so nothing is lost.
    std::function<void(const BenchmarkCaseResult&)> onCaseComplete;

    // Called once after the run's identity and file list are known and before the
    // first file executes. S3 uses it to write run_started.
    std::function<void(const BenchmarkRun&)> onRunStarted;

    // Called once after the terminal status and completedAt are computed and before
    // the run is returned. S3 uses it to write the terminal event (run_finished or
    // run_cancelled) and then regenerate summary.json from the journal.
    //
    // This is distinct from BenchmarkExecutor::onRunFinished, which is the legacy
    // recorder mirror. Keeping the journal seam on the request is what stops the
    // storage layer from leaking into the executor boundary.
    std::function<void(const BenchmarkRun&)> onRunFinished;

    // Optional per-mode index application directory provider. S2 established that
    // IndexManager::resolve() places an index under <applicationDirectory>/Index,
    // so isolating modes only requires giving each mode its own application
    // directory. S3 supplies this so benchmark indexes live under the suite's
    // runtime directory instead of beside the production index.
    //
    // When unset, the executor falls back to the original S2 location
    // (<applicationDirectory>/benchmark-s2-<mode>). Leaving it optional keeps the
    // storage policy out of the execution contract.
    std::function<std::string(GpuBackendKind)> modeIndexApplicationDirectory;
};

struct BenchmarkSuiteRequest {
    std::string suiteId;
    std::string label;
    std::vector<BenchmarkRequest> runs; // typically AUTO / CPU / GPU-max
};

// ---------------------------------------------------------------------------
// Execution boundary
// ---------------------------------------------------------------------------

struct BenchmarkFileItem {
    std::string path; // canonical UTF-8
    MediaKind media = MediaKind::Unknown;
    std::size_t sizeBytes = 0;
};

// Injectable boundary between the runner and the product scan path.
//
// The runner owns ordering, aggregation, cancellation and result preservation. The
// executor owns how a single file is actually analysed. Splitting them here is
// what lets the core be unit tested without running real media scans, and keeps
// the runner from being hard-wired to MediaSearchEngine.
class BenchmarkExecutor {
public:
    virtual ~BenchmarkExecutor() = default;

    // Enumerates the files for a run. Called once per run.
    virtual std::vector<BenchmarkFileItem> discover(const BenchmarkRequest& request) = 0;

    // Whether a mode can be executed at all in the current environment. Returning
    // false makes the runner record Skipped rather than silently succeeding.
    virtual bool modeAvailable(GpuBackendKind requested) const = 0;

    // Resolves requested mode to the effective backend using the existing
    // capability rules. Called even when the mode will be skipped, so a skipped
    // result still records what it would have resolved to.
    virtual GpuBackendKind resolveEffectiveMode(GpuBackendKind requested) const = 0;

    // Runs one file under one requested mode. Implementations must not throw:
    // a failure belongs in out.status/out.errorMessage, not in an exception.
    virtual void runMode(const BenchmarkRequest& request,
                         const BenchmarkFileItem& file,
                         GpuBackendKind requested,
                         BenchmarkModeResult& out) = 0;

    // Optional hook so a legacy recorder can mirror minimal progress. The
    // recorder is never read back.
    virtual void onRunFinished(const BenchmarkRun&) {}
};

// ---------------------------------------------------------------------------
// Production adapter
// ---------------------------------------------------------------------------
//
// Reuses the ordinary product path: Scanner for discovery, and
// MediaSearchEngine::scan() for analysis, with every other file in the run placed
// in ScanControl::ignoredPaths so that exactly one file is analysed per call.
//
// There is no per-file analysis API on MediaSearchEngine, so this is the only way
// to get a per-file measurement out of the real path without writing a second
// engine. The cost is that scan() walks the folder on every call, which makes a
// run O(files^2) in directory walking. That is accepted for S2: correctness and
// the result contract come first, and the walk overhead is a later optimisation.
//
// Declared here and defined in the .cpp so the heavy engine headers stay out of
// this header.
class ProductionBenchmarkExecutor : public BenchmarkExecutor {
public:
    explicit ProductionBenchmarkExecutor(std::string datasetFingerprint = {});
    ~ProductionBenchmarkExecutor() override;

    std::vector<BenchmarkFileItem> discover(const BenchmarkRequest& request) override;
    bool modeAvailable(GpuBackendKind requested) const override;
    GpuBackendKind resolveEffectiveMode(GpuBackendKind requested) const override;
    void runMode(const BenchmarkRequest& request,
                 const BenchmarkFileItem& file,
                 GpuBackendKind requested,
                 BenchmarkModeResult& out) override;

    // Dataset fingerprint recorded into the run, taken from the production scan
    // rather than assumed.
    const std::string& datasetFingerprint() const { return datasetFingerprint_; }
    void setDatasetFingerprint(std::string fp) { datasetFingerprint_ = std::move(fp); }

private:
    std::string datasetFingerprint_;
};

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

// The mode list is supplied by the caller rather than hardcoded, because the final
// mode policy is explicitly not decided in S2. The tests pass the three-mode
// contract; a later stage will pass the policy-resolved list.
inline const std::vector<GpuBackendKind>& benchmarkContractModes() {
    static const std::vector<GpuBackendKind> modes{
        GpuBackendKind::Auto,   // AUTO
        GpuBackendKind::Cpu,    // CPU-only
        GpuBackendKind::Cuda,   // GPU-max: a CUDA request in the current structure
    };
    return modes;
}

class BenchmarkRunner {
public:
    explicit BenchmarkRunner(BenchmarkExecutor& executor) : exec_(&executor) {}

    // Executes one run. Files are processed one at a time and all modes of a file
    // are finished before the next file starts; a failing mode does not stop the
    // remaining modes of that file, and a failing file does not stop the run.
    BenchmarkRun run(const BenchmarkRequest& request,
                     const std::vector<GpuBackendKind>& modes) const;

private:
    BenchmarkExecutor* exec_;
};

} // namespace msf
