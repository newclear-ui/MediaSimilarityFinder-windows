// Live streaming + persistence regression test for the scan worker path.
//
// The GUI shows groups only from what the worker streams (takePending) and
// what a later scan quick-loads from the index (loadMatches). This drives a
// real ScanWorker over 4 byte-identical BMPs headlessly and asserts both:
// matches stream during the scan, and they survive in a fresh engine handle
// (the "previous results reappear" path). QCoreApplication suffices (no GUI).
#include "mainwindow.h"
#include "media_search_engine.h"
#include <QCoreApplication>
#include <QObject>
#include <QSet>
#include <filesystem>
#include <fstream>
#include <iostream>

// Minimal 8x8 24-bit BMP. Identical files hash identically -> distance 0.
static void bmp(const std::filesystem::path& p) {
  std::ofstream f(p, std::ios::binary);
  const int w = 8, h = 8, row = w * 3, img = row * h, fs = 54 + img;
  unsigned char hd[54] = {0};
  hd[0] = 'B'; hd[1] = 'M';
  hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
  hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
  hd[26] = 1; hd[28] = 24;
  hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
  f.write((const char*)hd, 54);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const unsigned char v = (x >= 4) ? 255 : 0;
      f.put((char)v); f.put((char)v); f.put((char)v);
    }
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_stream_test";
  std::error_code ec; fs::remove_all(d, ec);
  fs::create_directories(d / "media", ec); fs::create_directories(d / "appdir", ec);
  for (int i = 0; i < 4; ++i) bmp(d / "media" / ("dup" + std::to_string(i) + ".bmp"));
  { std::ofstream f(d / "media" / "notes.txt"); f << "not media: must not count"; }
  { std::ofstream f(d / "media" / "clip.mp4"); f << "fake video: invalid data, listing count only"; }
  { std::ofstream f(d / "media" / "ignored.bmp"); f << "ignored"; }
  const std::string root = (d / "media").string(), ad = (d / "appdir").string();
  // Phase 1: synchronous worker scan; matches must stream live (4 identical
  // files -> 6 incremental pairs against previously added files).
  ScanWorker w(QString::fromStdString(root), QString::fromStdString(ad),
               8, 50, 50, false, true, false);
  QSet<QString> ignored;
  ignored.insert(QString::fromStdString((d / "media" / "ignored.bmp").string()));
  w.setIgnored(ignored);
  qulonglong target = 0;
  QObject::connect(&w, &ScanWorker::targetCount, [&](qulonglong n) { target = n; });
  w.run();
  if (target != 4) { std::cerr << "target=" << target << "\n"; return 5; }
  const auto pending = w.takePending();
  if (pending.size() < 6) { std::cerr << "streamed=" << pending.size() << "\n"; return 2; }
  // B7 binding/telemetry: the engine scan behind the worker must have
  // recorded its scheduler decision (measured, with a resolved backend).
  // GPU OFF here -> gpu_off 100/0; ON trees assert the same shape.
  {
    const std::string bj = w.scanEngine().benchmarkJson();
    if (bj.find("\"scheduler\":{\"state\":\"measured\"") == std::string::npos) {
      std::cerr << "scheduler not measured\n"; return 6;
    }
    if (bj.find("\"selectedBackend\":\"") == std::string::npos) {
      std::cerr << "no selected backend\n"; return 7;
    }
  }
  // Phase 2: a fresh engine on the same managed index must quick-load them.
  msf::MediaSearchEngine e2;
  if (!e2.openIndexForRoot(root, ad)) return 3;
  const auto stored = e2.loadMatches();
  e2.close();
  if (stored.size() < 6) { std::cerr << "stored=" << stored.size() << "\n"; return 4; }
  fs::remove_all(d, ec);
  std::cout << "scan_streaming=ok streamed=" << pending.size() << " stored=" << stored.size() << "\n";
  return 0;
}
