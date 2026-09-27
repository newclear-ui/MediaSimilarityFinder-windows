#include "image_verify.h"
#include "image_decoder.h"
#include "crop_fingerprint.h"
#include "video_fingerprint.h" // frame_ssim
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <unordered_map>
namespace msf {
namespace {
struct VerifyBuffers { GrayImage full; GrayImage asp; };
struct VerifyCacheKey { std::string path; std::uint64_t size=0; std::uint64_t modified=0; std::string quickHash; bool operator==(const VerifyCacheKey& o) const { return path==o.path&&size==o.size&&modified==o.modified&&quickHash==o.quickHash; } };
struct VerifyCacheKeyHash { std::size_t operator()(const VerifyCacheKey& k) const noexcept {
  std::size_t h=std::hash<std::string>{}(k.path); h^=std::hash<std::uint64_t>{}(k.size+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2)); h^=std::hash<std::uint64_t>{}(k.modified+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2)); h^=std::hash<std::string>{}(k.quickHash); return h; } };
constexpr std::size_t kVerifyCacheMax = 32;
std::mutex verifyCacheMutex;
std::list<std::pair<VerifyCacheKey,VerifyBuffers>> verifyCacheList;
std::unordered_map<VerifyCacheKey,std::list<std::pair<VerifyCacheKey,VerifyBuffers>>::iterator,VerifyCacheKeyHash> verifyCacheMap;
// D9a: counts a cache hit or miss at the exact lookup site. A decode that
// then fails is still a miss followed by a decode failure -- the existing
// error semantics are untouched, this only records which branch was taken.
bool verifyBuffersFor(const std::string& path, GrayImage& full, GrayImage& asp,
                      AnalyzeTelemetry* tel){
  constexpr int kDim = 64;
  std::error_code ec;
  const std::uint64_t sz = std::filesystem::file_size(path, ec);
  if(ec) return false;
  const std::uint64_t mt = (std::uint64_t)std::filesystem::last_write_time(path, ec).time_since_epoch().count();
  if(ec) return false;
  std::ifstream qf(path,std::ios::binary); unsigned char b[65536]; qf.read(reinterpret_cast<char*>(b),sizeof(b)); const std::size_t n=static_cast<std::size_t>(qf.gcount()); std::uint64_t q=1469598103934665603ULL; for(std::size_t i=0;i<n;++i){q^=b[i];q*=1099511628211ULL;}
  const VerifyCacheKey key{path, sz, mt, std::to_string(q)};
  {
    std::lock_guard<std::mutex> lock(verifyCacheMutex);
    auto it = verifyCacheMap.find(key);
    if(it != verifyCacheMap.end()){
      verifyCacheList.splice(verifyCacheList.begin(), verifyCacheList, it->second);
      if(tel) ++tel->verifyCacheHits;
      full = it->second->second.full; asp = it->second->second.asp;
      return true;
    }
  }
  if(tel) ++tel->verifyDecodeMisses;
  ImageDecoder dec; GrayImage f, a;
  if(!dec.decode(path, kDim, kDim, f) || !dec.decodePreserveAspect(path, kDim, a)) return false;
  if(f.width != kDim || f.height != kDim || f.pixels.size() != (std::size_t)kDim * kDim) return false;
  if(a.width <= 0 || a.height <= 0 || a.pixels.size() != (std::size_t)a.width * a.height) return false;
  {
    std::lock_guard<std::mutex> lock(verifyCacheMutex);
    verifyCacheList.emplace_front(key, VerifyBuffers{f, a});
    verifyCacheMap[key] = verifyCacheList.begin();
    while(verifyCacheList.size() > kVerifyCacheMax){
      verifyCacheMap.erase(verifyCacheList.back().first);
      verifyCacheList.pop_back();
    }
  }
  full = f; asp = a;
  return true;
}
void flipBuf(const GrayImage& src, GrayImage& dst){
  dst.width = src.width; dst.height = src.height;
  dst.pixels.resize(src.pixels.size());
  for(int y = 0; y < src.height; ++y) for(int x = 0; x < src.width; ++x)
    dst.pixels[(std::size_t)y * src.width + x] = src.pixels[(std::size_t)y * src.width + (src.width - 1 - x)];
}
// D9a: ssimEvals counts invocations; frameSsimEvals counts the actual
// frame_ssim calls, which is lower whenever an early dimension/size guard
// rejects the pair. The two therefore may differ, and that difference is
// itself a measurement rather than a bug.
double ssimBuf(const GrayImage& a, const GrayImage& b, AnalyzeTelemetry* tel){
  if(tel) ++tel->ssimEvals;
  if(a.width <= 0 || a.height <= 0 || a.width != b.width || a.height != b.height) return 0;
  if(a.pixels.size() != (std::size_t)a.width * a.height || b.pixels.size() != a.pixels.size()) return 0;
  GrayImage f; flipBuf(b, f);
  if(tel) tel->frameSsimEvals += 2;
  return std::max(frame_ssim(a.pixels.data(), b.pixels.data(), a.width, a.height),
                  frame_ssim(a.pixels.data(), f.pixels.data(), a.width, a.height));
}
} // namespace
double verifyImagePair(const std::string& pathA, const std::string& pathB,
                       bool isImage, double hammingSim, double threshold,
                       AnalyzeTelemetry* tel) {
  if (!isImage) return hammingSim;
  // D9a: only actual image verification counts. A video pair returns above
  // without doing any work, so counting the call site blindly would inflate
  // the denominator of msPerVerifyCall with pairs that cost nothing.
  if (tel) ++tel->verifyCalls;
  constexpr double kFast = 97.0;
  if (hammingSim >= kFast) return hammingSim;
  if (pathA.empty() || pathB.empty()) return hammingSim;
  GrayImage fA, aA, fB, aB;
  if (!verifyBuffersFor(pathA, fA, aA, tel) || !verifyBuffersFor(pathB, fB, aB, tel)) return hammingSim;
  double s = ssimBuf(fA, fB, tel);
  const double aspects[3] = {4.0 / 3.0, 1.0, 9.0 / 16.0};
  GrayImage fullA = centerCropResize(aA, (double)aA.width / aA.height);
  GrayImage fullB = centerCropResize(aB, (double)aB.width / aB.height);
  for (double asp : aspects) {
    GrayImage rA = centerCropResize(aA, asp), rB = centerCropResize(aB, asp);
    s = std::max(s, ssimBuf(rA, rB, tel));
    s = std::max(s, ssimBuf(fullA, rB, tel));
    s = std::max(s, ssimBuf(rA, fullB, tel));
  }
  return 0.5 * hammingSim + 0.5 * (100.0 * s);
}
}
