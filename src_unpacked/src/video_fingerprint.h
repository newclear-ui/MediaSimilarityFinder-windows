#pragma once
#include "video_decoder.h"
#include "crop_fingerprint.h"
#include <cstdint>
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>
namespace msf {
struct VideoFingerprint{double duration=0;std::vector<double> timestamps;std::vector<std::uint64_t> hashes;
 std::vector<std::uint64_t> mirrorHashes;
 std::uint64_t crop4x3=0,crop1x1=0,crop9x16=0;
 std::uint64_t mirrorCrop4x3=0,mirrorCrop1x1=0,mirrorCrop9x16=0;
 // Scene-change boundaries detected at the sampling cadence. Timestamps align
 // with entries in timestamps; video_similarity() uses them as stable DTW
 // anchors (scene cuts survive re-encodes even when frames shift).
 std::vector<double> sceneChanges;
 // L3 verification thumbnails: 48x48 gray per kept frame, 1:1 with hashes[].
 // video_similarity() scores aligned frame pairs with SSIM on these (gated by
 // Hamming, blended) instead of Hamming alone. Empty = pre-v5 data or
 // hand-built fingerprints: scoring falls back to Hamming-only.
 static constexpr int kThumbSize=48;
 std::vector<std::uint8_t> thumb48;};
struct VideoCropFingerprint {
 std::vector<std::uint64_t> a4x3, a1x1, a9x16;
 std::vector<std::uint64_t> mirrorA4x3, mirrorA1x1, mirrorA9x16;
 std::vector<double> timestamps;
};
struct VideoSimilarityOptions { double thresholdPercent=50.0; double gapPenalty=8.0; double timeToleranceSeconds=2.0; double sceneBonus=0; };
class VideoFingerprintEngine{
public:
 ~VideoFingerprintEngine();
  bool build(const std::string&,VideoFingerprint&) const;
  bool buildFull(const std::string&,VideoFingerprint&,VideoCropFingerprint&,int decodeSize=96) const;
 bool openPersistentCache(const std::string& sqlitePath) const;
 void closePersistentCache() const;
 void clearCache() const;
 std::size_t memoryCacheSize() const;
 // Read-only cache lookup (no decode on miss): lets callers attach cached
 // per-frame data (e.g. candidate anchors) without re-analyzing files.
  bool loadPersistent(const std::string&,std::uint64_t,std::uint64_t,VideoFingerprint&,VideoCropFingerprint*) const;
private:
  struct CacheEntry { std::uint64_t size=0, modified=0; VideoFingerprint fingerprint; VideoCropFingerprint crop; bool hasCrop=false; };
  static constexpr std::size_t kMemoryCacheMax = 64;
  mutable std::list<std::pair<std::string,CacheEntry>> cacheList_;
  mutable std::unordered_map<std::string,std::list<std::pair<std::string,CacheEntry>>::iterator> cacheMap_;
  mutable std::mutex cacheMutex_;
 mutable void* cacheDb_=nullptr;
 mutable void* loadStmt_=nullptr;
 mutable void* saveStmt_=nullptr;
 mutable std::mutex dbMutex_;
  static constexpr int kCacheFormatVersion=7;
 bool preparePersistentStatements() const;
 void finalizePersistentStatements() const;
  void savePersistent(const std::string&,std::uint64_t,std::uint64_t,const VideoFingerprint&,const VideoCropFingerprint*) const;
  bool memoryLookup(const std::string&,std::uint64_t,std::uint64_t,VideoFingerprint&,VideoCropFingerprint*,bool) const;
  void memoryStore(const std::string&,std::uint64_t,std::uint64_t,const VideoFingerprint&,const VideoCropFingerprint*) const;
};
double video_similarity(const VideoFingerprint&,const VideoFingerprint&,const VideoSimilarityOptions& options={});
double video_crop_similarity(const VideoFingerprint&, const VideoCropFingerprint&, const VideoFingerprint&, const VideoCropFingerprint&, const VideoSimilarityOptions& options={});
// L3 structural verification: mean SSIM over uniform 8x8 windows (MSSIM
// without Gaussian). Sub-8px edge strips are dropped, never stretched.
// Returns [0,1]. Pure function, no fingerprint state — unit-testable.
double frame_ssim(const std::uint8_t* a, const std::uint8_t* b, int w, int h);
}
