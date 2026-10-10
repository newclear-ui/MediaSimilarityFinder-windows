// ThumbnailStore (P4: 0.9.4.71). Backend-owned thumbnail authority (G1):
// persistent SQLite cache + small memory LRU + Backend-side decode chain.
// Replaces the former GUI fileThumb machinery (disk/shell/WIC/FFmpeg), which
// violated the process boundary. Independent of ScanWorker lifecycle: the
// store is opened per scan root and serves any time after that.
//
// Serve order (0.9.4.87: the shell fast lane was removed -- it used the
// Windows shell thumbnail-cache COM, which faulted in an unloaded shell DLL):
// disk JPEG -> engine art -> WIC decode -> FFmpeg video frame ->
// gray fingerprint fallback.
// Callers get finished JPEG bytes (bounded for IPC); the GUI only displays.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "database.h"

namespace msf {

// Raw BGRA art from the engine (mirrors ThumbBytes; defined here so the
// store stays independent of Qt).
struct RawArt {
    std::vector<unsigned char> bgra; // w*h*4, empty when absent
    int width = 0;
    int height = 0;
    bool gray = false; // single-channel gray (video fingerprint path)
    bool ok = false;
};

struct StoredThumb {
    std::vector<unsigned char> jpeg;
    int width = 0;
    int height = 0;
    bool ok = false;
};

class ThumbnailStore {
public:
    ThumbnailStore() = default;
    // Open (or re-resolve) the thumbnail table for a scan root. Safe to call
    // per scan; reopens only when the database path changes.
    bool ensureOpen(const std::string& appDir, const std::string& root);
    void close();
    // Serve one thumbnail. engineFetch supplies engine art (thumbMap_ +
    // video cache); everything else (disk/LRU/decode/shell/gray/persist)
    // happens here. desiredMaxDim caps decode work (256 display max).
    StoredThumb fetch(const std::string& path, int desiredMaxDim,
                      std::function<RawArt()> engineFetch);
    // Drop rows whose files are gone from the index (same-DB join).
    void prune();

private:
    struct MemEntry {
        std::vector<unsigned char> jpeg;
        int width = 0;
        int height = 0;
    };
    static constexpr std::size_t kMemMax = 256;

    bool diskGet(const std::string& path, std::int64_t modified, std::uint64_t size,
                 StoredThumb& out);
    void diskPut(const std::string& path, std::int64_t modified, std::uint64_t size,
                 const StoredThumb& t);
    static bool fileIdentity(const std::string& path, std::int64_t& modified, std::uint64_t& size);
    static RawArt wicArt(const std::string& path, int maxDim);
    static RawArt ffmpegArt(const std::string& path, int maxDim);

    std::mutex mutex_;
    Database db_;
    bool dbOpen_ = false;
    std::string dbPath_;
    std::list<std::pair<std::string, MemEntry>> memList_; // front = MRU
    std::unordered_map<std::string, std::list<std::pair<std::string, MemEntry>>::iterator> memMap_;
};

} // namespace msf
