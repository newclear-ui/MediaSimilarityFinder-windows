#pragma once
// S4 GUI Benchmark Storage ??per-mode latest-result snapshots for the GUI.
//
// SCOPE. Storage only. This layer decides WHERE the GUI's benchmark artifacts
// live, keeps them separate from the normal Search Index and from the Console
// suite, and serialises one finished run into one snapshot file per executed
// mode. It executes nothing: the GUI drives S2's BenchmarkRunner, which this
// layer only receives the result of.
//
// Layout (S4 brief 짠4-5, 짠4-6, storage-design.md):
//
//   <application root>/Benchmark/GUI/<source-label>_<root-id-short>/
//       auto.json          latest AUTO result
//       cpu.json           latest CPU-only result
//       gpu-max.json       latest GPU-max result
//       suite.lock         S3 BenchmarkSuiteLock, reused for instance exclusion
//       runtime/run-<run-id>/<mode>/Index/    execution cache, NOT evidence
//
// Three rules are load bearing:
//
//   1. A snapshot is per MODE. The aggregate is recomputed from that mode's own
//      results; the Case aggregate is never copied into all three files.
//   2. Only the modes that actually ran are written. A mode that was not selected
//      keeps its existing file, and is never deleted.
//   3. Reuse. Atomic replace comes from writeFileAtomic, the suite lock from
//      BenchmarkSuiteLock, naming from sanitizeSourceLabel/shortRootId, the
//      aggregate precedence from aggregateStatus, and JSON escaping from the
//      shared benchmark helpers. This layer introduces none of those itself.
//
// The GUI has no journal. runs.jsonl and summary.json belong to the Console and
// are neither written nor read here.

#include <cstddef>
#include <string>
#include <vector>

#include "benchmark_core.h"   // BenchmarkRun / BenchmarkRequest / GpuBackendKind
#include "benchmark_store.h"  // atomic write, suite lock, naming helpers
#include "resource_policy.h"  // ResourcePolicy

namespace msf {

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// Snapshot schema version. Deliberately NOT kBenchmarkSchemaVersion (9), which
// versions the legacy recorder's document, and NOT the journal schema (1), which
// versions append-only records. A GUI snapshot is neither.
constexpr int kBenchmarkGuiSnapshotSchemaVersion = 1;

struct BenchmarkGuiPaths {
    std::string guiRoot;    // <appRoot>/Benchmark/GUI
    std::string suiteDir;   // <guiRoot>/<source-label>_<root-id-short>
    std::string suiteLock;  // <suiteDir>/suite.lock
    std::string runtimeDir; // <suiteDir>/runtime
    std::string autoJson;   // <suiteDir>/auto.json
    std::string cpuJson;    // <suiteDir>/cpu.json
    std::string gpuMaxJson; // <suiteDir>/gpu-max.json

