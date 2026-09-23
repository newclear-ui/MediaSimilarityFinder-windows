// Display-path color regression test: previews must be color, never the
// fingerprint grayscale. Covers the WIC BGRA lane (PNG etc. without Qt
// plugins) and the FFmpeg RGB24 lane (video fallback when shell has no thumb).
#include "image_decoder.h"
#include "video_decoder.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
static void bmpRed(const std::filesystem::path& p) {
  const int w = 8, h = 8, img = w * h * 3, fs = 54 + img;
  unsigned char hd[54] = {0};
  hd[0] = 'B'; hd[1] = 'M';
  hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
  hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
  hd[26] = 1; hd[28] = 24;
  hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
  std::ofstream f(p, std::ios::binary);
  f.write((const char*)hd, 54);
  for (int i = 0; i < w * h; ++i) { f.put(0); f.put(0); f.put((char)255); } // B,G,R = red
}
int main() {
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_color_test";
  std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
#ifdef _WIN32
  // WIC BGRA lane: solid red must come back red, not gray.
  const std::string rp = (d / "red.bmp").string(); bmpRed(d / "red.bmp");
  msf::ImageDecoder id; msf::ColorImage c;
  if (!id.decodeColorAspect(rp, 32, c) || c.width <= 0 || c.height <= 0) return 1;
  if (c.bgra.size() != (size_t)c.width * c.height * 4) return 2;
  std::size_t red = 0; const std::size_t n = (size_t)c.width * c.height;
  for (std::size_t i = 0; i < n; ++i) {
    const unsigned char b = c.bgra[i * 4], g = c.bgra[i * 4 + 1], r = c.bgra[i * 4 + 2];
    if (r > 200 && g < 80 && b < 80) ++red;
  }
  if (red * 2 < n) { std::cerr << "red pixels " << red << "/" << n << "\n"; return 3; }
#endif
  // FFmpeg RGB24 lane: testsrc color bars must come back colorful.
  const std::string vp = (d / "bars.mp4").string();
  std::string gen = "ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc=size=160x90:rate=10:duration=3 -c:v mpeg4 -pix_fmt yuv420p \"" + vp + "\"";
  if (std::system(gen.c_str()) != 0) return 4;
  msf::VideoDecoder dec;
  if (!dec.open(vp)) return 5;
  msf::ColorFrame cf;
  if (!dec.frameAtColor(1.0, 80, 45, cf)) return 6;
  dec.close();
  if (cf.rgb.size() != (size_t)cf.width * cf.height * 3) return 7;
  std::size_t colorful = 0; const std::size_t m = (size_t)cf.width * cf.height;
  for (std::size_t i = 0; i < m; ++i) {
    const int r = cf.rgb[i * 3], g = cf.rgb[i * 3 + 1], b = cf.rgb[i * 3 + 2];
    const int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
    if (mx - mn > 60) ++colorful;
  }
  if (colorful * 20 < m) { std::cerr << "colorful pixels " << colorful << "/" << m << "\n"; return 8; }
  fs::remove_all(d, ec);
  std::cout << "color_thumb=ok\n";
  return 0;
}
