// 0.9.4.166 (P1): timestamp-basis investigation probe. Compares an 8s
// testsrc clip (stream start_time==0) against the same clip muxed with
// +2.5 s output timestamp offset (start_time>0).
// 0.9.4.167 (P1): the basis is now normalized in the decoder, so this test
// pins the fixed behavior: identical visual content at identical video
// times must yield identical kept timestamps and hashes, regardless of
// the container offset. Structural invariants hold in both builds;
// equality holds only with the fix (pre-fix: first-shift=2.5, kept 2 vs 3).
#include "video_fingerprint.h"
#include "video_decoder.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef MSF_HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
}
#endif

namespace fs = std::filesystem;

static int gen(const std::string& out, bool offset) {
  std::string c = "ffmpeg -hide_banner -loglevel error -y -f lavfi"
                  " -i testsrc=size=160x90:rate=10:duration=8 -c:v mpeg4 -pix_fmt yuv420p";
  if (offset) c += " -output_ts_offset 2.5";
  c += " -movflags +faststart \"" + out + "\"";
  return std::system(c.c_str());
}

static double streamStartSec(const std::string& p, double& durSec) {
  durSec = 0;
#ifdef MSF_HAS_FFMPEG
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, p.c_str(), nullptr, nullptr) < 0) return -999;
  if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return -999; }
  const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (si < 0) { avformat_close_input(&fmt); return -999; }
  const AVStream* st = fmt->streams[si];
  durSec = st->duration > 0 ? st->duration * av_q2d(st->time_base)
                            : (fmt->duration > 0 ? fmt->duration / (double)AV_TIME_BASE : 0);
  double start = (st->start_time == AV_NOPTS_VALUE)
                     ? 0.0
                     : st->start_time * av_q2d(st->time_base);
  avformat_close_input(&fmt);
  return start;
#else
  (void)p;
  return -999;
#endif
}

static int checkFile(msf::VideoFingerprintEngine& e, const std::string& p,
                     const char* tag, double& firstTs, double& lastTs, std::size_t& kept) {
  msf::VideoFingerprint vf;
  msf::VideoBuildStats st;
  if (!e.build(p, vf, nullptr, nullptr, &st)) { std::cerr << tag << " build failed\n"; return 1; }
  if (vf.hashes.empty()) { std::cerr << tag << " empty fingerprint\n"; return 2; }
  const auto& ts = vf.timestamps;
  for (std::size_t i = 1; i < ts.size(); ++i)
    if (!(ts[i] + 1e-9 >= ts[i - 1])) { std::cerr << tag << " timestamps not monotonic\n"; return 3; }
  double dur = 0;
  const double start = streamStartSec(p, dur);
  for (double t : ts)
    if (!(t >= -0.01 && t <= dur + 0.5)) { std::cerr << tag << " timestamp out of range\n"; return 4; }
  if (st.noptsFrames != 0) { std::cerr << tag << " unexpected NOPTS fallback\n"; return 5; }
  firstTs = ts.front();
  lastTs = ts.back();
  kept = vf.hashes.size();
  std::cout << "  [info] " << tag << " start=" << start << " dur=" << dur
            << " kept=" << kept << " first=" << firstTs << " last=" << lastTs
            << " nopts=" << st.noptsFrames << "\n";
  return 0;
}

int main() {
  std::error_code ec;
  const auto d = fs::path("D:/Temp/OpenCodeWork/msf_video_tsbasis");
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  const std::string clip0 = (d / "clip0.mp4").string();
  const std::string clipOff = (d / "clipOff.mp4").string();
  if (gen(clip0, false) != 0 || !fs::exists(clip0, ec)) {
    std::cerr << "fixture generation failed (environment)\n";
    return 2;
  }
  if (gen(clipOff, true) != 0 || !fs::exists(clipOff, ec)) {
    std::cerr << "offset fixture generation failed (environment)\n";
    return 2;
  }
  double d0 = 0, d1 = 0;
  const double s0 = streamStartSec(clip0, d0);
  const double s1 = streamStartSec(clipOff, d1);
  std::cout << "  [info] baseline start=" << s0 << " offset start=" << s1 << "\n";
  if (s0 > 0.001) { std::cerr << "baseline fixture has nonzero start_time\n"; return 2; }
  if (!(s1 > 1.0)) {
    std::cout << "video_timestamp_basis=SKIP (no nonzero start_time fixture)\n";
    fs::remove_all(d, ec);
    return 0;
  }
  msf::VideoFingerprintEngine e;
  double f0 = 0, l0 = 0, f1 = 0, l1 = 0;
  std::size_t k0 = 0, k1 = 0;
  if (int r = checkFile(e, clip0, "base ", f0, l0, k0)) return r + 10;
  msf::VideoFingerprintEngine e2;
  if (int r = checkFile(e2, clipOff, "offset", f1, l1, k1)) return r + 20;
  // 0.9.4.167: normalized basis. The offset clip must fingerprint
  // identically to the zero-start original (same visuals at same times).
  if (k1 != k0) { std::cerr << "kept count differs by basis\n"; return 30; }
  if (!(std::abs(f1 - f0) <= 1e-3) || !(std::abs(l1 - l0) <= 1e-3)) {
    std::cerr << "kept grid shifted by start_time\n"; return 31;
  }
  {
    msf::VideoFingerprint va, vb;
    msf::VideoFingerprintEngine ea, eb;
    if (!ea.build(clip0, va) || !eb.build(clipOff, vb)) return 32;
    if (va.hashes != vb.hashes) { std::cerr << "same content, different hashes\n"; return 33; }
    if (va.timestamps != vb.timestamps) {
      bool close = va.timestamps.size() == vb.timestamps.size();
      for (std::size_t i = 0; close && i < va.timestamps.size(); ++i)
        close = std::abs(va.timestamps[i] - vb.timestamps[i]) <= 1e-3;
      if (!close) { std::cerr << "same content, different timestamps\n"; return 34; }
    }
    std::cout << "  [ok] offset-independent fingerprint\n";
  }
  // Observation only (see header): does the whole kept grid ride with the
  // stream-time shift, or does sampling follow request time?
  std::cout << "  [info] first-shift=" << (f1 - f0) << " last-shift=" << (l1 - l0)
            << " kept0=" << k0 << " keptOff=" << k1 << "\n";
  fs::remove_all(d, ec);
  std::cout << "video_timestamp_basis=ok\n";
  return 0;
}
