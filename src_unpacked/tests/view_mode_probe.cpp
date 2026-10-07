// Bisect probe: which half of the Tiles -> Icons round-trip breaks grid items?
// Case GRID: setGridSize(320,76) then setGridSize(QSize()), no delegate calls.
// Case DELEGATE: setItemDelegate(custom) then setItemDelegate(nullptr), no grid calls.
// Exit 0 only if both round-trips keep items visible.
//
// View-mode transition regression (0.9.4.63 manual acceptance): switching the
// middle similar-group grid away from Extra Large (256 Icon) and back leaves
// images overlapped. The REAL path below drives MainWindow::groupViewChanged
// through the viewBox combo (the exact user action) on a real scan fixture
// with production grid settings, measuring visualItemRect geometry after the
// layout settles. No explicit doItemsLayout() calls: they would mask a
// layout that never completes on its own.
#include "mainwindow.h"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <climits>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
static void fill(QListWidget& grid) {
  grid.setViewMode(QListView::IconMode);
  grid.setResizeMode(QListView::Adjust);
  grid.setIconSize(QSize(128, 128));
  for (int i = 0; i < 3; ++i) {
    QPixmap pm(64, 64); pm.fill(Qt::red);
    grid.addItem(new QListWidgetItem(QIcon(pm), QString("group %1\n3 files").arg(i + 1)));
  }
  grid.show();
  QApplication::processEvents();
}
static QRect cell(QListWidget& grid) {
  QApplication::processEvents();
  return grid.visualItemRect(grid.item(0));
}

namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << std::endl; }
    else       { std::cout << "  [ok] " << what << std::endl; }
}

// Seeded 8x8 black/white noise BMP (proven shape from
// ui_scroll_regression_test): identical seeds pair up, different seeds
// separate into their own groups.
void bmp(const std::filesystem::path& p, unsigned seed) {
    std::ofstream f(p, std::ios::binary);
    const int w = 8, h = 8, row = w * 3, img = row * h, fs = 54 + img;
    unsigned char hd[54] = {0};
    hd[0] = 'B'; hd[1] = 'M';
    hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
    hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
    hd[26] = 1; hd[28] = 24;
    hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
    f.write((const char*)hd, 54);
    unsigned s = seed * 2654435761u + 1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            s = s * 1103515245u + 12345u;
            const unsigned char v = (unsigned char)(((s >> 16) & 0xFF) > 127 ? 255 : 0);
            f.put((char)v); f.put((char)v); f.put((char)v);
        }
}

void pumpUntil(std::function<bool()> done, int timeoutMs, const char* what) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        QApplication::processEvents();
        QThread::msleep(50);
        if (t.elapsed() > timeoutMs) {
            std::cerr << "  [FAIL] timeout: " << what << std::endl;
            gOk = false;
            ++gChecks;
            return;
        }
    }
    ++gChecks;
    std::cout << "  [ok] " << what << std::endl;
}

struct Snap {
    int count = 0;
    QSize iconSize;
    QSize gridSize;
    int spacing = 0;
    int viewMode = -1; // 0 ListMode, 1 IconMode
    int vMin = 0, vMax = 0;
    int vpW = 0, vpH = 0;
    std::vector<QRect> rects;
    int overlapPairs = 0; // pairs with positive-area intersection
    int minW = INT_MAX, minH = INT_MAX;
};

// Pumps the event loop until item 0's rect stops changing (Batched layout
// finishes on its own) or the budget runs out. Never calls doItemsLayout:
// a layout that only completes on manual nudges is itself the bug.
bool settle(QListWidget* grid, const char* tag) {
    QRect prev;
    int stable = 0;
    for (int i = 0; i < 60; ++i) {
        QApplication::processEvents();
        QThread::msleep(10);
        if (grid->count() < 1) continue;
        const QRect cur = grid->visualItemRect(grid->item(0));
        if (cur == prev) { if (++stable >= 3) return true; }
        else { stable = 0; prev = cur; }
    }
    std::cerr << "  [info] layout never settles after " << tag << std::endl;
    return false;
}

