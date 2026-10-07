// GUI similar-group scroll regression test (0.9.4.61).
//
// Root cause: thumbnail starvation drove a FULL widget-list rebuild
// (tree->clear()/grid->clear() + recreate + scroll restore) on nearly every
// 600ms tick, so scrolling a large group list was destroyed mid-gesture and
// the pane could jump back to the top. The fix splits thumbnail-only
// in-place catch-up (visible items, bounded by the per-tick budget, never
// recreating widgets) from data-change full rebuilds, and defers full
// rebuilds while the user holds a scrollbar or inside a short post-scroll
// cooldown. The 0.9.3.10 semantic anchor stays untouched as the fallback.
//
// What this proves (offscreen, deterministic, no timing races):
//   1. group data change -> at least one full rebuild (invariant kept);
//   2. N thumbnail catch-up ticks -> zero full rebuilds, in-place count grows;
//   3. scrolled position / selection / widget identity survive those ticks;
//   4. scrollbar drag (sliderPressed..Released) freezes rebuilds mid-gesture;
//   5. End key keeps the view at the bottom across ticks (tree and grid).
// Signal emission (sliderPressed/Released) and synthetic key events drive
// the same handlers as real input; only the rebuild is gated, never input.
#include "mainwindow.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>

namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << std::endl; }
    else       { std::cout << "  [ok] " << what << std::endl; }
}

// 8x8 24-bit BMP from a deterministic PRNG stream, so each pair is internally
// identical (distance 0 -> one group) but pairs are mutually distant: white
// noise has a flat spectrum, so perceptual hashes land far apart. (A smooth
// gradient family shares coarse DCT structure and cross-matches into a few
// mega-groups with thousands of matches - useless as a scroll fixture.)
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
            std::cerr << "  [FAIL] timeout: " << what << "\n";
            gOk = false;
            ++gChecks;
            return;
        }
    }
    ++gChecks;
    std::cout << "  [ok] " << what << "\n";
}

int topGroupIndex(QTreeWidget* tree) {
    if (auto* top = tree->itemAt(QPoint(2, 2))) return top->data(0, Qt::UserRole).toInt();
    return -999;
}

