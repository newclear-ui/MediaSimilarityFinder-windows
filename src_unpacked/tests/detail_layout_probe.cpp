// 0.9.4.121: right-pane width discipline + file-thumb retry parity probe.
//
// Field report (group 280): selecting a file widened the detail pane even
// though no text was long enough to overflow, the splitter handle could not
// shrink the right pane back, the right side encroached the middle group
// list (which by agreement stays ~3 images wide unless the user widens it),
// and after a scan start only the representative thumbnail survived in the
// file list. This probe drives the REAL selection/detail paths offscreen and
// reports (then asserts) the width chain and the thumbnail re-ask parity:
//
//   A. simLabel_ (similarity tab, full best-match path) must wrap inside the
//      allotted width instead of forcing the tab widget (hence the right
//      pane, hence the locked splitter) wider.
//   B. The hidden file-list tree must not inflate the right pane minimum via
//      its content-sized columns.
//   C. After a dropped answer (cache + pending cleared, as at scan start),
//      one catch-up tick must re-ask the file grid's rows and the preview -
//      the same retry discipline the middle grid already has.
#include "mainwindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
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
void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
}
void reportWidget(const char* name, QWidget* w) {
    if (!w) { std::cout << "  [info] " << name << "=null\n"; return; }
    std::cout << "  [info] " << name << " w=" << w->width()
              << " minW=" << w->minimumWidth()
              << " minHintW=" << w->minimumSizeHint().width()
              << " visible=" << w->isVisibleTo(w->window()) << "\n";
}
} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto d = fs::temp_directory_path() / "msf_detail_layout_probe";
    fs::remove_all(d, ec);
    fs::create_directories(d / "settings", ec);
    initAppSettings(QString::fromStdString((d / "settings").string()));
    qRegisterMetaType<QVector<BackendFile>>();
    MainWindow w;
    w.show();
    QApplication::processEvents();

    // 3 groups x 2 files. Group 1 carries a ~180-char path to stress every
    // unwrapped label and content-sized column in the right pane.
    const QString longDir = QString("C:/probe/") + QString(150, QChar('x'));
    struct Row { QString a, b; };
    const Row rowsDef[3] = {
        {QStringLiteral("C:/probe/g0_a.jpg"), QStringLiteral("C:/probe/g0_b.jpg")},
        {longDir + QStringLiteral("/g1_a.jpg"), longDir + QStringLiteral("/g1_b.jpg")},
        {QStringLiteral("C:/probe/g2_a.jpg"), QStringLiteral("C:/probe/g2_b.jpg")},
    };
    QVector<BackendFile> files;
    QStringList rows;
    for (const auto& r : rowsDef) {
        files.push_back({r.a, 1000, QStringLiteral("0000000000000000"), 0.0});
        files.push_back({r.b, 1000, QStringLiteral("0000000000000000"), 0.0});
        rows << (r.a + "\t" + r.b + "\t100.0\t1");
    }
    QMetaObject::invokeMethod(&w, "onResults", Qt::DirectConnection,
                              Q_ARG(QVector<BackendFile>, files), Q_ARG(QStringList, rows));
    auto* imgGrid = w.findChild<QListWidget*>("imgGrid");
    auto* fileGrid = w.findChild<QListWidget*>("fileGrid");
    auto* fileTree = w.findChild<QTreeWidget*>("fileList");
    QElapsedTimer tot;
    tot.start();
    while (tot.elapsed() < 30000) {
        QApplication::processEvents();
        if (imgGrid && imgGrid->count() == 3) break;
        QThread::msleep(5);
    }

    // Select the long-path group FIRST so the file list populates, then its
    // second file - the reported gesture. Rows are unordered (hash-bucket
    // order), so select by identity: the row whose tooltip (representative
    // path) carries the long-directory marker, never by row number.
    int longRow = -1;
    if (imgGrid) {
        const QString marker(50, QChar('x'));
        for (int r = 0; r < imgGrid->count(); ++r) {
            auto* it = imgGrid->item(r);
            if (it && it->toolTip().contains(marker)) { longRow = r; break; }
        }
        std::cout << "  [info] longPathRow=" << longRow << "\n";
    }
    if (imgGrid && longRow >= 0) imgGrid->setCurrentRow(longRow);
    tot.start();
    while (tot.elapsed() < 10000) {
        QApplication::processEvents();
        if (fileGrid && fileGrid->count() == 2) break;
        QThread::msleep(5);
    }
    if (fileGrid && fileGrid->count() >= 2) fileGrid->setCurrentRow(1);
    pump(400);

    auto* split = w.findChild<QSplitter*>("mainSplit");
    QWidget* rightW = split ? split->widget(2) : nullptr;
    QTabWidget* detailTabs = nullptr;
    if (rightW) {
        const auto tabs = rightW->findChildren<QTabWidget*>();
        if (!tabs.isEmpty()) detailTabs = tabs.first();
    }
    QWidget* viewStack = fileGrid ? fileGrid->parentWidget() : nullptr;

    std::cout << "  [info] winW=" << w.width() << "\n";
    if (split) {
        const auto sz = split->sizes();
        std::cout << "  [info] split sizes=" << sz.value(0) << "/" << sz.value(1)
                  << "/" << sz.value(2) << "\n";
    }
    reportWidget("rightW", rightW);
    reportWidget("viewStack", viewStack);
    if (fileTree) {
        std::cout << "  [info] fileTree col1W=" << fileTree->columnWidth(1)
                  << " headerLen=" << fileTree->header()->length() << "\n";
    }
    reportWidget("detailTabs", detailTabs);
    if (detailTabs) {
        std::cout << "  [info] detailTabs current=" << detailTabs->currentIndex() << "\n";
        for (int i = 0; i < detailTabs->count(); ++i) {
            QWidget* pg = detailTabs->widget(i);
            std::cout << "  [info] tabPage" << i << " minHintW="
                      << pg->minimumSizeHint().width() << "\n";
        }
    }
    // Every label under the right pane: who demands width?
    if (rightW) {
        const auto labs = rightW->findChildren<QLabel*>();
        for (auto* lb : labs) {
            const QString t = lb->text();
            const QString shortT = t.left(60).replace('\n', ' ');
            std::cout << "  [info] label w=" << lb->width()
                      << " minHintW=" << lb->minimumSizeHint().width()
                      << " wrap=" << lb->wordWrap()
                      << " text=\"" << shortT.toStdString() << "\"\n";
        }
    }
    QLabel* simLabel = nullptr;
    if (detailTabs && detailTabs->count() > 2) {
        // Tab order: detail(0), exif(1), similarity(2), hash(3).
        QWidget* tabS = detailTabs->widget(2);
        const auto labs = tabS->findChildren<QLabel*>();
        if (!labs.isEmpty()) simLabel = labs.first();
    }
    if (simLabel) {
        std::cout << "  [info] simLabel wrap=" << simLabel->wordWrap()
                  << " textLen=" << simLabel->text().size()
                  << " minHintW=" << simLabel->minimumSizeHint().width() << "\n";
    }
    check(simLabel && simLabel->wordWrap(),
          "similarity label wraps inside the allotted width");
    // Pre-fix the similarity tab alone demands 2034px for a ~180-char path;
    // post-fix the detail tab (~508: fixed 220 preview + wrapped form) leads.
    if (detailTabs) {
        const int tabsMin = detailTabs->minimumSizeHint().width();
        check(tabsMin <= 640, "detail tabs stay within design width with long paths");
    }
    // The whole right pane: pre-fix the tab inflation locked it at 2038 and
    // the splitter handle could not shrink it; post-fix only the fixed header
    // / toolbar / tab content (~592 here) remains, so the handle moves again.
    if (rightW && split) {
        const int rightMin = rightW->minimumSizeHint().width();
        check(rightMin <= 700, "right pane minimum is fixed content only");
    }
    if (imgGrid && imgGrid->count() > 0) {
        const int cellW = imgGrid->visualItemRect(imgGrid->item(0)).width();
        const int cols = cellW > 0 ? imgGrid->viewport()->width() / cellW : -1;
        std::cout << "  [info] midGrid viewportW=" << imgGrid->viewport()->width()
                  << " cellW=" << cellW << " cols~" << cols << "\n";
    }

    // Splitter mobility: ask for a narrow right pane and read back reality.
    if (split) {
        split->setSizes({180, 1300, 60});
        pump(300);
        const auto sz = split->sizes();
        std::cout << "  [info] after narrow ask split sizes=" << sz.value(0) << "/"
                  << sz.value(1) << "/" << sz.value(2) << "\n";
    }

    // ---- C: file-thumb retry parity ----
    // The retry itself is proven request-side via the monotonic counter,
    // independent of backend answer timing (the loopback answers
    // synchronously here, so the pending map never holds anything).
    // After dropping cache+pending, one catch-up tick must re-ask the middle
    // representatives (existing discipline, control) AND the file grid rows
    // AND the selected preview. The full-rebuild guard pins the tick to the
    // catch-up branch: a rebuild would re-request through row creation and
    // prove nothing about the pump.
    // A settling tick first: it lets lastFillSig_ catch up with matchSeq_ so
    // the measured tick cannot take the rebuild branch.
    w.testUiTick();
    pump(200);
    const qulonglong rebuildBefore = w.testFullRebuildCount();
    const qulonglong reqBefore = w.testThumbRequestCount();
    w.testDropThumbCache();
    w.testUiTick();
    const qulonglong rebuildAfter = w.testFullRebuildCount();
    const qulonglong reqAfter = w.testThumbRequestCount();
    const qulonglong delta = reqAfter - reqBefore;
    std::cout << "  [info] rebuildBefore=" << rebuildBefore << " rebuildAfter=" << rebuildAfter
              << " reqDelta=" << delta << "\n";
    check(rebuildAfter == rebuildBefore, "the tick took the catch-up branch (no rebuild)");
    // Only 3 middle rows exist, so a middle-only pump can never reach 4; the
    // file grid (2 visible rows) plus the preview add 3 on top post-fix.
    check(delta >= 4, "catch-up re-asks file rows and preview on top of middle");

    std::cout << "detail_layout_probe=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
