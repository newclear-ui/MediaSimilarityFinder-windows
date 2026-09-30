// S3 store tests: path policy, naming, per-mode index directories, suite lock.
//
// Uses a scratch directory under the system temp path. Nothing here touches a
// scanned source folder, and that separation is itself asserted.

#include <cstdio>
#include <filesystem>
#include <string>

#include "benchmark_store.h"
#include "index_manager.h"
#include "path_utils.h"

namespace {

int gChecks = 0, gFails = 0;
void chk(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) { ++gFails; std::printf("  [F] %s\n", what.c_str()); }
    else      { std::printf("  [ok] %s\n", what.c_str()); }
}

namespace fs = std::filesystem;

fs::path scratchRoot() {
    const fs::path p = fs::temp_directory_path() / "msf_s3_store_test";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

} // namespace

int main() {
    std::printf("S3 benchmark store selfcheck\n\n");
    const fs::path root = scratchRoot();
    const std::string appData = msf::path_to_utf8(root / "appdata");

    // ---- suite path layout ---------------------------------------------------
    {
        const auto p = msf::benchmarkSuitePaths(appData, "20260930-0801-01");
        const std::string want = appData + "/Benchmark/Console/suite-20260930-0801-01";
        chk(p.suiteDir == want, "suite directory follows storage-design.md");
        chk(p.suiteJson == want + "/suite.json",      "  suite.json");
        chk(p.runsJsonl == want + "/runs.jsonl",      "  runs.jsonl is the durable evidence");
        chk(p.summaryJson == want + "/summary.json",  "  summary.json is a derived view");
        chk(p.lockFile == want + "/suite.lock",       "  suite.lock");
        chk(p.runtimeDir == want + "/runtime",        "  runtime/ is not evidence");
        chk(msf::ensureBenchmarkSuiteDir(p),          "  suite directory created");
        chk(fs::is_directory(msf::path_from_utf8(p.runtimeDir)), "  runtime directory created");
    }

    // ---- sanitized naming: never a raw full path ---------------------------
    {
        const std::string raw = "D:\\Media\\Personal\\TestSet 2026";
        const std::string label = msf::sanitizeSourceLabel(raw);
        chk(label.find("Personal") == std::string::npos, "label drops the parent path");
        chk(label.find(':') == std::string::npos,        "label drops the drive colon");
        chk(label.find(' ') == std::string::npos,        "label drops spaces");
        chk(label.find("TestSet") != std::string::npos,  "label keeps the recognisable basename");
        chk(label.size() <= 48,                         "label is length bounded");
        chk(!msf::sanitizeSourceLabel("").empty(),      "empty root still yields a name");
        chk(msf::sanitizeSourceLabel("...") == "root",  "degenerate name falls back");
        chk(msf::shortRootId("D:/Media/TestSet").size() > 0, "short root id produced");
    }

    // ---- per-mode index application directory -------------------------------
    {
        const auto p = msf::benchmarkSuitePaths(appData, "s1");
        const std::string autoDir = msf::benchmarkModeIndexApplicationDir(p, "run-7", msf::GpuBackendKind::Auto);
        const std::string cpuDir  = msf::benchmarkModeIndexApplicationDir(p, "run-7", msf::GpuBackendKind::Cpu);
        const std::string gpuDir  = msf::benchmarkModeIndexApplicationDir(p, "run-7", msf::GpuBackendKind::Cuda);

        chk(autoDir.find("/runtime/run-run-7/auto") != std::string::npos,
            "auto mode index dir is under runtime/run-<id>/auto");
        chk(cpuDir.find("/runtime/run-run-7/cpu") != std::string::npos,
            "cpu mode index dir is under runtime/run-run-7/cpu");
        chk(gpuDir.find("/runtime/run-run-7/gpu-max") != std::string::npos,
            "gpu-max mode index dir is under runtime/run-<id>/gpu-max");
        chk(autoDir != cpuDir && cpuDir != gpuDir, "each mode gets a distinct index directory");

        // IndexManager::resolve writes <applicationDirectory>/Index/<id>, so the
        // per-mode application directory must land inside the suite, never next to
        // the production index and never in the source folder.
        chk(autoDir.rfind(p.suiteDir, 0) == 0,
            "mode index dir is inside the suite directory, not beside the production index");
        chk(autoDir.find("/Index/") == std::string::npos,
            "the caller passes a directory; Index/ is added by IndexManager");
    }

    // ---- benchmark artifacts never enter a source folder --------------------
    {
        const fs::path source = root / "SourceFolder";
        std::error_code ecSrc;
        fs::create_directories(source, ecSrc);
        const auto p = msf::benchmarkSuitePaths(appData, "s2");
        chk(msf::ensureBenchmarkSuiteDir(p), "suite created");
        chk(p.suiteDir.rfind(msf::path_to_utf8(source), 0) != 0,
            "suite dir is not inside the scanned source folder");
        chk(msf::writeSuiteJson(p, "s2", "label", "D:\\Media\\Set", "Set", "abc123", "fp0", "0.9.4.43"),
            "suite.json written");
        chk(fs::exists(msf::path_from_utf8(p.suiteJson)), "  suite.json exists");
        chk(msf::readFileIfExists(p.suiteJson).find("journalSchemaVersion") != std::string::npos,
            "  suite.json records the journal schema version");
        chk(fs::is_empty(source), "  the source folder is still empty");
    }

    // ---- suite lock: one active run per suite, suites independent ----------
    {
        const auto p = msf::benchmarkSuitePaths(appData, "lockA");
        msf::ensureBenchmarkSuiteDir(p);
        msf::BenchmarkSuiteLock first;
        chk(first.acquire(p.lockFile), "first run acquires the suite lock");
        chk(first.held(), "  lock reports held");

        msf::BenchmarkSuiteLock second;
        chk(!second.acquire(p.lockFile), "second run on the same suite is rejected");
        chk(!second.held(), "  rejected lock reports not held");

        const auto p2 = msf::benchmarkSuitePaths(appData, "lockB");
        msf::ensureBenchmarkSuiteDir(p2);
        msf::BenchmarkSuiteLock other;
        chk(other.acquire(p2.lockFile), "a different suite can be locked concurrently");

        first.release();
        chk(!first.held(), "release clears the held state");
        msf::BenchmarkSuiteLock third;
        chk(third.acquire(p.lockFile), "lock is re-acquirable after release");
        third.release();
        other.release();

        // The lock FILE persists; its presence must not be mistaken for a held
        // lock, which is why acquire is used rather than an existence check.
        chk(fs::exists(msf::path_from_utf8(p.lockFile)), "lock file remains on disk after release");
        msf::BenchmarkSuiteLock afterFile;
        chk(afterFile.acquire(p.lockFile), "an existing lock file does not block acquisition");
        afterFile.release();
    }

    // ---- lock released when the holder is destroyed -------------------------
    {
        const auto p = msf::benchmarkSuitePaths(appData, "lockC");
        msf::ensureBenchmarkSuiteDir(p);
        {
            msf::BenchmarkSuiteLock scoped;
            chk(scoped.acquire(p.lockFile), "scoped lock acquired");
        }
        msf::BenchmarkSuiteLock after;
        chk(after.acquire(p.lockFile), "lock released when the holder is destroyed");
        after.release();
    }

    // ---- runtime cleanup -----------------------------------------------------
    {
        const auto p = msf::benchmarkSuitePaths(appData, "s3");
        msf::ensureBenchmarkSuiteDir(p);
        const std::string modeDir = msf::benchmarkModeRuntimeDir(p, "run-1", msf::GpuBackendKind::Auto);
        std::error_code ec;
        fs::create_directories(msf::path_from_utf8(modeDir + "/Index/fake"), ec);
        chk(fs::exists(msf::path_from_utf8(modeDir)), "runtime mode dir exists before cleanup");
        chk(msf::cleanupRunRuntime(p, "run-1"), "runtime cleanup succeeds");
        chk(!fs::exists(msf::path_from_utf8(msf::benchmarkRunRuntimeDir(p, "run-1"))),
            "runtime dir removed after cleanup");
        chk(msf::cleanupRunRuntime(p, "run-1"), "cleanup of an absent runtime is a no-op success");

        // Cleanup must never touch the durable evidence.
        msf::writeSuiteJson(p, "s3", "l", "D:/M", "M", "id", "fp", "0.9.4.43");
        msf::writeFileAtomic(p.runsJsonl, "{}\n");
        chk(msf::cleanupRunRuntime(p, "run-2"), "cleanup of a different run id");
        chk(fs::exists(msf::path_from_utf8(p.runsJsonl)), "cleanup leaves runs.jsonl untouched");
        chk(fs::exists(msf::path_from_utf8(p.suiteJson)), "cleanup leaves suite.json untouched");
    }

    // ---- atomic write --------------------------------------------------------
    {
        const std::string f = msf::path_to_utf8(root / "atomic" / "x.json");
        chk(msf::writeFileAtomic(f, "{\"a\":1}\n"), "atomic write succeeds");
        chk(msf::readFileIfExists(f) == "{\"a\":1}\n", "  content round-trips");
        chk(msf::writeFileAtomic(f, "{\"a\":2}\n"), "atomic overwrite succeeds");
        chk(msf::readFileIfExists(f) == "{\"a\":2}\n", "  overwrite round-trips");
        // No temp file may be left behind.
        int temps = 0;
        for (const auto& e : fs::directory_iterator(root / "atomic")) {
            if (e.path().filename().string().find(".tmp.") != std::string::npos) ++temps;
        }
        chk(temps == 0, "no temp files left behind after atomic writes");
    }

    std::error_code ec0;
    fs::remove_all(root, ec0);
    std::printf("\nbenchmark_store_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
