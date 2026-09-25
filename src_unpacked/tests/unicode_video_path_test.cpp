// Regression: FFmpeg decode and video fingerprinting must survive UTF-8 paths
// outside the Windows ANSI code page. Source text stays ASCII so this test is
// independent of the compiler's source-file encoding.
#include "gpu_backend.h"
#include "path_utils.h"
#include "video_fingerprint.h"
#include "similarity.h"
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
namespace fs = std::filesystem;

static fs::path u8path(const std::string& s) {
  return fs::path(std::u8string(s.begin(), s.end()));
}

int main() {
  const auto root = fs::temp_directory_path() / "msf_unicode_video_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  const auto source = root / "source.mp4";
  const std::string command =
      "ffmpeg -hide_banner -loglevel error -y -f lavfi -i "
      "testsrc=size=128x96:rate=10:duration=2 -c:v mpeg4 -pix_fmt yuv420p \"" +
      source.string() + "\"";
  if (std::system(command.c_str()) != 0) return 1;

  const std::vector<std::string> labels = {
      "ja_\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",
      "zh_\xE4\xB8\xAD\xE6\x96\x87",
      "ar_\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A",
      "ko_\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4"};
  msf::GpuBackend gpu;
  const bool gpuAvailable = gpu.available();
  for (const auto& label : labels) {
    const auto dir = root / u8path(label);
    fs::create_directories(dir, ec);
    const auto a = dir / u8path(label + "_a.mp4");
    const auto b = dir / u8path(label + "_b.mp4");
    fs::copy_file(source, a, fs::copy_options::overwrite_existing, ec);
    if (ec) return 2;
    fs::copy_file(source, b, fs::copy_options::overwrite_existing, ec);
    if (ec) return 3;

    msf::VideoFingerprint va, vb;
    msf::VideoBuildStats stats;
    msf::VideoFingerprintEngine engine;
    const auto pathA = msf::path_to_utf8(a);
    const auto pathB = msf::path_to_utf8(b);
    if (!engine.build(pathA, va, gpuAvailable ? &gpu : nullptr, nullptr, &stats)) return 4;
    if (!engine.build(pathB, vb, gpuAvailable ? &gpu : nullptr)) return 5;
    if (va.hashes.size() < 2 || vb.hashes.size() < 2) return 6;
    if (msf::video_similarity(va, vb) < 99.9) return 7;
    if (gpuAvailable && !stats.gpuUsed) return 8;
  }
  fs::remove_all(root, ec);
  std::cout << "unicode_video_path=ok languages=" << labels.size()
            << " gpu=" << (gpuAvailable ? "yes" : "no") << "\n";
  return 0;
}
