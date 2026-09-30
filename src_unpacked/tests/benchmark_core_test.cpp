// S2 Run/Suite Benchmark Core tests.
//
// These tests drive the runner through an injected fake executor rather than the
// production scan path. That is the point of the executor boundary: ordering,
// aggregation, cancellation and result preservation are the S2 contract, and
// none of them should need a real media scan to be verified. The production
// adapter's own per-file behaviour is exercised by the S1/S2 CLI runs instead.
//
// The three modes below are a TEST CONTRACT, not a settled final policy.

#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "benchmark_core.h"
#include "index_manager.h"

namespace {

int gChecks = 0, gFails = 0;
void chk(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) { ++gFails; std::printf("  [F] %s\n", what.c_str()); }
    else      { std::printf("  [ok] %s\n", what.c_str()); }
}

// What the fake should do for a given (file, mode) pair.
struct Behaviour {
    msf::BenchmarkStatus status = msf::BenchmarkStatus::Success;
    msf::GpuBackendKind effective = msf::GpuBackendKind::Cpu;
    bool unavailable = false;   // modeAvailable() -> false  (Skipped)
    bool throwNow = false;      // executor throws
    double elapsedMs = 1.0;
};

class FakeExecutor : public msf::BenchmarkExecutor {
public:
    std::vector<msf::BenchmarkFileItem> files;
    // (filePath, modeName) -> behaviour
    std::map<std::string, Behaviour> plan;
    // Records the exact execution order, so mode interleaving can be detected.
    std::vector<std::string> order;
    int cancelAfterCalls = -1;   // isCancelled() turns true after N runMode calls
    int runModeCalls = 0;
    int finishCalls = 0;
    bool finishedOk = false;

    static std::string key(const std::string& file, msf::GpuBackendKind m) {
        return file + "|" + msf::gpuBackendKindName(m);
    }

    std::vector<msf::BenchmarkFileItem> discover(const msf::BenchmarkRequest&) override {
        return files;
    }
    bool modeAvailable(msf::GpuBackendKind m) const override {
        if (m == msf::GpuBackendKind::Cpu) return true;
        for (const auto& f : files) { auto it = plan.find(key(f.path, m));
            if (it != plan.end() && it->second.unavailable) return false; }
        return true;
    }
    msf::GpuBackendKind resolveEffectiveMode(msf::GpuBackendKind m) const override {
        if (m == msf::GpuBackendKind::Cpu) return msf::GpuBackendKind::Cpu;
        for (const auto& f : files) { auto it = plan.find(key(f.path, m));
            if (it != plan.end()) return it->second.effective; }
        return msf::GpuBackendKind::Cuda;
    }
    void runMode(const msf::BenchmarkRequest&, const msf::BenchmarkFileItem& file,
                 msf::GpuBackendKind m, msf::BenchmarkModeResult& out) override {
        order.push_back(file.path + "|" + msf::gpuBackendKindName(m));
        ++runModeCalls;
        if (cancelAfterCalls >= 0 && runModeCalls > cancelAfterCalls) {
            out.status = msf::BenchmarkStatus::Cancelled;
            out.completed = true;
            out.errorMessage = "cancelled";
            return;
        }
        Behaviour b;
        auto it = plan.find(key(file.path, m));
        if (it != plan.end()) b = it->second;
        if (b.throwNow) throw std::runtime_error("injected failure");
        out.effectiveMode = b.effective;
        out.elapsedMs = b.elapsedMs;
        out.completed = true;
        out.started = true;
        out.status = b.status;
        if (b.status == msf::BenchmarkStatus::Failed) out.errorMessage = "injected";
        out.summary.scanned = 1;
        out.summary.analyzed = 1;
    }
    void onRunFinished(const msf::BenchmarkRun& r) override {
        ++finishCalls;
        finishedOk = (r.status != msf::BenchmarkStatus::Success) || !r.cases.empty();
    }
};

msf::BenchmarkFileItem file(const std::string& p, msf::MediaKind k = msf::MediaKind::Image) {
    msf::BenchmarkFileItem f; f.path = p; f.media = k; f.sizeBytes = 100; return f;
}

msf::BenchmarkRequest request(std::string root = "D:\\Media") {
    msf::BenchmarkRequest r;
    r.sourceRoot = std::move(root);
    r.applicationDirectory = "C:\\app";
    r.suiteId = "suite-1";
    r.buildVersion = "test-version";
    r.mediaScope = msf::MediaScope::All;
    return r;
}

} // namespace

