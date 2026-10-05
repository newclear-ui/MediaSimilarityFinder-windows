#include "benchmark_core.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <system_error>

#include "index_manager.h"
#include "media_search_engine.h"
#include "path_utils.h"
#include "scanner.h"

namespace msf {

const char* benchmarkStatusName(BenchmarkStatus s) {
    switch (s) {
        case BenchmarkStatus::Success:   return "SUCCESS";
        case BenchmarkStatus::Failed:    return "FAILED";
        case BenchmarkStatus::Cancelled: return "CANCELLED";
        case BenchmarkStatus::Skipped:   return "SKIPPED";
    }
    return "SKIPPED";
}

BenchmarkStatus aggregateStatus(const std::vector<BenchmarkModeResult>& modes) {
    // Empty is treated as Skipped: nothing was executed, which is not success.
    if (modes.empty()) return BenchmarkStatus::Skipped;

    // Two passes, not one. A single pass that returned on the first non-success
    // would let a Failed at index 1 mask a Cancelled at index 2, which is the
    // exact opposite of the documented precedence. Cancelled must be searched for
    // across the whole set before Failed is considered anywhere.
    for (const auto& m : modes)
        if (m.status == BenchmarkStatus::Cancelled) return BenchmarkStatus::Cancelled;
    for (const auto& m : modes)
        if (m.status == BenchmarkStatus::Failed) return BenchmarkStatus::Failed;
    for (const auto& m : modes)
        if (m.status == BenchmarkStatus::Success) return BenchmarkStatus::Success;
    return BenchmarkStatus::Skipped;
}

namespace {

using Clock = std::chrono::steady_clock;

double msSince(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Monotonic elapsed for one execute() call. Wall clock only; CPU time is never
// mixed in.
struct Elapsed {
    Clock::time_point t0 = Clock::now();
    double finish() { return msSince(t0); }
};

BenchmarkScanSummary summarize(const SearchReport& r) {
    BenchmarkScanSummary s;
    s.scanned = r.scanned;
    s.added = r.added;
    s.modified = r.modified;
    s.unchanged = r.unchanged;
    s.removed = r.removed;
    s.analyzed = r.analyzed;
    s.failed = r.failed;
    s.candidates = r.candidates;
    s.groups = r.groups;
    s.indexedVideos = r.indexedVideos;
    s.videoCandidatePairs = r.videoCandidatePairs;
    return s;
}

std::string isoNow() {
    // system_clock is used only for the human-readable timestamp label. Every
    // measured duration in this file uses steady_clock.
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

MediaKind toMediaKind(int kind) {
    switch (kind) {
        case 1: return MediaKind::Image;
        case 2: return MediaKind::Video;
        default: return MediaKind::Unknown;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// ProductionBenchmarkExecutor
// ---------------------------------------------------------------------------

ProductionBenchmarkExecutor::ProductionBenchmarkExecutor(std::string datasetFingerprint)
    : datasetFingerprint_(std::move(datasetFingerprint)) {}

ProductionBenchmarkExecutor::~ProductionBenchmarkExecutor() = default;

std::vector<BenchmarkFileItem> ProductionBenchmarkExecutor::discover(const BenchmarkRequest& request) {
    std::vector<BenchmarkFileItem> out;
    // Media scope is the same axis the S1 parser already defines; no second
    // scope vocabulary is introduced here.
    const bool images = request.mediaScope != MediaScope::Videos;
    const bool videos = request.mediaScope != MediaScope::Images;

    // Discovery belongs to the run, not to a case, so it happens once and is not
    // charged to any case's elapsed time.
    //
    // The index root is computed, not resolved. IndexManager::resolve() resolves
    // AND creates, so using it here to learn a path left an Index/<id>/metadata.json
    // in the production application directory on every benchmark run -- the runner
    // only ever wanted to tell the scanner where the index folder is.
    const std::string excluded =
        path_to_utf8(IndexManager::indexRootFor(path_from_utf8(request.applicationDirectory)));

    for (const auto& fs : Scanner().scan(request.sourceRoot, excluded, nullptr)) {
        BenchmarkFileItem item;
        item.path = fs.path;
        item.media = toMediaKind(fs.kind);
        item.sizeBytes = static_cast<std::size_t>(fs.size);
        // Media kind filtering is also enforced by the scan itself, but filtering
        // here keeps the case list honest about what the run claims to cover.
        if (item.media == MediaKind::Image && !images) continue;
        if (item.media == MediaKind::Video && !videos) continue;
        out.push_back(std::move(item));
    }
    return out;
}

bool ProductionBenchmarkExecutor::modeAvailable(GpuBackendKind requested) const {
    // CPU is always available. A CUDA request is available only when a device is
    // actually present; otherwise the runner records Skipped rather than
    // pretending the mode ran.
    if (requested == GpuBackendKind::Cpu) return true;
    return GpuBackend{}.available();
}

GpuBackendKind ProductionBenchmarkExecutor::resolveEffectiveMode(GpuBackendKind requested) const {
    if (requested == GpuBackendKind::Cpu) return GpuBackendKind::Cpu;
    // AUTO and the GPU-max request both follow the existing capability rules. A
    // CUDA request on a machine without a device resolves down to CPU; that is a
    // capability outcome, not a policy decision, and no GPU-max policy is defined
    // in S2.
    return GpuBackend{}.available() ? GpuBackendKind::Cuda : GpuBackendKind::Cpu;
}

void ProductionBenchmarkExecutor::runMode(const BenchmarkRequest& request,
                                          const BenchmarkFileItem& file,
                                          GpuBackendKind requested,
                                          BenchmarkModeResult& out) {
    out.requestedMode = requested;
    out.effectiveMode = resolveEffectiveMode(requested);
    out.started = true;

    // Per-mode index isolation. IndexManager::resolve() places the index under
    // <applicationDirectory>/Index, so giving each mode its own application
    // directory separates them without adding any new indexing API, and without
    // letting one mode's fingerprints influence the next. Process isolation and
    // the OS filesystem cache are explicitly NOT controlled here; that remains a
    // documented uncontrolled condition.
    //
    // S3 supplies the directory so it lands under the suite runtime tree; without
    // a provider the S2 default applies. The mode chosen here is unchanged either
    // way, since S3 is a storage concern and must not alter what is measured.
    const std::string modeDir = request.modeIndexApplicationDirectory
        ? request.modeIndexApplicationDirectory(requested)
        : request.applicationDirectory + std::string("/benchmark-s2-") + gpuBackendKindName(requested);
    std::error_code ec;
    std::filesystem::create_directories(path_from_utf8(modeDir), ec);

    MediaSearchEngine engine;
    if (!engine.openIndexForRoot(request.sourceRoot, modeDir)) {
        out.status = BenchmarkStatus::Failed;
        out.completed = true;
        out.errorMessage = "could not open index for this mode";
        return;
    }

    // Start from the caller's policy when one was supplied; otherwise keep the
    // original S2 behaviour of starting from the engine's own policy. Either way
    // the ONLY mode-specific change is whether the GPU backend is used: S2 does not
    // define a GPU-max resource policy, and the preset axis stays separate from the
    // mode axis.
    ResourcePolicy policy = request.resourcePolicy ? *request.resourcePolicy
                                                   : engine.resourcePolicy();
    policy.gpuEnabled = (requested != GpuBackendKind::Cpu) && (out.effectiveMode != GpuBackendKind::Cpu);
    engine.setResourcePolicy(policy);

    // Per-file execution through the production path: every other file in the run
    // goes into ignoredPaths, so scan() analyses exactly this one. This is the
    // only way to obtain a per-file measurement from the ordinary engine, which
    // has no per-file analysis entry point.
    std::vector<BenchmarkFileItem> all = discover(request);
    std::unordered_set<std::string> ignored;
    ignored.reserve(all.size());
    for (const auto& f : all) if (f.path != file.path) ignored.insert(f.path);

    ScanControl control;
    control.ignoredPaths = std::move(ignored);
    control.scanImages = request.mediaScope != MediaScope::Videos;
    control.scanVideos = request.mediaScope != MediaScope::Images;
    control.telemetryEnabled = true;
    // This is the controlled-benchmark execution path, so its scan telemetry
    // is recorded with the Benchmark purpose (distinct from GUI diagnostics).
    control.telemetryPurpose = TelemetryPurpose::Benchmark;
    control.buildVersion = request.buildVersion;
    if (request.isCancelled) control.cancel.store(request.isCancelled());

    // Timing covers the scan call only. Index open, discovery and the ignored-path
    // build above are all outside the measured window on purpose.
    Elapsed e;
    const SearchReport rep = engine.scan(request.sourceRoot, request.distance, &control);
    out.elapsedMs = e.finish();

    out.summary = summarize(rep);
    out.completed = true;

    // Cancellation wins over any success signal: a cancelled scan did not produce
    // a valid measurement even if it happened to report counts.
    if (request.isCancelled && request.isCancelled()) {
        out.status = BenchmarkStatus::Cancelled;
        out.errorMessage = "cancelled";
        return;
    }
    if (!rep.completed) {
        out.status = BenchmarkStatus::Failed;
        out.errorMessage = "scan did not complete";
        return;
    }
    out.status = BenchmarkStatus::Success;
}

// ---------------------------------------------------------------------------
// BenchmarkRunner
// ---------------------------------------------------------------------------

BenchmarkRun BenchmarkRunner::run(const BenchmarkRequest& request,
                                  const std::vector<GpuBackendKind>& modes) const {
    BenchmarkRun run;
    run.sourceRoot = request.sourceRoot;
    run.suiteId = request.suiteId;
    run.scanImages = request.mediaScope != MediaScope::Videos;
    run.scanVideos = request.mediaScope != MediaScope::Images;
    run.mediaScope = request.mediaScope;
    run.distance = request.distance;
    run.buildVersion = request.buildVersion;
    // Provenance is copied, never resolved. The runner does not invoke git, so the
    // recorded value is the one the caller built with.
    run.gitCommit = request.gitCommit;
    run.startedAt = isoNow();
    run.status = BenchmarkStatus::Success;

    if (const auto* exec = dynamic_cast<const ProductionBenchmarkExecutor*>(exec_)) {
        run.datasetFingerprint = exec->datasetFingerprint();
    }

    // Source identity for the run record.
    //
    // The label is only a human-readable name, so it is derived from the canonical
    // root directly. IndexManager::resolve() is NOT used here: it resolves AND
    // creates, so calling it just to read a name left an Index/<id>/metadata.json
    // behind in the production application directory on every benchmark run.
    // A benchmark must not create anything in the production index location.
    {
        std::error_code labelEc;
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(path_from_utf8(request.sourceRoot), labelEc);
        if (!labelEc && !canonical.empty()) {
            run.sourceRootLabel = canonical.filename().string();
            run.sourceRootId = IndexManager::folderId(path_to_utf8(canonical));
        } else {
            run.sourceRootId = IndexManager::folderId(request.sourceRoot);
        }
    }

    // A run id is normally supplied by the caller, because durable storage has to
    // know the run identity before execution starts. Falling back to a
    // process-local counter keeps the original in-runner behaviour intact.
    static std::atomic<unsigned long long> counter{0};
    run.runId = request.runId.empty() ? std::to_string(counter.fetch_add(1) + 1)
                                      : request.runId;

    const std::vector<BenchmarkFileItem> files = exec_->discover(request);
    run.filesStarted = files.size();
    run.filesRemaining = files.size();

    // The run is identified and its file list is known, but nothing has been
    // executed yet. This is where durable storage records the run's start.
    if (request.onRunStarted) request.onRunStarted(run);

    bool anyCancelled = false;
    bool anyFailed = false;

    for (const auto& file : files) {
        // Cancellation is checked before the file starts, so a file that never
        // began is not recorded as if it had been attempted.
        if (request.isCancelled && request.isCancelled()) { anyCancelled = true; break; }

        BenchmarkCaseResult c;
        c.path = file.path;
        c.media = file.media;
        c.sizeBytes = file.sizeBytes;
        c.caseId = IndexManager::folderId(file.path);
        c.started = true;

        // All modes of this file finish before the next file begins. That ordering
        // is the contract, so it is driven here and not by the executor.
        for (GpuBackendKind requested : modes) {
            BenchmarkModeResult m;
            m.requestedMode = requested;
            m.effectiveMode = exec_->resolveEffectiveMode(requested);

            if (!exec_->modeAvailable(requested)) {
                // Not permitted: recorded as skipped, never as success.
                m.status = BenchmarkStatus::Skipped;
                m.started = false;
                m.completed = false;
                m.errorMessage = "mode unavailable in this environment";
                c.modeResults.push_back(std::move(m));
                continue;
            }
            if (request.isCancelled && request.isCancelled()) {
                // Never started, so it is Skipped rather than Cancelled.
                // Cancelled means "began and was interrupted"; recording an
                // unstarted mode as Cancelled would lose the mode lifecycle
                // distinction the contract requires.
                m.status = BenchmarkStatus::Skipped;
                m.started = false;
                m.completed = false;
                m.errorMessage = "cancelled before start";
                c.modeResults.push_back(std::move(m));
                continue;
            }

            try {
                exec_->runMode(request, file, requested, m);
            } catch (const std::exception& e) {
                // An executor must not throw, but a throwing one must not take the
                // run down either: the failure becomes a result.
                m.status = BenchmarkStatus::Failed;
                m.completed = true;
                m.errorMessage = e.what();
            } catch (...) {
                m.status = BenchmarkStatus::Failed;
                m.completed = true;
                m.errorMessage = "unknown error";
            }
            c.elapsedMs += m.elapsedMs;
            c.modeResults.push_back(std::move(m));
        }

        c.status = aggregateStatus(c.modeResults);
        c.completed = true;
        if (c.status == BenchmarkStatus::Failed && c.errorMessage.empty()) {
            for (const auto& m : c.modeResults) {
                if (m.status == BenchmarkStatus::Failed && !m.errorMessage.empty()) {
                    c.errorMessage = m.errorMessage;
                    break;
                }
            }
        }
        if (c.status == BenchmarkStatus::Cancelled) anyCancelled = true;
        if (c.status == BenchmarkStatus::Failed) anyFailed = true;

        run.cases.push_back(c);
        run.filesCompleted++;
        run.filesRemaining = files.size() - run.filesCompleted;
        if (request.onCaseComplete) request.onCaseComplete(run.cases.back());
    }

    // A case that never started because of cancellation is not created at all, so
    // the run level status is derived from the cases that exist plus the flag.
    if (anyCancelled) run.status = BenchmarkStatus::Cancelled;
    else if (anyFailed) run.status = BenchmarkStatus::Failed;
    else if (run.cases.empty()) run.status = BenchmarkStatus::Skipped;
    else run.status = BenchmarkStatus::Success;

    run.completedAt = isoNow();
    exec_->onRunFinished(run);
    // The storage seam is notified last, so the terminal record it writes already
    // carries the final status and completedAt.
    if (request.onRunFinished) request.onRunFinished(run);
    return run;
}

} // namespace msf
