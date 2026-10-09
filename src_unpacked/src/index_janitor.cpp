// See index_janitor.h.
#include "index_janitor.h"
#include "path_utils.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <system_error>

namespace msf {
namespace fs = std::filesystem;

std::int64_t parseMetadataUtcMs(const std::string& text) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    char z = 0;
    int n = 0;
    // Strict shape match: anything else (truncated copy, future format)
    // reads as unknown (-1) and is therefore kept, never deleted. %n pins
    // the end so trailing characters are rejected too.
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c%n",
                     &y, &mo, &d, &h, &mi, &s, &z, &n) != 7)
        return -1;
    if (z != 'Z' || n != (int)text.size()) return -1;
    if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 61)
        return -1;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
#ifdef _WIN32
    const std::time_t secs = _mkgmtime(&tm);
#else
    const std::time_t secs = timegm(&tm);
#endif
    if (secs == (std::time_t)-1) return -1;
    return (std::int64_t)secs * 1000;
}

std::int64_t dirMtimeMs(const std::filesystem::path& dir) {
    std::error_code ec;
    const auto t = fs::last_write_time(dir, ec);
    if (ec) return -1;
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return (std::int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        sys.time_since_epoch()).count();
}

// Minimal metadata reader: rootPath + lastScan without a JSON library.
// Returns false unless both keys are present and well-formed.
static bool readMetadata(const fs::path& metadata, std::string& root, std::string& lastScan) {
    std::error_code ec;
    if (!fs::is_regular_file(metadata, ec) || ec) return false;
    const auto size = fs::file_size(metadata, ec);
    if (ec || size == 0 || size > 65536) return false;
    std::string raw;
    raw.resize((std::size_t)size);
    FILE* f = nullptr;
#ifdef _WIN32
    _wfopen_s(&f, metadata.c_str(), L"rb");
#else
    f = std::fopen(metadata.c_str(), "rb");
#endif
    if (!f) return false;
    const std::size_t n = std::fread(raw.data(), 1, raw.size(), f);
    std::fclose(f);
    if (n != raw.size()) return false;
    auto grab = [&](const char* key, std::string& out) {
        const std::string k = std::string("\"") + key + "\"";
        const auto p = raw.find(k);
        if (p == std::string::npos) return false;
        const auto c = raw.find(':', p + k.size());
        if (c == std::string::npos) return false;
        const auto q1 = raw.find('"', c + 1);
        if (q1 == std::string::npos) return false;
        const auto q2 = raw.find('"', q1 + 1);
        if (q2 == std::string::npos) return false;
        out = raw.substr(q1 + 1, q2 - q1 - 1);
        return true;
    };
    return grab("rootPath", root) && grab("lastScan", lastScan);
}

JanitorStats sweepStaleIndexes(const fs::path& appDir, std::int64_t nowMs) {
    JanitorStats st;
    try {
        // Per-root indexes: <appDir>/Index/<folderId>/metadata.json.
        std::error_code ec;
        const fs::path indexRoot = appDir / "Index";
        if (fs::is_directory(indexRoot, ec) && !ec) {
            for (const auto& entry : fs::directory_iterator(indexRoot, ec)) {
                if (ec) break;
                if (!entry.is_directory()) continue;
                std::string root, lastScan;
                const bool ok = readMetadata(entry.path() / "metadata.json", root, lastScan);
                if (!ok) continue; // unparseable: keep, never delete
                const std::int64_t lastMs = parseMetadataUtcMs(lastScan);
                const std::int64_t mtime = dirMtimeMs(entry.path());
                std::error_code ex;
                // Root records use forward slashes; compare through the
                // filesystem so case/separator variants resolve identically.
                // Wide construction: stale roots may hold names outside the
                // ANSI code page, which must never throw the sweep.
                const bool rootExists = fs::exists(path_from_utf8(root), ex) && !ex;
                if (!shouldRemoveIndex(rootExists, lastMs, mtime, nowMs)) {
                    ++st.kept;
                    continue;
                }
                std::error_code rm;
                fs::remove_all(entry.path(), rm);
                if (!rm) ++st.indexRemoved;
                else ++st.kept;
            }
        }
        // TestMode scratch: <appDir>/TestMode/<unique-leaf>/ (always
        // regenerable, one leaf per run). Age of the leaf decides.
        const fs::path scratchRoot = appDir / "TestMode";
        if (fs::is_directory(scratchRoot, ec) && !ec) {
            for (const auto& entry : fs::directory_iterator(scratchRoot, ec)) {
                if (ec) break;
                if (!entry.is_directory()) continue;
                if (!shouldRemoveScratch(dirMtimeMs(entry.path()), nowMs)) {
                    ++st.kept;
                    continue;
                }
                std::error_code rm;
                fs::remove_all(entry.path(), rm);
                if (!rm) ++st.scratchRemoved;
                else ++st.kept;
            }
        }
    } catch (...) {
        // Sweeping must never break a scan.
    }
    return st;
}

} // namespace msf
