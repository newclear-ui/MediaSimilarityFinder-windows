// See thumbnail_store.h.
#include "thumbnail_store.h"

#include <chrono>
#include <cstring>

#include "backend_thumb.h"
#include "image_decoder.h"
#include "index_manager.h"
#include "path_utils.h"
#include "video_decoder.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <thumbcache.h>
#endif
#include <turbojpeg.h>

namespace msf {

// JPEG header probe (dimensions without a full decode).
bool jpegDims(const std::vector<unsigned char>& jpeg, int& w, int& h) {
    w = h = 0;
    if (jpeg.empty()) return false;
    tjhandle hnd = tjInitDecompress();
    if (!hnd) return false;
    int rw = 0, rh = 0;
    const int rc = tjDecompressHeader(hnd, const_cast<unsigned char*>(jpeg.data()),
                                      (unsigned long)jpeg.size(), &rw, &rh);
    tjDestroy(hnd);
    if (rc != 0 || rw <= 0 || rh <= 0) return false;
    w = rw;
    h = rh;
    return true;
}

bool ThumbnailStore::ensureOpen(const std::string& appDir, const std::string& root) {
    IndexPaths paths;
    if (!IndexManager::resolve(path_from_utf8(appDir), path_from_utf8(root), paths))
        return false;
    const std::string dbPath = path_to_utf8(paths.database);
    std::lock_guard<std::mutex> g(mutex_);
    if (dbOpen_ && dbPath_ == dbPath) return true;
    db_.close();
    dbOpen_ = false;
    if (!db_.open(dbPath) || !db_.initialize()) {
        db_.close();
        return false;
    }
    dbPath_ = dbPath;
    memList_.clear();
    memMap_.clear();
    return true;
}

void ThumbnailStore::close() {
    std::lock_guard<std::mutex> g(mutex_);
    db_.close();
    dbOpen_ = false;
    dbPath_.clear();
    memList_.clear();
    memMap_.clear();
}

void ThumbnailStore::prune() {
    std::lock_guard<std::mutex> g(mutex_);
    if (dbOpen_) db_.pruneThumbs();
}

bool ThumbnailStore::fileIdentity(const std::string& path, std::int64_t& modified,
                                  std::uint64_t& size) {
    std::error_code ec;
    const auto fp = path_from_utf8(path);
    const auto sz = std::filesystem::file_size(fp, ec);
    if (ec) return false;
    const auto mt = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::filesystem::last_write_time(fp, ec).time_since_epoch())
                        .count();
    if (ec) return false;
    modified = (std::int64_t)mt;
    size = (std::uint64_t)sz;
    return true;
}

bool ThumbnailStore::diskGet(const std::string& path, std::int64_t modified, std::uint64_t size,
                             StoredThumb& out) {
    if (!dbOpen_) return false;
    std::vector<unsigned char> jpeg;
    if (!db_.getThumb(path, modified, size, jpeg) || jpeg.empty()) return false;
    int w = 0, h = 0;
    if (!jpegDims(jpeg, w, h)) return false;
    out.jpeg = std::move(jpeg);
    out.width = w;
    out.height = h;
    out.ok = true;
    return true;
}

void ThumbnailStore::diskPut(const std::string& path, std::int64_t modified, std::uint64_t size,
                             const StoredThumb& t) {
    if (!dbOpen_ || !t.ok || t.jpeg.empty()) return;
    db_.putThumb(path, modified, size, t.jpeg);
}

#ifdef _WIN32
RawArt ThumbnailStore::shellArt(const std::string& path) {
    RawArt out;
    std::wstring wpath;
    {
        // UTF-8 product paths into the system-code-page shell: match the old
        // GUI behavior (Korean filenames kept working through toLocal8Bit).
        const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (n <= 1) return out;
        wpath.resize((size_t)n - 1);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), n);
    }
    IShellItem* item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(wpath.c_str(), nullptr, IID_PPV_ARGS(&item))))
        return out;
    // CLSID_ThumbnailCache needs INITGUID; spell it out to stay header-local
    // (same as the former GUI implementation).
    static const GUID kThumbCacheClsid = {0xc8199035, 0xdb49, 0x4e95, {0x91, 0xb7, 0x7e, 0x86, 0x2f, 0x12, 0x04, 0x59}};
    IThumbnailCache* cache = nullptr;
    if (FAILED(CoCreateInstance(kThumbCacheClsid, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&cache)))) {
        item->Release();
        return out;
    }
    ISharedBitmap* sb = nullptr;
    const HRESULT hr = cache->GetThumbnail(item, 256, WTS_INCACHEONLY | WTS_SCALETOREQUESTEDSIZE,
                                          &sb, nullptr, nullptr);
    if (SUCCEEDED(hr) && sb) {
        HBITMAP hb = nullptr;
        if (SUCCEEDED(sb->GetSharedBitmap(&hb)) && hb) {
            BITMAP bm{};
            if (::GetObject(hb, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
                BITMAPINFO bi{};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = bm.bmWidth;
                bi.bmiHeader.biHeight = -bm.bmHeight;
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                const int nPix = bm.bmWidth * bm.bmHeight;
                std::vector<unsigned char> buf((std::size_t)nPix * 4, 0);
                const HDC dc = ::GetDC(nullptr);
                if (dc && ::GetDIBits(dc, hb, 0, (UINT)bm.bmHeight, buf.data(), &bi,
                                      DIB_RGB_COLORS) == bm.bmHeight) {
                    for (int i = 3; i < nPix * 4; i += 4) buf[i] = 0xff;
                    out.bgra = std::move(buf);
                    out.width = bm.bmWidth;
                    out.height = bm.bmHeight;
                    out.ok = true;
                }
                if (dc) ::ReleaseDC(nullptr, dc);
            }
        }
        if (sb) sb->Release();
    }
    if (cache) cache->Release();
    if (item) item->Release();
    return out;
}
#else
RawArt ThumbnailStore::shellArt(const std::string&) {
    return RawArt();
}
#endif

