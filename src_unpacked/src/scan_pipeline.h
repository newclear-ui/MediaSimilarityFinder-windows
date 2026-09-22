#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
namespace msf {
enum class MediaKind { Unknown, Image, Video };
struct MediaFile { std::string path; MediaKind kind=MediaKind::Unknown; std::uint64_t size=0,modified=0,fingerprint=0,mirrorFingerprint=0; std::uint64_t crop4x3=0,crop1x1=0,crop9x16=0,mirrorCrop4x3=0,mirrorCrop1x1=0,mirrorCrop9x16=0; };
struct MediaMatch { std::size_t left=0,right=0; double percent=0; };
struct ScanStats { std::size_t files=0,indexed=0,candidates=0,groups=0,possiblePairs=0; double candidateReductionPercent=0; std::vector<MediaMatch> matches; };
class ScanPipeline {
 std::vector<MediaFile> files_;
public:
 using MatchCallback = std::function<void(const MediaMatch&)>;
 void add(const MediaFile& f);
 ScanStats analyze(unsigned maxDistance=8);
 ScanStats analyze(unsigned maxDistance, const MatchCallback& onMatch);
 const std::vector<MediaFile>& files() const;
};
}
