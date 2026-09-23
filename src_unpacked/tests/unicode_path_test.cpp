// Regression: non-ASCII (non-ANSI-codepage) filenames must not throw or crash.
// On Windows, constructing std::filesystem::path from a narrow UTF-8 string
// throws filesystem_error when the name holds characters outside the process
// ANSI code page — even when an error_code is supplied (the conversion itself
// throws). All such boundaries must go through path_from_utf8()/wide APIs.
// Filenames below spell "test" in Korean via explicit UTF-8 byte escapes so
// the test is independent of the compiler's source-file encoding.
#include "database.h"
#include "media_pipeline.h"
#include "path_utils.h"
#include "scanner.h"
#include "video_fingerprint.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
namespace fs = std::filesystem;
static fs::path kofile(const fs::path& dir, const char* tail) {
  // U+D14C U+C2A4 U+D2B8 ("test" in Korean) + tail, interpreted as UTF-8.
  std::string n = std::string("test_\xEC\x8C\x8C\xEC\x8A\xA4\xED\x8A\xB8_") + tail;
  return dir / fs::path(std::u8string(n.begin(), n.end()));
}
static void writeBmp(const fs::path& p) {
  const int w = 8, h = 8, row = ((w * 3 + 3) / 4) * 4, img = row * h, fsz = 54 + img;
  std::vector<unsigned char> hd(54, 0);
  hd[0] = 'B'; hd[1] = 'M';
  hd[2] = fsz & 255; hd[3] = (fsz >> 8) & 255; hd[4] = (fsz >> 16) & 255; hd[5] = (fsz >> 24) & 255;
  hd[10] = 54; hd[14] = 40; hd[18] = w; hd[22] = h; hd[26] = 1; hd[28] = 24;
  std::ofstream f(p, std::ios::binary);
  f.write((char*)hd.data(), hd.size());
  std::vector<unsigned char> px(row * h, 0);
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    unsigned char v = (x < 4 ? (unsigned char)20 : (unsigned char)220);
    px[y * row + x * 3 + 0] = v; px[y * row + x * 3 + 1] = v; px[y * row + x * 3 + 2] = v;
  }
  f.write((char*)px.data(), px.size());
}
static void writePgm(const fs::path& p) {
  std::ofstream f(p, std::ios::binary);
  f << "P5\n8 8\n255\n";
  for (int i = 0; i < 64; i++) f.put((char)((i % 8) < 4 ? 20 : 220));
}
int main() {
  auto d = fs::temp_directory_path() / "msf_unicode_test";
  std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
  const fs::path bmp = kofile(d, ".bmp");
  const fs::path pgm = kofile(d, ".pgm");
  const fs::path dbp = kofile(d, ".sqlite");
  writeBmp(bmp); writePgm(pgm);
  // 1. WIC wide path decodes a Korean-named BMP.
  msf::MediaPipeline pipe; std::uint64_t fp = 0;
  if (!pipe.image(msf::path_to_utf8(bmp), fp) || fp == 0) return 1;
  // 2. PGM fallback opens through the wide path as well.
  std::uint64_t fp2 = 0;
  if (!pipe.image(msf::path_to_utf8(pgm), fp2) || fp2 == 0) return 2;
  // 3. Video fingerprint build must not throw on a Korean path
  // (BMP content under an .mp4 name fails decode gracefully).
  const fs::path fake = kofile(d, ".mp4");
  fs::copy_file(bmp, fake, ec);
  msf::VideoFingerprintEngine ve; msf::VideoFingerprint vf;
  ve.build(msf::path_to_utf8(fake), vf); // must return, never terminate
  // 4. Scanner finds Korean-named files.
  msf::Scanner sc;
  auto found = sc.scan(msf::path_to_utf8(d));
  if (found.size() < 2) return 3;
  // 5. SQLite database on a Korean path works (SQLite takes UTF-8).
  msf::Database db;
  if (!db.open(msf::path_to_utf8(dbp)) || !db.initialize()) return 4;
  msf::FileState a{"a.jpg", 100, 10, "x"};
  if (!db.upsert(a) || db.all().size() != 1) return 5;
  db.close();
  fs::remove_all(d, ec);
  std::cout << "unicode_path=ok\n";
  return 0;
}
