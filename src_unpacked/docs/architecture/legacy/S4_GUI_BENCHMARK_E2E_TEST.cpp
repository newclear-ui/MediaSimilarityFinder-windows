// S4 GUI benchmark END-TO-END: the real MainWindow, the real BenchmarkWorker and
// the real ProductionBenchmarkExecutor over real generated media.
//
// This is deliberately separate from the fake-executor tests. Nothing here
// substitutes the search path, so what it proves is that the whole S4 chain
// actually executes: controls -> storage lock -> worker -> S2 runner -> engine ->
// per-mode snapshots -> UI state.
//
// A note on where it writes: MainWindow uses the product's portable-aware base
// directory, which for a test binary is its own output folder. The suite this test
// creates is removed again at the end, and nothing else is touched.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>

#include "benchmark_gui_store.h"
#include "benchmark_worker.h"
#include "dataset_fingerprint.h"
#include "mainwindow.h"
#include "path_utils.h"

namespace {

int gChecks = 0, gFails = 0;
void chk(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) { ++gFails; std::printf("  [F] %s\n", what.c_str()); }
    else      { std::printf("  [ok] %s\n", what.c_str()); }
}

namespace fs = std::filesystem;

QCheckBox* cb(MainWindow& w, const char* n) { return w.findChild<QCheckBox*>(n); }
QPushButton* pb(MainWindow& w, const char* n) { return w.findChild<QPushButton*>(n); }
QLabel* lbl(MainWindow& w, const char* n) { return w.findChild<QLabel*>(n); }

bool makeMedia(const fs::path& dir, int imageCount, bool video) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string d = msf::path_to_utf8(dir);
    for (int i = 0; i < imageCount; ++i) {
        const std::string out = d + "/img" + std::to_string(i) + ".png";
        const std::string cmd = "ffmpeg -hide_banner -loglevel error -y -f lavfi -i "
                                "testsrc=size=48x36:rate=5:duration=1 -frames:v 1 \"" + out + "\"";
        if (std::system(cmd.c_str()) != 0) return false;
    }
    if (video) {
        const std::string out = d + "/vid.mp4";
        const std::string cmd = "ffmpeg -hide_banner -loglevel error -y -f lavfi -i "
                                "testsrc=size=48x36:rate=5:duration=1 -c:v mpeg4 -pix_fmt yuv420p \"" + out + "\"";
        if (std::system(cmd.c_str()) != 0) return false;
    }
    return true;
}

