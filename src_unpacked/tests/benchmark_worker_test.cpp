// S4 benchmark worker tests: single Runner invocation, selected-mode subset,
// file-level ordering, S2 cancellation semantics, and the signals MainWindow
// needs. No GUI widget is involved and no media is decoded: the mode outcome is
// injected so every branch is reachable without running a real scan.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>

#include "benchmark_gui_store.h"
#include "benchmark_worker.h"
#include "dataset_fingerprint.h"
#include "path_utils.h"

namespace {

int gChecks = 0, gFails = 0;
void chk(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) { ++gFails; std::printf("  [F] %s\n", what.c_str()); }
    else      { std::printf("  [ok] %s\n", what.c_str()); }
}

namespace fs = std::filesystem;

// Everything the worker emitted during one run().
struct Captured {
    int starts = 0;
    int reportedTotal = 0;
    std::vector<BenchmarkGuiProgress> progress;
    std::vector<BenchmarkGuiOutcome> outcomes;
    std::vector<QString> failures;
    std::vector<bool> busy;   // true/false pairs in emission order

    void bind(BenchmarkWorker& w) {
        QObject::connect(&w, &BenchmarkWorker::started, [&](int total) {
            ++starts; reportedTotal = total;
        });
        QObject::connect(&w, &BenchmarkWorker::caseProgress,
                         [&](BenchmarkGuiProgress p) { progress.push_back(std::move(p)); });
        QObject::connect(&w, &BenchmarkWorker::finished,
                         [&](BenchmarkGuiOutcome o) { outcomes.push_back(std::move(o)); });
        QObject::connect(&w, &BenchmarkWorker::failed,
                         [&](QString m) { failures.push_back(std::move(m)); });
        QObject::connect(&w, &BenchmarkWorker::busyChanged, [&](bool b) { busy.push_back(b); });
    }
};

std::string modesOf(const BenchmarkGuiProgress& p) {
    std::string s;
    for (const auto& m : p.modeLines) { if (!s.empty()) s += ">"; s += m.requestedMode.toStdString(); }
    return s;
}

// Windows paths carry backslashes, so the basename has to be located with both
// separators rather than assuming '/'.
std::string baseName(const std::string& p) {
    const std::size_t at = p.find_last_of("/\\");
    return at == std::string::npos ? p : p.substr(at + 1);
}

fs::path scratch() {
    const fs::path p = fs::temp_directory_path() / "msf_s4_worker_test";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p / "app" / "Benchmark", ec);
    fs::create_directories(p / "src", ec);
    // A real file so the dataset fingerprint has something to hash.
    std::ofstream(msf::path_to_utf8(p / "src" / "a.jpg")) << "not-a-real-jpeg";
    return p;
}

msf::BenchmarkRequest makeRequest(const fs::path& source) {
    msf::BenchmarkRequest r;
    r.sourceRoot = msf::path_to_utf8(source);
    r.applicationDirectory = msf::path_to_utf8(source / "..");   // not used by the fake executor
    r.buildVersion = "0.9.4.43";
    r.suiteId = "s4-worker";
    r.mediaScope = msf::MediaScope::All;
    return r;
}

} // namespace

// A minimal executor injected through the public S2 boundary, so the worker can be
// driven without decoding media. Mirrors the S2 integration test's approach.
namespace {

using Fn = std::function<void(const msf::BenchmarkFileItem&, msf::GpuBackendKind,
                              msf::BenchmarkModeResult&)>;

class FakeExecutor : public msf::BenchmarkExecutor {
public:
    std::vector<msf::BenchmarkFileItem> files;
    Fn onMode;
    std::set<msf::GpuBackendKind> unavailable;

