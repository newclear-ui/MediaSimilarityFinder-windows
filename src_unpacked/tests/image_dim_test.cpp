// Header-only dimension probe: PNG IHDR + JPEG SOF must parse without decode
// or process spawn (the ffprobe-per-file console flash + UI stall this
// replaces). Garbage and truncated inputs must fail cleanly.
//
// Fixtures: the JPEG comes from ffmpeg's built-in mjpeg encoder, the PNG is
// assembled byte-for-byte here. The vcpkg ffmpeg build this project uses has
// no PNG encoder (only mjpeg/mpeg2video/mpeg4 are compiled in), and the probe
// only parses the IHDR anyway, so a header-sized file is exactly the input
// under test. That keeps the test independent of the encoder feature set.
#include "image_decoder.h"
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
// Bitwise CRC-32 (ISO-HDLC) for the PNG chunk trailers.
std::uint32_t crc32(const unsigned char* p, std::size_t n) {
  std::uint32_t c = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c ^ 0xFFFFFFFFu;
}
void putU32(std::string& out, std::uint32_t v) {
  out.push_back((char)((v >> 24) & 0xFF));
  out.push_back((char)((v >> 16) & 0xFF));
  out.push_back((char)((v >> 8) & 0xFF));
  out.push_back((char)(v & 0xFF));
}
void putChunk(std::string& out, const char* type, const std::string& data) {
  putU32(out, (std::uint32_t)data.size());
  const std::size_t typeAt = out.size();
  out.append(type, 4);
  out += data;
  putU32(out, crc32((const unsigned char*)out.data() + typeAt, 4 + data.size()));
}
// Minimal but well-formed PNG: signature + IHDR + IEND (no pixel data).
void writePngHeader(const std::string& path, std::uint32_t w, std::uint32_t h) {
  static const unsigned char kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  std::string png((const char*)kSig, 8);
  std::string ihdr;
  putU32(ihdr, w);
  putU32(ihdr, h);
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // colour type: truecolour
  ihdr.push_back(0);  // compression
  ihdr.push_back(0);  // filter
  ihdr.push_back(0);  // interlace
  putChunk(png, "IHDR", ihdr);
  putChunk(png, "IEND", std::string());
  std::ofstream f(path, std::ios::binary);
  f.write(png.data(), (std::streamsize)png.size());
}
}  // namespace

int main() {
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_dim_test";
  std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
  const std::string jpg = (d / "t.jpg").string(), png = (d / "t.png").string();
  std::string g1 = "ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc=size=160x90:rate=1:duration=1 -c:v mjpeg -frames:v 1 \"" + jpg + "\"";
  if (std::system(g1.c_str()) != 0) {
    // No usable ffmpeg on PATH: environment gap, not a decoder regression.
    std::cerr << "image_dim=skipped (ffmpeg fixture generation failed)\n";
    return 77;
  }
  writePngHeader(png, 160, 90);
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
