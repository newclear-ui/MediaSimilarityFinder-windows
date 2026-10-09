// Index janitor regression (0.9.4.79). Regression-test indexes resolve under
// the same <appDir>/Index/<folderId>/ tree as production scans, so staleness
// (not location) is managed: roots deleted after tests leave orphan index
// directories behind. The janitor removes them once meaningless for 7+ days.
// Pure filesystem logic under fake clocks plus one temp-dir sweep; no scan.
#include "index_janitor.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << std::endl; }
    else       { std::cout << "  [ok] " << what << std::endl; }
}

std::string utcDaysAgo(int days) {
    const std::time_t t = std::time(nullptr) - (std::time_t)days * 86400;
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::int64_t nowMs() {
    return (std::int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void setDirMtimeDaysAgo(const fs::path& dir, int days) {
    const auto sysOld =
        std::chrono::system_clock::now() - std::chrono::hours(24 * days);
    const fs::file_time_type ft = fs::file_time_type::clock::now() +
        std::chrono::duration_cast<fs::file_time_type::duration>(sysOld - std::chrono::system_clock::now());
    std::error_code ec;
    fs::last_write_time(dir, ft, ec);
}

void writeMeta(const fs::path& dir, const std::string& root, const std::string& lastScan) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream f(dir / "metadata.json", std::ios::binary | std::ios::trunc);
    f << "{\n  \"rootPath\": \"" << root << "\",\n  \"lastScan\": \"" << lastScan << "\"\n}\n";
}

} // namespace

int main() {
    using msf::kJanitorStaleMs;
    const std::int64_t now = nowMs();
    const std::int64_t day = 24LL * 60 * 60 * 1000;
    // Timestamp parsing.
    check(msf::parseMetadataUtcMs("2026-10-08T19:48:14Z") > 0, "valid stamp parses");
    check(msf::parseMetadataUtcMs("2026-10-08T19:48:14Z") <
              msf::parseMetadataUtcMs("2026-10-09T19:48:14Z"),
          "ordering preserved");
    check(msf::parseMetadataUtcMs("garbage") < 0, "garbage rejected");
    check(msf::parseMetadataUtcMs("2026-10-08 19:48:14") < 0, "wrong shape rejected");
    check(msf::parseMetadataUtcMs("2026-13-40T25:61:61Z") < 0, "out-of-range rejected");
    // Index decision matrix.
    check(!msf::shouldRemoveIndex(true, now - 8 * day, now - 8 * day, now),
          "existing root never removed");
    check(!msf::shouldRemoveIndex(false, now - 6 * day, now - 8 * day, now),
          "fresh lastScan kept");
    check(!msf::shouldRemoveIndex(false, now - 8 * day, now - 6 * day, now),
          "fresh directory kept");
    check(msf::shouldRemoveIndex(false, now - 8 * day, now - 8 * day, now),
          "stale root + stale scan + stale dir removed");
    check(!msf::shouldRemoveIndex(false, -1, now - 8 * day, now), "unknown scan kept");
    // Scratch decision.
    check(msf::shouldRemoveScratch(now - 8 * day, now), "old scratch removed");
    check(!msf::shouldRemoveScratch(now - 6 * day, now), "fresh scratch kept");
    if (!gOk) return 1;

    // End-to-end sweep on a temp app dir.
    std::error_code ec;
    const fs::path app = fs::temp_directory_path() / "msf_janitor_test";
    fs::remove_all(app, ec);
    const fs::path idx = app / "Index";
    const std::string goneRoot = (app / "nope-root").string();
    const std::string liveRoot = (app / "live-root").string();
    fs::create_directories(liveRoot, ec);
    // Stale on all three axes -> removed.
    writeMeta(idx / "old1", goneRoot, utcDaysAgo(8));
    setDirMtimeDaysAgo(idx / "old1", 8);
    // Fresh scan -> kept.
    writeMeta(idx / "fresh1", goneRoot, utcDaysAgo(6));
    setDirMtimeDaysAgo(idx / "fresh1", 8);
    // Live root -> kept despite old scan.
    writeMeta(idx / "real1", liveRoot, utcDaysAgo(30));
    setDirMtimeDaysAgo(idx / "real1", 30);
    // Garbage metadata -> kept (conservative).
    {
        fs::create_directories(idx / "bad1", ec);
        std::ofstream f(idx / "bad1" / "metadata.json", std::ios::binary);
        f << "not json at all";
    }
    // Old scratch leaf -> removed; fresh leaf -> kept.
    const fs::path scratch = app / "TestMode";
    fs::create_directories(scratch / "oldleaf" / "Index" / "x", ec);
    setDirMtimeDaysAgo(scratch / "oldleaf", 8);
    fs::create_directories(scratch / "newleaf", ec);
    const msf::JanitorStats st = msf::sweepStaleIndexes(app, nowMs());
    check(st.indexRemoved == 1, "exactly one stale index removed");
    check(st.scratchRemoved == 1, "exactly one old scratch leaf removed");
    check(fs::exists(idx / "fresh1", ec), "fresh-scan dir kept");
    check(fs::exists(idx / "real1", ec), "live-root dir kept");
    check(fs::exists(idx / "bad1", ec), "unparseable dir kept");
    check(fs::exists(scratch / "newleaf", ec), "fresh scratch kept");
    check(!fs::exists(idx / "old1", ec), "stale index gone");
    check(!fs::exists(scratch / "oldleaf", ec), "old scratch gone");
    fs::remove_all(app, ec);
    std::cout << "janitor_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
