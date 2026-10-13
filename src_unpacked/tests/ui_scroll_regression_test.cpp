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
#include <QMetaObject>
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
    // Phase 0b: while the scan streams, once the group grid has grown, scroll it
    // and click a visible row; the streamed refresh must not jump the list to the
    // top. This exercises the append-only fill path that post-scan ticks never do.
    {
        auto* liveGrid = w.findChild<QListWidget*>("imgGrid");
        QElapsedTimer t; t.start();
        while (liveGrid && liveGrid->count() < 60 && t.elapsed() < 60000) {
            QApplication::processEvents();
            QThread::msleep(20);
        }
        if (liveGrid && liveGrid->count() >= 60) {
            liveGrid->scrollToBottom();
            liveGrid->doItemsLayout();
            QApplication::processEvents();
            auto* bar = liveGrid->verticalScrollBar();
            const int before = bar ? bar->value() : -1;
            const QPoint mid(liveGrid->viewport()->width() / 2, liveGrid->viewport()->height() / 2);
            if (auto* it = liveGrid->itemAt(mid)) liveGrid->setCurrentItem(it);
            for (int i = 0; i < 3; ++i) { w.testUiTick(); QApplication::processEvents(); }
            std::cout << "  [info] during-scan select-scroll before=" << before
                      << " after=" << (bar ? bar->value() : -1) << "\n";
            check(bar && bar->value() == before, "during-scan click keeps the scroll position");
        } else {
            std::cout << "  [info] during-scan grid never reached 60 items; phase skipped\n";
            ++gChecks;
        }
    }
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

    // Phase 1b: selecting a visible group after scrolling must not move the
    // list. User-reported defect: scroll down, click a row, the list jumps to
    // the top. This pins that contract for the grid.
    {
        grid->scrollToBottom();
        grid->doItemsLayout();
        QApplication::processEvents();
        auto* bar = grid->verticalScrollBar();
        const int before = bar ? bar->value() : -1;
        const QPoint mid(grid->viewport()->width() / 2, grid->viewport()->height() / 2);
        if (auto* it = grid->itemAt(mid)) grid->setCurrentItem(it);
        QApplication::processEvents();
        std::cout << "  [info] select-scroll before=" << before
                  << " after=" << (bar ? bar->value() : -1) << "\n";
        check(bar && bar->value() == before, "clicking a visible group keeps the scroll position");
    }

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

    // Phase 2b: repeated catch-up before backend arrivals must not duplicate
    // thumbnail work. The second tick revisits the same visible misses while
    // the first tick's requests are still in flight.
    w.testDropThumbCache();
    w.testUiTick();
    const qulonglong pending1 = w.testThumbPendingCount();
    w.testUiTick();
    check(w.testThumbPendingCount() == pending1, "repeat catch-up tick: no duplicate thumbnail requests");
    QApplication::processEvents();

    // Phase 3: scrollbar drag freezes rebuilds mid-gesture (starved state).
    const qulonglong f1 = w.testFullRebuildCount();
    if (bar) {
        // Settle to the held position before arming the synthetic press: a
        // pending coalesced layout may still adjust geometry once, but after
        // the press every tick must leave the dragged position alone.
        bar->setValue(bar->maximum() / 2);
        QApplication::processEvents();
        const int heldValue = bar->value();
        bar->sliderPressed(); // same handler as a real drag press (public signal)
        QApplication::processEvents();
        for (int i = 0; i < 3; ++i) { w.testUiTick(); QApplication::processEvents(); }
        check(w.testFullRebuildCount() == f1, "ticks during slider press: zero full rebuilds");
        check(bar->value() == heldValue, "dragged position kept during ticks");
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

    // Phase 6 (0.9.4.94): a deep full rebuild keeps the scroll position.
    // Batched grids used to restore against a still-small maximum, clamping a
    // deep offset toward the top ("past a depth, jumps to the top"). The
    // rebuild now lays out synchronously before restoring.
    {
        // Settle to a converged bottom first: the Batched maximum can still
        // grow right after scrollToBottom, leaving the value behind. Repeat
        // until the value matches the maximum.
        auto* gbar = grid->verticalScrollBar();
        for (int i = 0; i < 30 && gbar && gbar->value() != gbar->maximum(); ++i) {
            grid->scrollToBottom();
            grid->doItemsLayout();
            QApplication::processEvents();
        }
        const int deepMax = gbar ? gbar->maximum() : 0;
        const int deepVal = gbar ? gbar->value() : -1;
        std::cout << "  [info] deep-rebuild max=" << deepMax << " value=" << deepVal << "\n";
        check(gbar && deepMax > 0 && deepVal == deepMax, "deep scroll settled at a nonzero maximum");
        const qulonglong f6 = w.testFullRebuildCount();
        QMetaObject::invokeMethod(&w, "groupSearchChanged", Q_ARG(QString, QString()));
        // Let any post-rebuild batching settle WITHOUT touching the scrollbar:
        // layout growth must never move the restored value by itself.
        for (int i = 0; i < 10; ++i) QApplication::processEvents();
        check(w.testFullRebuildCount() == f6 + 1, "filter refresh ran one full rebuild");
        const int maxAfter = gbar ? gbar->maximum() : -1;
        const int valAfter = gbar ? gbar->value() : -1;
        std::cout << "  [info] after rebuild max=" << maxAfter << " value=" << valAfter << "\n";
        check(gbar && maxAfter > 0 && valAfter == maxAfter, "deep full rebuild stays at the bottom");
    }

    // Phase 7 (0.9.4.94): same-group right-pane refills keep the file scroll.
    // refreshFileViews() recreated every row with no restore, so any refill
    // (finish, streaming, zoom) threw the file list back to the top.
    {
        auto* fileGrid = w.findChild<QListWidget*>("fileGrid");
        check(fileGrid != nullptr, "right file grid found");
        if (fileGrid) {
            QMetaObject::invokeMethod(&w, "setViewMode", Q_ARG(int, 0)); // right grid mode
            if (auto* first = grid->item(0)) grid->setCurrentItem(first);
            QApplication::processEvents();
            std::cout << "  [info] fileGrid items=" << fileGrid->count() << "\n";
            check(fileGrid->count() == 2, "selected group shows its two files");
            w.resize(300, 160);
            QApplication::processEvents();
            QMetaObject::invokeMethod(&w, "zoomChanged", Q_ARG(int, 256));
            QApplication::processEvents();
            auto* fbar = fileGrid->verticalScrollBar();
            const int fmax = fbar ? fbar->maximum() : 0;
            std::cout << "  [info] fileGrid max=" << fmax << "\n";
            check(fbar && fmax > 0, "zoomed file grid overflows (scrollable)");
            fileGrid->scrollToBottom();
            fileGrid->doItemsLayout();
            QApplication::processEvents();
            const int fbefore = fbar ? fbar->value() : -1;
            check(fbefore == fmax, "file grid at bottom before refill");
            QMetaObject::invokeMethod(&w, "zoomChanged", Q_ARG(int, 256));
            QApplication::processEvents();
            const int fafter = fbar ? fbar->value() : -1;
            std::cout << "  [info] fileGrid before=" << fbefore << " after=" << fafter << "\n";
            check(fafter == fmax, "same-group refill keeps the file scroll");
        }
    }

    // Phase 8 (0.9.4.94): scanFinished skips the duplicate finish rebuild.
    // onResults already rebuilt everything for the same scan; repeating it in
    // scanFinished doubled the worst-case "writing report" stall with no new
    // data. Cancelled scans never pass through onResults and still rebuild.
    {
        const qulonglong f8 = w.testFullRebuildCount();
        QVector<BackendFile> noFiles;
        QStringList noRows;
        QMetaObject::invokeMethod(&w, "onResults",
            Q_ARG(QVector<BackendFile>, noFiles), Q_ARG(QStringList, noRows));
        QApplication::processEvents();
        check(w.testFullRebuildCount() == f8 + 1, "synthetic onResults rebuilt once");
        QMetaObject::invokeMethod(&w, "scanFinished",
            Q_ARG(QString, QStringLiteral("Scan complete: 0 files, 0 analyzed, 0 candidates, 0 groups|0|0|0|0|0")));
        QApplication::processEvents();
        check(w.testFullRebuildCount() == f8 + 1, "completed scanFinished adds no second rebuild");
        QMetaObject::invokeMethod(&w, "scanFinished",
            Q_ARG(QString, QStringLiteral("CANCELLED|10|5")));
        QApplication::processEvents();
        check(w.testFullRebuildCount() == f8 + 2, "cancelled scanFinished still rebuilds");
    }

    // Phase 9 (0.9.4.94): a damaged detailed-log record reports instead of
    // vanishing. The parse failure used to return silently (no dialog, no
    // message), which reads as "log generation never completes".
    {
        auto* status = w.findChild<QLabel*>("statusMsg");
        check(status != nullptr, "status message label found");
        QMetaObject::invokeMethod(&w, "showDetailedLogDialog",
            Q_ARG(QString, QStringLiteral("{damaged record")));
        QApplication::processEvents();
        const QString shown = status ? status->text() : QString();
        const bool msgOk = shown == trStr(UiLang::En, "detailLogParseFail") ||
                           shown == trStr(UiLang::Ko, "detailLogParseFail");
        std::cout << "  [info] parse-fail status=" << shown.toStdString() << "\n";
        check(msgOk, "damaged record shows a failure message, no silent return");
        check(QApplication::activeModalWidget() == nullptr, "no dialog opened for a damaged record");
    }

    fs::remove_all(d, ec);
    std::cout << "scroll_regression_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
