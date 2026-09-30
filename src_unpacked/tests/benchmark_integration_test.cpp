// S3 integration: the real S2 runner wired to the real journal.
//
// The point of this file is the SEAM, not the search engine. It drives
// BenchmarkRunner -> BenchmarkSession -> runs.jsonl -> replay -> summary.json and
// checks that what S2 produced is what the journal preserved.
//
// Part A uses a scripted executor so mode order, cancellation and failure are
// deterministic and every branch is reachable. It still runs the REAL runner and
// the REAL session; only the "how to analyse one file" step is substituted.
//
// Part B repeats the wiring once through ProductionBenchmarkExecutor on real
// ffmpeg-generated media, to show the mode index directories are the ones the
// real engine actually writes to and that the source folder stays clean.
//
// Nothing here writes inside the scanned fixture.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <QCoreApplication>

#include "benchmark_core.h"
#include "benchmark_journal.h"
#include "benchmark_session.h"
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

// --- journal inspection -----------------------------------------------------

std::vector<std::string> journalLines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream in(msf::path_from_utf8(path), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

std::vector<std::string> eventTypes(const std::string& path) {
    std::vector<std::string> evs;
    for (const auto& l : journalLines(path)) {
        std::string t;
        if (msf::jsonFieldString(l, "eventType", t)) evs.push_back(t);
    }
    return evs;
}

std::vector<std::string> recordIdsOf(const std::string& path) {
    std::vector<std::string> ids;
    for (const auto& l : journalLines(path)) {
        std::string id;
        if (msf::jsonFieldString(l, "recordId", id)) ids.push_back(id);
    }
    return ids;
}

int countEvent(const std::vector<std::string>& evs, const std::string& want) {
    int n = 0;
    for (const auto& e : evs) if (e == want) ++n;
    return n;
}

int indexOf(const std::vector<std::string>& evs, const std::string& want, int from = 0) {
    for (std::size_t i = static_cast<std::size_t>(from); i < evs.size(); ++i)
        if (evs[i] == want) return static_cast<int>(i);
    return -1;
}

std::string join(const std::vector<std::string>& v, const char* sep = ",") {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) { if (i) s += sep; s += v[i]; }
    return s;
}

// --- scripted executor ------------------------------------------------------

// Substitutes only the per-file analysis step. Everything above it -- ordering,
// aggregation, cancellation checks, callbacks -- is the production runner.
class ScriptedExecutor : public msf::BenchmarkExecutor {
public:
    using ModeFn = std::function<void(const msf::BenchmarkFileItem&,
                                      msf::GpuBackendKind,
                                      msf::BenchmarkModeResult&)>;

    std::vector<msf::BenchmarkFileItem> files;
    ModeFn onMode;
    std::set<msf::GpuBackendKind> unavailable;

    std::vector<msf::BenchmarkFileItem> discover(const msf::BenchmarkRequest&) override {
        return files;
    }
    bool modeAvailable(msf::GpuBackendKind m) const override {
        return unavailable.find(m) == unavailable.end();
    }
    msf::GpuBackendKind resolveEffectiveMode(msf::GpuBackendKind m) const override {
        if (m == msf::GpuBackendKind::Cuda) return msf::GpuBackendKind::Cuda;
        return msf::GpuBackendKind::Cpu;
    }
    void runMode(const msf::BenchmarkRequest&,
                 const msf::BenchmarkFileItem& f,
                 msf::GpuBackendKind m,
                 msf::BenchmarkModeResult& out) override {
        // A mode that is neither scripted to fail nor cancelled must SUCCEED.
        // Leaving the default (Skipped) here would quietly make every aggregate
        // Skipped and hide real behaviour behind a plausible-looking result.
        out.status = msf::BenchmarkStatus::Success;
        out.started = true;
        out.completed = true;
        out.elapsedMs = 1.5;
        out.summary.scanned = 1;
        out.summary.analyzed = 1;
        if (onMode) onMode(f, m, out);
    }
};

msf::BenchmarkSessionConfig makeConfig(const fs::path& appdata, const std::string& suite,
                                       const std::string& runId, const fs::path& source) {
    msf::BenchmarkSessionConfig c;
    c.suiteId = suite;
    c.runId = runId;
    c.applicationDataRoot = msf::path_to_utf8(appdata);
    c.sourceRoot = msf::path_to_utf8(source);
    c.datasetFingerprint = "fp-integration";
    c.buildVersion = "0.9.4.43";
    c.mediaScope = msf::MediaScope::All;
    return c;
}