RawArt ThumbnailStore::wicArt(const std::string& path, int maxDim) {
    RawArt out;
    ImageDecoder dec;
    ColorImage c;
    const int dim = std::max(32, std::min(256, maxDim));
    if (dec.decodeColorAspect(path, dim, c) && c.width > 0 && c.height > 0 &&
        c.bgra.size() == (size_t)c.width * c.height * 4) {
        out.bgra = std::move(c.bgra);
        out.width = c.width;
        out.height = c.height;
        out.ok = true;
    }
    return out;
}

RawArt ThumbnailStore::ffmpegArt(const std::string& path, int maxDim) {
    RawArt out;
    VideoDecoder dec;
    if (!dec.open(path)) return out;
    ColorFrame cf;
    const int dim = std::max(32, std::min(256, maxDim));
    if (dec.frameAtColor(0.5, dim, dim, cf) && cf.rgb.size() == (size_t)cf.width * cf.height * 3 &&
        cf.width > 0 && cf.height > 0) {
        // JPEG encoder below takes BGRA; expand RGB here (cheap, small art).
        std::vector<unsigned char> bgra((size_t)cf.width * cf.height * 4, 255);
        for (size_t i = 0, n = (size_t)cf.width * cf.height; i < n; ++i) {
            bgra[i * 4 + 0] = cf.rgb[i * 3 + 2];
            bgra[i * 4 + 1] = cf.rgb[i * 3 + 1];
            bgra[i * 4 + 2] = cf.rgb[i * 3 + 0];
        }
        out.bgra = std::move(bgra);
        out.width = cf.width;
        out.height = cf.height;
        out.ok = true;
    }
    dec.close();
    return out;
}

StoredThumb ThumbnailStore::fetch(const std::string& path, int desiredMaxDim,
                                  std::function<RawArt()> engineFetch) {
    StoredThumb miss;
    std::int64_t modified = 0;
    std::uint64_t size = 0;
    const bool haveId = fileIdentity(path, modified, size);
    const std::string memKey =
        path + '|' + std::to_string(desiredMaxDim) + '|' + (haveId ? std::to_string(modified) : std::string("?"));
    {
        std::lock_guard<std::mutex> g(mutex_);
        auto it = memMap_.find(memKey);
        if (it != memMap_.end()) {
            memList_.splice(memList_.begin(), memList_, it->second);
            StoredThumb hit;
            hit.jpeg = it->second->second.jpeg;
            hit.width = it->second->second.width;
            hit.height = it->second->second.height;
            hit.ok = true;
            return hit;
        }
    }
    // Disk before decode (fast indexed read; rescans reuse decoded thumbs).
    if (haveId) {
        StoredThumb disk;
        {
            std::lock_guard<std::mutex> g(mutex_);
            if (diskGet(path, modified, size, disk) && disk.ok) {
                memList_.emplace_front(memKey, MemEntry{disk.jpeg, disk.width, disk.height});
                if (memList_.size() > kMemMax) {
                    memMap_.erase(memList_.back().first);
                    memList_.pop_back();
                }
                memMap_[memKey] = memList_.begin();
                return disk;
            }
        }
    }
    // Engine art (analysis thumbnails) before fresh decode.
    RawArt art = engineFetch ? engineFetch() : RawArt();
    bool grayOnly = false;
    if (!art.ok) {
        art = shellArt(path);
        if (!art.ok) {
            const bool isVid = [&] {
                const size_t dot = path.find_last_of('.');
                std::string e = (dot == std::string::npos) ? std::string() : path.substr(dot + 1);
                for (auto& ch : e) ch = (char)tolower((unsigned char)ch);
                return e == "mp4" || e == "mkv" || e == "avi" || e == "mov" ||
                       e == "webm" || e == "m4v" || e == "wmv";
            }();
            if (isVid)
                art = ffmpegArt(path, desiredMaxDim);
            else
                art = wicArt(path, desiredMaxDim);
        }
    } else {
        grayOnly = art.gray;
    }
    if (!art.ok || art.width <= 0 || art.height <= 0 || art.bgra.empty()) return miss;
    JpegThumb jpg;
    if (art.gray)
        jpg = encodeJpegGray(art.bgra.data(), art.width, art.height);
    else if (art.bgra.size() == (size_t)art.width * art.height * 4)
        jpg = encodeJpegThumb(art.bgra.data(), art.width, art.height);
    else
        return miss;
    if (!jpg.ok || jpg.bytes.empty()) return miss;
    StoredThumb done;
    done.jpeg = std::move(jpg.bytes);
    done.width = jpg.width;
    done.height = jpg.height;
    done.ok = true;
    {
        std::lock_guard<std::mutex> g(mutex_);
        memList_.emplace_front(memKey, MemEntry{done.jpeg, done.width, done.height});
        if (memList_.size() > kMemMax) {
            memMap_.erase(memList_.back().first);
            memList_.pop_back();
        }
        memMap_[memKey] = memList_.begin();
        // Persist color art for future rescans (192px policy stays GUI-side
        // history; here we store what we served). Gray engine thumbs never
        // persist: they would stick as B&W entries.
        if (haveId && !grayOnly) diskPut(path, modified, size, done);
    }
    return done;
}

} // namespace msf
