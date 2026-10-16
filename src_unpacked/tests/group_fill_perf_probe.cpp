// 0.9.4.117: large result-set application must not freeze the GUI.
//
// Field symptom (0.9.4.115 session): applying the user's loaded index
// (9,055 groups / ~15k pairs / 177k files) stalled the GUI event loop for
// >10 s (observed as a single supervisor "health quiet 1/3", during which
// the window was unresponsive). This probe feeds the REAL onResults slot a
// synthetic 9,055-group / 18,110-file result and measures:
//   - the synchronous time of the onResults call itself,
//   - event-loop responsiveness while the fill proceeds (timer ticks),
//   - the total fill wall time and the final row counts of both panes.
#include "mainwindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QListWidget>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <filesystem>
#include <iostream>

namespace {
int gChecks = 0;
bool gOk = true;
void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << "\n"; }
    else       { std::cout << "  [ok] " << what << "\n"; }
}
} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto d = fs::temp_directory_path() / "msf_group_fill_probe";
    fs::remove_all(d, ec);
    fs::create_directories(d / "settings", ec);
    initAppSettings(QString::fromStdString((d / "settings").string()));
    qRegisterMetaType<QVector<BackendFile>>();
    MainWindow w;
    w.show();
    QApplication::processEvents();

    // 9,055 groups x 2 files - the exact shape of the field stall.
    // An optional argv[1] shrinks the count for stage-scaling diagnostics.
    const int kGroups = argc > 1 ? std::atoi(argv[1]) : 9055;
    QVector<BackendFile> files;
    QStringList rows;
    files.reserve(kGroups * 2);
    rows.reserve(kGroups);
    for (int i = 0; i < kGroups; ++i) {
        const QString a = QString("C:/probe/g%1_a.jpg").arg(i);
        const QString b = QString("C:/probe/g%1_b.jpg").arg(i);
        files.push_back({a, 1000, QStringLiteral("0000000000000000"), 0.0});
        files.push_back({b, 1000, QStringLiteral("0000000000000000"), 0.0});
        rows << (a + "\t" + b + "\t100.0\t1");
    }
    int ticks = 0;
    QTimer tick;
    tick.setInterval(50);
    QObject::connect(&tick, &QTimer::timeout, [&] { ++ticks; });
    tick.start();
    QElapsedTimer et;
    et.start();
    QMetaObject::invokeMethod(&w, "onResults", Qt::DirectConnection,
                              Q_ARG(QVector<BackendFile>, files), Q_ARG(QStringList, rows));
    const qint64 applyMs = et.elapsed();
    auto* grid = w.findChild<QListWidget*>("imgGrid");
    auto* tree = w.findChild<QTreeWidget*>("imgTree");
    // Pump until the fill completes (sync today, deferred after the fix).
    QElapsedTimer tot;
    tot.start();
    while (tot.elapsed() < 60000) {
        QApplication::processEvents();
        if (grid && grid->count() == kGroups) break;
        QThread::msleep(5);
    }
    const qint64 fillMs = tot.elapsed();
    const int gridCount = grid ? grid->count() : -1;
    const int treeCount = tree ? tree->topLevelItemCount() : -1;
    std::cout << "  [info] applyMs=" << applyMs << " fillMs=" << fillMs
              << " ticks=" << ticks << " grid=" << gridCount << " tree=" << treeCount << "\n";
    check(applyMs < 1500, "onResults returns promptly (no synchronous mega-fill)");
    check(ticks >= 3, "the event loop processed timers during the fill");
    check(gridCount == kGroups, "grid holds every group after the fill");
    check(treeCount == kGroups, "tree holds every group after the fill");

    // Scenario A (0.9.4.118): a second identical apply must not churn rows.
    // The pre-fix full rebuild cleared the pane synchronously (item(0) became
    // null) and re-created every row; the append-first path leaves it intact.
    {
        auto* first0 = grid ? grid->item(0) : nullptr;
        QElapsedTimer et2;
        et2.start();
        QMetaObject::invokeMethod(&w, "onResults", Qt::DirectConnection,
                                  Q_ARG(QVector<BackendFile>, files), Q_ARG(QStringList, rows));
        const qint64 applyMs2 = et2.elapsed();
        const bool sameRows = grid && first0 && grid->item(0) == first0;
        std::cout << "  [info] second applyMs=" << applyMs2 << " rowsKept=" << sameRows << "\n";
        check(applyMs2 < 1500, "repeat apply returns promptly");
        check(sameRows, "repeat apply keeps the existing rows (no full rebuild)");
    }
    // Scenario B (0.9.4.118): the first apply on a FRESH window (an empty
    // pane routes through the append path) must stay responsive - a large
    // append must defer exactly like the full-rebuild path.
    {
        MainWindow w2;
        w2.show();
        QApplication::processEvents();
        QElapsedTimer et3;
        et3.start();
        QMetaObject::invokeMethod(&w2, "onResults", Qt::DirectConnection,
                                  Q_ARG(QVector<BackendFile>, files), Q_ARG(QStringList, rows));
        const qint64 applyMs3 = et3.elapsed();
        auto* g2 = w2.findChild<QListWidget*>("imgGrid");
        QElapsedTimer wt;
        wt.start();
        while (wt.elapsed() < 60000) {
            QApplication::processEvents();
            if (g2 && g2->count() == kGroups) break;
            QThread::msleep(5);
        }
        const int g2count = g2 ? g2->count() : -1;
        std::cout << "  [info] fresh-window applyMs=" << applyMs3 << " rows=" << g2count << "\n";
        check(applyMs3 < 1500, "fresh-window first apply returns promptly");
        check(g2count == kGroups, "fresh-window list completes");
    }

    std::cout << "group_fill_probe=" << (gOk ? "ok" : "FAILED") << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