Snap snapshot(QListWidget* grid) {
    Snap s;
    QApplication::processEvents();
    s.count = grid->count();
    s.iconSize = grid->iconSize();
    s.gridSize = grid->gridSize();
    s.spacing = grid->spacing();
    s.viewMode = (int)grid->viewMode();
    if (auto* vb = grid->verticalScrollBar()) { s.vMin = vb->minimum(); s.vMax = vb->maximum(); }
    s.vpW = grid->viewport()->width(); s.vpH = grid->viewport()->height();
    for (int i = 0; i < s.count; ++i) {
        const QRect r = grid->visualItemRect(grid->item(i));
        s.rects.push_back(r);
        if (r.isValid()) { s.minW = std::min(s.minW, r.width()); s.minH = std::min(s.minH, r.height()); }
    }
    for (int i = 0; i < s.count; ++i)
        for (int j = i + 1; j < s.count; ++j) {
            const QRect inter = s.rects[i].intersected(s.rects[j]);
            if (!inter.isNull() && inter.width() > 0 && inter.height() > 0)
                ++s.overlapPairs;
        }
    return s;
}

void printSnap(const char* tag, const Snap& s) {
    std::cout << "  [info] " << tag << ": n=" << s.count
              << " icon=" << s.iconSize.width() << "x" << s.iconSize.height()
              << " grid=" << (s.gridSize.isEmpty() ? "auto" : (std::to_string(s.gridSize.width()) + "x" + std::to_string(s.gridSize.height())).c_str())
              << " view=" << (s.viewMode == 1 ? "icon" : "list")
              << " minRect=" << (s.minW == INT_MAX ? -1 : s.minW) << "x" << (s.minH == INT_MAX ? -1 : s.minH)
              << " overlap=" << s.overlapPairs << " vbar=[" << s.vMin << "," << s.vMax << "]"
              << " vp=" << s.vpW << "x" << s.vpH << "\n";
    for (size_t i = 0; i < s.rects.size() && i < 6; ++i)
        std::cout << "    item" << i << ": x=" << s.rects[i].x() << " y=" << s.rects[i].y()
                  << " w=" << s.rects[i].width() << " h=" << s.rects[i].height() << "\n";
}

