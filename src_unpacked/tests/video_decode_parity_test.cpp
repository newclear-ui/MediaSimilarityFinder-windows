// Parity: single-sweep 96px decode + software-derived 32px frames must be
// low-frequency equivalent to a direct 32px decode for pHash purposes.
// Real re-encoded clip (ffmpeg testsrc), not synthetic pixels.
#include "fingerprint.h"
#include "video_decoder.h"
#include "video_fingerprint.h"
#include "video_sampling.h"
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <iostream>
int main() {
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_decode_parity";
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  const auto src = (d / "src.mp4").string();
  const std::string cmd =
      "ffmpeg -hide_banner -loglevel error -y -f lavfi -i "
      "testsrc=size=256x192:rate=10:duration=6 -c:v mpeg4 -pix_fmt yuv420p \"" +
      src + "\"";
  if (std::system(cmd.c_str()) != 0) return 1;
  msf::VideoDecoder dec;
  if (!dec.open(src)) return 2;
  msf::VideoInfo info;
  dec.info(info);
  const auto plan = msf::make_sample_plan(info.duration);
  std::vector<msf::VideoFrame> direct, hi, derived;
  if (!dec.framesAt(plan.timestamps, 32, 32, direct) || direct.empty()) return 3;
  if (!dec.framesAt96Plus32(plan.timestamps, hi, derived)) return 4;
  dec.close();
  if (direct.size() != derived.size() || hi.size() != derived.size()) {
    std::cerr << "count drift direct=" << direct.size()
              << " derived=" << derived.size() << "\n";
    return 5;
  }
  unsigned worst = 0;
  msf::VideoFingerprint fa, fb;
  for (std::size_t i = 0; i < direct.size(); ++i) {
    if (direct[i].timestamp != derived[i].timestamp) return 6;
    const auto ha =
        msf::perceptual_hash_pair(direct[i].gray, 32, 32).normal;
    const auto hb =
        msf::perceptual_hash_pair(derived[i].gray, 32, 32).normal;
    const unsigned dist = std::popcount(ha ^ hb);
    worst = std::max(worst, dist);
    fa.timestamps.push_back(direct[i].timestamp);
    fb.timestamps.push_back(derived[i].timestamp);
    fa.hashes.push_back(ha);
    fb.hashes.push_back(hb);
    fa.mirrorHashes.push_back(0);
    fb.mirrorHashes.push_back(0);
  }
  // No thumbs: pure Hamming DTW path. Small resampling drift must not move
  // the verdict.
  const double sim = msf::video_similarity(fa, fb);
  std::cout << "decode_parity=ok frames=" << direct.size()
            << " worstBits=" << worst << " similarity=" << sim << "\n";
  if (worst > 4) return 7;
  if (sim < 95.0) return 8;
  fs::remove_all(d, ec);
  return 0;
}
