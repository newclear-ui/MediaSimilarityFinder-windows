// Disk thumbnail-cache regression test: put/get round-trip, stale-entry
// rejection on size/mtime change, and prune of index-orphaned rows.
#include "database.h"
#include <filesystem>
#include <iostream>
int main() {
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_thumb_test";
  std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
  const std::string dbp = (d / "index.sqlite").string();
  msf::Database db;
  if (!db.open(dbp) || !db.initialize()) return 1;
  const std::vector<unsigned char> jpeg = {0xFF, 0xD8, 0xFF, 0xD9};
  if (!db.putThumb("a.jpg", 100, 1000, jpeg)) return 2;
  std::vector<unsigned char> got;
  if (!db.getThumb("a.jpg", 100, 1000, got) || got != jpeg) return 3;
  // Stale on mtime or size change.
  if (db.getThumb("a.jpg", 101, 1000, got)) return 4;
  if (db.getThumb("a.jpg", 100, 1001, got)) return 5;
  // Prune drops rows whose file left the index, keeps live ones.
  msf::FileState live{"a.jpg", 100, 10, "x"};
  if (!db.upsert(live)) return 6;
  if (!db.putThumb("gone.jpg", 50, 500, jpeg)) return 7;
  if (!db.pruneThumbs()) return 8;
  if (!db.getThumb("a.jpg", 100, 1000, got)) return 9;
  if (db.getThumb("gone.jpg", 50, 500, got)) return 10;
  db.close();
  fs::remove_all(d, ec);
  std::cout << "thumb_cache=ok\n";
  return 0;
}
