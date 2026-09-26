// MainWindow scan-workflow regression test (Node A interactive-workflow gap).
//
// CTest/engine suites already drive ScanWorker headlessly, and --smoke only
// shows the window. Nothing exercised the real MainWindow slot path:
// folder -> startScan() -> worker thread -> drainMatches -> group/grid
// render -> scanFinished. This test does exactly that, offscreen.
//
// Design notes:
// - QSettings is redirected to a temp dir via initAppSettings (same pattern
//   as ui_settings_test), so the real user config is never touched. The
//   folder is pre-seeded through "ui/lastFolder", which the MainWindow ctor
//   reads into the folder box — no private-member access.
// - GPU and benchmark checkboxes are left at their defaults (both on):
//   pure user workflow on both trees. CPU fallback on the CPU tree and the
//   CUDA path on the GPU tree must agree (engine parity suites prove the
//   verdicts; this proves the UI path carries them).
// - Completion is observed through the public widget state the user sees:
//   the scan button re-enables via setRunning(false). The modal benchmark
//   summary is dismissed by a closer timer (see above), never by force.
// - The managed index lands under applicationDirPath (Release dir, inside
//   git-ignored build-*), same as a real run. Temp media uses a fresh dir
//   per run, so no state leaks between runs.
#include "mainwindow.h"
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <filesystem>
#include <fstream>
#include <iostream>

// Minimal 8x8 24-bit BMP. Identical files hash identically -> distance 0,
// so 4 copies form exactly 1 group (same fixture shape as scan_streaming).
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
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  namespace fs = std::filesystem;
  std::error_code ec;
  auto d = fs::temp_directory_path() / "msf_workflow_test";
  fs::remove_all(d, ec);
  fs::create_directories(d / "media", ec);
  fs::create_directories(d / "settings", ec);
  for (int i = 0; i < 4; ++i)
    bmp(d / "media" / ("dup" + std::to_string(i) + ".bmp"));
  initAppSettings(QString::fromStdString((d / "settings").string()));
  QSettings().setValue("ui/lastFolder", QString::fromStdString((d / "media").string()));
  QSettings().sync();

  MainWindow w;
  w.show();
  QApplication::processEvents();
  auto* folder = w.findChild<QLineEdit*>("folder");
  if (!folder) { std::cerr << "no folder box\n"; return 1; }
  if (folder->text().isEmpty()) { std::cerr << "lastFolder not loaded\n"; return 2; }
  // Default configuration is used as-is (GPU checkbox on, benchmark on):
  // this is exactly what a user launch does. The end-of-scan benchmark
  // summary is modal (dlg.exec()), so a closer timer dismisses it — timers
  // fire inside modal loops on the same thread. Seeing the dialog also
  // proves the summary path ran.
  bool dialogSeen = false;
  QTimer closer;
  closer.setInterval(250);
  QObject::connect(&closer, &QTimer::timeout, [&]() {
    if (QWidget* m = QApplication::activeModalWidget()) {
      dialogSeen = true;
      m->close();
    }
  });
  closer.start();
  auto* scan = w.findChild<QPushButton*>("scan");
  if (!scan) { std::cerr << "no scan button\n"; return 3; }
  scan->click();
  QApplication::processEvents();
  if (scan->isEnabled()) { std::cerr << "scan did not start\n"; return 4; }
  // Pump until setRunning(false) re-enables the button (user-visible idle).
  QElapsedTimer t;
  t.start();
  while (!scan->isEnabled()) {
    QApplication::processEvents();
    QThread::msleep(50);
    if (t.elapsed() > 120000) { std::cerr << "scan did not finish\n"; return 5; }
  }
  QApplication::processEvents();
  // The user-visible result: exactly 1 group rendered in the image tree.
  auto* tree = w.findChild<QTreeWidget*>("imgTree");
  auto* title = w.findChild<QLabel*>("groupTitle");
  if (!tree || !title) { std::cerr << "no result widgets\n"; return 6; }
  if (tree->topLevelItemCount() != 1) {
    std::cerr << "groups=" << tree->topLevelItemCount() << "\n";
    return 7;
  }
  // "(1)" is language-independent (both KO/EN append the count the same way).
  if (!title->text().contains("(1)")) {
    std::cerr << "title=" << title->text().toStdString() << "\n";
    return 8;
  }
  if (!dialogSeen) { std::cerr << "no benchmark summary dialog\n"; return 9; }
  fs::remove_all(d, ec);
  std::cout << "scan_workflow=ok groups=1\n";
  return 0;
}
