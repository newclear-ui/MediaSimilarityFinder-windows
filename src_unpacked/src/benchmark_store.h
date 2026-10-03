#pragma once
// S3 Benchmark Storage ??path policy, suite lock, and runtime isolation.
//
// SCOPE. This layer decides WHERE benchmark artifacts live and guarantees that
// only one run owns a suite at a time. It does not write results; the durable
// evidence is the journal (benchmark_journal.h).
//
// Layout (storage-design.md contract):
//
//   Application data root/
//     Benchmark/Console/suite-<suite-id>/
//       suite.json
//       runs.jsonl          <- durable evidence
//       summary.json        <- regenerated from the journal, never the source
//       suite.lock          <- OS-level exclusive handle, not a flag file
//       runtime/run-<run-id>/<mode>/Index/   <- isolated execution cache, NOT evidence
//
// Two rules are load bearing:
//   1. Nothing is ever written inside the scanned source folder.
//   2. Benchmark index/cache is never treated as the normal Search Index. The
//      production index stays at <appDir>/Index; benchmark runs get their own
//      root and their own per-mode application directory.

#include <cstdint>
#include <string>
#include <vector>

#include "gpu_backend.h"   // GpuBackendKind

namespace msf {

// Journal/record schema version for the benchmark journal. Deliberately SEPARATE
// from TelemetryRecorder::kBenchmarkSchemaVersion (9): that number versions the
// legacy recorder's JSON document, while this versions the journal's record
// contract. They are not the same thing and must not drift together.
constexpr int kBenchmarkJournalSchemaVersion = 1;

// Directory and mode names follow the design wording, not a new vocabulary.
const char* benchmarkModeDirName(GpuBackendKind mode); // "auto" / "cpu" / "gpu-max"

struct BenchmarkSuitePaths {
    std::string consoleRoot;   // <appData>/Benchmark/Console
    std::string suiteDir;      // <consoleRoot>/suite-<suiteId>
    std::string suiteJson;     // <suiteDir>/suite.json
    std::string runsJsonl;     // <suiteDir>/runs.jsonl
    std::string summaryJson;   // <suiteDir>/summary.json
    std::string lockFile;      // <suiteDir>/suite.lock
    std::string runtimeDir;    // <suiteDir>/runtime
};

// Human-identifiable but never a raw full source path: a sanitized basename plus
// a short stable root id, per storage-design.md.
std::string sanitizeSourceLabel(const std::string& sourceRoot);
std::string shortRootId(const std::string& canonicalRoot);

// Builds the canonical suite layout from an application data root and a suite id.
BenchmarkSuitePaths benchmarkSuitePaths(const std::string& applicationDataRoot,
                                        const std::string& suiteId);

// Per-run runtime directory, and the application directory handed to
// IndexManager::resolve() for one mode.
//
// IndexManager::resolve() writes to <applicationDirectory>/Index/<id>, so
// passing the per-mode runtime directory isolates each mode's index without any
// change to IndexManager.
std::string benchmarkRunRuntimeDir(const BenchmarkSuitePaths& suite, const std::string& runId);
std::string benchmarkModeRuntimeDir(const BenchmarkSuitePaths& suite,
                                    const std::string& runId,
                                    GpuBackendKind mode);

// Same value as benchmarkModeRuntimeDir; named for the IndexManager contract so
// the call site reads correctly.
std::string benchmarkModeIndexApplicationDir(const BenchmarkSuitePaths& suite,
                                             const std::string& runId,
                                             GpuBackendKind mode);

// Creates the suite directory tree. Returns false on I/O failure.
bool ensureBenchmarkSuiteDir(const BenchmarkSuitePaths& suite);

// Removes a run's runtime tree. Runtime is execution scratch, not evidence, so
// this is safe to call after a run finishes. It is never called implicitly during
// recovery: recovery must first decide a run's state, and a cleanup failure here
// must not be treated as journal data loss.
bool cleanupRunRuntime(const BenchmarkSuitePaths& suite, const std::string& runId);

// ---------------------------------------------------------------------------
// Suite lock
// ---------------------------------------------------------------------------
//
// One active run per suite; different suites run concurrently.
//
// This is an OS-level exclusive handle, not a flag file. A flag file would leave
// a stale lock behind on crash and would also fail to stop two processes from
// appending to the same journal. Here the OS releases the handle when the
// process dies, so a crash cannot wedge a suite permanently.
//
// On Windows the lock is a CreateFileW open with no sharing; a second open fails
// with a sharing violation. The non-Windows path uses an exclusive create.
class BenchmarkSuiteLock {
public:
    BenchmarkSuiteLock() = default;
    ~BenchmarkSuiteLock();
    BenchmarkSuiteLock(const BenchmarkSuiteLock&) = delete;
    BenchmarkSuiteLock& operator=(const BenchmarkSuiteLock&) = delete;
    BenchmarkSuiteLock(BenchmarkSuiteLock&& other) noexcept;
    BenchmarkSuiteLock& operator=(BenchmarkSuiteLock&& other) noexcept;

    // Acquires the suite lock for the lifetime of this object. Returns false when
    // another run already holds it. The lock file is created if absent and left
    // in place afterwards; its presence alone never means "locked".
    bool acquire(const std::string& lockFilePath);
    void release();
    bool held() const { return held_; }

private:
    bool held_ = false;
    std::string path_;
#ifdef _WIN32
    void* handle_ = nullptr;   // HANDLE
#else
    int fd_ = -1;
#endif
};

// ---------------------------------------------------------------------------
// suite.json / summary.json
// ---------------------------------------------------------------------------

// Atomic write: temp file in the same directory, then rename, with a
// remove+rename fallback. The pattern mirrors ProfileStore::save, but is kept
// benchmark specific rather than bending that class into a generic store.
// Failure never throws; a missing summary is not data loss because the journal
// is the source of truth.
bool writeFileAtomic(const std::string& path, const std::string& contents);

std::string readFileIfExists(const std::string& path);

bool writeSuiteJson(const BenchmarkSuitePaths& suite,
                    const std::string& suiteId,
                    const std::string& label,
                    const std::string& sourceRoot,
                    const std::string& sourceRootLabel,
                    const std::string& sourceRootId,
                    const std::string& datasetFingerprint,
                    const std::string& buildVersion);

// Summary is always derived from the journal. Anything missing here may still
// exist in runs.jsonl, and recovery must never assume the summary is complete.
bool writeSummaryJson(const BenchmarkSuitePaths& suite, const std::string& summaryJsonBody);

// ---------------------------------------------------------------------------
// Shared JSON / timestamp helpers
// ---------------------------------------------------------------------------
//
// Every benchmark JSON writer goes through these: the Console suite.json, the
// Console journal records and the GUI mode snapshots. They live here so three
// writers cannot drift into three different escaping or timestamp formats.
//
// The escaping itself is delegated to the product's existing
// TelemetryRecorder::escapeJson, so no second escaper is introduced. Reusing a
// function here does not reuse the legacy recorder's schema or meaning.
std::string telemetryJsonString(const std::string& value); // quoted + escaped
std::string telemetryJsonBool(bool value);                // true / false
std::string benchmarkNowStamp();                          // %Y-%m-%dT%H:%M:%SZ

} // namespace msf
