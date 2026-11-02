// 0.9.4.167 V3: segmented kept-frame selection vs legacy variance ranking.
// 20 s clip (6 s textured + 14 s flat): with keptTarget=2,
// - legacy keeps only textured frames (flat half has nothing above the
//   variance floor, so trimming never reaches it);
// - MSF_VIDEO_KEPT=segmented keeps one per half (time-axis coverage).
// Deterministic: same fixture, fixed thresholds, first-max-wins ties.
#include "video_fingerprint.h"
#include "video_policy.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int gen(const std::string& out) {
  // 6 s textured + 14 s flat gray (20 s total): the default 5 s plan lands
  // at {0,5,10,15} (20 excused by the endpoint tolerance), so the uniform
  // 2-segment split puts textured samples in seg0 and flat ones in seg1.
  const std::string c = std::string("ffmpeg -hide_banner -loglevel error -y"
      " -f lavfi -i \"testsrc=size=160x90:rate=10:duration=6,format=yuv420p\""
      " -f lavfi -i \"color=c=0x808080:size=160x90:rate=10:duration=14,format=yuv420p\""
      " -filter_complex \"[0:v][1:v]concat=n=2:v=1:a=0[out]\" -map \"[out]\""
      " -c:v mpeg4 -pix_fmt yuv420p -movflags +faststart \"") + out + "\"";
  return std::system(c.c_str());
}

static int countFlat(const msf::VideoFingerprint& vf) {
  int n = 0;
  for (double t : vf.timestamps)
    if (t >= 6.0 - 1e-9) ++n;
  return n;
}

int main() {
  std::error_code ec;
  const auto d = fs::path("D:/Temp/OpenCodeWork/msf_video_kept");
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  const std::string clip = (d / "half.mp4").string();
  if (gen(clip) != 0 || !fs::exists(clip, ec)) {
    std::cerr << "fixture generation failed (environment)\n";
    return 2;
  }
  msf::VideoPolicy custom;
  // 12-15 s lands on the G1/G2 boundary; pin both cohorts to keptTarget=2
  // so the test does not depend on container duration rounding.
  custom.keptTarget[0] = 2; custom.keptTarget[1] = 2;

  // Baseline (legacy variance ranking).
  msf::VideoFingerprintEngine e1;
  if (!e1.openPersistentCache((d / "db1.sqlite").string())) return 3;
  e1.setVideoPolicy(custom);
  msf::VideoFingerprint v1;
  if (!e1.build(clip, v1)) { std::cerr << "legacy build failed\n"; return 4; }
  if (v1.hashes.size() != 2) { std::cerr << "legacy kept != 2\n"; return 4; }
  const int flatLegacy = countFlat(v1);
  e1.closePersistentCache();

  // Segmented selection via diagnostic switch.
#if defined(_WIN32)
  _putenv_s("MSF_VIDEO_KEPT", "segmented");
#else
  setenv("MSF_VIDEO_KEPT", "segmented", 1);
#endif
  msf::VideoFingerprintEngine e2;
  if (!e2.openPersistentCache((d / "db2.sqlite").string())) return 5;
  e2.setVideoPolicy(custom);
  msf::VideoFingerprint v2;
  if (!e2.build(clip, v2)) { std::cerr << "segmented build failed\n"; return 6; }
  const int flatSeg = countFlat(v2);
#if defined(_WIN32)
  _putenv_s("MSF_VIDEO_KEPT", "");
#else
  unsetenv("MSF_VIDEO_KEPT");
#endif
  e2.closePersistentCache();

  std::cout << "  [info] legacy flat=" << flatLegacy << " segmented flat=" << flatSeg
            << " kept=" << v2.hashes.size() << "\n";
  if (v2.hashes.size() != 2) { std::cerr << "segmented kept != 2\n"; return 7; }
  if (flatLegacy != 0) { std::cerr << "legacy unexpectedly covers flat half\n"; return 8; }
  if (flatSeg < 1) { std::cerr << "segmented misses flat half\n"; return 9; }
  fs::remove_all(d, ec);
  std::cout << "video_kept_select=ok\n";
  return 0;
}
