#include "benchmark_gui_store.h"

#include <filesystem>
#include <sstream>

#include "path_utils.h"

namespace msf {

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

std::string BenchmarkGuiPaths::snapshotPath(GpuBackendKind mode) const {
    switch (mode) {
        case GpuBackendKind::Auto:  return autoJson;
        case GpuBackendKind::Cpu:   return cpuJson;
        case GpuBackendKind::Cuda:  return gpuMaxJson;
    }
    return std::string();
}

BenchmarkGuiPaths benchmarkGuiPaths(const std::string& applicationRoot,
                                    const std::string& sourceRoot,
                                    const std::string& labelOverride,
                                    const std::string& rootIdOverride) {
    BenchmarkGuiPaths p;
    p.guiRoot = applicationRoot + "/Benchmark/GUI";

    // Never a raw full source path as a folder name. sanitizeSourceLabel keeps a
    // recognisable basename, shortRootId keeps the stable root identity so the
    // benchmark folder and the index folder agree on which source is meant.
    const std::string label = labelOverride.empty() ? sanitizeSourceLabel(sourceRoot)
                                                    : labelOverride;
    const std::string rootId = rootIdOverride.empty() ? shortRootId(sourceRoot)
                                                      : rootIdOverride;
    p.suiteDir = p.guiRoot + "/" + label + "_" + rootId;

    p.suiteLock = p.suiteDir + "/suite.lock";
    p.runtimeDir = p.suiteDir + "/runtime";
    p.autoJson = p.suiteDir + "/auto.json";
    p.cpuJson = p.suiteDir + "/cpu.json";
    p.gpuMaxJson = p.suiteDir + "/gpu-max.json";
    return p;
}

bool ensureBenchmarkGuiSuiteDir(const BenchmarkGuiPaths& paths) {
    std::error_code ec;
    std::filesystem::create_directories(path_from_utf8(paths.runtimeDir), ec);
    return !ec;
}

std::string benchmarkGuiRunRuntimeDir(const BenchmarkGuiPaths& paths, const std::string& runId) {
    return paths.runtimeDir + "/run-" + runId;
}

std::string benchmarkGuiModeIndexApplicationDir(const BenchmarkGuiPaths& paths,
                                                const std::string& runId,
                                                GpuBackendKind mode) {
    return benchmarkGuiRunRuntimeDir(paths, runId) + "/" + benchmarkModeDirName(mode);
}

bool cleanupBenchmarkGuiRunRuntime(const BenchmarkGuiPaths& paths, const std::string& runId) {
    if (runId.empty()) return true;
    std::error_code ec;
    std::filesystem::remove_all(path_from_utf8(benchmarkGuiRunRuntimeDir(paths, runId)), ec);
    // A runtime tree that is already gone is a success: cleanup is idempotent.
    return !ec;
}

// ---------------------------------------------------------------------------
// Snapshots
// ---------------------------------------------------------------------------

bool benchmarkGuiSnapshotExists(const BenchmarkGuiPaths& paths, GpuBackendKind mode) {
    const std::string p = paths.snapshotPath(mode);
    if (p.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(path_from_utf8(p), ec);
}

std::string readBenchmarkGuiSnapshot(const BenchmarkGuiPaths& paths, GpuBackendKind mode) {
    const std::string p = paths.snapshotPath(mode);
    if (p.empty()) return std::string();
    return readFileIfExists(p);
}

std::string buildBenchmarkGuiSnapshotJson(const BenchmarkRun& run,
                                         GpuBackendKind mode,
                                         const std::string& buildVersion,
                                         const ResourcePolicy* resourcePolicy) {
    // Collect this mode's results across every case, in S2's case order. The
    // aggregate is computed from THIS list, never copied from the Case aggregate:
    // one Case that failed on CPU must not mark the AUTO and GPU-max files failed.
    std::vector<BenchmarkModeResult> forMode;
    std::size_t success = 0, failed = 0, cancelled = 0, skipped = 0;
    double elapsedMs = 0.0;
    BenchmarkScanSummary sum;
    std::string firstError;

    for (const auto& c : run.cases) {
        for (const auto& m : c.modeResults) {
            if (m.requestedMode != mode) continue;

            switch (m.status) {
                case BenchmarkStatus::Success:   ++success;   break;
                case BenchmarkStatus::Failed:    ++failed;    break;
                case BenchmarkStatus::Cancelled: ++cancelled; break;
                case BenchmarkStatus::Skipped:   ++skipped;   break;
            }
            elapsedMs += m.elapsedMs;
            sum.scanned += m.summary.scanned;
            sum.added += m.summary.added;
            sum.modified += m.summary.modified;
            sum.unchanged += m.summary.unchanged;
            sum.removed += m.summary.removed;
            sum.analyzed += m.summary.analyzed;
            sum.candidates += m.summary.candidates;
            sum.groups += m.summary.groups;
            sum.indexedVideos += m.summary.indexedVideos;
            sum.videoCandidatePairs += m.summary.videoCandidatePairs;
            if (firstError.empty() && !m.errorMessage.empty()) firstError = m.errorMessage;
            forMode.push_back(m);
        }
    }

    // The precedence itself is S2's, not a second implementation of it.
    const BenchmarkStatus status =
        forMode.empty() ? BenchmarkStatus::Skipped : aggregateStatus(forMode);

    std::ostringstream o;
    o << "{\"snapshotSchemaVersion\":" << kBenchmarkGuiSnapshotSchemaVersion
      << ",\"appVersion\":" << benchmarkJsonString(buildVersion.empty() ? run.buildVersion
                                                                        : buildVersion)
      // One GUI suite per source folder: identical across the three mode files.
      << ",\"suiteId\":" << benchmarkJsonString(run.suiteId.empty() ? shortRootId(run.sourceRoot)
                                                                    : run.suiteId)
      << ",\"runId\":" << benchmarkJsonString(run.runId)
      << ",\"mode\":" << benchmarkJsonString(benchmarkModeDirName(mode))
      << ",\"mediaScope\":" << benchmarkJsonString(mediaScopeName(run.mediaScope))
      << ",\"sourceRoot\":" << benchmarkJsonString(run.sourceRoot)
      << ",\"sourceRootLabel\":" << benchmarkJsonString(run.sourceRootLabel)
      << ",\"sourceRootId\":" << benchmarkJsonString(run.sourceRootId)
      << ",\"datasetFingerprint\":" << benchmarkJsonString(run.datasetFingerprint)
      << ",\"startedAt\":" << benchmarkJsonString(run.startedAt)
      << ",\"completedAt\":" << benchmarkJsonString(run.completedAt)
      << ",\"writtenAt\":" << benchmarkJsonString(benchmarkNowStamp())
      << ",\"status\":" << benchmarkJsonString(benchmarkStatusName(status))
      << ",\"casesWithMode\":" << forMode.size()
      << ",\"casesCompleted\":" << success
      << ",\"casesFailed\":" << failed
      << ",\"casesCancelled\":" << cancelled
      << ",\"casesSkipped\":" << skipped
      << ",\"elapsedMs\":" << elapsedMs
      << ",\"errorMessage\":" << benchmarkJsonString(firstError)
      << ",\"summary\":{"
      << "\"scanned\":" << sum.scanned
      << ",\"added\":" << sum.added
      << ",\"modified\":" << sum.modified
      << ",\"unchanged\":" << sum.unchanged
      << ",\"removed\":" << sum.removed
      << ",\"analyzed\":" << sum.analyzed
      << ",\"candidates\":" << sum.candidates
      << ",\"groups\":" << sum.groups
      << ",\"indexedVideos\":" << sum.indexedVideos
      << ",\"videoCandidatePairs\":" << sum.videoCandidatePairs
      << "}"
      // Recorded so a reader knows what the benchmark ran under. This block is only
      // written when a policy was supplied, so existing snapshots stay valid.
      << (resourcePolicy ? (std::string(",\"resourcePolicy\":{\"mode\":") +
                            std::to_string(static_cast<int>(resourcePolicy->mode)) +
                            ",\"cpuPercent\":" + std::to_string(resourcePolicy->cpuPercent) +
                            ",\"gpuPercent\":" + std::to_string(resourcePolicy->gpuPercent) +
                            ",\"gpuEnabled\":" + benchmarkJsonBool(resourcePolicy->gpuEnabled) + "}")
                           : std::string())
      << ",\"note\":" << benchmarkJsonString(
             "per-mode projection of a single GUI benchmark run; the aggregate is computed from this "
             "mode's own results and the normal Search Index is never involved")
      << ",\"cases\":[";
    bool first = true;
    for (const auto& c : run.cases) {
        for (const auto& m : c.modeResults) {
            if (m.requestedMode != mode) continue;
            if (!first) o << ",";
            first = false;
            o << "{\"caseId\":" << benchmarkJsonString(c.caseId)
              << ",\"path\":" << benchmarkJsonString(c.path)
              << ",\"effectiveMode\":" << benchmarkJsonString(gpuBackendKindName(m.effectiveMode))
              << ",\"status\":" << benchmarkJsonString(benchmarkStatusName(m.status))
              << ",\"started\":" << benchmarkJsonBool(m.started)
              << ",\"elapsedMs\":" << m.elapsedMs
              << "}";
        }
    }
    o << "]}\n";
    return o.str();
}

BenchmarkGuiSnapshotResult writeBenchmarkGuiSnapshots(const BenchmarkGuiPaths& paths,
                                                      const BenchmarkRun& run,
                                                      const std::string& buildVersion,
                                                      const ResourcePolicy* resourcePolicy) {
    BenchmarkGuiSnapshotResult result;

    // Only modes that actually ran are written. Deriving the set from the run's
    // own results, rather than from a caller-supplied list, removes the chance of
    // writing a mode the user never selected.
    std::vector<GpuBackendKind> modes;
    for (const auto& c : run.cases) {
        for (const auto& m : c.modeResults) {
            bool seen = false;
            for (auto already : modes) if (already == m.requestedMode) { seen = true; break; }
            if (!seen) modes.push_back(m.requestedMode);
        }
    }
    if (modes.empty()) {
        result.error = "the run contains no mode results, so no snapshot was written";
        return result;
    }

    if (!ensureBenchmarkGuiSuiteDir(paths)) {
        result.error = "could not create the GUI suite directory";
        return result;
    }

    for (GpuBackendKind mode : modes) {
        const std::string target = paths.snapshotPath(mode);
        if (target.empty()) continue;
        // Atomic replace: a failed write leaves the previous snapshot intact.
        if (!writeFileAtomic(target, buildBenchmarkGuiSnapshotJson(run, mode, buildVersion, resourcePolicy))) {
            result.error = std::string("could not write ") + benchmarkModeDirName(mode) + ".json";
            return result;
        }
        result.written.push_back(mode);
    }
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------
// BenchmarkGuiStorage
// ---------------------------------------------------------------------------

BenchmarkGuiStorage::BenchmarkGuiStorage(Config config) : cfg_(std::move(config)) {}

BenchmarkGuiStorage::~BenchmarkGuiStorage() { release(); }

bool BenchmarkGuiStorage::begin(std::string& errorOut) {
    if (active_) { errorOut = "already active"; return true; }
    if (cfg_.runId.empty()) {
        errorOut = "runId is required: the runtime tree must exist before execution starts";
        return false;
    }
    if (cfg_.applicationRoot.empty()) {
        errorOut = "applicationRoot is required";
        return false;
    }
    if (cfg_.sourceRoot.empty()) {
        errorOut = "sourceRoot is required";
        return false;
    }

    paths_ = benchmarkGuiPaths(cfg_.applicationRoot, cfg_.sourceRoot,
                               cfg_.sourceRootLabel, cfg_.sourceRootId);
    if (!ensureBenchmarkGuiSuiteDir(paths_)) {
        errorOut = "could not create the GUI suite directory";
        return false;
    }

    // Lock before the runtime tree, mirroring S3. A rejected instance must not
    // disturb the running benchmark's snapshot or runtime.
    if (!lock_.acquire(paths_.suiteLock)) {
        errorOut = "this benchmark is already running for this source folder";
        busy_ = true;
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(
        path_from_utf8(benchmarkGuiRunRuntimeDir(paths_, cfg_.runId)), ec);
    if (ec) {
        errorOut = "could not create the run runtime directory";
        lock_.release();
        return false;
    }

    active_ = true;
    return true;
}

void BenchmarkGuiStorage::attach(BenchmarkRequest& request) const {
    request.runId = cfg_.runId;
    const BenchmarkGuiPaths* p = &paths_;
    const std::string runId = cfg_.runId;
    // Each mode's index lands under the GUI suite runtime tree, so the benchmark
    // index is never the production Search Index and never sits beside it.
    // IndexManager is unchanged: it still writes <applicationDirectory>/Index.
    request.modeIndexApplicationDirectory = [p, runId](GpuBackendKind mode) {
        return benchmarkGuiModeIndexApplicationDir(*p, runId, mode);
    };
}

BenchmarkGuiSnapshotResult BenchmarkGuiStorage::writeSnapshots(const BenchmarkRun& run) const {
    return writeBenchmarkGuiSnapshots(paths_, run, cfg_.buildVersion, &cfg_.resourcePolicy);
}

bool BenchmarkGuiStorage::cleanupRuntime() {
    if (cfg_.runId.empty()) return true;
    return cleanupBenchmarkGuiRunRuntime(paths_, cfg_.runId);
}

void BenchmarkGuiStorage::release() {
    lock_.release();
    active_ = false;
}

} // namespace msf
