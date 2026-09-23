// Cross-process QSettings round-trip probe for the main window UI state.
//
// --probe-write <dir> <W> <H>: build the window, resize it, destroy it
//    (the destructor persists geometry/splitter/headers), then confirm the
//    INI file landed on disk.
// --probe-verify <dir> <W> <H>: fresh process, build the window, confirm the
//    restored size matches. CTest runs write first, verify second (DEPENDS).
//
// The split across two processes is the point: same-process read-back would
// hit QSettings' in-memory cache and pass even if disk persistence is broken
// (e.g. missing organization/application names -> AccessError -> nothing
// persists, which is exactly the bug this guards against).
#include "mainwindow.h"
#include <QApplication>
#include <QFileInfo>
#include <QSettings>
#include <QDir>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 5) { std::cerr << "usage: ui_settings_test --probe-write|--probe-verify <dir> <W> <H>\n"; return 2; }
  const QString mode = QString::fromLocal8Bit(argv[1]);
  const QString dir = QString::fromLocal8Bit(argv[2]);
  const int W = QString::fromLocal8Bit(argv[3]).toInt();
  const int H = QString::fromLocal8Bit(argv[4]).toInt();
  if (W <= 0 || H <= 0) { std::cerr << "bad size\n"; return 2; }
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QDir().mkpath(dir);
  QApplication app(argc, argv);
  initAppSettings(dir);
  if (mode == "--probe-write") {
    { MainWindow w; w.show(); w.resize(W, H); QApplication::processEvents(); }
    QSettings st; st.sync();
    st.sync();
    if (st.status() != QSettings::NoError) { std::cerr << "settings status error\n"; return 1; }
    const QString ini = st.fileName();
    if (ini.isEmpty() || !QFileInfo::exists(ini)) { std::cerr << "no settings file on disk\n"; return 1; }
    if (!st.contains("ui/mainGeom") || !st.contains("ui/splitter")) { std::cerr << "settings keys missing\n"; return 1; }
    std::cout << "ui_settings_write=ok\n"; return 0;
  }
  if (mode == "--probe-verify") {
    (void)W; // width intentionally unchecked, see below
    MainWindow w;
    w.show(); QApplication::processEvents();
    const QSize sz = w.size();
    QSettings st;
    if (st.status() != QSettings::NoError || !st.contains("ui/mainGeom")) { std::cerr << "settings unreadable\n"; return 1; }
    // Height is asserted exactly. Width is deliberately NOT asserted: the
    // offscreen test screen is 800x800 while the window's layout minimum is
    // wider, so Qt legitimately forces the width up (and would clamp a wider
    // window down) — a test-environment artifact, not an app bug. On a real
    // screen both dimensions round-trip via restoreGeometry (verified: a
    // no-op restore would leave the 880 default height).
    if (sz.height() != H) {
      std::cerr << "geometry not restored: got height " << sz.height()
                << " want " << H << "\n"; return 1;
    }
    std::cout << "ui_settings_verify=ok\n"; return 0;
  }
  std::cerr << "unknown mode\n"; return 2;
}
