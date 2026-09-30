#include "benchmark_store.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "benchmark.h"          // BenchmarkRecorder::escapeJson
#include "index_manager.h"      // folderId / canonicalRoot
#include "path_utils.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace msf {
namespace {
namespace fs = std::filesystem;
} // namespace

const char* benchmarkModeDirName(GpuBackendKind mode) {
    switch (mode) {
        case GpuBackendKind::Auto: return "auto";
        case GpuBackendKind::Cuda: return "gpu-max";
        case GpuBackendKind::Cpu:  return "cpu";
    }
    return "auto";
}

std::string sanitizeSourceLabel(const std::string& sourceRoot) {
    // Keep the last path component so a reader can recognise the folder, but drop
    // the drive/parent chain: storage-design.md forbids using the raw full source
    // path as a folder name.
    std::string base = sourceRoot;
    const std::size_t cut = base.find_last_of("/\\");
    if (cut != std::string::npos) base = base.substr(cut + 1);
    if (base.empty()) base = "root";

    std::string out;
    out.reserve(base.size());
    for (char c : base) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                     || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        out.push_back(ok ? c : '_');
    }
    // Trim trailing separators/dots so the name never ends in '.' or '..'.
    while (!out.empty() && (out.back() == '.' || out.back() == '_')) out.pop_back();
    if (out.empty()) out = "root";
    if (out.size() > 48) out.resize(48);
    return out;
}

std::string shortRootId(const std::string& canonicalRoot) {
    // Reuse the existing stable root id rather than inventing a second hash, so
    // the index folder and the benchmark folder agree on identity.
    return IndexManager::folderId(canonicalRoot);
}

BenchmarkSuitePaths benchmarkSuitePaths(const std::string& applicationDataRoot,
                                        const std::string& suiteId) {
    BenchmarkSuitePaths p;
    p.consoleRoot = applicationDataRoot + "/Benchmark/Console";
    p.suiteDir = p.consoleRoot + "/suite-" + suiteId;
    p.suiteJson = p.suiteDir + "/suite.json";
    p.runsJsonl = p.suiteDir + "/runs.jsonl";
    p.summaryJson = p.suiteDir + "/summary.json";
    p.lockFile = p.suiteDir + "/suite.lock";
    p.runtimeDir = p.suiteDir + "/runtime";
    return p;
}

std::string benchmarkRunRuntimeDir(const BenchmarkSuitePaths& suite, const std::string& runId) {
    return suite.runtimeDir + "/run-" + runId;
}

std::string benchmarkModeRuntimeDir(const BenchmarkSuitePaths& suite,
                                    const std::string& runId,
                                    GpuBackendKind mode) {
    return benchmarkRunRuntimeDir(suite, runId) + "/" + benchmarkModeDirName(mode);
}

std::string benchmarkModeIndexApplicationDir(const BenchmarkSuitePaths& suite,
                                             const std::string& runId,
                                             GpuBackendKind mode) {
    return benchmarkModeRuntimeDir(suite, runId, mode);
}

bool ensureBenchmarkSuiteDir(const BenchmarkSuitePaths& suite) {
    std::error_code ec;
    fs::create_directories(path_from_utf8(suite.suiteDir), ec);
    if (ec) return false;
    fs::create_directories(path_from_utf8(suite.runtimeDir), ec);
    return !ec;
}

bool cleanupRunRuntime(const BenchmarkSuitePaths& suite, const std::string& runId) {
    // Runtime holds only execution scratch. Removing it never touches
    // suite.json / runs.jsonl / summary.json, so a failure here is not journal
    // data loss and is reported as such rather than escalated.
    const std::string dir = benchmarkRunRuntimeDir(suite, runId);
    std::error_code ec;
    if (!fs::exists(path_from_utf8(dir), ec)) return true;
    fs::remove_all(path_from_utf8(dir), ec);
    return !ec;
}

// ---------------------------------------------------------------------------
// BenchmarkSuiteLock
// ---------------------------------------------------------------------------

BenchmarkSuiteLock::~BenchmarkSuiteLock() { release(); }

BenchmarkSuiteLock::BenchmarkSuiteLock(BenchmarkSuiteLock&& o) noexcept {
    held_ = o.held_; path_ = std::move(o.path_);
#ifdef _WIN32
    handle_ = o.handle_;
    o.handle_ = nullptr;
#else
    fd_ = o.fd_; o.fd_ = -1;
#endif
    o.held_ = false;
}

