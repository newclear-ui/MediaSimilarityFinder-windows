// 0.9.4.170 (F-2): hardware decode must produce byte-identical fingerprints
// or cleanly fall back to software. Synthetic two-tone fixture: build with
// MSF_VIDEO_HW=off (software) and with auto (hardware when available); the
// kept hashes/timestamps must match exactly whenever HW was actually used.
// When no HW device exists the test still pins SW determinism (SW vs SW).
#include "video_fingerprint.h"
#include "video_decoder.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static void setHw(const char* v) {
#if defined(_WIN32)
  if (v) _putenv_s("MSF_VIDEO_HW", v);
  else _putenv_s("MSF_VIDEO_HW", "");
#else
  if (v) setenv("MSF_VIDEO_HW", v, 1);
  else unsetenv("MSF_VIDEO_HW");
#endif
}

int main() {
  std::error_code ec;
  const auto d = fs::path("D:/Temp/OpenCodeWork/msf_video_hw");
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  const std::string clip = (d / "twotone.mp4").string();
  // Fixture must use a codec whose native FFmpeg decoder offers a HW path
  // (H.264); the native mpeg4 decoder has no get_format HW branch, so an
  // mpeg4 file can never engage HW in this build (SW-only by construction).
  const std::string gen = std::string("ffmpeg -hide_banner -loglevel error -y"
      " -f lavfi -i color=c=red:size=160x90:rate=10:duration=3"
      " -f lavfi -i color=c=blue:size=160x90:rate=10:duration=3"
      " -filter_complex \"[0:v]format=yuv420p[b0];[1:v]format=yuv420p[b1];[b0][b1]concat=n=2:v=1:a=0[out]\""
      " -map \"[out]\" -c:v h264_mf -pix_fmt yuv420p -movflags +faststart \"") + clip + "\"";
  if (std::system(gen.c_str()) != 0 || !fs::exists(clip, ec)) {
    std::cerr << "fixture generation failed (environment)\n";
    return 2;
  }
  const bool hwAvail = msf::VideoDecoder::probeHwDevice();
  std::cout << "  [info] hwAvailable=" << hwAvail
            << " backend=" << msf::VideoDecoder::probeHwBackend() << "\n";

  msf::VideoFingerprint sw, hw;
  msf::VideoBuildStats sst, hst;
  setHw("off");
  {
    msf::VideoFingerprintEngine e;
    if (!e.openPersistentCache((d / "sw.sqlite").string())) return 3;
    if (!e.build(clip, sw, nullptr, nullptr, &sst)) { std::cerr << "sw build failed\n"; return 4; }
    e.closePersistentCache();
  }
  setHw(nullptr);
  {
    msf::VideoFingerprintEngine e;
    if (!e.openPersistentCache((d / "hw.sqlite").string())) return 5;
    if (!e.build(clip, hw, nullptr, nullptr, &hst)) { std::cerr << "hw build failed\n"; return 6; }
    e.closePersistentCache();
  }
  std::cout << "  [info] sw kept=" << sw.hashes.size() << " hw kept=" << hw.hashes.size()
            << " hwUsed=" << hst.hwUsed << " backend=" << hst.hwBackend << "\n";
  if (sw.hashes.size() != hw.hashes.size()) { std::cerr << "kept count differs\n"; return 7; }
  if (!hst.hwUsed) {
    // No HW path engaged (no device, or codec refused it): SW determinism only.
    if (sw.hashes != hw.hashes || sw.timestamps != hw.timestamps) {
      std::cerr << "sw/sw mismatch\n"; return 8;
    }
    std::cout << "  [info] hw not engaged; sw determinism pinned\n";
  } else {
    if (sw.hashes != hw.hashes) { std::cerr << "hw hash mismatch\n"; return 9; }
    if (sw.timestamps != hw.timestamps) { std::cerr << "hw timestamp mismatch\n"; return 10; }
    std::cout << "  [ok] hw identical to sw (" << hst.hwBackend << ")\n";
  }
  fs::remove_all(d, ec);
  std::cout << "video_hw_decode=ok\n";
  return 0;
}
