#include "database.h"
#include "media_search_engine.h"
#include <filesystem>
#include <fstream>
#include <iostream>
// Cancel must keep partial progress instead of rolling everything back:
// the walked file list and every checkpointed chunk survive, the report is
// marked incomplete, and a later full scan resumes to completion.
static void img(const std::filesystem::path& p, int high) {
  std::ofstream f(p, std::ios::binary);
  f << "P5\n64 64\n255\n";
  for (int i = 0; i < 4096; i++) f.put((char)((i % 64) < 32 ? high : 20));
}
int main() {
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_cancel_test";
  std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
  for (int i = 0; i < 6; i++) img(d / ("c" + std::to_string(i) + ".jpg"), 200 - i);
  const std::string dbp = (d / "index.sqlite").string();
  msf::MediaSearchEngine e;
  if (!e.openIndex(dbp)) return 1;
  // Phase 1: cancel right after the first file finishes analyzing.
  msf::ScanControl c1;
  c1.progress = [&](std::size_t done, std::size_t, const std::string&) {
    if (done >= 1) c1.cancel.store(true);
  };
  auto r1 = e.scan(d.string(), 8, &c1);
  if (r1.completed) return 2;
  // Phase 2: partial progress survived in a fresh handle.
  msf::Database db;
  if (!db.open(dbp) || !db.initialize()) return 3;
  if (db.all().empty()) return 4;
  db.close();
  // Phase 3: a full rescan resumes and completes everything.
  msf::MediaSearchEngine e2;
  if (!e2.openIndex(dbp)) return 5;
  auto r2 = e2.scan(d.string(), 8, nullptr);
  if (!r2.completed) return 6;
  if (e2.files().size() != 6) return 7;
  e2.close();
  fs::remove_all(d, ec);
  std::cout << "scan_cancel=ok\n";
  return 0;
}
