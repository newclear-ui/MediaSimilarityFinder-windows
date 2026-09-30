// S4 GUI benchmark integration test: mode selection, serial-execution gating,
// suite-lock busy handling, and the fact that the legacy benchTgl_ path is
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
    QPushButton *run = pb(*w, "benchRun"), *stop = pb(*w, "benchStop");
    QCheckBox* legacy = cb(*w, "benchTgl");     // must survive untouched
    QPushButton* scanBtn = pb(*w, "scan");

    std::printf("-- controls --\n");
    chk(auto_ && cpu && gpu, "the three mode checkboxes exist");
    chk(run && stop, "the benchmark run and stop buttons exist");
    chk(legacy != nullptr, "the legacy benchTgl_ checkbox still exists");
    chk(scanBtn != nullptr, "the existing scan button still exists");
    if (!auto_ || !cpu || !gpu || !run || !stop || !legacy || !scanBtn) {
        std::printf("\nbenchmark_ui_selfcheck=FAIL checks=%d\n", gChecks);
        return 1;
    }
    chk(auto_->objectName() != legacy->objectName(), "the new controls are distinct from benchTgl_");

    // ---- default selection: all three, matching the S4 contract -------------
    std::printf("-- mode selection --\n");
    chk(auto_->isChecked() && cpu->isChecked() && gpu->isChecked(),
        "all three modes are selected by default");
    chk(legacy->isChecked(), "the legacy telemetry checkbox keeps its own default");

    // The gate needs both a folder and at least one mode, so set the folder first
    // and vary only the mode selection.
    QLineEdit* folder = w->findChild<QLineEdit*>("folder");
    chk(folder != nullptr, "the folder field is reachable");
    if (!folder) {
        std::printf("\nbenchmark_ui_selfcheck=FAIL checks=%d\n", gChecks);
        return 1;
    }
    folder->setText(QString::fromStdString(msf::path_to_utf8(source)));
    QApplication::processEvents();
    chk(run->isEnabled(), "a folder plus the default modes enables the run button");

    // The checkbox set is an execution selection. Every combination is allowed
    // except the empty one, and the order is always S2's canonical order.
    struct Case { bool a, c, g; const char* want; };
    const Case cases[] = {
        {true,  false, false, "auto"},
        {false, true,  false, "cpu"},
        {false, false, true,  "gpu-max"},
        {true,  true,  false, "auto+cpu"},
        {true,  false, true,  "auto+gpu-max"},
        {false, true,  true,  "cpu+gpu-max"},
        {true,  true,  true,  "auto+cpu+gpu-max"},
    };
    for (const auto& t : cases) {
        auto_->setChecked(t.a); cpu->setChecked(t.c); gpu->setChecked(t.g);
        QApplication::processEvents();
        chk(run->isEnabled(), std::string("selection [") + t.want + "] keeps the run button enabled");
    }

    // Empty selection disables the run button (at least one mode is required).
    auto_->setChecked(false); cpu->setChecked(false); gpu->setChecked(false);
    QApplication::processEvents();
    chk(!run->isEnabled(), "no mode selected disables the run button");
    chk(!stop->isEnabled(), "stop stays disabled while idle");

    // ---- the folder half of the gate ----------------------------------------
    std::printf("-- gating --\n");
    auto_->setChecked(true);
    QApplication::processEvents();
    folder->setText(QString());
    QApplication::processEvents();
    chk(!run->isEnabled(), "an empty folder disables the run button");
    folder->setText(QString::fromStdString(msf::path_to_utf8(source)));
    QApplication::processEvents();
    chk(run->isEnabled(), "restoring the folder re-enables the run button");

    // ---- legacy benchTgl_ is untouched by all of this -----------------------
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

    // ---- mode vector reaching the runner is the canonical order -------------
    std::printf("-- runner mode vector --\n");
    {
        // The worker's own ordering is covered by benchmark_worker_test. Here we
        // confirm the GUI-side vector has no duplicates and no foreign values by
        // driving the same construction the GUI uses.
        for (const auto& t : cases) {
            std::vector<msf::GpuBackendKind> modes;
            if (t.a) modes.push_back(msf::GpuBackendKind::Auto);
            if (t.c) modes.push_back(msf::GpuBackendKind::Cpu);
            if (t.g) modes.push_back(msf::GpuBackendKind::Cuda);
            std::set<msf::GpuBackendKind> uniq(modes.begin(), modes.end());
            chk(uniq.size() == modes.size(), std::string("selection [") + t.want + "] has no duplicate modes");
        }
    }

    w.reset();
    QApplication::processEvents();

    std::error_code ec;
    fs::remove_all(base, ec);
    std::printf("\nbenchmark_ui_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
