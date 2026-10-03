// S4 GUI storage tests: path policy, per-mode snapshots, atomic replacement,
// preservation of unselected modes, and instance exclusion via the S3 suite lock.
//
// This is the storage layer only. No benchmark is executed here and no media is
// decoded; the BenchmarkRun values are built directly so every branch of the
// per-mode aggregation is reachable and deterministic.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "benchmark_core.h"
#include "benchmark_gui_store.h"
#include "benchmark_journal.h"   // jsonFieldString/Number: the product's own flat-JSON readers
#include "benchmark_store.h"
#include "path_utils.h"

namespace {

int gChecks = 0, gFails = 0;
void chk(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) { ++gFails; std::printf("  [F] %s\n", what.c_str()); }
    else      { std::printf("  [ok] %s\n", what.c_str()); }
}

namespace fs = std::filesystem;

std::string field(const std::string& json, const std::string& key) {
    std::string v;
    msf::jsonFieldString(json, key, v);
    return v;
}

double num(const std::string& json, const std::string& key) {
    double v = 0.0;
    msf::jsonFieldNumber(json, key, v);
    return v;
}

// One mode result for one case.
msf::BenchmarkModeResult mres(msf::GpuBackendKind req, msf::GpuBackendKind eff,
                              msf::BenchmarkStatus st, double ms) {
    msf::BenchmarkModeResult m;
    m.requestedMode = req;
    m.effectiveMode = eff;
    m.status = st;
    m.started = (st != msf::BenchmarkStatus::Skipped);
    m.completed = (st != msf::BenchmarkStatus::Skipped && st != msf::BenchmarkStatus::Cancelled);
    m.elapsedMs = ms;
    m.summary.scanned = 1;
    m.summary.analyzed = 1;
    m.summary.groups = 1;
    if (st == msf::BenchmarkStatus::Failed) m.errorMessage = "index open failed";
    if (st == msf::BenchmarkStatus::Cancelled) m.errorMessage = "cancelled by user";
    return m;
}

msf::BenchmarkCaseResult kase(const std::string& id, const std::string& path,
                              std::vector<msf::BenchmarkModeResult> modes) {
    msf::BenchmarkCaseResult c;
    c.caseId = id;
    c.path = path;
    c.media = msf::MediaKind::Image;
    c.modeResults = std::move(modes);
    for (const auto& m : c.modeResults) c.elapsedMs += m.elapsedMs;
    c.status = msf::aggregateStatus(c.modeResults);
    c.started = true;
    c.completed = true;
    if (c.status == msf::BenchmarkStatus::Failed) c.errorMessage = "index open failed";
    return c;
}

msf::BenchmarkRun runWith(const std::string& root, std::vector<msf::BenchmarkCaseResult> cases) {
    msf::BenchmarkRun r;
    r.runId = "r1";
    r.sourceRoot = root;
    r.sourceRootLabel = "TestSet";
    r.sourceRootId = "abcdef0123456789";
    r.datasetFingerprint = "fp-gui";
    r.buildVersion = "0.9.4.43";
    r.mediaScope = msf::MediaScope::All;
    r.startedAt = "2026-09-30T00:00:00Z";
    r.completedAt = "2026-09-30T00:01:00Z";
    r.cases = std::move(cases);
    r.status = msf::BenchmarkStatus::Success;
    for (const auto& c : r.cases) if (c.status == msf::BenchmarkStatus::Failed) r.status = msf::BenchmarkStatus::Failed;
    return r;
}

fs::path scratch() {
    const fs::path p = fs::temp_directory_path() / "msf_s4_gui_store_test";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p / "approot", ec);
    fs::create_directories(p / "source", ec);
    return p;
}

