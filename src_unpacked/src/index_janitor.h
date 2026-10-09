// Index janitor (0.9.4.79): lifecycle for managed index storage.
//
// Regression tests (and Test Mode runs) resolve their indexes under the same
// <appDir>/Index/<folderId>/ tree as production scans, so nothing is
// scattered elsewhere. The remaining problem is staleness: fixture roots are
// deleted after tests, leaving orphan index directories behind. The janitor
// removes them once they have been meaningless for at least 7 days.
//
// Removal requires ALL of the following (conservative by design):
//   1. metadata.json parses and yields a rootPath,
//   2. that root path no longer exists on disk,
//   3. the recorded lastScan is at least 7 days old,
//   4. the directory itself was last modified at least 7 days ago
//      (metadata rewrites use atomic rename, so any live scan refreshes it).
// TestMode scratch leaves (<appDir>/TestMode/<leaf>/) are always regenerable
// (one unique leaf per run), so they need only the 7-day directory-mtime rule.
// Anything unparseable, fresh, or outside Index/ and TestMode/ is left alone.
// sweep() never throws and never fails a scan: every step is guarded.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace msf {

// Minimum age before a meaningless directory may be removed: 7 days.
inline constexpr std::int64_t kJanitorStaleMs = 7LL * 24 * 60 * 60 * 1000;

// Parse "2026-10-08T19:48:14Z" (IndexManager::nowUtc shape) to UTC epoch ms.
// Returns -1 when the text is not exactly that shape.
std::int64_t parseMetadataUtcMs(const std::string& text);

// Pure decision: remove a per-root index directory only when the root is
// gone AND both the recorded scan and the directory itself are stale.
inline bool shouldRemoveIndex(bool rootExists, std::int64_t lastScanMs,
                              std::int64_t dirMtimeMs, std::int64_t nowMs) {
    if (rootExists) return false;
    if (lastScanMs < 0 || dirMtimeMs < 0) return false;
    if (nowMs - lastScanMs < kJanitorStaleMs) return false;
    if (nowMs - dirMtimeMs < kJanitorStaleMs) return false;
    return true;
}

// Pure decision: TestMode scratch leaves need only directory age.
inline bool shouldRemoveScratch(std::int64_t dirMtimeMs, std::int64_t nowMs) {
    if (dirMtimeMs < 0) return false;
    return nowMs - dirMtimeMs >= kJanitorStaleMs;
}

struct JanitorStats {
    int indexRemoved = 0;
    int scratchRemoved = 0;
    int kept = 0;
};

// UTC epoch ms from a filesystem mtime, or -1 when unavailable.
std::int64_t dirMtimeMs(const std::filesystem::path& dir);

// Sweep <appDir>/Index and <appDir>/TestMode. Returns what happened.
// Never throws; failures are counted as kept, never fatal.
JanitorStats sweepStaleIndexes(const std::filesystem::path& appDir,
                              std::int64_t nowMs);

} // namespace msf