int main() {
    std::printf("S2 benchmark core selfcheck\n\n");

    // ---- aggregate precedence (C) -----------------------------------------
    {
        std::vector<msf::BenchmarkModeResult> v(3);
        v[0].status = msf::BenchmarkStatus::Success;
        v[1].status = msf::BenchmarkStatus::Success;
        v[2].status = msf::BenchmarkStatus::Success;
        chk(msf::aggregateStatus(v) == msf::BenchmarkStatus::Success, "all Success -> Success");

        v[1].status = msf::BenchmarkStatus::Failed;
        chk(msf::aggregateStatus(v) == msf::BenchmarkStatus::Failed, "Failed + Success -> Failed");

        v[0].status = msf::BenchmarkStatus::Cancelled;
        chk(msf::aggregateStatus(v) == msf::BenchmarkStatus::Cancelled, "Cancelled + Failed -> Cancelled");

        for (auto& m : v) m.status = msf::BenchmarkStatus::Skipped;
        chk(msf::aggregateStatus(v) == msf::BenchmarkStatus::Skipped, "all Skipped -> Skipped");

        chk(msf::aggregateStatus({}) == msf::BenchmarkStatus::Skipped, "empty -> Skipped (not success)");
    }

    // ---- FAILED != CANCELLED (B) -------------------------------------------
    {
        chk(static_cast<int>(msf::BenchmarkStatus::Failed) !=
            static_cast<int>(msf::BenchmarkStatus::Cancelled), "Failed and Cancelled are distinct values");
        chk(std::string(msf::benchmarkStatusName(msf::BenchmarkStatus::Failed)) == "FAILED", "status name FAILED");
        chk(std::string(msf::benchmarkStatusName(msf::BenchmarkStatus::Cancelled)) == "CANCELLED", "status name CANCELLED");
        chk(std::string(msf::benchmarkStatusName(msf::BenchmarkStatus::Skipped)) == "SKIPPED", "status name SKIPPED");
        chk(std::string(msf::benchmarkStatusName(msf::BenchmarkStatus::Success)) == "SUCCESS", "status name SUCCESS");
    }

    // ---- single file, three modes, in order (I / 11) ------------------------
    {
        FakeExecutor ex;
        ex.files = { file("f1.jpg") };
        msf::BenchmarkRunner runner(ex);
        const auto run = runner.run(request(), msf::benchmarkContractModes());

        chk(run.cases.size() == 1, "single file -> one case");
        const auto& c = run.cases[0];
        chk(c.path == "f1.jpg", "  case carries the file path");
        chk(c.caseId == msf::IndexManager::folderId("f1.jpg"), "  case id is derived from the path");
        chk(c.modeResults.size() == 3, "  three mode results");
        chk(c.status == msf::BenchmarkStatus::Success, "  case Success");

        chk(c.modeResults[0].requestedMode == msf::GpuBackendKind::Auto, "  order: AUTO first");
        chk(c.modeResults[1].requestedMode == msf::GpuBackendKind::Cpu,  "  order: CPU second");
        chk(c.modeResults[2].requestedMode == msf::GpuBackendKind::Cuda, "  order: GPU-max (CUDA) third");
        chk(ex.order.size() == 3 && ex.order[0].find("AUTO") != std::string::npos,
            "  executor saw AUTO -> CPU -> CUDA in that order");
    }

    // ---- requested vs effective are recorded separately (G) -----------------
    {
        FakeExecutor ex;
        ex.files = { file("f1.jpg") };
        Behaviour gpu; gpu.effective = msf::GpuBackendKind::Cpu;  // no device
        ex.plan[FakeExecutor::key("f1.jpg", msf::GpuBackendKind::Cuda)] = gpu;
        msf::BenchmarkRunner runner(ex);
        const auto run = runner.run(request(), msf::benchmarkContractModes());
        const auto& m = run.cases[0].modeResults[2];
        chk(m.requestedMode == msf::GpuBackendKind::Cuda, "requested mode is CUDA");
        chk(m.effectiveMode == msf::GpuBackendKind::Cpu, "  effective mode resolved down to CPU");
        chk(m.requestedMode != m.effectiveMode, "  requested and effective are not conflated");
    }

    // ---- multi file: no interleaving across files ---------------------------
    {
        FakeExecutor ex;
        ex.files = { file("a.jpg"), file("b.mp4", msf::MediaKind::Video), file("c.jpg") };
        msf::BenchmarkRunner runner(ex);
        const auto run = runner.run(request(), msf::benchmarkContractModes());
        chk(run.cases.size() == 3, "multi file -> three cases");
        chk(ex.order.size() == 9, "  nine executions (3 files x 3 modes)");

        bool grouped = true;
        for (int i = 0; i < 9; i += 3) {
            const std::string f = ex.order[i].substr(0, ex.order[i].find('|'));
            if (ex.order[i+1].substr(0, ex.order[i+1].find('|')) != f) grouped = false;
            if (ex.order[i+2].substr(0, ex.order[i+2].find('|')) != f) grouped = false;
        }
        chk(grouped, "  all three modes of a file complete before the next file");
        chk(run.filesCompleted == 3, "  filesCompleted counted");
    }

    // ---- one mode fails, remaining modes still run (C / 11) -----------------
    {
        FakeExecutor ex;
        ex.files = { file("f1.jpg") };
        Behaviour bad; bad.status = msf::BenchmarkStatus::Failed; bad.effective = msf::GpuBackendKind::Cpu;
        ex.plan[FakeExecutor::key("f1.jpg", msf::GpuBackendKind::Cpu)] = bad;
        msf::BenchmarkRunner runner(ex);
        const auto run = runner.run(request(), msf::benchmarkContractModes());
        const auto& c = run.cases[0];
        chk(ex.runModeCalls == 3, "all three modes executed despite the failure");
        chk(c.status == msf::BenchmarkStatus::Failed, "case -> Failed");
        chk(c.modeResults[1].status == msf::BenchmarkStatus::Failed, "  the failing mode is Failed");
        chk(c.modeResults[2].status == msf::BenchmarkStatus::Success, "  the later mode still Success");
        chk(!c.errorMessage.empty(), "  case error message captured");
        chk(run.status == msf::BenchmarkStatus::Failed, "run -> Failed");
    }

    // ---- information preservation (11) -------------------------------------
    {
        FakeExecutor ex;
        ex.files = { file("f1.jpg") };
        Behaviour bad; bad.status = msf::BenchmarkStatus::Failed;
        ex.plan[FakeExecutor::key("f1.jpg", msf::GpuBackendKind::Cpu)] = bad;
        Behaviour can; can.status = msf::BenchmarkStatus::Cancelled;
        ex.plan[FakeExecutor::key("f1.jpg", msf::GpuBackendKind::Cuda)] = can;
        msf::BenchmarkRunner runner(ex);
        const auto run = runner.run(request(), msf::benchmarkContractModes());
        const auto& c = run.cases[0];
        chk(c.status == msf::BenchmarkStatus::Cancelled, "aggregate is Cancelled");
        chk(c.modeResults[0].status == msf::BenchmarkStatus::Success,  "  AUTO Success preserved verbatim");
        chk(c.modeResults[1].status == msf::BenchmarkStatus::Failed,    "  CPU Failed preserved verbatim");
        chk(c.modeResults[2].status == msf::BenchmarkStatus::Cancelled, "  GPU-max Cancelled preserved verbatim");
    }

    // ---- cancellation (3 / 11) ---------------------------------------------
    {
        FakeExecutor ex;
        ex.files = { file("a.jpg"), file("b.jpg") };
        ex.cancelAfterCalls = 1;  // first mode of first file succeeds, then cancel
        msf::BenchmarkRequest req = request();
        req.isCancelled = [&ex]() { return ex.runModeCalls > 1; };
        msf::BenchmarkRunner runner(ex);
        const auto run = runner.run(req, msf::benchmarkContractModes());

        chk(run.status == msf::BenchmarkStatus::Cancelled, "cancelled run -> Cancelled");
        chk(run.cases.size() == 1, "  only the started case exists");
        const auto& c = run.cases[0];
        chk(c.status == msf::BenchmarkStatus::Cancelled, "  case -> Cancelled");
        chk(c.modeResults[0].status == msf::BenchmarkStatus::Success,   "completed mode preserved");
        chk(c.modeResults[1].status == msf::BenchmarkStatus::Cancelled, "in-flight mode -> Cancelled");

        // The contract distinguishes a mode that was interrupted from one that
        // never started. Only the interrupted one is Cancelled.
        chk(c.modeResults[2].status == msf::BenchmarkStatus::Skipped,
            "  not-started mode -> Skipped (not Cancelled)");
        chk(c.modeResults[2].started == false, "    and it is not marked as started");
        chk(c.modeResults[2].completed == false, "    nor completed");

        // Exactly one Cancelled, and the aggregate did not erase anything.
        int cancelled = 0, skipped = 0;
        for (const auto& m : c.modeResults) {
            if (m.status == msf::BenchmarkStatus::Cancelled) ++cancelled;
            if (m.status == msf::BenchmarkStatus::Skipped)   ++skipped;
        }
        chk(cancelled == 1, "  exactly one Cancelled mode recorded");
        chk(skipped == 1,   "  one Skipped mode recorded for the unstarted mode");
        chk(c.modeResults.size() == 3, "  all three mode results preserved after aggregation");
    }

    // ---- unavailable mode -> Skipped, never success (A) ---------------------
    {
        FakeExecutor ex;
        ex.files = { file("f1.jpg") };
        Behaviour na; na.unavailable = true;
        ex.plan[FakeExecutor::key("f1.jpg", msf::GpuBackendKind::Cuda)] = na;
        msf::BenchmarkRunner runner(ex);
        const auto run = runner.run(request(), msf::benchmarkContractModes());
        const auto& c = run.cases[0];
        chk(c.modeResults[2].status == msf::BenchmarkStatus::Skipped, "unavailable mode -> Skipped");
        chk(c.modeResults[2].status != msf::BenchmarkStatus::Success, "  not recorded as success");
        chk(ex.runModeCalls == 2, "  unavailable mode was not executed");
        chk(c.status == msf::BenchmarkStatus::Success, "  case still Success via the executed modes");
    }

    // ---- all modes unavailable -> case Skipped ------------------------------
    {
        FakeExecutor ex;
        ex.files = { file("f1.jpg") };
        msf::BenchmarkRequest req = request();
        std::vector<msf::GpuBackendKind> none;  // empty mode list
        msf::BenchmarkRunner runner(ex);
        const auto run = runner.run(req, none);
        chk(run.cases.size() == 1, "empty mode list still yields a case record");
        chk(run.cases[0].status == msf::BenchmarkStatus::Skipped, "  case -> Skipped");
        chk(run.cases[0].modeResults.empty(), "  no mode results");
    }

    // ---- executor throwing does not kill the run ---------------------------
    {
        FakeExecutor ex;
        ex.files = { file("f1.jpg") };
        Behaviour boom; boom.throwNow = true;
        ex.plan[FakeExecutor::key("f1.jpg", msf::GpuBackendKind::Cpu)] = boom;
        msf::BenchmarkRunner runner(ex);
        const auto run = runner.run(request(), msf::benchmarkContractModes());
        chk(run.cases.size() == 1, "a throwing executor still produces a case");
        chk(run.cases[0].status == msf::BenchmarkStatus::Failed, "  and the case is Failed, not a crash");
    }

    // ---- timing: case elapsed is the sum of mode elapsed (4) ----------------
    {
        FakeExecutor ex;
        ex.files = { file("f1.jpg") };
        Behaviour a; a.elapsedMs = 2.0; ex.plan[FakeExecutor::key("f1.jpg", msf::GpuBackendKind::Auto)] = a;
        Behaviour b; b.elapsedMs = 3.0; ex.plan[FakeExecutor::key("f1.jpg", msf::GpuBackendKind::Cpu)] = b;
        Behaviour c; c.elapsedMs = 5.0; ex.plan[FakeExecutor::key("f1.jpg", msf::GpuBackendKind::Cuda)] = c;
        msf::BenchmarkRunner runner(ex);
        const auto run = runner.run(request(), msf::benchmarkContractModes());
        const double sum = run.cases[0].modeResults[0].elapsedMs
                          + run.cases[0].modeResults[1].elapsedMs
                          + run.cases[0].modeResults[2].elapsedMs;
        chk(sum == 10.0, "mode elapsed recorded per mode");
        chk(run.cases[0].elapsedMs == sum, "case elapsed is the sum of its modes");
    }

    // ---- onCaseComplete hook is called once per case (journal seam) ----------
    {
        FakeExecutor ex;
        ex.files = { file("a.jpg"), file("b.jpg") };
        int calls = 0;
        msf::BenchmarkRequest req = request();
        req.onCaseComplete = [&calls](const msf::BenchmarkCaseResult&) { ++calls; };
        msf::BenchmarkRunner runner(ex);
        runner.run(req, msf::benchmarkContractModes());
        chk(calls == 2, "onCaseComplete called once per completed case");
        chk(ex.finishCalls == 1, "onRunFinished called once");
    }

    // ---- media scope travels into the run ----------------------------------
    {
        FakeExecutor ex;
        ex.files = { file("a.jpg") };
        msf::BenchmarkRequest req = request();
        req.mediaScope = msf::MediaScope::Videos;
        msf::BenchmarkRunner runner(ex);
        const auto run = runner.run(req, msf::benchmarkContractModes());
        chk(run.mediaScope == msf::MediaScope::Videos, "media scope carried into the run");
        chk(run.scanImages == false && run.scanVideos == true, "  videos scope maps to the existing pair");
    }

    // ---- determinism: same input, same output (10) --------------------------
    {
        FakeExecutor a, b;
        a.files = { file("x.jpg"), file("y.jpg") };
        b.files = a.files;
        msf::BenchmarkRunner ra(a); msf::BenchmarkRunner rb(b);
        const auto r1 = ra.run(request(), msf::benchmarkContractModes());
        const auto r2 = rb.run(request(), msf::benchmarkContractModes());
        bool same = r1.cases.size() == r2.cases.size();
        for (std::size_t i = 0; same && i < r1.cases.size(); ++i)
            same = r1.cases[i].status == r2.cases[i].status
                && r1.cases[i].modeResults.size() == r2.cases[i].modeResults.size();
        chk(same && a.order == b.order, "identical inputs produce identical results and order");
    }

    std::printf("\nbenchmark_core_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
