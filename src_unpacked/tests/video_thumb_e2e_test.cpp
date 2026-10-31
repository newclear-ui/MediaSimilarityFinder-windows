// Video thumbnail end-to-end probe: a scanned video must serve a thumbnail
// through the same path BackendSession::requestThumb uses (disk cache ->
// engine art via getVideoThumb/peekThumb48 -> ThumbnailStore::fetch).
#include "mainwindow.h"
#include "scan_worker.h"
#include "media_search_engine.h"
#include "thumbnail_store.h"
#include <QCoreApplication>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_video_thumb_e2e";
  std::error_code ec; fs::remove_all(d, ec);
  fs::create_directories(d / "media", ec); fs::create_directories(d / "appdir", ec);
  const std::string clip = (d / "media" / "clip.mp4").string();
  std::string c1 = "ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc=size=160x90:rate=10:duration=8 -c:v mpeg4 -pix_fmt yuv420p \"" + clip + "\"";
  if (std::system(c1.c_str()) != 0) return 1;
  const std::string root = (d / "media").string(), ad = (d / "appdir").string();
  ScanWorker w(QString::fromStdString(root), QString::fromStdString(ad),
               8, 50, 50, false, false, true);
  w.run();
  // 1) Engine art must exist for the analyzed video (48x48 gray from kept frames).
  std::vector<unsigned char> gray;
  if (!w.scanEngine().getVideoThumb(clip, gray)) { std::cerr << "no engine video thumb\n"; return 2; }
  if (gray.size() != 48u * 48u) { std::cerr << "engine thumb wrong size=" << gray.size() << "\n"; return 3; }
  // 2) ThumbnailStore must serve it through the exact backend fetch path.
  msf::ThumbnailStore store;
  if (!store.ensureOpen(ad, root)) { std::cerr << "thumb store open failed\n"; return 4; }
  msf::RawArt art;
  art.bgra = std::move(gray); art.width = 48; art.height = 48; art.gray = true; art.ok = true;
  msf::StoredThumb st = store.fetch(clip, 128, [&]() { return std::move(art); });
  if (!st.ok || st.jpeg.empty()) { std::cerr << "thumbnail store served nothing\n"; return 5; }
  // 3) Disk persistence round-trip: a fresh store must serve the cached thumb
  // without the engine (covers post-scan / reloaded views).
  msf::ThumbnailStore store2;
  if (!store2.ensureOpen(ad, root)) { std::cerr << "thumb store reopen failed\n"; return 6; }
  msf::StoredThumb st2 = store2.fetch(clip, 128, []() { return msf::RawArt{}; });
  if (!st2.ok || st2.jpeg.empty()) { std::cerr << "disk-cached thumb not served\n"; return 7; }
  fs::remove_all(d, ec);
  std::cout << "video_thumb_e2e=ok bytes=" << st.jpeg.size() << "\n";
  return 0;
}
