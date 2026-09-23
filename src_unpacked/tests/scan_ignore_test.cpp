#include "media_search_engine.h"
#include <filesystem>
#include <fstream>
#include <iostream>
static void img(const std::filesystem::path& p, int high) {
  std::ofstream f(p, std::ios::binary);
  f << "P5\n64 64\n255\n";
  for (int i = 0; i < 4096; i++) f.put((char)((i % 64) < 32 ? high : 20));
}
int main() {
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_ignore_test";
  std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
  img(d / "a.jpg", 220); img(d / "b.jpg", 200); img(d / "c.jpg", 20);
  msf::MediaSearchEngine e;
  if (!e.openIndex((d / "index.sqlite").string())) return 1;
  msf::ScanControl c1;
  auto r1 = e.scan(d.string(), 8, &c1);
  if (r1.analyzed != 3 || e.files().size() != 3) return 2;
  for (const auto& f : e.files()) {
    if ((int)f.kind != 1 || f.duration != 0.0) return 3;
  }
  std::string victim;
  for (const auto& f : e.files())
    if (f.path.find("b.") != std::string::npos) victim = f.path;
  if (victim.empty()) return 4;
  msf::ScanControl c2;
  c2.ignoredPaths.insert(victim);
  auto r2 = e.scan(d.string(), 8, &c2);
  if (r2.scanned != 2) return 5;
  if (e.files().size() != 2) return 6;
  for (const auto& f : e.files())
    if (f.path == victim) return 7;
  e.close();
  fs::remove_all(d, ec);
  std::cout << "scan_ignore=ok\n";
  return 0;
}
