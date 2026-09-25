// Image second-stage regression: Hamming-only verdicts let same-low-frequency
// false positives through (dark smooth photos within D<=8). verifyImagePair
// must cut them while keeping true duplicates (exact, re-encode-like) and
// degrading gracefully on missing files / video kinds.
#include "image_verify.h"
#include <filesystem>
#include <fstream>
#include <iostream>
// Minimal 8x8 24-bit BMP. mode 0 = solid color, 1 = checker, 2 = inverse checker.
static void bmp(const std::filesystem::path& p, int mode, unsigned char r, unsigned char g, unsigned char b, int w = 8, int h = 8) {
  std::ofstream f(p, std::ios::binary);
  const int row = ((w * 3 + 3) / 4) * 4, img = row * h, fs = 54 + img;
  unsigned char hd[54] = {0};
  hd[0] = 'B'; hd[1] = 'M';
  hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
  hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
  hd[26] = 1; hd[28] = 24;
  hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
  f.write((const char*)hd, 54);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      unsigned char v = 0;
      if (mode == 0) v = 0;
      else if (mode == 1) v = ((x + y) % 2) ? 200 : 50;
      else if (mode == 2) v = ((x + y) % 2) ? 50 : 200;
      else if (mode == 3) v = (x < 4 || x >= 12) ? 128 : (((x + y) % 2) ? 200 : 50);
      else v = ((x + y) % 2) ? 200 : 50;
      f.put((char)(mode == 0 ? b : v)); f.put((char)(mode == 0 ? g : v)); f.put((char)(mode == 0 ? r : v));
    }
    for (int k = w * 3; k < row; ++k) f.put(0);
  }
}
int main() {
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_imgverify_test";
  std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
  const std::string red = (d / "red.bmp").string(), red2 = (d / "red2.bmp").string();
  const std::string chkA = (d / "chkA.bmp").string(), chkB = (d / "chkB.bmp").string();
  bmp(d / "red.bmp", 0, 255, 0, 0); bmp(d / "red2.bmp", 0, 255, 0, 0);
  bmp(d / "chkA.bmp", 1, 0, 0, 0); bmp(d / "chkB.bmp", 2, 0, 0, 0);
  constexpr double kThr = 87.5; // scan-time D8 line
  // 1. Near-identical fast path, no decode needed (missing files still pass).
  if (msf::verifyImagePair("nope-a", "nope-b", true, 100.0, kThr) != 100.0) return 1;
  if (msf::verifyImagePair(red, red2, true, 100.0, kThr) < 99.0) return 2;
  // 2. Grey zone, identical content: blend of high H + SSIM~1 stays passing.
  if (msf::verifyImagePair(red, red2, true, 90.0, kThr) < kThr) return 3;
  // 3. Grey zone, different content (inverse checkers: same mean, anti-
  //    correlated structure -> SSIM ~0): must drop below the scan line.
  //    (Flat-vs-flat would NOT separate — luminance term stays high — so the
  //    fixtures are textured, like real reported photos.)
  if (msf::verifyImagePair(chkA, chkB, true, 90.0, kThr) >= kThr) return 4;
  // 4. Decode-failure fallback keeps the Hamming verdict (legacy behavior).
  if (msf::verifyImagePair("nope-a", "nope-b", true, 90.0, kThr) != 90.0) return 5;
  // 5. Video pairs pass through untouched (temporal L2/L3 owns them).
  if (msf::verifyImagePair(red, red2, false, 90.0, kThr) != 90.0) return 6;
  // 6. Empty paths pass through (adhoc/monitor queries without a query file).
  if (msf::verifyImagePair("", red2, true, 90.0, kThr) != 90.0) return 7;
  // 7. True crop duplicate in the grey zone: wide photo with a checker center
  //    vs the center alone. Crop-vs-full SSIM must keep it passing.
  const std::string wide = (d / "wide.bmp").string(), center = (d / "center.bmp").string();
  bmp(d / "wide.bmp", 3, 0, 0, 0, 16, 8); bmp(d / "center.bmp", 1, 0, 0, 0, 8, 8);
  if (msf::verifyImagePair(wide, center, true, 90.0, kThr) < kThr) return 8;
  fs::remove_all(d, ec);
  std::cout << "image_verify=ok\n";
  return 0;
}
