#pragma once
#include "video_decoder.h"
#include "crop_fingerprint.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
namespace msf {
struct VideoFingerprint{double duration=0;std::vector<double> timestamps;std::vector<std::uint64_t> hashes;
 std::vector<std::uint64_t> mirrorHashes;
 std::uint64_t crop4x3=0,crop1x1=0,crop9x16=0;
 std::uint64_t mirrorCrop4x3=0,mirrorCrop1x1=0,mirrorCrop9x16=0;};
struct VideoCropFingerprint {
 std::vector<std::uint64_t> a4x3, a1x1, a9x16;
 std::vector<std::uint64_t> mirrorA4x3, mirrorA1x1, mirrorA9x16;
 std::vector<double> timestamps;
};
struct VideoSimilarityOptions { double thresholdPercent=50.0; double gapPenalty=8.0; double timeToleranceSeconds=2.0; };
class VideoFingerprintEngine{
public:
 ~VideoFingerprintEngine();
 bool build(const std::string&,VideoFingerprint&) const;
 bool buildCropAware(const std::string&, const VideoFingerprint&, VideoCropFingerprint&, int decodeSize=96) const;
 bool openPersistentCache(const std::string& sqlitePath) const;
 void closePersistentCache() const;
 void clearCache() const;
 std::size_t memoryCacheSize() const;
private:
 struct CacheEntry { std::uint64_t size=0, modified=0; VideoFingerprint fingerprint; };
 mutable std::unordered_map<std::string,CacheEntry> cache_;
 mutable std::mutex cacheMutex_;
 mutable void* cacheDb_=nullptr;
 mutable void* loadStmt_=nullptr;
 mutable void* saveStmt_=nullptr;
 mutable std::mutex dbMutex_;
 static constexpr int kCacheFormatVersion=3;
 bool preparePersistentStatements() const;
 void finalizePersistentStatements() const;
 bool loadPersistent(const std::string&,std::uint64_t,std::uint64_t,VideoFingerprint&) const;
 void savePersistent(const std::string&,std::uint64_t,std::uint64_t,const VideoFingerprint&) const;
};
double video_similarity(const VideoFingerprint&,const VideoFingerprint&,const VideoSimilarityOptions& options={});
double video_crop_similarity(const VideoFingerprint&, const VideoCropFingerprint&, const VideoFingerprint&, const VideoCropFingerprint&, const VideoSimilarityOptions& options={});
}