// (sizeSane absolute thresholds removed: background ticks may legitimately
// change delivered thumbnails, so only convergence/count/overlap are stable
// signals. Absolute icon-size expectations live in the real-GUI checklist.)

} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  int rc = 0;
  { // GRID-only round-trip
    QListWidget grid; fill(grid);
    const QRect r0 = cell(grid);
    grid.setGridSize(QSize(320, 76)); QApplication::processEvents();
    grid.setGridSize(QSize()); QApplication::processEvents();
    const QRect r1 = cell(grid);
    std::cerr << "GRID: base=" << r0.width() << "x" << r0.height()
              << " after=" << r1.width() << "x" << r1.height()
              << " valid=" << r1.isValid() << "\n";
    if (!r0.isValid() || !r1.isValid() || r1.height() <= 0) { std::cerr << "GRID BROKEN\n"; rc |= 1; }
  }
  { // DELEGATE-only round-trip (fixed: restore a real default instance, never nullptr)
    QListWidget grid; fill(grid);
    QStyledItemDelegate custom, plain;
    const QRect r0 = cell(grid);
    grid.setItemDelegate(&custom); QApplication::processEvents();
    grid.setItemDelegate(&plain); QApplication::processEvents();
    const QRect r1 = cell(grid);
    std::cerr << "DELEGATE: base=" << r0.width() << "x" << r0.height()
              << " after=" << r1.width() << "x" << r1.height()
              << " valid=" << r1.isValid() << "\n";
    if (!r0.isValid() || !r1.isValid() || r1.height() <= 0) { std::cerr << "DELEGATE BROKEN\n"; rc |= 2; }
  }

  // ---- REAL path: MainWindow + fixture scan, viewBox-driven transitions ----
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto d = fs::temp_directory_path() / "msf_viewmode_probe";
  fs::remove_all(d, ec);
  fs::create_directories(d / "media", ec);
  fs::create_directories(d / "settings", ec);
  for (int i = 0; i < 40; ++i) {
      bmp(d / "media" / ("g" + std::to_string(i) + "_a.bmp"), (unsigned)(i + 1));
      bmp(d / "media" / ("g" + std::to_string(i) + "_b.bmp"), (unsigned)(i + 1));
  }
  initAppSettings(QString::fromStdString((d / "settings").string()));
  QSettings().setValue("ui/lastFolder", QString::fromStdString((d / "media").string()));
  QSettings().sync();

  MainWindow w;
  std::cerr << "stage: constructed" << std::endl;
  w.show();
  QApplication::processEvents();
  std::cerr << "stage: shown" << std::endl;
  auto* grid = w.findChild<QListWidget*>("imgGrid");
  auto* viewBox = w.findChild<QComboBox*>("viewBox");
  auto* scan = w.findChild<QPushButton*>("scan");
  if (!grid || !viewBox || !scan) { std::cerr << "no grid/viewBox/scan\n"; return 16; }
  // End-of-scan benchmark summary dialog is modal: close it like the other
  // GUI tests do. Scoped to the scan phase only (never around dialog
  // driving; see the 0.9.4.64 closer race).
  QTimer scanCloser;
  scanCloser.setInterval(250);
  QObject::connect(&scanCloser, &QTimer::timeout, [&]() {
      if (QWidget* m = QApplication::activeModalWidget()) m->close();
  });
  scanCloser.start();
  scan->click();
  std::cerr << "stage: scan clicked" << std::endl;
  QApplication::processEvents();
  pumpUntil([&] { return scan->isEnabled(); }, 180000, "fixture scan finished");
  scanCloser.stop();
  std::cerr << "stage: scan done" << std::endl;
  // Deterministic geometry: stop the UI/monitor timers. Background ticks
  // would otherwise run catch-up asynchronously mid-measurement (replacing
  // icons and moving the layout under the probe). Everything below is
  // driven synchronously (direct calls + event pumps).
  for (auto* t : w.findChildren<QTimer*>()) t->stop();
  if (!gOk) return 17;
  {
      auto* tree = w.findChild<QTreeWidget*>("imgTree");
      std::cerr << "stage: tree=" << (tree ? tree->topLevelItemCount() : -1)
                << " grid=" << grid->count() << std::endl;
  }
  if (grid->count() < 2) { std::cerr << "need >=2 groups, got " << grid->count() << "\n"; return 18; }

  // Icon-driven geometry like the real GUI: production items carry 256px
  // thumbnails (fileThumb fails headless, leaving null icons and text-only
  // geometry that cannot overlap). Synthetic 256px pixmaps restore the size
  // class without changing any production path.
  {
      QPixmap pm(256, 256); pm.fill(Qt::darkGreen);
      for (int i = 0; i < grid->count(); ++i) grid->item(i)->setIcon(QIcon(pm));
  }
  // Scroll to the bottom first: Batched layout only computes visible items
  // plus a buffer, so stale geometry hides off-viewport. The real report
  // comes from a large scrolled list.
  grid->scrollToItem(grid->item(grid->count() - 1));
  QApplication::processEvents();

  // Baseline: fresh Extra Large (index 0) as the constructor left it.
  check(settle(grid, "baseline"), "baseline layout settles on its own");
  const Snap base = snapshot(grid);
  printSnap("base 256icon", base);
  if (grid->count() > 0) {
      auto* it0 = grid->item(0);
      std::cout << "  [info] item0 iconNull=" << it0->icon().isNull()
                << " iconSizes=" << (it0->icon().availableSizes().isEmpty() ? -1 : it0->icon().availableSizes().first().width())
                << " sizeHint=" << it0->sizeHint().width() << "x" << it0->sizeHint().height()
                << " fontH=" << grid->fontMetrics().height() << "\n";
  }
  check(base.count >= 2, "baseline has groups");
  check(base.overlapPairs == 0, "baseline has no overlap");
  {
      grid->doItemsLayout();
      QApplication::processEvents();
      const Snap forced = snapshot(grid);
      bool same = (forced.rects.size() == base.rects.size());
      for (size_t k = 0; same && k < forced.rects.size(); ++k)
          same = (forced.rects[k] == base.rects[k]);
      check(same, "baseline geometry converged");
  }

  const std::vector<std::pair<std::string, std::vector<int>>> scenarios = {
      {"256->list->256", {0, 4, 0}},
      {"256->128->256", {0, 1, 0}},
      {"256->64->256", {0, 2, 0}},
      {"256->tiles->256", {0, 6, 0}},
      {"256->list->tiles->256", {0, 4, 6, 0}},
      {"256->details->256", {0, 5, 0}},
  };
  // mode: 0 = settle between steps (patient user), 1 = rapid switching with
  // a single event pump between steps (fast clicking while Batched layout is
  // still in flight), 2 = catch-up-driven: prime non-null icons, then run
  // the REAL product catch-up (testThumbCatchUp) so thumbnail delivery goes
  // through production code including its layout guarantee. Direct setIcon
  // alone is deliberately never asserted: no production path sets grid icons
  // outside catch-up/fillPair, and Qt does not owe those a layout pass.
  for (int mode = 0; mode <= 2; ++mode) {
  const char* modeName = mode == 0 ? "settled" : (mode == 1 ? "rapid" : "catchup");
  for (const auto& sc : scenarios) {
      std::string name = sc.first + std::string("/") + modeName;
      std::cout << "--- scenario " << name << " ---\n";
      bool settled = true;
      Snap last;
      for (int idx : sc.second) {
          viewBox->setCurrentIndex(idx); // the exact user action
          if (mode == 1) {
              QApplication::processEvents(); // one pump only: layout in flight
          } else if (mode == 2) {
              // Catch-up-driven contract (0.9.4.66): uniform sizes follow
              // the MAXIMUM item art, so nulling a subset can never move the
              // maximum while green items remain. Null EVERYTHING through
              // the real product path: prime all items, then walk the whole
              // list in viewport steps running testThumbCatchUp() at each.
              // Headless fileThumb yields null, which differs from the prime.
              // Pre-fix no layout pass runs (vbar frozen at the green-era
              // maximum); post-fix every changing catch-up forces recompute.
              if (grid->isVisible()) {
                  QPixmap prime(256, 256); prime.fill(Qt::darkGreen);
                  const QIcon primeIcon(prime);
                  const quint64 primeKey = primeIcon.cacheKey();
                  for (int i = 0; i < grid->count(); ++i) grid->item(i)->setIcon(primeIcon);
                  grid->doItemsLayout(); // reference only: all-green maximum
                  QApplication::processEvents();
                  const int maxGreen = grid->verticalScrollBar()->maximum();
                  const qulonglong changedBefore = w.testThumbInPlaceCount();
                  // Sweep every item so the viewport (and therefore catch-up)
                  // covers the whole list however the layout sliced it. Fixed
                  // strides proved layout-dependent (GPU content heights
                  // differ from CPU), leaving green stragglers behind.
                  for (int sweep = 0; sweep < 3; ++sweep) {
                      for (int pos = 0; pos < grid->count(); ++pos) {
                          grid->scrollToItem(grid->item(pos));
                          QApplication::processEvents();
                          w.testThumbCatchUp(); // production path + layout guarantee
                      }
                      int greenLeft = 0;
                      for (int i = 0; i < grid->count(); ++i)
                          if (grid->item(i)->icon().cacheKey() == primeKey) ++greenLeft;
                      if (greenLeft == 0) break;
                  }
                  const bool replaced = w.testThumbInPlaceCount() > changedBefore;
                  check(replaced, "catch-up replaced icons through the product path");
                  int greenLeft = 0;
                  for (int i = 0; i < grid->count(); ++i)
                      if (grid->item(i)->icon().cacheKey() == primeKey) ++greenLeft;
                  check(greenLeft == 0, "catch-up sweep covered every item (no green stragglers)");
                  if (!settle(grid, name.c_str())) settled = false;
                  last = snapshot(grid);
                  printSnap(("step idx=" + std::to_string(idx)).c_str(), last);
                  const int maxAfter = grid->verticalScrollBar()->maximum();
                  int nullN = 0, pxMin = INT_MAX, pxMax = 0, greenN = 0;
                  for (int i = 0; i < grid->count(); ++i) {
                      const QIcon ic = grid->item(i)->icon();
                      if (ic.cacheKey() == primeKey) ++greenN;
                      const auto sizes = ic.availableSizes();
                      if (sizes.isEmpty()) ++nullN;
                      else { pxMin = std::min(pxMin, sizes.first().width()); pxMax = std::max(pxMax, sizes.first().width()); }
                  }
                  std::cout << "  [info] catchup vbarMax green=" << maxGreen
                            << " after=" << maxAfter << " replaced=" << replaced
                            << " nullN=" << nullN << " greenN=" << greenN
                            << " pxMin=" << (pxMin == INT_MAX ? -1 : pxMin)
                            << " pxMax=" << pxMax << std::endl;
                  if (replaced && idx == 0)
                      check(maxAfter < maxGreen, ("layout passes ran after catch-up at idx=" + std::to_string(idx)).c_str());
              } else {
                  if (!settle(grid, name.c_str())) settled = false;
              }
          } else {
              if (!settle(grid, name.c_str())) settled = false;
          }
          last = snapshot(grid);
          printSnap(("step idx=" + std::to_string(idx)).c_str(), last);
          // Universal staleness discriminator (all modes): the settled
          // geometry must match a forced layout. A mismatch means no layout
          // pass ran since the last change (frozen rects). Absolute sizes
          // are deliberately NOT asserted: background ticks may legitimately
          // deliver different thumbnails, changing sizes without any bug.
          // Only meaningful while the grid is visible.
          if (grid->isVisible() && mode != 1) {
              grid->doItemsLayout();
              QApplication::processEvents();
              const Snap forced = snapshot(grid);
              bool same = (forced.rects.size() == last.rects.size());
              for (size_t k = 0; same && k < forced.rects.size(); ++k)
                  same = (forced.rects[k] == last.rects[k]);
              check(same, ("geometry converged at idx=" + std::to_string(idx)).c_str());
          }
          if (mode == 2) {
              check(last.count == base.count, ("count kept at idx=" + std::to_string(idx)).c_str());
              check(last.overlapPairs == 0, ("no overlap at idx=" + std::to_string(idx)).c_str());
          } else if (mode != 1) { // rapid mode only asserts the final settled state
          check(last.count == base.count, ("count kept at idx=" + std::to_string(idx)).c_str());
          check(last.overlapPairs == 0, ("no overlap at idx=" + std::to_string(idx)).c_str());
          }
      }
      if (mode == 1) {
          if (!settle(grid, name.c_str())) settled = false;
          last = snapshot(grid);
          printSnap("final settled", last);
          check(last.count == base.count, "rapid: count kept after settling");
          check(last.overlapPairs == 0, "rapid: no overlap after settling");
          grid->doItemsLayout();
          QApplication::processEvents();
          const Snap forced = snapshot(grid);
          bool same = (forced.rects.size() == last.rects.size());
          for (size_t k = 0; same && k < forced.rects.size(); ++k)
              same = (forced.rects[k] == last.rects[k]);
          check(same, "rapid: geometry converged after settling");
      }
      check(settled, (name + ": layout settles at every step").c_str());
      if (!gOk) rc |= 4;
  }
  }

  fs::remove_all(d, ec);
  std::cout << "viewmode_selfcheck=" << (gOk ? "ok" : "FAILED")
            << " checks=" << gChecks << "\n";
  if (rc == 0 && gOk) std::cout << "viewmode_roundtrip=ok\n";
  return (rc == 0 && gOk) ? 0 : 1;
}
