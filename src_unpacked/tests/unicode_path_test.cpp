// Regression: non-ASCII (non-ANSI-codepage) filenames must not throw or crash.
// On Windows, constructing std::filesystem::path from a narrow UTF-8 string
// throws filesystem_error when the name holds characters outside the process
// ANSI code page ??even when an error_code is supplied (the conversion itself
// throws). All such boundaries must go through path_from_utf8()/wide APIs.
// Filenames below spell "test" in Korean via explicit UTF-8 byte escapes so
// the test is independent of the compiler's source-file encoding.
#include "database.h"
#include "dataset_fingerprint.h"
#include "image_verify.h"
#include "media_pipeline.h"
#include "media_search_engine.h"
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
  // 6. Dataset fingerprint must not throw on Korean-named files (0.9.4.46+).
  // canonicalRelativePath() built fs::path from narrow UTF-8 bytes, which MSVC
  // reinterprets as the ANSI code page and throws "No mapping for the Unicode
  // character..." for any non-ASCII name, failing the whole scan at startup.
  {
    const auto fp = msf::computeDatasetFingerprint(msf::path_to_utf8(d));
    if (fp.state != "measured" || fp.fileCount < 2 || fp.fingerprint.empty()) return 6;
  }
  // 7-10. Names outside the ANSI code page entirely (0.9.4.77). Korean names
  // above are representable in CP949, so they never exercised the throwing
  // narrow conversions in verifyBuffersFor/thumbQuickHash/quickIdentity. U+20000
  // (CJK Extension B, UTF-8 F0 A0 80 80) is not mappable to CP949: pre-fix,
  // analyzing such a file threw filesystem_error and failed the whole scan.
  {
    const auto deep = d / "deep";
    fs::create_directories(deep, ec);
    // "deep_<U+20000>_a.bmp": explicit UTF-8 escapes, encoding-independent.
    const std::string deepName = std::string("deep_\xF0\xA0\x80\x80_");
    const fs::path bmpA = deep / fs::path(std::u8string(deepName.begin(), deepName.end()) + u8"a.bmp");
    const fs::path bmpB = deep / fs::path(std::u8string(deepName.begin(), deepName.end()) + u8"b.bmp");
    writeBmp(bmpA);
    { std::error_code c2; fs::copy_file(bmpA, bmpB, c2); if (c2) return 7; }
    const std::string utfA = msf::path_to_utf8(bmpA);
    // 7. Walker finds both files.
    msf::Scanner sc2;
    if (sc2.scan(msf::path_to_utf8(deep)).size() < 2) return 7;
    // 8. verifyImagePair in the grey zone must not throw: it decodes both
    // files through verifyBuffersFor (used to die in the 64KiB key read).
    // hammingSim 90 is below the >=97 near-identical shortcut, forcing decode.
    msf::verifyImagePair(utfA, msf::path_to_utf8(bmpB), true, 90.0, 80.0, nullptr);
    // 9. Thumbnail quick-hash roundtrip must not throw (Database::putThumb).
    {
      msf::Database db2;
      const fs::path tdb = d / "deep.sqlite";
      if (!db2.open(msf::path_to_utf8(tdb)) || !db2.initialize()) return 9;
      std::error_code se;
      const auto sz = (std::uint64_t)fs::file_size(bmpA, se);
      const auto mt = (std::int64_t)fs::last_write_time(bmpA, se).time_since_epoch().count();
      if (se) return 9;
      const std::vector<unsigned char> jpeg{0xFF, 0xD8, 0xFF, 0xD9};
      if (!db2.putThumb(utfA, mt, sz, jpeg)) return 9;
      std::vector<unsigned char> back;
      if (!db2.getThumb(utfA, mt, sz, back) || back != jpeg) return 9;
      db2.close();
    }
    // 10. A full engine scan over unmappable names completes and indexes.
    {
      msf::MediaSearchEngine eng;
      const std::string ad = msf::path_to_utf8(d / "deepidx");
      if (!eng.openIndexForRoot(msf::path_to_utf8(deep), ad)) return 10;
      const auto r = eng.scan(msf::path_to_utf8(deep));
      if (!r.completed || r.analyzed != 2) return 10;
      // Match persistence is the ScanWorker's job; mirror it here so the
      // stored-pair read path is also covered on unmappable names.
      if (!eng.saveMatches(r.matches)) return 10;
      if (eng.loadMatches().size() < 1) return 10;
    }
  }
  fs::remove_all(d, ec);
  std::cout << "unicode_path=ok\n";
  return 0;
}
