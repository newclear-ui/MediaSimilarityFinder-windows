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
    QCheckBox *auto_ = cb(*w, "strategyAuto"), *cpu = cb(*w, "strategyCpu"),
             *gpu = cb(*w, "strategyGpu");
    QPushButton* scanBtn = pb(*w, "scan");
    QPushButton* run = pb(*w, "benchRun");
    QPushButton* stop = pb(*w, "benchStop");
    QLabel* statusLbl = w->findChild<QLabel*>("benchStatus");
    QCheckBox* logTgl = cb(*w, "logTgl");

    std::printf("-- controls --\n");
    chk(auto_ && cpu && gpu, "the three strategy checkboxes exist");
    chk(run == nullptr && stop == nullptr && statusLbl == nullptr,
        "no benchmark run/stop/status widgets exist");
    chk(logTgl != nullptr, "the [Detailed Logs] checkbox exists");
    chk(scanBtn != nullptr, "the existing scan button still exists");
    if (!auto_ || !cpu || !gpu || !logTgl || !scanBtn) {
        std::printf("\ngui_detailed_log_selfcheck=FAIL checks=%d\n", gChecks);
        return 1;
    }
    chk(auto_->objectName() != logTgl->objectName(), "the strategy controls are distinct from logTgl_");
    chk(logTgl->text().contains(QStringLiteral("Benchmark")) == false,
        "the toggle label carries no Benchmark name");

    // ---- single-select execution strategy, AUTO default --------------------
    std::printf("-- execution strategy --\n");
    chk(auto_->isChecked() && !cpu->isChecked() && !gpu->isChecked(),
        "default strategy is AUTO only");
    cpu->setChecked(true); QApplication::processEvents();
    chk(!auto_->isChecked() && cpu->isChecked() && !gpu->isChecked(),
        "selecting CPU-only clears AUTO (mutually exclusive)");
    gpu->setChecked(true); QApplication::processEvents();
    chk(!cpu->isChecked() && gpu->isChecked(),
        "selecting GPU-max clears CPU (mutually exclusive)");
    auto_->setChecked(false); QApplication::processEvents();
    chk(auto_->isChecked() || cpu->isChecked() || gpu->isChecked(),
        "the last strategy cannot be unchecked (always one selected)");
    auto_->setChecked(true); QApplication::processEvents();
    chk(auto_->isChecked() && !cpu->isChecked() && !gpu->isChecked(),
        "AUTO restored as the single default");
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