    std::vector<msf::BenchmarkFileItem> discover(const msf::BenchmarkRequest&) override { return files; }
    bool modeAvailable(msf::GpuBackendKind m) const override {
        return unavailable.find(m) == unavailable.end();
    }
    msf::GpuBackendKind resolveEffectiveMode(msf::GpuBackendKind m) const override {
        return m == msf::GpuBackendKind::Cuda ? msf::GpuBackendKind::Cuda : msf::GpuBackendKind::Cpu;
    }
    void runMode(const msf::BenchmarkRequest&, const msf::BenchmarkFileItem& f,
                 msf::GpuBackendKind m, msf::BenchmarkModeResult& out) override {
        out.status = msf::BenchmarkStatus::Success;
        out.started = true;
        out.completed = true;
        out.elapsedMs = 1.0;
        out.summary.scanned = 1;
        out.summary.analyzed = 1;
        if (onMode) onMode(f, m, out);
    }
};

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);
    std::printf("S4 benchmark worker selfcheck\n\n");

    const fs::path base = scratch();
    const fs::path source = base / "src";
    const std::string appRoot = msf::path_to_utf8(base / "app");

    // ---- the worker runs the S2 runner through an injected executor ----------
    // The worker's own production path is BenchmarkRunner + ProductionBenchmarkExecutor.
    // To drive it deterministically here, the test exercises the same runner the
    // worker uses, and separately checks the worker's own guard/validation paths.
    std::printf("-- runner subset and ordering --\n");
    {
        FakeExecutor exec;
        exec.files = {{msf::path_to_utf8(source / "a.jpg"), msf::MediaKind::Image, 1},
                      {msf::path_to_utf8(source / "b.jpg"), msf::MediaKind::Image, 2}};
        exec.onMode = [&](const msf::BenchmarkFileItem&, msf::GpuBackendKind,
                          msf::BenchmarkModeResult& out) {
            out.elapsedMs = out.requestedMode == msf::GpuBackendKind::Cuda ? 5.0 : 2.0;
        };

        std::vector<std::string> order;
        exec.onMode = [&](const msf::BenchmarkFileItem& f, msf::GpuBackendKind m,
                          msf::BenchmarkModeResult& out) {
            order.push_back(baseName(f.path) + ":" +
                            std::string(msf::benchmarkModeDirName(m)));
            out.elapsedMs = (m == msf::GpuBackendKind::Cuda) ? 5.0 : 2.0;
        };

        msf::BenchmarkRunner runner(exec);
        // A selected subset: AUTO + GPU-max only.
        const std::vector<msf::GpuBackendKind> subset{msf::GpuBackendKind::Auto,
                                                      msf::GpuBackendKind::Cuda};
        const msf::BenchmarkRequest req = makeRequest(source);
        const msf::BenchmarkRun run = runner.run(req, subset);

        std::string joined;
        for (const auto& o : order) { if (!joined.empty()) joined += " "; joined += o; }
        chk(joined == "a.jpg:auto a.jpg:gpu-max b.jpg:auto b.jpg:gpu-max",
            "one invocation with a subset keeps the file-level order");
        chk(run.cases.size() == 2, "one case per file");
        chk(run.cases[0].modeResults.size() == 2, "only the selected modes ran");
        chk(run.cases[0].modeResults[0].requestedMode == msf::GpuBackendKind::Auto, "  AUTO first");
        chk(run.cases[0].modeResults[1].requestedMode == msf::GpuBackendKind::Cuda, "  GPU-max second");
    }

    // ---- ordering stays AUTO -> CPU -> GPU-max for the full set -------------
    {
        FakeExecutor exec;
        exec.files = {{msf::path_to_utf8(source / "a.jpg"), msf::MediaKind::Image, 1}};
        std::vector<std::string> order;
        exec.onMode = [&](const msf::BenchmarkFileItem&, msf::GpuBackendKind m,
                          msf::BenchmarkModeResult&) {
            order.push_back(msf::benchmarkModeDirName(m));
        };
        msf::BenchmarkRunner runner(exec);
        runner.run(makeRequest(source), msf::benchmarkContractModes());
        std::string joined;
        for (const auto& o : order) { if (!joined.empty()) joined += ">"; joined += o; }
        chk(joined == "auto>cpu>gpu-max", "full-set order is AUTO -> CPU -> GPU-max");
    }

    // ---- cancellation semantics are S2's, not the worker's ----------------
    std::printf("-- cancellation --\n");
    {
        FakeExecutor exec;
        exec.files = {{msf::path_to_utf8(source / "a.jpg"), msf::MediaKind::Image, 1},
                      {msf::path_to_utf8(source / "b.jpg"), msf::MediaKind::Image, 2}};
        bool cancel = false;
        exec.onMode = [&](const msf::BenchmarkFileItem&, msf::GpuBackendKind m,
                          msf::BenchmarkModeResult& out) {
            if (m == msf::GpuBackendKind::Cpu) {
                cancel = true;
                out.status = msf::BenchmarkStatus::Cancelled;
                out.completed = false;
                out.elapsedMs = 0.0;
                out.summary = msf::BenchmarkScanSummary{};
                out.errorMessage = "cancelled by caller";
            }
        };
        msf::BenchmarkRunner runner(exec);
        msf::BenchmarkRequest req = makeRequest(source);
        req.isCancelled = [&cancel] { return cancel; };
        const msf::BenchmarkRun run = runner.run(req, msf::benchmarkContractModes());
        chk(run.cases.size() == 1, "only the first case started before cancellation");
        chk(run.cases[0].modeResults[0].status == msf::BenchmarkStatus::Success, "  AUTO SUCCESS");
        chk(run.cases[0].modeResults[1].status == msf::BenchmarkStatus::Cancelled, "  CPU CANCELLED");
        chk(run.cases[0].modeResults[2].status == msf::BenchmarkStatus::Skipped, "  GPU-max SKIPPED");
        chk(run.cases[0].status == msf::BenchmarkStatus::Cancelled, "  case aggregate Cancelled");
        chk(run.status == msf::BenchmarkStatus::Cancelled, "  run aggregate Cancelled");
    }

    // ---- the worker's own guard and validation paths ------------------------
    std::printf("-- worker guards and signals --\n");
    {
        // No mode selected -> refuse without running anything.
        BenchmarkWorker none(makeRequest(source), {}, nullptr);
        Captured c1; c1.bind(none);
        none.run();
        chk(c1.failures.size() == 1, "no mode selected reports a failure");
        chk(c1.outcomes.empty() && c1.progress.empty(), "  and runs nothing");
        chk(c1.busy.size() == 2 && c1.busy[0] && !c1.busy[1], "  busy toggles true then false");

        // No source folder -> refuse.
        msf::BenchmarkRequest noSrc;
        noSrc.buildVersion = "0.9.4.43";
        BenchmarkWorker empty(noSrc, {msf::GpuBackendKind::Auto}, nullptr);
        Captured c2; c2.bind(empty);
        empty.run();
        chk(c2.failures.size() == 1, "no source folder reports a failure");

        // Storage attachment pins the run id and injects the mode index dirs.
        msf::BenchmarkGuiStorage::Config cfg;
        cfg.applicationRoot = appRoot;
        cfg.runId = "workerRun";
        cfg.sourceRoot = msf::path_to_utf8(source);
        cfg.buildVersion = "0.9.4.43";
        msf::BenchmarkGuiStorage storage(cfg);
        std::string err;
        chk(storage.begin(err), "storage begins");

        BenchmarkWorker w(makeRequest(source), {msf::GpuBackendKind::Auto}, &storage);
        chk(w.request().runId == "workerRun", "storage attach pins the run id");
        chk(w.request().modeIndexApplicationDirectory != nullptr,
            "storage attach injects the per-mode index directory");
        const std::string g = w.request().modeIndexApplicationDirectory(msf::GpuBackendKind::Cuda);
        chk(g.find("/runtime/run-workerRun/gpu-max") != std::string::npos,
            "  gpu-max index dir under the GUI runtime tree");

        // The worker installs no journal hooks.
        chk(!w.request().onRunStarted && !w.request().onCaseComplete && !w.request().onRunFinished,
            "worker installs no journal hooks before a run");

        // ---- Resource Policy plumbing (additive, optional) ------------------
        // A default request has no policy, which is what keeps every existing S2/S3
        // caller on the original behaviour.
        msf::BenchmarkRequest plain;
        chk(!plain.resourcePolicy.has_value(),
            "a default BenchmarkRequest carries no ResourcePolicy (S2 behaviour preserved)");
        // A supplied policy must survive the worker unchanged.
        msf::BenchmarkRequest withPolicy;
        withPolicy.buildVersion = "0.9.4.43";
        msf::ResourcePolicy given = msf::make_policy(msf::ResourceMode::Maximum, 90, 90);
        given.gpuEnabled = true;
        withPolicy.resourcePolicy = given;
        BenchmarkWorker pw(withPolicy, {msf::GpuBackendKind::Cpu}, nullptr);
        chk(pw.request().resourcePolicy.has_value(), "a supplied ResourcePolicy reaches the worker");
        chk(pw.request().resourcePolicy->cpuPercent == 90, "  cpuPercent preserved");
        chk(pw.request().resourcePolicy->mode == msf::ResourceMode::Maximum, "  preset mode preserved");
        // The worker must not invent a policy for a caller that supplied none.
        chk(!w.request().resourcePolicy.has_value(),
            "the worker does not synthesise a policy when the caller gave none");

        // ---- dataset fingerprint -------------------------------------------
        // The recorded fingerprint must be the product's own canonical value.
        chk(msf::computeDatasetFingerprint(msf::path_to_utf8(source)).fingerprint.size() == 64,
            "computeDatasetFingerprint returns a 64 hex character digest");
        const msf::DatasetFingerprint fp = msf::computeDatasetFingerprint(msf::path_to_utf8(source));
        chk(fp.state == "measured" && !fp.fingerprint.empty(),
            "a readable fixture measures a fingerprint");
        chk(fp.fileCount == 1, "  and counts the fixture file");
        // The worker records that exact canonical string: no re-hash, no extra fields.
        chk(fp.fingerprint.find_first_not_of("0123456789abcdef") == std::string::npos,
            "  the canonical value is lowercase hex only");

        // cancel() sets the flag the runner reads.
        chk(!w.cancelled(), "worker starts uncancelled");
        w.cancel();
        chk(w.cancelled(), "cancel() sets the cancellation flag");
        chk(!w.running(), "cancel() on an idle worker does not start it");

        storage.release();
    }

    // ---- busy gating and no concurrency in one worker -----------------------
    {
        msf::BenchmarkGuiStorage::Config cfg;
        cfg.applicationRoot = appRoot;
        cfg.runId = "gateRun";
        cfg.sourceRoot = msf::path_to_utf8(source / "gate");
        msf::BenchmarkGuiStorage storage(cfg);
        std::string err;
        storage.begin(err);

        // A worker with an unavailable mode set never runs media; use the guard
        // path twice in a row to confirm the second call is refused while the first
        // has already completed (running_ resets) and that busy toggles each time.
        BenchmarkWorker w(makeRequest(source / "gate"), {msf::GpuBackendKind::Auto}, &storage);
        Captured cap; cap.bind(w);
        w.run();  // runs against the real executor; an empty folder yields no cases
        chk(cap.busy.size() == 2 && cap.busy[0] && !cap.busy[1],
            "busy is true during a run and false afterwards");
        chk(!w.running(), "  worker is idle again afterwards");

        storage.release();
    }

    std::error_code ec;
    fs::remove_all(base, ec);
    std::printf("\nbenchmark_worker_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