std::set<std::string> treeOf(const fs::path& root) {
    std::set<std::string> out;
    std::error_code ec;
    if (!fs::exists(root, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        std::string rel = msf::path_to_utf8(fs::relative(it->path(), root));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        out.insert(rel);
    }
    return out;
}

const char* kThreeModes = "AUTO+CPU+GPU-max";
const char* kTwoModes = "AUTO+GPU-max";

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("S4 GUI benchmark store selfcheck\n\n");

    const fs::path base = scratch();
    const std::string approot = msf::path_to_utf8(base / "approot");
    const std::string source = msf::path_to_utf8(base / "source");
    const std::string srcLabel = msf::sanitizeSourceLabel(source);
    const std::string srcId = msf::shortRootId(source);
    const std::string suiteDirName = srcLabel + "_" + srcId;

    // ---- path policy -------------------------------------------------------
    {
        const auto p = msf::benchmarkGuiPaths(approot, source);
        chk(p.guiRoot == approot + "/Benchmark/GUI", "gui root is <appRoot>/Benchmark/GUI");
        chk(p.suiteDir == approot + "/Benchmark/GUI/" + suiteDirName,
            "suite dir is <label>_<short-root-id>");
        chk(p.autoJson == p.suiteDir + "/auto.json",     "auto.json path");
        chk(p.cpuJson == p.suiteDir + "/cpu.json",       "cpu.json path");
        chk(p.gpuMaxJson == p.suiteDir + "/gpu-max.json","gpu-max.json path");
        chk(p.suiteLock == p.suiteDir + "/suite.lock",   "suite lock path");
        chk(p.snapshotPath(msf::GpuBackendKind::Auto) == p.autoJson, "  snapshotPath(AUTO)");
        chk(p.snapshotPath(msf::GpuBackendKind::Cpu) == p.cpuJson,   "  snapshotPath(CPU)");
        chk(p.snapshotPath(msf::GpuBackendKind::Cuda) == p.gpuMaxJson, "  snapshotPath(GPU-max)");

        // A raw full source path must never become a folder or file name.
        chk(suiteDirName.find("TestSet") == std::string::npos, "  no raw basename leaked");
        chk(suiteDirName.find(':') == std::string::npos,        "  no drive colon leaked");
        chk(suiteDirName.find('\\') == std::string::npos,       "  no parent separator leaked");
        chk(suiteDirName.find(base.filename().string()) == std::string::npos,
            "  no parent directory name leaked");
        chk(msf::ensureBenchmarkGuiSuiteDir(p), "suite dir created");
        chk(fs::is_directory(msf::path_from_utf8(p.runtimeDir)), "  runtime dir created");

        // Same source -> same suite; different source -> different suite.
        chk(msf::benchmarkGuiPaths(approot, source).suiteDir == p.suiteDir, "stable across calls");
        chk(msf::benchmarkGuiPaths(approot, source + "/other").suiteDir != p.suiteDir,
            "a different source gets a different suite");
    }

    // ---- runtime mode index directories ------------------------------------
    {
        const auto p = msf::benchmarkGuiPaths(approot, source);
        const std::string a = msf::benchmarkGuiModeIndexApplicationDir(p, "r1", msf::GpuBackendKind::Auto);
        const std::string c = msf::benchmarkGuiModeIndexApplicationDir(p, "r1", msf::GpuBackendKind::Cpu);
        const std::string g = msf::benchmarkGuiModeIndexApplicationDir(p, "r1", msf::GpuBackendKind::Cuda);
        chk(a.find("/runtime/run-r1/auto") != std::string::npos,   "auto index dir under runtime");
        chk(c.find("/runtime/run-r1/cpu") != std::string::npos,    "cpu index dir under runtime");
        chk(g.find("/runtime/run-r1/gpu-max") != std::string::npos,"gpu-max index dir under runtime");
        chk(a != c && c != g, "each mode has its own index directory");
        chk(a.rfind(p.suiteDir, 0) == 0, "index dirs live inside the GUI suite, not beside production");
        chk(msf::cleanupBenchmarkGuiRunRuntime(p, "r1"), "runtime cleanup succeeds");
        chk(msf::cleanupBenchmarkGuiRunRuntime(p, "r1"), "cleanup of an absent runtime is a success");
    }

    // ---- three snapshots, all three modes -----------------------------------
    {
        const auto p = msf::benchmarkGuiPaths(approot, source);
        auto run = runWith(source, {
            kase("c1", source + "/a.jpg", {
                mres(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 2.0),
                mres(msf::GpuBackendKind::Cpu,  msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 3.0),
                mres(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cuda, msf::BenchmarkStatus::Success, 5.0),
            }),
            kase("c2", source + "/b.jpg", {
                mres(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 2.0),
                mres(msf::GpuBackendKind::Cpu,  msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 3.0),
                mres(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cuda, msf::BenchmarkStatus::Success, 5.0),
            }),
        });
        const auto res = msf::writeBenchmarkGuiSnapshots(p, run);
        chk(res.ok && res.error.empty(), "writing snapshots succeeds");
        chk(res.written.size() == 3, "three modes written");

        const std::string autoJ = msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Auto);
        const std::string cpuJ  = msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Cpu);
        const std::string gpuJ  = msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Cuda);
        chk(!autoJ.empty() && !cpuJ.empty() && !gpuJ.empty(), "auto/cpu/gpu-max.json all exist");
        chk(msf::benchmarkGuiSnapshotExists(p, msf::GpuBackendKind::Auto), "  existence check");

        chk(num(autoJ, "snapshotSchemaVersion") == 1.0, "snapshot schema version is its own 1");
        chk(field(autoJ, "appVersion") == "0.9.4.43", "appVersion recorded");
        chk(field(autoJ, "runId") == "r1", "runId recorded");
        chk(field(autoJ, "mode") == "auto", "mode recorded");
        chk(field(cpuJ,  "mode") == "cpu",  "  cpu file says cpu");
        chk(gpuJ.find("\"mode\":\"gpu-max\"") != std::string::npos, "  gpu file says gpu-max");
        chk(field(autoJ, "mediaScope") == "all", "actual mediaScope stored");
        chk(field(autoJ, "sourceRoot") == source, "canonical sourceRoot in metadata");
        chk(field(autoJ, "sourceRootLabel") == "TestSet", "sourceRootLabel recorded");
        chk(field(autoJ, "sourceRootId") == "abcdef0123456789", "sourceRootId recorded");
        chk(field(autoJ, "datasetFingerprint") == "fp-gui", "datasetFingerprint recorded");
        chk(field(autoJ, "startedAt") == "2026-09-30T00:00:00Z", "startedAt recorded");
        chk(field(autoJ, "completedAt") == "2026-09-30T00:01:00Z", "completedAt recorded");
        chk(field(autoJ, "status") == "SUCCESS", "status recorded");
        chk(num(autoJ, "casesWithMode") == 2.0, "casesWithMode counted for this mode only");
        chk(num(autoJ, "elapsedMs") == 4.0, "elapsed is this mode's elapsed (2+2)");
        chk(autoJ.find("\"c1\"") != std::string::npos, "per-case identity kept");
        chk(autoJ.find("\"c2\"") != std::string::npos, "  both cases present");
        chk(autoJ.find("\"analyzed\":2") != std::string::npos, "summary aggregated for this mode");

        // The three files share one suiteId.
        const std::string sid = field(autoJ, "suiteId");
        chk(!sid.empty() && field(cpuJ, "suiteId") == sid && field(gpuJ, "suiteId") == sid,
            "one suiteId shared across the three mode files");
    }

    // ---- per-mode aggregate is recomputed, never copied from the Case -------
    {
        const auto p = msf::benchmarkGuiPaths(approot, source + "/mixed");
        auto run = runWith(source + "/mixed", {
            kase("c1", source + "/mixed/a.jpg", {
                mres(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 1.0),
                mres(msf::GpuBackendKind::Cpu,  msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Failed,  2.0),
                mres(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cuda, msf::BenchmarkStatus::Success, 3.0),
            }),
        });
        chk(run.cases[0].status == msf::BenchmarkStatus::Failed, "  the Case aggregate is Failed");

        chk(msf::writeBenchmarkGuiSnapshots(p, run).ok, "snapshots written");
        chk(field(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Auto), "status") == "SUCCESS",
            "auto.json is Success");
        chk(field(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Cpu), "status") == "FAILED",
            "cpu.json is Failed");
        chk(field(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Cuda), "status") == "SUCCESS",
            "gpu-max.json is Success");
        chk(field(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Cpu), "errorMessage")
                == "index open failed",
            "  cpu.json keeps the failing mode's message");
        chk(num(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Auto), "casesFailed") == 0.0,
            "  auto.json counts no failures");
        chk(num(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Cpu), "casesFailed") == 1.0,
            "  cpu.json counts exactly one failure");
    }

    // ---- cancellation stays distinct from failure --------------------------
    {
        const auto p = msf::benchmarkGuiPaths(approot, source + "/cancel");
        auto run = runWith(source + "/cancel", {
            kase("c1", source + "/cancel/a.jpg", {
                mres(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success,   1.0),
                mres(msf::GpuBackendKind::Cpu,  msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Cancelled, 0.0),
                mres(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cuda, msf::BenchmarkStatus::Skipped,   0.0),
            }),
        });
        chk(run.cases[0].status == msf::BenchmarkStatus::Cancelled, "  the Case aggregate is Cancelled");
        chk(msf::writeBenchmarkGuiSnapshots(p, run).ok, "snapshots written");
        chk(field(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Cpu), "status") == "CANCELLED",
            "cpu.json is Cancelled, not Failed");
        const std::string gpuJ = msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Cuda);
        chk(field(gpuJ, "status") == "SKIPPED", "gpu-max.json is SKIPPED, not SUCCESS");
        chk(num(gpuJ, "casesSkipped") == 1.0, "  an unmeasured mode is counted as skipped, never 0-success");
        chk(num(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Auto), "casesCompleted") == 1.0,
            "auto.json still records its one completed case");
    }

    // ---- unselected mode keeps its previous snapshot ------------------------
    {
        const auto p = msf::benchmarkGuiPaths(approot, source + "/subset");
        const std::string src2 = source + "/subset";
        // First run: all three modes.
        chk(msf::writeBenchmarkGuiSnapshots(
                p, runWith(src2, {kase("c1", src2 + "/a.jpg", {
                    mres(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 1.0),
                    mres(msf::GpuBackendKind::Cpu,  msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 2.0),
                    mres(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cuda, msf::BenchmarkStatus::Success, 3.0),
                })})).ok, "first run writes three files");
        const std::string cpuBefore = msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Cpu);

        // Second run: AUTO + GPU-max only. CPU must be preserved untouched.
        auto second = runWith(src2, {kase("c2", src2 + "/b.jpg", {
            mres(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Failed, 9.0),
            mres(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cuda, msf::BenchmarkStatus::Success, 8.0),
        })});
        second.runId = "r2";
        const auto res = msf::writeBenchmarkGuiSnapshots(p, second);
        chk(res.ok && res.written.size() == 2, "second run writes only the two selected modes");
        chk(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Cpu) == cpuBefore,
            "the unselected cpu.json is preserved byte for byte");
        chk(field(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Cpu), "runId") == "r1",
            "  and still describes the earlier run");
        chk(field(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Auto), "status") == "FAILED",
            "auto.json replaced with the new result");
        chk(field(msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Auto), "runId") == "r2",
            "  atomic replace swapped the whole file, not merged into it");
    }

    // ---- a run with no mode results writes nothing --------------------------
    {
        const auto p = msf::benchmarkGuiPaths(approot, source + "/empty");
        msf::BenchmarkRun empty = runWith(source + "/empty", {});
        const auto res = msf::writeBenchmarkGuiSnapshots(p, empty);
        chk(!res.ok, "a run without mode results reports failure instead of writing empty files");
        chk(!res.error.empty(), "  with a reason");
        chk(!msf::benchmarkGuiSnapshotExists(p, msf::GpuBackendKind::Auto), "  no snapshot created");
    }

    // ---- instance exclusion reuses the S3 suite lock -----------------------
    {
        msf::BenchmarkGuiStorage::Config cfg;
        cfg.applicationRoot = approot;
        cfg.runId = "lockA";
        cfg.sourceRoot = source + "/locked";
        cfg.buildVersion = "0.9.4.43";

        msf::BenchmarkGuiStorage first(cfg);
        std::string err;
        chk(first.begin(err), "first instance takes the suite");
        chk(first.active() && !first.isBusy(), "  and is active");

        msf::BenchmarkGuiStorage::Config cfg2 = cfg;
        cfg2.runId = "lockB";
        msf::BenchmarkGuiStorage second(cfg2);
        std::string err2;
        chk(!second.begin(err2), "a second instance on the same suite is refused");
        chk(second.isBusy(), "  reported as busy, not as an I/O failure");
        chk(err2.find("already running") != std::string::npos, "  the reason is shown to the user");
        chk(!second.active(), "  it does not become active");

        // The refused instance must not have disturbed the holder's snapshot.
        const auto held = msf::writeBenchmarkGuiSnapshots(first.paths(),
            runWith(cfg.sourceRoot, {kase("c1", cfg.sourceRoot + "/a.jpg", {
                mres(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu, msf::BenchmarkStatus::Success, 1.0)})}));
        chk(held.ok, "the holder can still write its snapshot");
        const std::string before = msf::readBenchmarkGuiSnapshot(first.paths(), msf::GpuBackendKind::Auto);
        msf::BenchmarkGuiStorage::Config cfg3 = cfg;
        cfg3.runId = "lockC";
        msf::BenchmarkGuiStorage third(cfg3);
        std::string err3;
        third.begin(err3);
        chk(msf::readBenchmarkGuiSnapshot(first.paths(), msf::GpuBackendKind::Auto) == before,
            "the refused attempt left the existing snapshot untouched");

        first.release();
        msf::BenchmarkGuiStorage::Config cfg4 = cfg;
        cfg4.runId = "lockD";
        msf::BenchmarkGuiStorage fourth(cfg4);
        std::string err4;
        chk(fourth.begin(err4), "the suite is lockable again after release");
        fourth.release();

        // Different source -> different suite -> concurrent execution allowed.
        msf::BenchmarkGuiStorage::Config other;
        other.applicationRoot = approot;
        other.runId = "otherA";
        other.sourceRoot = source + "/elsewhere";
        msf::BenchmarkGuiStorage fifth(other);
        std::string err5;
        chk(fifth.begin(err5), "a different source can run at the same time");
        fifth.release();
        third.release();
    }

    // ---- attach() pins the run id and injects the mode index dirs -----------
    {
        msf::BenchmarkGuiStorage::Config cfg;
        cfg.applicationRoot = approot;
        cfg.runId = "attachRun";
        cfg.sourceRoot = source + "/attach";
        cfg.buildVersion = "0.9.4.43";
        msf::BenchmarkGuiStorage s(cfg);
        std::string err;
        chk(s.begin(err), "storage begins");

        msf::BenchmarkRequest req;
        chk(req.modeIndexApplicationDirectory == nullptr, "a bare request has no provider");
        s.attach(req);
        chk(req.runId == "attachRun", "attach pins the caller-supplied run id");
        chk(req.modeIndexApplicationDirectory != nullptr, "per-mode index directory injected");
        const std::string g = req.modeIndexApplicationDirectory(msf::GpuBackendKind::Cuda);
        chk(g.find("/runtime/run-attachRun/gpu-max") != std::string::npos, "  gpu-max under the GUI runtime");
        chk(g.rfind(s.paths().suiteDir, 0) == 0, "  inside the GUI suite, never beside production");

        // attach() must not introduce journal hooks: the GUI has no journal.
        chk(!req.onRunStarted && !req.onCaseComplete && !req.onRunFinished,
            "attach installs no journal hooks");
        s.cleanupRuntime();
    }

    // ---- Resource Policy is recorded, and the field is additive -------------
    {
        const auto p = msf::benchmarkGuiPaths(approot, source + "/policy");
        auto run = runWith(source + "/policy", {kase("c1", source + "/policy/a.jpg", {
            mres(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu, msf::BenchmarkStatus::Success, 1.0),
        })});
        msf::ResourcePolicy pol = msf::make_policy(msf::ResourceMode::Maximum, 90, 90);

        // With a policy supplied.
        chk(msf::writeBenchmarkGuiSnapshots(p, run, "0.9.4.43", &pol).ok, "snapshot written with a policy");
        const std::string j = msf::readBenchmarkGuiSnapshot(p, msf::GpuBackendKind::Auto);
        chk(j.find("\"resourcePolicy\"") != std::string::npos, "the snapshot records the policy");
        chk(j.find("\"cpuPercent\":90") != std::string::npos, "  cpuPercent recorded");
        chk(j.find("\"mode\":1") != std::string::npos, "  preset mode recorded (Maximum=1)");

        // Without one: the block is simply absent, so older snapshots stay readable.
        const auto p2 = msf::benchmarkGuiPaths(approot, source + "/policy2");
        chk(msf::writeBenchmarkGuiSnapshots(p2, run, "0.9.4.43", nullptr).ok, "snapshot written without a policy");
        const std::string j2 = msf::readBenchmarkGuiSnapshot(p2, msf::GpuBackendKind::Auto);
        chk(j2.find("\"resourcePolicy\"") == std::string::npos, "no policy block when none was supplied");
        chk(j2.find("\"mode\":\"auto\"") != std::string::npos, "  and the rest of the document is unchanged");

        // The storage holder records the policy from its config, like the GUI does.
        msf::BenchmarkGuiStorage::Config cfg;
        cfg.applicationRoot = approot;
        cfg.runId = "policyRun";
        cfg.sourceRoot = source + "/policy3";
        cfg.resourcePolicy = pol;
        msf::BenchmarkGuiStorage s(cfg);
        std::string err;
        chk(s.begin(err), "storage begins with a policy configured");
        chk(s.writeSnapshots(runWith(cfg.sourceRoot, {kase("c1", cfg.sourceRoot + "/a.jpg", {
            mres(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu, msf::BenchmarkStatus::Success, 1.0)})})).ok,
            "writeSnapshots succeeds");
        const std::string j3 = msf::readBenchmarkGuiSnapshot(
            msf::benchmarkGuiPaths(approot, cfg.sourceRoot), msf::GpuBackendKind::Auto);
        chk(j3.find("\"cpuPercent\":90") != std::string::npos,
            "the configured policy reaches the snapshot through writeSnapshots");
        s.release();
    }

    // ---- isolation ----------------------------------------------------------
    {
        // A dedicated application root, so the assertions below see only what this
        // one lifecycle produced and cannot be satisfied by an earlier test's suite.
        const fs::path isoApp = base / "isoapp";
        const fs::path isoSrc = base / "isosource";
        fs::create_directories(isoApp);
        fs::create_directories(isoSrc);
        std::ofstream(msf::path_to_utf8(isoSrc / "a.jpg")) << "x";
        const std::string isoRoot = msf::path_to_utf8(isoApp);
        const std::string isoSource = msf::path_to_utf8(isoSrc);
        const std::string isoSuite =
            msf::sanitizeSourceLabel(isoSource) + "_" + msf::shortRootId(isoSource);

        msf::BenchmarkGuiStorage::Config cfg;
        cfg.applicationRoot = isoRoot;
        cfg.runId = "isoRun";
        cfg.sourceRoot = isoSource;
        cfg.buildVersion = "0.9.4.43";
        msf::BenchmarkGuiStorage s(cfg);
        std::string err;
        chk(s.begin(err), "isolated storage begins");

        msf::BenchmarkRequest req;
        s.attach(req);
        for (msf::GpuBackendKind m : {msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,
                                      msf::GpuBackendKind::Cuda}) {
            std::error_code ec2;
            fs::create_directories(
                msf::path_from_utf8(req.modeIndexApplicationDirectory(m) + "/Index/fake"), ec2);
        }
        s.writeSnapshots(runWith(isoSource, {kase("c1", isoSource + "/a.jpg", {
            mres(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 1.0),
            mres(msf::GpuBackendKind::Cpu,  msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 2.0),
            mres(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cuda, msf::BenchmarkStatus::Success, 3.0),
        })}));
        chk(s.cleanupRuntime(), "runtime cleanup succeeds");
        s.release();

        // 1. the scanned source folder still holds only the file it had
        const auto srcTree = treeOf(isoSrc);
        chk(srcTree.size() == 1 && srcTree.count("a.jpg") == 1,
            "source folder has no benchmark artifacts");

        // 2. every GUI artifact is inside one <label>_<shortid> suite directory
        const auto guiTree = treeOf(isoApp / "Benchmark" / "GUI");
        chk(!guiTree.empty(), "GUI artifacts exist");
        // treeOf() also lists directories, so the suite directory itself appears
        // as a bare entry; everything else must sit underneath it.
        bool onlySuite = true;
        for (const auto& rel : guiTree) {
            if (rel != isoSuite && rel.rfind(isoSuite + "/", 0) != 0) onlySuite = false;
        }
        chk(onlySuite, "  all under one <label>_<shortid> suite dir");

        // 3. the Console tree was never created
        chk(!fs::exists(isoApp / "Benchmark" / "Console"),
            "the GUI never creates Benchmark/Console");
        chk(treeOf(isoApp / "Benchmark" / "Console").empty(), "  and writes nothing there");

        // 4. cleanup removed only the runtime tree; snapshots survive
        bool runtimeGone = true, snapsPresent = false;
        for (const auto& rel : guiTree) {
            if (rel.find("/runtime/") != std::string::npos) runtimeGone = false;
            if (rel == isoSuite + "/auto.json") snapsPresent = true;
        }
        chk(runtimeGone, "  the runtime tree is gone after cleanup");
        chk(snapsPresent, "  auto.json survived runtime cleanup");
        chk(!fs::exists(isoApp / "Benchmark" / "GUI" / isoSuite / "runtime" / "run-isoRun"),
            "  run runtime directory removed");

        // 5. the production index location was never touched
        chk(!fs::exists(isoApp / "Index"), "no index created beside production");
        chk(!fs::exists(isoApp / "Benchmark" / "GUI" / "runs.jsonl"),
            "no Console journal created by the GUI");
    }

    std::error_code ec;
    fs::remove_all(base, ec);
    std::printf("\nbenchmark_gui_store_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
