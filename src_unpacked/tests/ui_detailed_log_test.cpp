// S4 GUI detailed-log integration test: execution resource strategy,
// [Detailed Logs] toggle, media scope toggles, and the absence of any
// benchmark run/stop/status workflow.
//
// The state machine is driven through the real MainWindow, found by objectName
// exactly like the existing GUI regression tests. No benchmark is executed
// here: this covers the decisions and gating, which is where the S4 rules
// live. Telemetry recording semantics are covered by benchmark_test and
// scan_workflow_test.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>

#include "mainwindow.h"
#include "path_utils.h"

namespace {

namespace fs = std::filesystem;

int gChecks = 0, gFails = 0;
void chk(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) { ++gFails; std::printf("  [F] %s\n", what.c_str()); }
    else      { std::printf("  [ok] %s\n", what.c_str()); }
}

QCheckBox* cb(MainWindow& w, const char* name) { return w.findChild<QCheckBox*>(name); }
QPushButton* pb(MainWindow& w, const char* name) { return w.findChild<QPushButton*>(name); }

fs::path scratch() {
    const fs::path p = fs::temp_directory_path() / "msf_s4_ui_detailed_log_test";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p / "source", ec);
    return p;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    std::printf("S4 GUI detailed-log integration selfcheck\n\n");

    const fs::path base = scratch();
    const fs::path source = base / "source";
    std::ofstream(msf::path_to_utf8(source / "a.jpg")) << "x";

    std::unique_ptr<MainWindow> w(new MainWindow());
    QApplication::processEvents();

    // ---- controls exist with stable automation names ------------------------
    QComboBox* strategy = w->findChild<QComboBox*>("strategyBox");
    QPushButton* scanBtn = pb(*w, "scan");
    QPushButton* run = pb(*w, "benchRun");
    QPushButton* stop = pb(*w, "benchStop");
    QLabel* statusLbl = w->findChild<QLabel*>("benchStatus");
    QCheckBox* logTgl = cb(*w, "logTgl");

    std::printf("-- controls --\n");
    chk(strategy != nullptr, "the execution-strategy dropdown exists");
    chk(run == nullptr && stop == nullptr && statusLbl == nullptr,
        "no benchmark run/stop/status widgets exist");
    chk(logTgl != nullptr, "the [Detailed Logs] checkbox exists");
    chk(scanBtn != nullptr, "the existing scan button still exists");
    if (!strategy || !logTgl || !scanBtn) {
        std::printf("\ngui_detailed_log_selfcheck=FAIL checks=%d\n", gChecks);
        return 1;
    }
    chk(strategy->count() == 3, "the strategy dropdown holds three choices");
    chk(logTgl->text().contains(QStringLiteral("Benchmark")) == false,
        "the toggle label carries no Benchmark name");

    // ---- single-select execution strategy, AUTO default --------------------
    // A dropdown holds exactly one selection by construction.
    std::printf("-- execution strategy --\n");
    chk(strategy->currentIndex() == 0, "default strategy is AUTO");
    strategy->setCurrentIndex(1); QApplication::processEvents();
    chk(strategy->currentIndex() == 1, "CPU-only selects as the single strategy");
    strategy->setCurrentIndex(2); QApplication::processEvents();
    chk(strategy->currentIndex() == 2, "GPU-max selects as the single strategy");
    strategy->setCurrentIndex(0); QApplication::processEvents();
    chk(strategy->currentIndex() == 0, "AUTO restored");
    chk(logTgl->isChecked(), "the detailed-log checkbox keeps its own default");

    // ---- photo/video toggle, both-off forbidden ----------------------------
    std::printf("-- media scope toggles --\n");
    QPushButton* img = w->findChild<QPushButton*>("mediaImgBtn");
    QPushButton* vid = w->findChild<QPushButton*>("mediaVidBtn");
    chk(img && vid, "photo and video toggle buttons exist");
    if (!img || !vid) {
        std::printf("\ngui_detailed_log_selfcheck=FAIL checks=%d\n", gChecks);
        return 1;
    }
    chk(img->isCheckable() && vid->isCheckable(), "both are checkable");
    chk(img->isChecked() && vid->isChecked(), "default both on");
    img->setChecked(false); QApplication::processEvents();
    chk(!img->isChecked() && vid->isChecked(), "photo off, video on is allowed");
    vid->setChecked(false); QApplication::processEvents();
    chk(img->isChecked() || vid->isChecked(), "both off is refused");
    img->setChecked(true); vid->setChecked(true); QApplication::processEvents();

    QLineEdit* folder = w->findChild<QLineEdit*>("folder");
    chk(folder != nullptr, "the folder field is reachable");
    if (!folder) {
        std::printf("\ngui_detailed_log_selfcheck=FAIL checks=%d\n", gChecks);
        return 1;
    }

    // ---- logTgl_ is untouched by all of this --------------------------------
    chk(logTgl->isEnabled(), "the detailed-log checkbox stays enabled");
    chk(logTgl->isChecked(), "  and keeps its checked state");
    chk(logTgl->parent() != nullptr, "  and is still parented in the toolbar");

    w.reset();
    QApplication::processEvents();

    std::error_code ec;
    fs::remove_all(base, ec);
    std::printf("\ngui_detailed_log_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
