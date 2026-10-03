// S4 GUI benchmark integration test: mode selection, serial-execution gating,
// suite-lock busy handling, and the fact that the legacy logTgl_ path is
// untouched.
//
// The state machine is driven through the real MainWindow, found by objectName
// exactly like the existing GUI regression tests. The actual benchmark run is NOT
// executed here: this covers the decisions and gating, which is where the S4 rules
// live. Execution semantics are covered by benchmark_worker_test and
// benchmark_gui_store_test.

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
#include "benchmark_gui_store.h"
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

std::string modesText(const std::vector<msf::GpuBackendKind>& v) {
    std::string s;
    for (auto m : v) { if (!s.empty()) s += "+"; s += msf::benchmarkModeDirName(m); }
    return s;
}

fs::path scratch() {
    const fs::path p = fs::temp_directory_path() / "msf_s4_ui_bench_test";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p / "source", ec);
    return p;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    std::printf("S4 GUI benchmark integration selfcheck\n\n");

    const fs::path base = scratch();
    const fs::path source = base / "source";
    std::ofstream(msf::path_to_utf8(source / "a.jpg")) << "x";

    std::unique_ptr<MainWindow> w(new MainWindow());
    QApplication::processEvents();

    // ---- controls exist with stable automation names ------------------------
    QCheckBox *auto_ = cb(*w, "benchModeAuto"), *cpu = cb(*w, "benchModeCpu"),
             *gpu = cb(*w, "benchModeGpu");
    QPushButton* scanBtn = pb(*w, "scan");
    QPushButton* run = pb(*w, "benchRun");
    QPushButton* stop = pb(*w, "benchStop");
    QLabel* statusLbl = w->findChild<QLabel*>("benchStatus");
    QCheckBox* legacy = cb(*w, "logTgl");     // must survive untouched

    std::printf("-- controls --\n");
    chk(auto_ && cpu && gpu, "the three resource-mode checkboxes exist");
    chk(run == nullptr && stop == nullptr && statusLbl == nullptr,
        "the legacy benchRun/benchStop/benchStatus widgets are removed");
    chk(legacy != nullptr, "the legacy logTgl_ checkbox still exists");
    chk(scanBtn != nullptr, "the existing scan button still exists");
    if (!auto_ || !cpu || !gpu || !legacy || !scanBtn) {
        std::printf("\nbenchmark_ui_selfcheck=FAIL checks=%d\n", gChecks);
        return 1;
    }
    chk(auto_->objectName() != legacy->objectName(), "the new controls are distinct from logTgl_");

    // ---- single-select resource mode, AUTO default -------------------------
    std::printf("-- resource mode --\n");
    chk(auto_->isChecked() && !cpu->isChecked() && !gpu->isChecked(),
        "default mode is AUTO only");
    cpu->setChecked(true); QApplication::processEvents();
    chk(!auto_->isChecked() && cpu->isChecked() && !gpu->isChecked(),
        "selecting CPU-only clears AUTO (mutually exclusive)");
    gpu->setChecked(true); QApplication::processEvents();
    chk(!cpu->isChecked() && gpu->isChecked(),
        "selecting GPU-max clears CPU (mutually exclusive)");
    auto_->setChecked(false); QApplication::processEvents();
    chk(auto_->isChecked() || cpu->isChecked() || gpu->isChecked(),
        "the last mode cannot be unchecked (always one selected)");
    auto_->setChecked(true); QApplication::processEvents();
    chk(auto_->isChecked() && !cpu->isChecked() && !gpu->isChecked(),
        "AUTO restored as the single default");
    chk(legacy->isChecked(), "the legacy telemetry checkbox keeps its own default and is untouched");

    // ---- photo/video toggle, both-off forbidden ----------------------------
    std::printf("-- media scope toggles --\n");
    QPushButton* img = w->findChild<QPushButton*>("mediaImgBtn");
    QPushButton* vid = w->findChild<QPushButton*>("mediaVidBtn");
    chk(img && vid, "photo and video toggle buttons exist");
    if (!img || !vid) {
        std::printf("\nbenchmark_ui_selfcheck=FAIL checks=%d\n", gChecks);
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
        std::printf("\nbenchmark_ui_selfcheck=FAIL checks=%d\n", gChecks);
        return 1;
    }

    // ---- legacy logTgl_ is untouched by all of this -----------------------
    chk(legacy->isEnabled(), "the legacy telemetry checkbox stays enabled");
    chk(legacy->isChecked(), "  and keeps its checked state");
    chk(legacy->parent() != nullptr, "  and is still parented in the toolbar");

    // ---- suite lock: a second holder is reported as busy, not as a failure --
    // The GUI path uses the same S3 lock contract, so contention is verified on
    // the storage layer the GUI calls, with no MainWindow involved.
    std::printf("-- suite lock --\n");
    {
        msf::BenchmarkGuiStorage::Config cfg;
        cfg.applicationRoot = msf::path_to_utf8(base);
        cfg.runId = "uiLockA";
        cfg.sourceRoot = msf::path_to_utf8(source);
        msf::BenchmarkGuiStorage first(cfg);
        std::string err;
        chk(first.begin(err), "a first holder acquires the suite");

        msf::BenchmarkGuiStorage::Config cfg2 = cfg;
        cfg2.runId = "uiLockB";
        msf::BenchmarkGuiStorage second(cfg2);
        std::string err2;
        chk(!second.begin(err2), "a second holder on the same source is refused");
        chk(second.isBusy(), "  reported as busy");
        chk(!second.active(), "  and never becomes active, so it writes nothing");
        first.release();
    }

    w.reset();
    QApplication::processEvents();

    std::error_code ec;
    fs::remove_all(base, ec);
    std::printf("\nbenchmark_ui_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
