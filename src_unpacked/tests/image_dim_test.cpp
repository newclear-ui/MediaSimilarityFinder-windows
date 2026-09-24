// Header-only dimension probe: PNG IHDR + JPEG SOF must parse without decode
// or process spawn (the ffprobe-per-file console flash + UI stall this
// replaces). Garbage and truncated inputs must fail cleanly.
#include "image_decoder.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
int main() {
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_dim_test";
  std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
  const std::string jpg = (d / "t.jpg").string(), png = (d / "t.png").string();
  std::string g1 = "ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc=size=160x90:rate=1:duration=1 -frames:v 1 \"" + jpg + "\"";
  if (std::system(g1.c_str()) != 0) return 1;
  std::string g2 = "ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc=size=160x90:rate=1:duration=1 -frames:v 1 \"" + png + "\"";
  if (std::system(g2.c_str()) != 0) return 2;
  msf::ImageDecoder dec; int w = 0, h = 0;
  if (!dec.dimensionsFast(jpg, w, h) || w != 160 || h != 90) return 3;
  w = h = 0;
  if (!dec.dimensionsFast(png, w, h) || w != 160 || h != 90) return 4;
  // Garbage + truncated inputs fail, never crash.
  const std::string bad = (d / "bad.jpg").string();
  { std::ofstream f(bad, std::ios::binary); f << "not an image at all, just text padding........"; }
  if (dec.dimensionsFast(bad, w, h)) return 5;
  const std::string tiny = (d / "tiny.png").string();
  { std::ofstream f(tiny, std::ios::binary); const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10}; f.write((const char*)sig, 8); }
  if (dec.dimensionsFast(tiny, w, h)) return 6;
  fs::remove_all(d, ec);
  std::cout << "image_dim=ok\n";
  return 0;
}
