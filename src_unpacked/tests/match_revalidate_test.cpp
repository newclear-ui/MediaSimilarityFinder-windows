// Engine-version upgrade path: stored pairs from an older verdict generation
// are re-checked with the current logic instead of forcing a full rescan.
// Seeds an unversioned DB (engineVersion "0.0.0") with one true pair (identical
// BMPs) and one bogus pair (solid vs checker), then asserts revalidation
// keeps the true pair, drops the bogus one, and stamps the current version.
// A second call must be a no-op. Also covers semver ordering + db stamp.
#include "media_search_engine.h"
#include "database.h"
#include "index_manager.h"
#include "path_utils.h"
#include "semver.h"
#include <filesystem>
#include <fstream>
#include <iostream>
// 8x8 24-bit BMP. mode 0 = solid color, 1 = checker.
static void bmp(const std::filesystem::path& p, int mode, unsigned char r, unsigned char g, unsigned char b) {
  std::ofstream f(p, std::ios::binary);
  const int w = 8, h = 8, img = w * h * 3, fs = 54 + img;
  unsigned char hd[54] = {0};
  hd[0] = 'B'; hd[1] = 'M';
  hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
  hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
  hd[26] = 1; hd[28] = 24;
  hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
  f.write((const char*)hd, 54);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned char v = ((x + y) % 2) ? 200 : 50;
      f.put((char)(mode == 0 ? b : v)); f.put((char)(mode == 0 ? g : v)); f.put((char)(mode == 0 ? r : v));
    }
}
int main() {
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_revalidate_test";
  std::error_code ec; fs::remove_all(d, ec);
  fs::create_directories(d / "media", ec); fs::create_directories(d / "appdir", ec);
  bmp(d / "media" / "a.bmp", 0, 255, 0, 0); bmp(d / "media" / "b.bmp", 0, 255, 0, 0);
  bmp(d / "media" / "c.bmp", 1, 0, 0, 0);
  const std::string root = (d / "media").string(), ad = (d / "appdir").string();
  const std::string A = (d / "media" / "a.bmp").string(), B = (d / "media" / "b.bmp").string();
  const std::string C = (d / "media" / "c.bmp").string();
  msf::MediaSearchEngine e;
  if (!e.openIndexForRoot(root, ad)) return 1;
  // Real scan builds fingerprints + index rows (no matches saved by scan()).
  if (!e.scan(root, 8, nullptr).completed) return 2;
  // Simulate a legacy DB: one true pair, one bogus pair, no version stamp.
  if (!e.saveMatches({{A, B, 95.0}, {A, C, 90.0}})) return 3;
  msf::Database db;
  msf::IndexPaths paths;
  if (!msf::IndexManager::resolve(msf::path_from_utf8(ad), msf::path_from_utf8(root), paths)) return 4;
  if (!db.open(msf::path_to_utf8(paths.database)) || !db.initialize()) return 5;
  if (db.engineVersion() != "0.0.0") return 6; // unversioned legacy DB
  // DB schema stamp is written by initialize(); engine stamp untouched.
  if (db.dbVersion() != msf::Database::kDatabaseVersion) return 6;
  // Malformed rows sort below any real version.
  if (!msf::semverLess("", "1.0.1")) return 6;
  if (!msf::semverLess("abc", "0.0.1")) return 6;
  if (msf::compareSemver("1.0.10", "1.0.2") <= 0) return 6; // numeric, not lexicographic
  if (msf::compareSemver("1.1.0", "1.0.10") <= 0) return 6;
  if (msf::compareSemver("2.0.1", "1.9.9") <= 0) return 6;
   if (msf::compareSemver("1.2.0", msf::MediaSearchEngine::kEngineVersion) != 0) return 6;
  db.close();
  int kept = 0, dropped = 0;
  if (!e.revalidateMatches(nullptr, &kept, &dropped)) return 7;
  if (kept != 1 || dropped != 1) { std::cerr << "kept=" << kept << " dropped=" << dropped << "\n"; return 8; }
  const auto stored = e.loadMatches();
  if (stored.size() != 1) return 9;
  const auto& m = stored.front();
  const bool isTrue = (m.leftPath == A && m.rightPath == B) || (m.leftPath == B && m.rightPath == A);
  if (!isTrue) return 10;
  if (!db.open(msf::path_to_utf8(paths.database)) || !db.initialize()) return 11;
  if (db.engineVersion() != msf::MediaSearchEngine::kEngineVersion) return 12;
  db.close();
  // Second call: current version -> no-op, counters zero.
  kept = -1; dropped = -1;
  if (!e.revalidateMatches(nullptr, &kept, &dropped)) return 13;
  if (kept != 0 || dropped != 0) return 14;
  e.close();
  fs::remove_all(d, ec);
  std::cout << "match_revalidate=ok\n";
  return 0;
}