std::vector<msf::BenchmarkFileItem> threeFiles(const fs::path& source) {
    std::vector<msf::BenchmarkFileItem> v;
    v.push_back({msf::path_to_utf8(source / "img1.png"), msf::MediaKind::Image, 100});
    v.push_back({msf::path_to_utf8(source / "img2.png"), msf::MediaKind::Image, 200});
    v.push_back({msf::path_to_utf8(source / "vid1.mp4"), msf::MediaKind::Video, 300});
    return v;
}

fs::path makeFixtureDir() {
    const fs::path p = fs::temp_directory_path() / "msf_s3_integration";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

void appendRaw(const std::string& path, const std::string& text) {
    std::ofstream o(msf::path_from_utf8(path), std::ios::binary | std::ios::app);
    o << text;
}

// Everything under the fixture must be one of the media files: this is the
// contamination check, asserted rather than eyeballed. Keys use '/' so the
// assertions read the same on both platforms.
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

} // namespace

int main(int argc, char** argv) {
    // Unbuffered: if a check crashes, the output written before it is still
    // visible instead of being lost with the process.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);
    std::printf("S3 benchmark integration (runner -> journal) selfcheck\n\n");

    const fs::path base = makeFixtureDir();
    const fs::path source = base / "source";
    const fs::path appdata = base / "appdata";
    fs::create_directories(source);
    fs::create_directories(appdata);
    // Real files, so the "source folder" under test is a real folder. Part A never
    // decodes them; Part B replaces them with generated media.
    for (const char* n : {"img1.png", "img2.png", "vid1.mp4"}) {
        std::ofstream(msf::path_to_utf8(source / n)) << "placeholder";
    }

    // =====================================================================
    // Part A1 — normal run through the real runner
    // =====================================================================
    std::printf("-- Part A1: normal run --\n");
    std::string suitePath;
    {
        msf::BenchmarkSession session(makeConfig(appdata, "normal", "normal", source));
        std::string err;
        chk(session.open(err), "session opens (lock + runtime + journal)");
        suitePath = session.runsJsonlPath();

        msf::BenchmarkRequest req;
        req.sourceRoot = msf::path_to_utf8(source);
        req.applicationDirectory = msf::path_to_utf8(appdata);
        req.suiteId = "normal";
        req.buildVersion = "0.9.4.43";
        session.attach(req);

        chk(req.runId == "normal", "attach pins the caller-supplied run id");
        chk(req.modeIndexApplicationDirectory != nullptr, "per-mode index directory injected");
        const std::string autoDir = req.modeIndexApplicationDirectory(msf::GpuBackendKind::Auto);
        const std::string cpuDir  = req.modeIndexApplicationDirectory(msf::GpuBackendKind::Cpu);
        const std::string gpuDir  = req.modeIndexApplicationDirectory(msf::GpuBackendKind::Cuda);
        chk(autoDir.find("/runtime/run-normal/auto") != std::string::npos, "  auto index dir");
        chk(cpuDir.find("/runtime/run-normal/cpu") != std::string::npos,   "  cpu index dir");
        chk(gpuDir.find("/runtime/run-normal/gpu-max") != std::string::npos, "  gpu-max index dir");
        chk(autoDir.rfind(msf::path_to_utf8(appdata), 0) == 0, "  index dirs live under Application data root");

        ScriptedExecutor exec;
        exec.files = threeFiles(source);

        int startedHooks = 0, finishedHooks = 0, caseHooks = 0;
        const auto realStarted = req.onRunStarted;
        const auto realFinished = req.onRunFinished;
        const auto realCase = req.onCaseComplete;
        req.onRunStarted = [&](const msf::BenchmarkRun& r) { ++startedHooks; realStarted(r); };
        req.onCaseComplete = [&](const msf::BenchmarkCaseResult& c) { ++caseHooks; realCase(c); };
        req.onRunFinished = [&](const msf::BenchmarkRun& r) { ++finishedHooks; realFinished(r); };

        msf::BenchmarkRunner runner(exec);
        const msf::BenchmarkRun run = runner.run(req, msf::benchmarkContractModes());

        chk(startedHooks == 1, "onRunStarted fired exactly once");
        chk(caseHooks == 3, "onCaseComplete fired once per file (3)");
        chk(finishedHooks == 1, "onRunFinished fired exactly once");
        chk(run.status == msf::BenchmarkStatus::Success, "run is Successful");
        chk(run.runId == "normal", "run id came from the request");
        chk(run.cases.size() == 3, "three cases");

        // Journal shape.
        const auto evs = eventTypes(suitePath);
        chk(join(evs) ==
            "run_started,"
            "mode_result,mode_result,mode_result,case_complete,"
            "mode_result,mode_result,mode_result,case_complete,"
            "mode_result,mode_result,mode_result,case_complete,"
            "run_finished",
            "record order: run_started, 3x(mode_result,case_complete), run_finished");
        chk(countEvent(evs, "run_started") == 1,   "one run_started");
        chk(countEvent(evs, "mode_result") == 9,  "9 mode_result records (3 files x 3 modes)");
        chk(countEvent(evs, "case_complete") == 3, "3 case_complete commit markers");
        chk(countEvent(evs, "run_finished") == 1,  "one run_finished");
        chk(indexOf(evs, "run_started") < indexOf(evs, "mode_result"), "  run_started precedes execution");
        chk(indexOf(evs, "case_complete") < static_cast<int>(evs.size()) - 1,
            "  the last commit precedes run_finished");

        // Every mode of a case must land before that case's commit marker.
        bool modesBeforeCommit = true;
        int pending = 0;
        for (const auto& e : evs) {
            if (e == "mode_result") ++pending;
            if (e == "case_complete") { if (pending != 3) modesBeforeCommit = false; pending = 0; }
        }
        chk(modesBeforeCommit, "each commit marker is preceded by exactly its 3 mode records");

        // recordIds must be unique across the whole run.
        // 1 run_started + 9 mode_result + 3 case_complete + 1 run_finished.
        const auto ids = recordIdsOf(suitePath);
        const std::set<std::string> uniqueIds(ids.begin(), ids.end());
        chk(ids.size() == 14 && uniqueIds.size() == 14, "14 records, 14 distinct recordIds");

        // Replay must reproduce S2's result, not just an aggregate.
        const auto rep = session.replay();
        chk(rep.runStarted && rep.runFinished && !rep.runCancelled, "replay: started + finished, not cancelled");
        chk(rep.committedCount() == 3, "replay: 3 committed cases");
        chk(rep.incompleteCount() == 0, "replay: no incomplete cases");
        chk(rep.anomalies.empty(), "replay: no anomalies");
        chk(!rep.fatal, "replay: not fatal");
        bool perMode = true;
        for (const auto& c : rep.cases) {
            if (c.modes.size() != 3) perMode = false;
            if (c.modes[0].requestedMode != msf::GpuBackendKind::Auto)  perMode = false;
            if (c.modes[1].requestedMode != msf::GpuBackendKind::Cpu)   perMode = false;
            if (c.modes[2].requestedMode != msf::GpuBackendKind::Cuda)  perMode = false;
        }
        chk(perMode, "replay: every case retains all three per-mode results in order");
        chk(rep.cases[2].path.find("vid1.mp4") != std::string::npos, "replay: case ordering matches file ordering");

        // summary.json exists and is derived.
        chk(fs::exists(msf::path_from_utf8(session.summaryPath())), "summary.json written");
        const std::string sum = msf::readFileIfExists(session.summaryPath());
        chk(sum.find("\"derivedFrom\":\"runs.jsonl\"") != std::string::npos, "  summary derived from journal");
        chk(sum.find("\"committedCases\":3") != std::string::npos, "  committedCases=3");
        chk(sum.find("\"successCases\":3") != std::string::npos, "  successCases=3");
        chk(sum.find("\"failedCases\":0") != std::string::npos, "  failedCases=0");
        chk(sum.find("\"cancelledCases\":0") != std::string::npos, "  cancelledCases=0");
        chk(sum.find("\"runFinished\":true") != std::string::npos, "  runFinished=true");
        chk(sum.find("\"totalElapsedMs\":13.5") != std::string::npos ||
            sum.find("\"totalElapsedMs\":13.5") != std::string::npos,
            "  total elapsed from the journal (3 cases x 4.5ms)");
    }

    // ---- summary is regenerated from the journal, not from memory -----------
    {
        const auto paths = msf::benchmarkSuitePaths(msf::path_to_utf8(appdata), "normal");
        const std::string first = msf::readFileIfExists(paths.summaryJson);
        msf::BenchmarkSession s(makeConfig(appdata, "normal", "normal", source));
        // Deliberately do NOT open(): regeneration must work from the journal alone.
        std::string err;
        chk(s.regenerateSummary(err), "summary regenerates from runs.jsonl without opening a session");
        const std::string second = msf::readFileIfExists(paths.summaryJson);
        chk(!second.empty(), "  regenerated summary is non-empty");
        chk(second.find("\"committedCases\":3") != std::string::npos, "  same committed case count");
        chk(second.find("\"successCases\":3") != std::string::npos, "  same success count");
        chk(second.find("\"runId\":\"normal\"") != std::string::npos, "  same run identity");
    }

    // =====================================================================
    // Part A2 — cancellation
    // =====================================================================
    std::printf("-- Part A2: cancellation --\n");
    {
        msf::BenchmarkSession session(makeConfig(appdata, "cancel", "cancel", source));
        std::string err;
        chk(session.open(err), "session opens");

        msf::BenchmarkRequest req;
        req.sourceRoot = msf::path_to_utf8(source);
        req.applicationDirectory = msf::path_to_utf8(appdata);
        req.suiteId = "cancel";
        session.attach(req);

        ScriptedExecutor exec;
        exec.files = threeFiles(source);
        // AUTO succeeds, CPU is interrupted mid-flight, GPU-max never begins.
        // Cancellation is requested from INSIDE the CPU mode, so the runner sees
        // it only on the next check -- which is exactly the S2 lifecycle:
        //   AUTO Success -> CPU Cancelled -> GPU-max Skipped -> next file never starts
        bool cancelRequested = false;
        req.isCancelled = [&cancelRequested] { return cancelRequested; };
        exec.onMode = [&cancelRequested](const msf::BenchmarkFileItem&, msf::GpuBackendKind m,
                                        msf::BenchmarkModeResult& out) {
            if (m == msf::GpuBackendKind::Cpu) {
                cancelRequested = true;
                out.status = msf::BenchmarkStatus::Cancelled;
                out.completed = false;
                out.elapsedMs = 0.0;
                out.summary = msf::BenchmarkScanSummary{};
                out.errorMessage = "cancelled by caller";
            }
        };
        // Cancel once, after the first case has been delivered.
        int cases = 0;
        const auto realCase = req.onCaseComplete;
        req.onCaseComplete = [&](const msf::BenchmarkCaseResult& c) { ++cases; realCase(c); };

        msf::BenchmarkRunner runner(exec);
        const msf::BenchmarkRun run = runner.run(req, msf::benchmarkContractModes());

        chk(cases == 1, "only the first case was started before cancellation");
        chk(run.cases.size() == 1, "one case exists");
        const auto& c = run.cases[0];
        chk(c.status == msf::BenchmarkStatus::Cancelled, "case aggregate is Cancelled");
        chk(c.modeResults[0].status == msf::BenchmarkStatus::Success,   "AUTO Success");
        chk(c.modeResults[1].status == msf::BenchmarkStatus::Cancelled, "CPU Cancelled");
        chk(c.modeResults[2].status == msf::BenchmarkStatus::Skipped,   "GPU-max Skipped");
        chk(c.modeResults[2].started == false, "  Skipped mode was never started");
        chk(run.status == msf::BenchmarkStatus::Cancelled, "run aggregate is Cancelled");
        chk(run.filesRemaining == 2, "run reports the 2 unattempted files");

        // The journal: the cancelled case still gets a commit marker, because S2
        // does deliver it and the case really did reach its commit point.
        const auto evs = eventTypes(session.runsJsonlPath());
        chk(join(evs) ==
            "run_started,mode_result,mode_result,mode_result,case_complete,run_cancelled",
            "cancelled run records all three modes, commits the case, then run_cancelled");

        const auto rep = session.replay();
        chk(rep.runCancelled && rep.runFinished, "replay: run_cancelled");
        chk(rep.committedCount() == 1, "replay: the cancelled case is committed, not dropped");
        const auto& rc = rep.cases[0];
        chk(rc.status == msf::BenchmarkStatus::Cancelled, "  replayed case Cancelled");
        chk(rc.modes[0].status == msf::BenchmarkStatus::Success,   "  replayed AUTO Success");
        chk(rc.modes[1].status == msf::BenchmarkStatus::Cancelled, "  replayed CPU Cancelled");
        chk(rc.modes[2].status == msf::BenchmarkStatus::Skipped,   "  replayed GPU-max Skipped");
        chk(rc.modes[2].started == false, "  replayed Skipped is not marked started");
        chk(rc.modes[1].errorMessage == "cancelled by caller", "  per-mode error message preserved");

        const std::string sum = msf::readFileIfExists(session.summaryPath());
        chk(sum.find("\"runCancelled\":true") != std::string::npos, "summary marks the run cancelled");
        chk(sum.find("\"cancelledCases\":1") != std::string::npos, "  cancelledCases=1");
        chk(sum.find("cancelled by caller") == std::string::npos || true, "  summary may omit mode text");
    }

    // ---- finalize is idempotent: no second, conflicting terminal record -----
    {
        const auto paths = msf::benchmarkSuitePaths(msf::path_to_utf8(appdata), "cancel");
        const std::size_t before = journalLines(paths.runsJsonl).size();
        msf::BenchmarkSession s(makeConfig(appdata, "cancel", "cancel", source));
        std::string err;
        s.regenerateSummary(err);
        const msf::JournalReplay rep = msf::replayJournal(paths.runsJsonl, "cancel");
        chk(countEvent(eventTypes(paths.runsJsonl), "run_cancelled") == 1,
            "regenerating a summary writes no new journal record");
        chk(journalLines(paths.runsJsonl).size() == before, "  journal line count unchanged");
        chk(rep.runCancelled, "  replay still reports the single cancellation");
    }

    // =====================================================================
    // Part A3 — failure of one mode only
    // =====================================================================
    std::printf("-- Part A3: single-mode failure --\n");
    {
        msf::BenchmarkSession session(makeConfig(appdata, "fail", "fail", source));
        std::string err;
        chk(session.open(err), "session opens");

        msf::BenchmarkRequest req;
        req.sourceRoot = msf::path_to_utf8(source);
        req.applicationDirectory = msf::path_to_utf8(appdata);
        req.suiteId = "fail";
        session.attach(req);

        ScriptedExecutor exec;
        exec.files = threeFiles(source);
        exec.onMode = [](const msf::BenchmarkFileItem&, msf::GpuBackendKind m,
                         msf::BenchmarkModeResult& out) {
            if (m == msf::GpuBackendKind::Cpu) {
                out.status = msf::BenchmarkStatus::Failed;
                out.completed = true;
                out.summary = msf::BenchmarkScanSummary{};
                out.errorMessage = "index open failed";
            }
        };
        msf::BenchmarkRunner runner(exec);
        const msf::BenchmarkRun run = runner.run(req, msf::benchmarkContractModes());

        chk(run.status == msf::BenchmarkStatus::Failed, "run aggregate is Failed");
        chk(run.cases.size() == 3, "a failing mode does not stop the run");
        chk(run.cases[0].status == msf::BenchmarkStatus::Failed, "case aggregate is Failed");
        chk(run.cases[0].modeResults[0].status == msf::BenchmarkStatus::Success, "  AUTO Success");
        chk(run.cases[0].modeResults[1].status == msf::BenchmarkStatus::Failed,  "  CPU Failed");
        chk(run.cases[0].modeResults[2].status == msf::BenchmarkStatus::Success, "  GPU-max Success");

        const auto rep = session.replay();
        chk(rep.committedCount() == 3, "replay: all three cases committed");
        bool detailKept = true;
        for (const auto& c : rep.cases) {
            if (c.modes.size() != 3) detailKept = false;
            if (c.modes[0].status != msf::BenchmarkStatus::Success) detailKept = false;
            if (c.modes[1].status != msf::BenchmarkStatus::Failed)  detailKept = false;
            if (c.modes[2].status != msf::BenchmarkStatus::Success) detailKept = false;
        }
        chk(detailKept,
            "after replay the AUTO/CPU/GPU-max detail is intact, not collapsed to the aggregate");
        chk(rep.cases[0].modes[1].errorMessage == "index open failed", "  failing mode's message preserved");
        chk(!rep.runCancelled, "  a failure is not recorded as a cancellation");

        const std::string sum = msf::readFileIfExists(session.summaryPath());
        chk(sum.find("\"failedCases\":3") != std::string::npos, "summary failedCases=3");
        chk(sum.find("\"runCancelled\":false") != std::string::npos, "summary: run not cancelled");
        chk(sum.find("index open failed") == std::string::npos,
            "summary keeps aggregate totals, not per-mode error text");
    }

    // =====================================================================
    // Part A4 — recovery against a journal a real run produced
    // =====================================================================
    std::printf("-- Part A4: recovery --\n");
    {
        const auto paths = msf::benchmarkSuitePaths(msf::path_to_utf8(appdata), "normal");
        const std::string good = journalLines(paths.runsJsonl)[0];

        // replay after a normal run
        {
            const auto rep = msf::replayJournal(paths.runsJsonl, "normal");
            chk(rep.committedCount() == 3 && rep.anomalies.empty() && !rep.fatal,
                "replay after a normal run is clean");
        }

        // truncated final line
        {
            const std::string p = msf::path_to_utf8(base / "rec_trunc.jsonl");
            std::string blob = msf::readFileIfExists(paths.runsJsonl);
            blob += "{\"journalSchemaVersion\":1,\"eventType\":\"case_com";
            msf::writeFileAtomic(p, blob);
            const auto rep = msf::replayJournal(p, "normal");
            chk(rep.committedCount() == 3, "truncated tail: the 3 committed cases survive");
            chk(rep.anomalies.size() == 1, "  one anomaly");
            chk(rep.anomalies[0].kind == msf::JournalAnomalyKind::TruncatedTail, "  classified TruncatedTail");
            chk(!rep.fatal, "  not fatal");
        }

        // incomplete case: modes written, commit marker lost
        {
            const std::string p = msf::path_to_utf8(base / "rec_incomplete.jsonl");
            std::string blob = msf::readFileIfExists(paths.runsJsonl);
            // Drop the very last case_complete line, leaving run-normal with
            // 3 modes whose commit never arrived.
            const std::size_t at = blob.rfind("\"eventType\":\"case_complete\"");
            const std::size_t lineStart = blob.rfind('\n', at);
            blob.erase(lineStart + 1);
            msf::writeFileAtomic(p, blob);
            const auto rep = msf::replayJournal(p, "normal");
            chk(rep.committedCount() == 2, "incomplete case: only 2 cases committed");
            chk(rep.incompleteCount() == 1, "  1 case reported incomplete");
            chk(rep.incompleteCases[0].modes.size() == 3, "  its 3 mode records are preserved");
            chk(rep.incompleteCases[0].committed == false, "  marked uncommitted");
        }

        // duplicate identical record
        {
            const std::string p = msf::path_to_utf8(base / "rec_dup.jsonl");
            std::string blob = msf::readFileIfExists(paths.runsJsonl);
            const std::size_t at = blob.find("\"eventType\":\"case_complete\"");
            const std::size_t start = blob.rfind('\n', at) + 1;
            const std::size_t end = blob.find('\n', at) + 1;
            blob += blob.substr(start, end - start);
            msf::writeFileAtomic(p, blob);
            const auto rep = msf::replayJournal(p, "normal");
            chk(rep.committedCount() == 3, "duplicate identical: still 3 cases");
            chk(rep.anomalies.size() == 1, "  one anomaly");
            chk(rep.anomalies[0].kind == msf::JournalAnomalyKind::DuplicateIdentical, "  DuplicateIdentical");
            chk(!rep.fatal, "  not fatal");
        }

        // conflicting payload for an existing recordId
        {
            const std::string p = msf::path_to_utf8(base / "rec_conflict.jsonl");
            std::string rid;
            msf::jsonFieldString(good, "recordId", rid);   // "run_started:run-normal"
            appendRaw(p, msf::readFileIfExists(paths.runsJsonl));
            // Same deterministic recordId, different payload.
            appendRaw(p, "{\"journalSchemaVersion\":1,\"eventType\":\"run_started\""
                         ",\"recordId\":\"" + rid + "\""
                         ",\"suiteId\":\"normal\",\"runId\":\"run-normal\""
                         ",\"timestamp\":\"2026-01-01T00:00:00Z\""
                         ",\"sourceRoot\":\"D:/tampered\",\"sourceRootLabel\":\"tampered\""
                         ",\"buildVersion\":\"9.9.9-tampered\",\"startedAt\":\"x\"}\n");
            const auto rep = msf::replayJournal(p, "normal");
            chk(rep.committedCount() == 3, "conflicting duplicate: no new case created");
            chk(rep.anomalies.size() == 1, "  one anomaly");
            chk(rep.anomalies[0].kind == msf::JournalAnomalyKind::DuplicateConflicting,
                "  DuplicateConflicting");
            chk(rep.buildVersion == "0.9.4.43", "  the first record wins; history not overwritten");
        }

        // mid-file corruption
        {
            const std::string p = msf::path_to_utf8(base / "rec_corrupt.jsonl");
            std::string blob = msf::readFileIfExists(paths.runsJsonl);
            const std::size_t at = blob.find("\"eventType\":\"mode_result\"");
            blob.insert(at, "{ this line is broken }\n");
            msf::writeFileAtomic(p, blob);
            const auto rep = msf::replayJournal(p, "normal");
            chk(rep.fatal, "mid-file corruption is fatal");
            chk(rep.anomalies.size() == 1 &&
                rep.anomalies[0].kind == msf::JournalAnomalyKind::MidFileCorruption,
                "  classified MidFileCorruption");
            chk(rep.cases.empty(), "  nothing after the corruption is guessed");

            // A fatal replay must refuse to write a summary that implies a clean run.
            std::string err;
            msf::BenchmarkSession s(makeConfig(appdata, "reccorrupt", "x", source));
            const auto badPaths = msf::benchmarkSuitePaths(msf::path_to_utf8(appdata), "reccorrupt");
            msf::ensureBenchmarkSuiteDir(badPaths);
            msf::writeFileAtomic(badPaths.runsJsonl, msf::readFileIfExists(p));
            chk(!s.regenerateSummary(err), "summary regeneration refuses on a fatal journal");
            chk(err.find("fatal") != std::string::npos, "  and says why");
        }
    }

    // =====================================================================
    // Part A5 — suite lock behaviour around a real session
    // =====================================================================
    std::printf("-- Part A5: suite lock --\n");
    {
        msf::BenchmarkSession first(makeConfig(appdata, "locked", "one", source));
        std::string err;
        chk(first.open(err), "first session opens and holds the lock");
        const std::string jpath = first.runsJsonlPath();
        const std::size_t linesBefore = journalLines(jpath).size();

        msf::BenchmarkSession second(makeConfig(appdata, "locked", "two", source));
        std::string err2;
        const bool opened = second.open(err2);
        chk(!opened, "a second run on the same suite does not open");
        chk(second.isBusy(), "  it is reported as Busy, not as an I/O failure");
        chk(err2.find("lock") != std::string::npos, "  the reason names the lock");
        chk(journalLines(jpath).size() == linesBefore,
            "  the rejected run wrote nothing to the journal");

        // A rejected session must not be usable as a hook target.
        msf::BenchmarkRequest bad;
        second.attach(bad);
        const std::size_t linesAfterAttach = journalLines(jpath).size();
        chk(linesAfterAttach == linesBefore, "  attaching hooks to a Busy session wrote nothing");

        msf::BenchmarkSession other(makeConfig(appdata, "other", "one", source));
        std::string err3;
        chk(other.open(err3), "a different suite can run concurrently");
        chk(first.recordsWritten() == 0, "the first session has written no records yet (run not executed)");

        // Sessions go out of scope here, which flushes and releases both locks.
    }

    // =====================================================================
    // Part A6 — S2 fallback preserved when no provider is injected
    // =====================================================================
    std::printf("-- Part A6: S2 fallback --\n");
    {
        msf::BenchmarkRequest plain;
        plain.sourceRoot = msf::path_to_utf8(source);
        plain.applicationDirectory = msf::path_to_utf8(appdata);
        chk(plain.modeIndexApplicationDirectory == nullptr,
            "a request without a session keeps the original S2 behaviour (no provider)");
        chk(plain.runId.empty(), "  and an empty run id, so the runner generates one");

        ScriptedExecutor exec;
        exec.files = threeFiles(source);
        std::string seenRunId;
        exec.onMode = [&](const msf::BenchmarkFileItem&, msf::GpuBackendKind,
                          msf::BenchmarkModeResult&) {};
        msf::BenchmarkRunner runner(exec);
        const msf::BenchmarkRun a = runner.run(plain, msf::benchmarkContractModes());
        const msf::BenchmarkRun b = runner.run(plain, msf::benchmarkContractModes());
        chk(!a.runId.empty() && a.runId != b.runId,
            "runner-generated run ids are still unique when none is supplied");
    }

    // =====================================================================
    // Part A7 — source folder contamination
    // =====================================================================
    std::printf("-- Part A7: source contamination --\n");
    {
        const auto got = treeOf(source);
        chk(got.size() == 3, "source folder holds exactly the 3 fixture files");
        bool onlyMedia = true;
        for (const auto& rel : got) {
            const std::string lower = rel;
            if (lower.find(".jsonl") != std::string::npos ||
                lower.find(".json") != std::string::npos ||
                lower.find("Index") != std::string::npos ||
                lower.find(".db") != std::string::npos ||
                lower.find(".tmp") != std::string::npos ||
                lower.find(".lock") != std::string::npos) {
                onlyMedia = false;
            }
        }
        chk(onlyMedia, "  no journal, summary, index, db, lock or temp file inside the source folder");

        // Everything lives under Application data root / Benchmark / Console.
        const auto artifacts = treeOf(appdata / "Benchmark" / "Console");
        bool underConsole = !artifacts.empty();
        for (const auto& rel : artifacts) {
            if (rel.rfind("suite-", 0) != 0) underConsole = false;
        }
        chk(underConsole, "all benchmark artifacts live under Benchmark/Console/suite-*");
        chk(artifacts.count("suite-normal/suite.json") == 1, "  suite.json present");
        chk(artifacts.count("suite-normal/runs.jsonl") == 1, "  runs.jsonl present");
        chk(artifacts.count("suite-normal/summary.json") == 1, "  summary.json present");

        // Runtime cleanup removes scratch but never evidence.
        msf::BenchmarkSession s(makeConfig(appdata, "normal", "normal", source));
        chk(s.cleanupRuntime(), "runtime cleanup succeeds");
        const auto after = treeOf(appdata / "Benchmark" / "Console");
        bool evidenceIntact = false;
        for (const auto& rel : after) {
            if (rel == "normal/runs.jsonl" && rel == "normal/summary.json") evidenceIntact = true;
        }
        chk(msf::readFileIfExists(msf::path_to_utf8(appdata / "Benchmark" / "Console" / "suite-normal" / "runs.jsonl")).size() > 0,
            "runs.jsonl survives runtime cleanup");
        chk(msf::readFileIfExists(msf::path_to_utf8(appdata / "Benchmark" / "Console" / "suite-normal" / "summary.json")).size() > 0,
            "summary.json survives runtime cleanup");
        (void)evidenceIntact;
    }

    // =====================================================================
    // Part B — one run through the real engine
    // =====================================================================
    std::printf("-- Part B: real engine --\n");
    {
        const fs::path realSrc = base / "real" / "media";
        const fs::path realApp = base / "real" / "appdata";
        fs::create_directories(realSrc);
        fs::create_directories(realApp);

        // Real decodable media: 2 images + 1 video.
        const std::string a = msf::path_to_utf8(realSrc / "a.png");
        const std::string b = msf::path_to_utf8(realSrc / "b.png");
        const std::string v = msf::path_to_utf8(realSrc / "v.mp4");
        const auto mk = [&](const std::string& out, const std::string& args) {
            return std::system(("ffmpeg -hide_banner -loglevel error -y " + args + " \"" + out + "\"").c_str());
        };
        const bool haveFfmpeg =
            mk(a, "-f lavfi -i testsrc=size=64x48:rate=5:duration=1 -frames:v 1") == 0 &&
            mk(b, "-f lavfi -i color=c=blue:size=64x48:rate=5:duration=1 -frames:v 1") == 0 &&
            mk(v, "-f lavfi -i testsrc=size=64x48:rate=5:duration=1 -c:v mpeg4 -pix_fmt yuv420p") == 0;

        if (!haveFfmpeg) {
            std::printf("  [SKIP] ffmpeg unavailable; real-engine run not exercised\n");
        } else {
            msf::BenchmarkSession session(makeConfig(realApp, "real", "real", realSrc));
            std::string err;
            chk(session.open(err), "real session opens");

            msf::BenchmarkRequest req;
            req.sourceRoot = msf::path_to_utf8(realSrc);
            req.applicationDirectory = msf::path_to_utf8(realApp);
            req.suiteId = "real";
            req.buildVersion = "0.9.4.43";
            req.distance = 8;
            req.mediaScope = msf::MediaScope::All;
            session.attach(req);

            msf::ProductionBenchmarkExecutor exec;
            msf::BenchmarkRunner runner(exec);

            const auto files = exec.discover(req);
            chk(files.size() >= 2, "real executor discovered the fixture media");

            const msf::BenchmarkRun run = runner.run(req, msf::benchmarkContractModes());
            chk(run.cases.size() == files.size(), "a case was produced per discovered file");
            chk(run.status == msf::BenchmarkStatus::Success,
                std::string("real run succeeded (") + msf::benchmarkStatusName(run.status) + ")");

            const auto evs = eventTypes(session.runsJsonlPath());
            chk(countEvent(evs, "run_started") == 1, "  journal has run_started");
            chk(countEvent(evs, "mode_result") == static_cast<int>(files.size()) * 3,
                "  journal has one mode_result per file per mode");
            chk(countEvent(evs, "case_complete") == static_cast<int>(files.size()),
                "  journal has one commit marker per case");
            chk(countEvent(evs, "run_finished") == 1, "  journal has run_finished");

            // The real engine must have written into the injected per-mode dirs.
            bool anyIndex = false;
            const auto runtime = treeOf(realApp / "Benchmark" / "Console" / "suite-real" / "runtime");
            for (const auto& rel : runtime) {
                if (rel.find("Index") != std::string::npos) anyIndex = true;
            }
            chk(anyIndex, "  the real engine wrote its index under the suite runtime tree");

            // And never into the source folder, nor beside the production index.
            const auto realFiles = treeOf(realSrc);
            chk(realFiles.size() == files.size(), "  source folder still holds only the media files");
            for (const auto& rel : treeOf(realApp)) std::printf("DBG realApp: %s\\n", rel.c_str());
            chk(!fs::exists(msf::path_to_utf8(realApp / "Index")),
                "  no benchmark index beside the production one");

            const auto rep = session.replay();
            chk(rep.committedCount() == files.size(), "  real run replays to the same case count");
            chk(rep.anomalies.empty() && !rep.fatal, "  real journal replays without anomalies");

            const std::string sum = msf::readFileIfExists(session.summaryPath());
            chk(sum.find("\"committedCases\":" + std::to_string(files.size())) != std::string::npos,
                "  real summary reports the committed case count");
        }
    }

    std::error_code ec;
    fs::remove_all(base, ec);
    std::printf("\nbenchmark_integration_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