    // auto.json / cpu.json / gpu-max.json by mode. An unmapped mode yields "".
    std::string snapshotPath(GpuBackendKind mode) const;
};

// Builds the GUI suite paths. The directory name is a sanitized basename plus
// the stable short root id, never a raw full source path; the canonical sourceRoot
// is stored inside the JSON metadata instead. labelOverride / rootIdOverride let
// a caller reuse identity it already computed; empty means derive from sourceRoot.
BenchmarkGuiPaths benchmarkGuiPaths(const std::string& applicationRoot,
                                    const std::string& sourceRoot,
                                    const std::string& labelOverride = std::string(),
                                    const std::string& rootIdOverride = std::string());

bool ensureBenchmarkGuiSuiteDir(const BenchmarkGuiPaths& paths);

// Per-run runtime tree and the per-mode application directory handed to
// IndexManager::resolve(), so each mode's index is isolated under the GUI suite
// and never beside the production Search Index.
std::string benchmarkGuiRunRuntimeDir(const BenchmarkGuiPaths& paths, const std::string& runId);
std::string benchmarkGuiModeIndexApplicationDir(const BenchmarkGuiPaths& paths,
                                                const std::string& runId,
                                                GpuBackendKind mode);

// Removes the run's runtime tree. Runtime is execution scratch, not evidence, so
// this is safe after a run finishes. It is never called implicitly: a caller that
// might need to recover must decide the run's state first.
bool cleanupBenchmarkGuiRunRuntime(const BenchmarkGuiPaths& paths, const std::string& runId);

// ---------------------------------------------------------------------------
// Snapshots
// ---------------------------------------------------------------------------

bool benchmarkGuiSnapshotExists(const BenchmarkGuiPaths& paths, GpuBackendKind mode);

// Returns the file contents, or an empty string when there is no snapshot yet.
std::string readBenchmarkGuiSnapshot(const BenchmarkGuiPaths& paths, GpuBackendKind mode);

// The JSON body for one mode's projection of a finished run. Exposed so it can be
// inspected without writing.
//
// resourcePolicy is recorded as given so a reader can tell what the benchmark
// actually ran under. It is additive: existing snapshots stay valid because the
// block is only written when a policy was supplied.
std::string buildBenchmarkGuiSnapshotJson(const BenchmarkRun& run,
                                         GpuBackendKind mode,
                                         const std::string& buildVersion = std::string(),
                                         const ResourcePolicy* resourcePolicy = nullptr);

struct BenchmarkGuiSnapshotResult {
    bool ok = false;
    std::vector<GpuBackendKind> written;  // modes replaced by this call
    std::string error;
};

// Writes one snapshot per mode that actually ran, atomically. Modes absent from
// the run are left untouched, which is what preserves an unselected mode's
// previous result.
BenchmarkGuiSnapshotResult writeBenchmarkGuiSnapshots(const BenchmarkGuiPaths& paths,
                                                      const BenchmarkRun& run,
                                                      const std::string& buildVersion = std::string(),
                                                      const ResourcePolicy* resourcePolicy = nullptr);

// ---------------------------------------------------------------------------
// Storage holder
// ---------------------------------------------------------------------------
//
// Owns the lock and the runtime tree for one GUI benchmark run, and installs the
// per-mode index directories on the request. Deliberately installs NO journal
// hooks: the GUI has no journal, and reusing BenchmarkSession here would imply
// Console suite storage.
//
// Order is fixed: lock, then runtime tree. A rejected run creates nothing.

class BenchmarkGuiStorage {
public:
    struct Config {
        std::string applicationRoot;  // portable/app data root
        std::string runId;            // caller-supplied: runtime dirs exist before execution
        std::string sourceRoot;
        std::string sourceRootLabel;  // optional identity reuse
        std::string sourceRootId;     // optional identity reuse
        std::string datasetFingerprint;
        std::string buildVersion;
        MediaScope mediaScope = MediaScope::All;
        // The ResourcePolicy this run executes under, copied from the caller's
        // already-resolved policy. Recorded in every snapshot so the stored result
        // states what it was measured with.
        ResourcePolicy resourcePolicy;
    };

    explicit BenchmarkGuiStorage(Config config);
    ~BenchmarkGuiStorage();
    BenchmarkGuiStorage(const BenchmarkGuiStorage&) = delete;
    BenchmarkGuiStorage& operator=(const BenchmarkGuiStorage&) = delete;

    // Acquires the suite lock, then prepares the run's runtime tree. Returns false
    // when another GUI instance already holds this suite's lock; check isBusy().
    bool begin(std::string& errorOut);

    bool isBusy() const { return busy_; }   // another instance holds this suite
    bool active() const { return active_; } // this instance owns the suite

    const BenchmarkGuiPaths& paths() const { return paths_; }
    const std::string& runId() const { return cfg_.runId; }

    // Pins the run id and injects the per-mode index application directory.
    // Leaves every other request field alone; the S2 fallback still applies if this
    // is never called.
    void attach(BenchmarkRequest& request) const;

    BenchmarkGuiSnapshotResult writeSnapshots(const BenchmarkRun& run) const;

    // Removes this run's runtime tree and releases the suite lock.
    bool cleanupRuntime();
    void release();

private:
    Config cfg_;
    BenchmarkGuiPaths paths_;
    BenchmarkSuiteLock lock_;
    bool busy_ = false;
    bool active_ = false;
};

} // namespace msf