// Language-independent completion detection: after a click the run button disables
// itself, and the terminal handler re-enables it.
bool waitRun(MainWindow& w, int timeoutMs, bool& startedSeen) {
    QPushButton* run = pb(w, "benchRun");
    if (!run) return false;
    QElapsedTimer t; t.start();
    while (t.elapsed() < timeoutMs) {
        QApplication::processEvents();
        if (!run->isEnabled()) startedSeen = true;
        else if (startedSeen) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return false;
}

std::string guiSuiteDir(const fs::path& appDir, const fs::path& source) {
    const auto p = msf::benchmarkGuiPaths(msf::path_to_utf8(appDir), msf::path_to_utf8(source));
    return p.suiteDir;
}

// Snapshot of the production Index directory, so the test can prove whether the
// benchmark run added anything there. Mere existence is not enough: earlier
// sessions and the CLI already leave entries behind.
std::set<std::string> productionIndexEntries(const fs::path& appDir) {
    std::set<std::string> out;
    std::error_code ec;
    const fs::path idx = appDir / "Index";
    if (!fs::exists(idx, ec)) return out;
    for (auto it = fs::directory_iterator(idx, ec); it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        out.insert(it->path().filename().string());
    }
    return out;
}

// Reads one top-level string field out of a flat JSON object.
std::string jsonField(const std::string& json, const std::string& key) {
    const std::string pat = "\"" + key + "\":\"";
    const std::size_t at = json.find(pat);
    if (at == std::string::npos) return std::string();
    const std::size_t s = at + pat.size();
    const std::size_t e = json.find('"', s);
    return e == std::string::npos ? std::string() : json.substr(s, e - s);
}

// Reads one top-level NUMBER field (cpuPercent/gpuPercent are numbers, not strings).
std::string jsonNumField(const std::string& json, const std::string& key) {
    const std::string pat = "\"" + key + "\":";
    const std::size_t at = json.find(pat);
    if (at == std::string::npos) return std::string();
    std::size_t s = at + pat.size();
    std::size_t e = s;
    while (e < json.size() && (std::isdigit(static_cast<unsigned char>(json[e])) || json[e] == '-')) ++e;
    return json.substr(s, e - s);
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    std::printf("S4 GUI benchmark END-TO-END (real engine)\n\n");

    const fs::path base = fs::temp_directory_path() / "msf_s4_gui_e2e";
    std::error_code ec;
    fs::remove_all(base, ec);
    // Enough files that a cancel lands mid-run rather than after it finished.
    const fs::path source = base / "media";
    const bool haveMedia = makeMedia(source, 24, true);
    const fs::path appDir = fs::path(QCoreApplication::applicationDirPath().toStdString());

    if (!haveMedia) {
        std::printf("  [SKIP] ffmpeg unavailable: the real engine path was NOT exercised\n");
        std::printf("\ngui_benchmark_e2e=INCOMPLETE checks=%d reason=ffmpeg-unavailable\n", gChecks);
        return 0;
    }

    std::unique_ptr<MainWindow> w(new MainWindow());
    QApplication::processEvents();
    // Three checkpoints pin down WHO touches the production index, if anyone does.
    const std::set<std::string> prodAtStart = productionIndexEntries(appDir);
    const std::set<std::string> prodAfterCtor = productionIndexEntries(appDir);
    QCheckBox *bAuto = cb(*w, "benchModeAuto"), *bCpu = cb(*w, "benchModeCpu"),
             *bGpu = cb(*w, "benchModeGpu");
    QPushButton *bRun = pb(*w, "benchRun"), *bStop = pb(*w, "benchStop"),
                *bScan = pb(*w, "scan"), *bPause = pb(*w, "pause");
    QLabel* status = lbl(*w, "benchStatus");
    QLineEdit* folder = w->findChild<QLineEdit*>("folder");
    if (!bAuto || !bCpu || !bGpu || !bRun || !bStop || !bScan || !bPause || !status || !folder) {
        std::printf("\ngui_benchmark_e2e=FAIL checks=%d reason=controls-missing\n", gChecks);
        return 1;
    }
    folder->setText(QString::fromStdString(msf::path_to_utf8(source)));
    QApplication::processEvents();

    const std::string suite = guiSuiteDir(appDir, source);

    // ---- 1. all three modes, real execution --------------------------------
    // ---- Resource Policy: the preset the user chose must reach the snapshot --
    // Driven through the real MainWindow combo, so policy_ is rebuilt by the
    // existing make_policy() path in resourceChanged() rather than assigned.
    // The preset must be chosen BEFORE the run, because the snapshot records the
    // policy the run was started with.
    QComboBox* preset = w->findChild<QComboBox*>();
    chk(preset != nullptr, "the resource preset combo is reachable");
    if (preset) preset->setCurrentIndex(0);   // "Maximum 90%"
    QApplication::processEvents();

    std::printf("-- 1. AUTO + CPU + GPU-max, real run --\n");
    bAuto->setChecked(true); bCpu->setChecked(true); bGpu->setChecked(true);
    QApplication::processEvents();
    chk(bRun->isEnabled(), "run button armed");
    QApplication::processEvents();
    bRun->click();
    bool seen = false;
    const bool done = waitRun(*w, 180000, seen);
    chk(done, "the real benchmark run completed");
    chk(bRun->isEnabled(), "run button returned to enabled (idle restored)");
    chk(!bStop->isEnabled(), "stop button disabled again");
    chk(!bPause->isEnabled(), "the scan pause control stays disabled (it is a scan-only control)");

    const auto paths = msf::benchmarkGuiPaths(msf::path_to_utf8(appDir), msf::path_to_utf8(source));
    chk(msf::benchmarkGuiSnapshotExists(paths, msf::GpuBackendKind::Auto),  "auto.json created");
    chk(msf::benchmarkGuiSnapshotExists(paths, msf::GpuBackendKind::Cpu),   "cpu.json created");
    chk(msf::benchmarkGuiSnapshotExists(paths, msf::GpuBackendKind::Cuda),  "gpu-max.json created");
    const std::string autoJ = msf::readBenchmarkGuiSnapshot(paths, msf::GpuBackendKind::Auto);
    chk(!autoJ.empty(), "auto.json has content");
    chk(autoJ.find("\"mode\":\"auto\"") != std::string::npos, "  records mode=auto");
    chk(autoJ.find("\"sourceRoot\"") != std::string::npos, "  records the canonical source root");
    chk(autoJ.find("\"casesWithMode\":") != std::string::npos, "  records how many cases ran");

    // ---- datasetFingerprint: the real canonical value must be recorded ------
    // This is the same call MediaSearchEngine makes during a normal scan, so the
    // value is comparable with a scan's own benchmark fingerprint.
    const msf::DatasetFingerprint expected = msf::computeDatasetFingerprint(msf::path_to_utf8(source));
    const std::string recordedFp = jsonField(autoJ, "datasetFingerprint");
    std::printf("    (dataset fingerprint recorded: %s)\n",
                recordedFp.empty() ? "<EMPTY>" : recordedFp.c_str());
    chk(expected.state == "measured", "the fixture root measures a dataset fingerprint");
    chk(!recordedFp.empty(), "the snapshot records a dataset fingerprint (no longer empty)");
    chk(recordedFp == expected.fingerprint,
        "  and it is exactly the canonical computeDatasetFingerprint value, verbatim");
    chk(recordedFp.size() == 64, "  64 hex characters, no extra fields appended");

    // ---- Resource Policy: the preset the user chose must reach the snapshot --
    {
        const std::string maxJ = msf::readBenchmarkGuiSnapshot(paths, msf::GpuBackendKind::Auto);
        std::printf("    (preset Maximum -> snapshot cpuPercent: %s)\n",
                    jsonNumField(maxJ, "cpuPercent").c_str());
        chk(maxJ.find("\"resourcePolicy\"") != std::string::npos, "the snapshot carries a resourcePolicy block");
        chk(maxJ.find("\"cpuPercent\":90") != std::string::npos,
            "the Maximum preset chosen in the toolbar is recorded as cpuPercent 90");
        chk(maxJ.find("\"cpuPercent\":55") == std::string::npos,
            "  and it is no longer the Balanced default of 55");
        chk(maxJ.find("\"mode\":1") != std::string::npos, "  the preset mode is recorded (Maximum=1)");
        if (preset) { preset->setCurrentIndex(2); QApplication::processEvents(); }  // back to Balanced
    }

    // The CPU build has no CUDA backend, so GPU-max is Skipped there. That is the
    // correct S2 result and must not be reported as a success.
    const std::string gpuJ = msf::readBenchmarkGuiSnapshot(paths, msf::GpuBackendKind::Cuda);
    const std::string gpuStatus = jsonField(gpuJ, "status");
    chk(!gpuStatus.empty(), "gpu-max.json records a status");
    std::printf("    (gpu-max status on this build: %s)\n", gpuStatus.c_str());

    // Runtime index lives only under the GUI suite. The production index is
    // compared by CONTENT, not by existence: entries from earlier CLI runs are
    // already there, so only new entries would indicate pollution.
    const std::set<std::string> prodAfterRun = productionIndexEntries(appDir);
    std::vector<std::string> added;
    for (const auto& e : prodAfterRun) if (!prodAfterCtor.count(e)) added.push_back(e);
    std::printf("    (production Index entries added by MainWindow construction: %zu)\n",
                prodAfterCtor.size() - prodAtStart.size());
    std::printf("    (production Index entries added by the benchmark run: %zu)\n", added.size());
    for (const auto& e : added) std::printf("      NEW %s\n", e.c_str());
    chk(added.empty(), "the benchmark run added nothing to the production Index");
    const bool guiHasRuntime = fs::exists(fs::u8path(suite + "/runtime"));
    std::printf("    (GUI runtime dir present after run: %s)\n", guiHasRuntime ? "yes" : "no");
    chk(guiHasRuntime, "the run wrote its index under the GUI suite runtime tree");

    // The scanned folder itself must contain only its media.
    int stray = 0;
    for (auto it = fs::recursive_directory_iterator(source, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (name.find(".json") != std::string::npos || name.find(".jsonl") != std::string::npos ||
            name.find("Index") != std::string::npos || name.find(".db") != std::string::npos)
            ++stray;
    }
    chk(stray == 0, "the scanned folder gained no benchmark artifact");

    // ---- 2. subset selection updates only the selected modes ---------------
    std::printf("-- 2. AUTO + GPU-max only: cpu.json preserved --\n");
    const std::string cpuBefore = msf::readBenchmarkGuiSnapshot(paths, msf::GpuBackendKind::Cpu);
    const std::string autoBefore = msf::readBenchmarkGuiSnapshot(paths, msf::GpuBackendKind::Auto);
    bCpu->setChecked(false);
    QApplication::processEvents();
    bRun->click();
    seen = false;
    chk(waitRun(*w, 180000, seen), "the subset run completed");
    chk(msf::readBenchmarkGuiSnapshot(paths, msf::GpuBackendKind::Cpu) == cpuBefore,
        "the unselected cpu.json is preserved byte for byte");
    chk(msf::readBenchmarkGuiSnapshot(paths, msf::GpuBackendKind::Auto) != autoBefore,
        "the selected auto.json was replaced");

    // ---- 3. suite lock busy: no start, snapshot untouched -------------------
    std::printf("-- 3. suite lock busy --\n");
    {
        msf::BenchmarkGuiStorage::Config cfg;
        cfg.applicationRoot = msf::path_to_utf8(appDir);
        cfg.runId = "e2e-holder";
        cfg.sourceRoot = msf::path_to_utf8(source);
        msf::BenchmarkGuiStorage holder(cfg);
        std::string err;
        chk(holder.begin(err), "an external holder takes the same suite lock");
        const std::string before = msf::readBenchmarkGuiSnapshot(paths, msf::GpuBackendKind::Auto);

        bRun->click();
        QApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        QApplication::processEvents();
        chk(msf::readBenchmarkGuiSnapshot(paths, msf::GpuBackendKind::Auto) == before,
            "a refused start leaves the existing snapshot unchanged");
        chk(bRun->isEnabled(), "the run button is usable again after the refusal");
        holder.release();
    }

    // ---- 4. cancel ---------------------------------------------------------
    std::printf("-- 4. cancel mid-run --\n");
    {
        bCpu->setChecked(true);
        QApplication::processEvents();
        bRun->click();
        // Let the run get under way, then stop it.
        QElapsedTimer t; t.start();
        while (t.elapsed() < 4000 && bRun->isEnabled()) {
            QApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const bool wasRunning = !bRun->isEnabled();
        chk(wasRunning, "the run was in progress when cancel was pressed");
        chk(bStop->isEnabled(), "stop is enabled while a benchmark runs");
        chk(!bPause->isEnabled(), "the scan pause control is disabled during a benchmark");
        chk(!bScan->isEnabled(), "the scan start is blocked during a benchmark");
        chk(!bAuto->isEnabled(), "mode checkboxes are locked during a benchmark");
        bStop->click();
        QApplication::processEvents();
        seen = false;
        const bool stopped = waitRun(*w, 60000, seen);
        chk(stopped, "cancel returned the UI to idle");
        chk(bRun->isEnabled(), "run button re-enabled after cancel");
        chk(bScan->isEnabled(), "scan start re-enabled after cancel");
        chk(!bAuto->isEnabled() == false, "mode checkboxes usable again after cancel");
    }

    // ---- 5. cleanup --------------------------------------------------------
    std::error_code cec;
    fs::remove_all(fs::u8path(suite), cec);
    // The test wrote under the product's portable base directory (this binary's
    // output folder), so the empty parent folders it created are removed too and
    // the build output is left as it was found.
    fs::remove(fs::u8path(guiSuiteDir(appDir, source) + "/.."), cec);
    fs::remove(fs::u8path(msf::benchmarkGuiPaths(msf::path_to_utf8(appDir), "").guiRoot + "/.."), cec);
    fs::remove(fs::u8path(msf::benchmarkGuiPaths(msf::path_to_utf8(appDir), "").guiRoot), cec);
    fs::remove_all(base, cec);

    w.reset();
    QApplication::processEvents();

    std::printf("\ngui_benchmark_e2e=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
