// GUI usability regression (0.9.4.63): display-settings round-trip, splitter
// middle-first policy, and filename selectability. Follows the offscreen
// patterns of scan_workflow_test (fresh settings dir, modal-dialog timers,
// widget lookup by automation objectName — no private-member access).
#include "mainwindow.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
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

// Minimal PGM (proven shape from search_engine_test). Two identical pairs.
void pgm(const std::filesystem::path& p, int high) {
    std::ofstream f(p, std::ios::binary);
    f << "P5\n64 64\n255\n";
    for (int i = 0; i < 4096; i++) f.put((char)((i % 64) < 32 ? high : 20));
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

// Drives the modal display-settings dialog: polls until it appears, sets the
// checkbox, accepts. A fire cap quits the app instead of hanging forever if
// the dialog never opens (broken feature must fail, not hang CTest).
void driveSettingsDialog(MainWindow& w, bool show, int* fires) {
    QTimer poll;
    poll.setInterval(100);
    int n = 0;
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        ++n;
        if (auto* dlg = w.findChild<QDialog*>("displaySettingsDlg")) {
            if (auto* cb = dlg->findChild<QCheckBox*>("showDetailLogBox")) {
                cb->setChecked(show);
                dlg->accept();
            }
        }
        if (n > 200) QApplication::quit();
    });
    poll.start();
    if (auto* act = w.findChild<QAction*>("displaySettingsAct")) act->trigger();
    poll.stop();
    if (fires) *fires = n;
    QApplication::processEvents();
}

int paneWidth(MainWindow& w, const char* name) {
    if (auto* p = w.findChild<QWidget*>(name)) return p->width();
    return -1;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    namespace fs = std::filesystem;
    std::error_code ec;
    auto d = fs::temp_directory_path() / "msf_usability_test";
    fs::remove_all(d, ec);
    fs::create_directories(d / "media", ec);
    fs::create_directories(d / "settings", ec);
    pgm(d / "media" / "a.jpg", 220); pgm(d / "media" / "a2.jpg", 220);
    pgm(d / "media" / "b.jpg", 200); pgm(d / "media" / "b2.jpg", 200);
    initAppSettings(QString::fromStdString((d / "settings").string()));
    QSettings().setValue("ui/lastFolder", QString::fromStdString((d / "media").string()));
    QSettings().sync();

    MainWindow w;
    w.show();
    QApplication::processEvents();
    QTimer closer;
    closer.setInterval(250);
    QObject::connect(&closer, &QTimer::timeout, [&]() {
        if (QWidget* m = QApplication::activeModalWidget()) m->close();
    });
    closer.start();

    // ---- Phase A: Show Detailed Logs round-trip (no scan needed). ----
    auto* logTgl = w.findChild<QCheckBox*>("logTgl");
    if (!logTgl) { std::cerr << "no logTgl\n"; return 1; }
    check(!logTgl->isHidden(), "default ON: Detailed Logs checkbox visible");
    driveSettingsDialog(w, false, nullptr);
    check(QSettings().value("ui/showDetailLog", true).toBool() == false,
          "OFF persists to QSettings");
    check(logTgl->isHidden(), "OFF hides the checkbox");
    check(logTgl->isChecked(), "hidden checkbox keeps its checked state (telemetry untouched)");
    driveSettingsDialog(w, true, nullptr);
    check(QSettings().value("ui/showDetailLog", false).toBool() == true,
          "ON persists to QSettings");
    check(!logTgl->isHidden(), "ON shows the checkbox again");

    // ---- Phase B: splitter middle-first growth across three widths. ----
    auto* mid = w.findChild<QWidget*>("middlePane");
    auto* right = w.findChild<QWidget*>("rightPane");
    auto* left = w.findChild<QWidget*>("leftPane");
    if (!mid || !right || !left) { std::cerr << "no panes\n"; return 2; }
    const int mid0 = mid->width(), right0 = right->width();
    std::cout << "  [info] default panes L/M/R=" << left->width()
              << "/" << mid0 << "/" << right0 << "\n";
    check(mid0 > right0, "default: middle pane wider than right pane");
    w.resize(1850, 880);
    QApplication::processEvents();
    const int mid1 = mid->width(), right1 = right->width();
    w.resize(2200, 880);
    QApplication::processEvents();
    const int mid2 = mid->width(), right2 = right->width();
    std::cout << "  [info] widths L/M/R: 1500=(" << paneWidth(w, "leftPane") << "/"
              << mid0 << "/" << right0 << ") 2200=(" << left->width() << "/"
              << mid2 << "/" << right2 << ")\n";
    const int dMid1 = mid1 - mid0, dRight1 = right1 - right0;
    const int dMid2 = mid2 - mid1, dRight2 = right2 - right1;
    check(dMid1 > dRight1 && dRight1 <= 8 && dMid1 >= 300,
          "1500->1850: middle absorbs the growth, right stays put");
    check(dMid2 > dRight2 && dRight2 <= 8 && dMid2 >= 300,
          "1850->2200: middle absorbs the growth, right stays put");
    check(mid2 > mid0, "middle pane strictly wider after growth");

    // ---- Phase C: filename selectable like full path (needs a scan). ----
    auto* scan = w.findChild<QPushButton*>("scan");
    if (!scan) { std::cerr << "no scan button\n"; return 3; }
    scan->click();
    QApplication::processEvents();
    pumpUntil([&] { return scan->isEnabled(); }, 180000, "fixture scan finished");
    if (!gOk) return 4;
    QApplication::processEvents();
    auto* tree = w.findChild<QTreeWidget*>("imgTree");
    auto* files = w.findChild<QListWidget*>("fileGrid");
    if (!tree || !files || tree->topLevelItemCount() < 1) {
        std::cerr << "no groups to select\n";
        return 5;
    }
    tree->setCurrentItem(tree->topLevelItem(0));
    QApplication::processEvents();
    if (files->count() < 1) { std::cerr << "no files in group\n"; return 6; }
    files->setCurrentRow(0);
    QApplication::processEvents();
    bool nameOk = false, pathOk = false;
    for (auto* lb : w.findChildren<QLabel*>()) {
        const QString t = lb->text();
        if (t.isEmpty() || t == "-") continue;
        const auto flags = lb->textInteractionFlags();
        const bool sel = (flags & Qt::TextSelectableByMouse) && (flags & Qt::TextSelectableByKeyboard);
        if (t.endsWith(".jpg", Qt::CaseInsensitive) && !t.contains('/') && !t.contains('\\')) {
            if (sel) nameOk = true;
            else std::cerr << "  [info] filename label not selectable: " << t.toStdString() << "\n";
        }
        if (t.contains("msf_usability_test")) {
            if (sel) pathOk = true;
        }
    }
    check(nameOk, "filename value selectable by mouse and keyboard");
    check(pathOk, "full-path value still selectable (existing behavior kept)");

    fs::remove_all(d, ec);
    std::cout << "usability_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