BenchmarkSuiteLock& BenchmarkSuiteLock::operator=(BenchmarkSuiteLock&& o) noexcept {
    if (this != &o) {
        release();
        held_ = o.held_; path_ = std::move(o.path_);
#ifdef _WIN32
        handle_ = o.handle_; o.handle_ = nullptr;
#else
        fd_ = o.fd_; o.fd_ = -1;
#endif
        o.held_ = false;
    }
    return *this;
}

bool BenchmarkSuiteLock::acquire(const std::string& lockFilePath) {
    if (held_) return true;

    std::error_code ec;
    const fs::path p = path_from_utf8(lockFilePath);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    path_ = lockFilePath;

#ifdef _WIN32
    // Share mode 0 means "no other handle may open this file". A second process
    // attempting the same open fails with ERROR_SHARING_VIOLATION, which is the
    // mutual exclusion we want. The handle is released by the OS if the process
    // dies, so a crash cannot leave a permanently wedged lock behind.
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    handle_ = h;
#else
    const int fd = ::open(p.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) return false;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) { ::close(fd); return false; }
    fd_ = fd;
#endif
    held_ = true;
    return true;
}

void BenchmarkSuiteLock::release() {
    if (!held_) return;
#ifdef _WIN32
    if (handle_) { CloseHandle(static_cast<HANDLE>(handle_)); handle_ = nullptr; }
#else
    if (fd_ >= 0) { ::flock(fd_, LOCK_UN); ::close(fd_); fd_ = -1; }
#endif
    // The lock file itself is intentionally left on disk. Its presence is never
    // treated as "locked"; only the live handle is.
    held_ = false;
}

// ---------------------------------------------------------------------------
// atomic file helpers
// ---------------------------------------------------------------------------

bool writeFileAtomic(const std::string& path, const std::string& contents) {
    std::error_code ec;
    const fs::path dst = path_from_utf8(path);
    if (dst.has_parent_path()) fs::create_directories(dst.parent_path(), ec);
    if (ec) return false;

    static unsigned long long seq = 0;
    const fs::path tmp = dst.parent_path()
                       / (dst.filename().string() + ".tmp." + std::to_string(++seq));
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        o.flush();
        if (!o) { std::error_code c; fs::remove(tmp, c); return false; }
    }
    std::error_code re;
    fs::rename(tmp, dst, re);
    if (re) {
        // Fallback for the case where a rename will not overwrite. Same pattern as
        // the profile store.
        std::error_code c;
        fs::remove(dst, c);
        fs::rename(tmp, dst, re);
        if (re) { std::error_code c2; fs::remove(tmp, c2); return false; }
    }
    return true;
}

std::string readFileIfExists(const std::string& path) {
    std::ifstream in(path_from_utf8(path), std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string benchmarkJsonString(const std::string& s) {
    return "\"" + BenchmarkRecorder::escapeJson(s) + "\"";
}

std::string benchmarkJsonBool(bool b) { return b ? "true" : "false"; }

std::string benchmarkNowStamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

bool writeSuiteJson(const BenchmarkSuitePaths& suite,
                    const std::string& suiteId,
                    const std::string& label,
                    const std::string& sourceRoot,
                    const std::string& sourceRootLabel,
                    const std::string& sourceRootId,
                    const std::string& datasetFingerprint,
                    const std::string& buildVersion) {
    if (!ensureBenchmarkSuiteDir(suite)) return false;
    std::ostringstream o;
    o << "{\"journalSchemaVersion\":" << kBenchmarkJournalSchemaVersion
      << ",\"suiteId\":" << benchmarkJsonString(suiteId)
      << ",\"label\":" << benchmarkJsonString(label)
      << ",\"sourceRoot\":" << benchmarkJsonString(sourceRoot)
      << ",\"sourceRootLabel\":" << benchmarkJsonString(sourceRootLabel)
      << ",\"sourceRootId\":" << benchmarkJsonString(sourceRootId)
      << ",\"datasetFingerprint\":" << benchmarkJsonString(datasetFingerprint)
      << ",\"buildVersion\":" << benchmarkJsonString(buildVersion)
      << ",\"createdAt\":" << benchmarkJsonString(benchmarkNowStamp())
      << ",\"storageFormat\":1"
      << ",\"note\":" << benchmarkJsonString("runs.jsonl is the durable evidence; summary.json is regenerated from it")
      << "}\n";
    return writeFileAtomic(suite.suiteJson, o.str());
}

bool writeSummaryJson(const BenchmarkSuitePaths& suite, const std::string& summaryJsonBody) {
    return writeFileAtomic(suite.summaryJson, summaryJsonBody);
}

} // namespace msf