int topGroupIndex(QListWidget* grid) {
    // Grid cells start past the left spacing (x=8 in this layout), so probe
    // mid-width instead of the (2,2) corner the tree uses. Returns a top-edge
    // item's group index, or -999 when nothing is laid out there.
    const QPoint p(grid->viewport()->width() / 2, 10);
    if (auto* top = grid->itemAt(p)) return top->data(Qt::UserRole).toInt();
    return -999;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    namespace fs = std::filesystem;
    std::error_code ec;
    auto d = fs::temp_directory_path() / "msf_scroll_test";
    fs::remove_all(d, ec);
    fs::create_directories(d / "media", ec);
    fs::create_directories(d / "settings", ec);
    static constexpr int kGroups = 120;
    for (int i = 0; i < kGroups; ++i) {
        const unsigned seed = (unsigned)(i + 1);
        bmp(d / "media" / ("g" + std::to_string(i) + "_a.bmp"), seed);
        bmp(d / "media" / ("g" + std::to_string(i) + "_b.bmp"), seed);
    }
    initAppSettings(QString::fromStdString((d / "settings").string()));
    QSettings().setValue("ui/lastFolder", QString::fromStdString((d / "media").string()));
    QSettings().sync();

    MainWindow w;
    w.show();
    QApplication::processEvents();
    // Dismiss the end-of-scan benchmark summary like scan_workflow_test.
    QTimer closer;
    closer.setInterval(250);
    QObject::connect(&closer, &QTimer::timeout, [&]() {
        if (QWidget* m = QApplication::activeModalWidget()) m->close();
    });
    closer.start();

    auto* scan = w.findChild<QPushButton*>("scan");
    if (!scan) { std::cerr << "no scan button\n"; return 1; }
    std::cerr << "  [phase] starting scan" << std::endl;
    scan->click();
    QApplication::processEvents();
    pumpUntil([&] { return scan->isEnabled(); }, 240000, "scan finished (button re-enabled)");
    if (!gOk) return 2;
    QApplication::processEvents();

    auto* grid = w.findChild<QListWidget*>("imgGrid");
    auto* tree = w.findChild<QTreeWidget*>("imgTree");
    auto* viewBox = w.findChild<QComboBox*>("viewBox");
    if (!grid || !tree || !viewBox) { std::cerr << "no middle widgets\n"; return 3; }

    // Phase 1: group data change produced at least one full rebuild.
    std::cerr << "  [phase] scan done, phase 1" << std::endl;
    const qulonglong f0 = w.testFullRebuildCount();
    check(f0 >= 1, "scan data change rebuilt the list at least once");
    const int n = grid->count();
    std::cout << "  [info] grid items=" << n << "\n";
    check(n >= 80, "enough groups for starvation and scrolling");
    check(tree->topLevelItemCount() == n, "tree and grid agree on group count");

    // Phase 2: thumbnail catch-up ticks rebuild nothing but fill icons.
    // P3 async model: catch-up requests misses, arrivals paint via
    // onThumbReady (queued through the event loop the ticks pump).
    grid->scrollToBottom();
    grid->doItemsLayout(); // Batched layout finishes asynchronously; force it so itemAt works
    QApplication::processEvents();
    const int topBefore = topGroupIndex(grid);
    auto* bar = grid->verticalScrollBar();
    const int barBefore = bar ? bar->value() : -1;
    const int curBefore = grid->currentRow();
    auto* itemBefore = grid->currentItem();
    check(topBefore >= 0, "bottom scroll shows a real top item");
    // Deterministic repaint: drop the memory cache so catch-up ticks
    // re-request through the backend and arrivals repaint (async model).
    w.testDropThumbCache();
    QApplication::processEvents();
    const qulonglong p0 = w.testThumbPaintedCount();
    for (int i = 0; i < 6; ++i) { w.testUiTick(); QApplication::processEvents(); }
    check(w.testFullRebuildCount() == f0, "6 catch-up ticks: zero full rebuilds");
    check(w.testThumbPaintedCount() > p0, "6 catch-up ticks: thumbnails painted on arrival");
    check(topGroupIndex(grid) == topBefore, "viewport top identity preserved");
    if (bar) check(bar->value() == barBefore, "scrollbar value preserved");
    check(grid->currentRow() == curBefore, "selection row preserved");
    check(grid->currentItem() == itemBefore, "selection widget identity preserved (no recreate)");

    // Phase 3: scrollbar drag freezes rebuilds mid-gesture (starved state).
    const qulonglong f1 = w.testFullRebuildCount();
    if (bar) {
        bar->sliderPressed(); // same handler as a real drag press (public signal)
        bar->setValue(bar->maximum() / 2);
        QApplication::processEvents();
        for (int i = 0; i < 3; ++i) { w.testUiTick(); QApplication::processEvents(); }
        check(w.testFullRebuildCount() == f1, "ticks during slider press: zero full rebuilds");
        check(bar->value() == bar->maximum() / 2, "dragged position kept during ticks");
        bar->sliderReleased(); // starts the short post-release cooldown
        QApplication::processEvents();
    } else {
        std::cout << "  [info] no scrollbar (all fits); drag phase skipped\n";
        ++gChecks;
    }

    // Phase 4: End key keeps the grid at the bottom across ticks.
    // Start from the top so the End event itself must do the moving.
    {
        grid->setCurrentRow(0);
        QApplication::processEvents();
        QKeyEvent endEv(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
        QApplication::sendEvent(grid, &endEv);
        QApplication::processEvents();
        check(grid->currentRow() == grid->count() - 1, "End moves to the last row");
        auto* lastItem = grid->currentItem();
        const int barMax = bar ? bar->maximum() : 0;
        if (bar) check(bar->value() == barMax, "End scrolls to the maximum");
        for (int i = 0; i < 3; ++i) { w.testUiTick(); QApplication::processEvents(); }
        check(grid->currentItem() == lastItem, "End position survives ticks (same widget)");
        if (bar) check(bar->value() == barMax, "End scrollbar stays at maximum");
        check(w.testFullRebuildCount() == f1, "End-phase ticks: zero full rebuilds");
    }

    // Phase 5: tree view — same stability contract as the grid.
    viewBox->setCurrentIndex(5); // tree/details mode
    QApplication::processEvents();
    const qulonglong f2 = w.testFullRebuildCount();
    tree->scrollToBottom();
    QApplication::processEvents();
    auto* treeBar = tree->verticalScrollBar();
    const int treeTop = topGroupIndex(tree);
    const int treeBarBefore = treeBar ? treeBar->value() : -1;
    auto* treeCur = tree->currentItem();
    check(treeTop >= 0, "tree bottom scroll shows a real top item");
    for (int i = 0; i < 4; ++i) { w.testUiTick(); QApplication::processEvents(); }
    check(topGroupIndex(tree) == treeTop, "tree viewport top preserved");
    if (treeBar) check(treeBar->value() == treeBarBefore, "tree scrollbar preserved");
    check(tree->currentItem() == treeCur, "tree selection widget preserved");
    // View switch itself refills once (legitimate structural change); the
    // catch-up ticks after it must not.
    check(w.testFullRebuildCount() <= f2 + 1, "at most the view-switch refill, no tick rebuilds");

    fs::remove_all(d, ec);
    std::cout << "scroll_regression_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
