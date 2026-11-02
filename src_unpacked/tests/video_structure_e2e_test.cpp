// 0.9.4.167 V2: structure probe end-to-end. B/W/B synthetic clip
// (black 3 s, white 2 s, black 3 s): the probe must find the two cuts and
// three segments on real decoded frames. Diagnostic path only
// (MSF_VIDEO_STRUCTURE=1); default builds stay uninstrumented.
#include "video_fingerprint.h"
#include "video_policy.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main() {
  std::error_code ec;
  const auto d = fs::path("D:/Temp/OpenCodeWork/msf_video_struct");
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  const std::string clip = (d / "bwb.mp4").string();
  // Black 3 s + white 2 s + black 3 s at 10 fps. Frame selection admits
  // the first frame with t+0.05>=target, so a white block ending exactly
  // on a sample lands white; ending it at 5.0 keeps the tail black.
  const std::string gen = std::string("ffmpeg -hide_banner -loglevel error -y"
      " -f lavfi -i color=c=black:size=160x90:rate=10:duration=3"
      " -f lavfi -i color=c=white:size=160x90:rate=10:duration=2"
      " -f lavfi -i color=c=black:size=160x90:rate=10:duration=3"
      " -filter_complex \"[0:v]format=yuv420p[b0];[1:v]format=yuv420p[b1];[2:v]format=yuv420p[b2];[b0][b1][b2]concat=n=3:v=1:a=0[out]\""
      " -map \"[out]\" -c:v mpeg4 -pix_fmt yuv420p -movflags +faststart \"") + clip + "\"";
  if (std::system(gen.c_str()) != 0 || !fs::exists(clip, ec)) {
    std::cerr << "fixture generation failed (environment)\n";
    return 2;
  }
#if defined(_WIN32)
  _putenv_s("MSF_VIDEO_STRUCTURE", "1");
#else
  setenv("MSF_VIDEO_STRUCTURE", "1", 1);
#endif
  msf::VideoFingerprintEngine e;
  if (!e.openPersistentCache((d / "db.sqlite").string())) return 3;
  // Short 1 s probe interval so the 2 s white block is always sampled.
  msf::VideoPolicy custom;
  custom.shortIntervalSec = 1.0;
  e.setVideoPolicy(custom);
  msf::VideoFingerprint vf;
  msf::VideoBuildStats st;
  if (!e.build(clip, vf, nullptr, nullptr, &st)) { std::cerr << "build failed\n"; return 4; }
  std::cout << "  [info] probed=" << st.structProbed << " segments=" << st.structSegments
            << " conf=" << st.structConfidence << "\n";
  if (!st.structProbed) { std::cerr << "probe did not run\n"; return 5; }
  if (st.structSegments != 3) { std::cerr << "expected 3 segments\n"; return 6; }
  if (!(st.structConfidence > 0.0)) { std::cerr << "no confidence\n"; return 7; }
#if defined(_WIN32)
  _putenv_s("MSF_VIDEO_STRUCTURE", "");
#else
  unsetenv("MSF_VIDEO_STRUCTURE");
#endif
  e.closePersistentCache();
  fs::remove_all(d, ec);
  std::cout << "video_structure_e2e=ok\n";
  return 0;
}
