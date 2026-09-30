// S3 journal tests: write ordering, recovery granularity, idempotency, and
// faithful reconstruction of the per-mode state that S2 produced.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "benchmark_journal.h"
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

fs::path scratchJournal() {
    const fs::path p = fs::temp_directory_path() / "msf_s3_journal_test";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

msf::BenchmarkModeResult mode(msf::GpuBackendKind req, msf::GpuBackendKind eff,
                              msf::BenchmarkStatus st, double ms) {
    msf::BenchmarkModeResult m;
    m.requestedMode = req;
    m.effectiveMode = eff;
    m.status = st;
    // A skipped mode was never started, and neither was a cancelled one
    // completed. Encoding this is what lets replay rebuild the exact mode
    // state S2 reported.
    m.started = (st != msf::BenchmarkStatus::Skipped);
    m.completed = (st != msf::BenchmarkStatus::Skipped &&
                   st != msf::BenchmarkStatus::Cancelled);
    m.elapsedMs = ms;
    m.summary.scanned = 1;
    m.summary.analyzed = 1;
    if (st == msf::BenchmarkStatus::Failed) m.errorMessage = "boom";
    return m;
}

msf::BenchmarkCaseResult makeCase(const std::string& id, const std::string& path,
                                  std::vector<msf::BenchmarkModeResult> modes) {
    msf::BenchmarkCaseResult c;
    c.caseId = id;
    c.path = path;
    c.media = msf::MediaKind::Image;
    c.modeResults = std::move(modes);
    c.elapsedMs = 0.0;
    for (const auto& m : c.modeResults) c.elapsedMs += m.elapsedMs;
    c.status = msf::aggregateStatus(c.modeResults);
    c.started = true;
    c.completed = true;
    if (c.status == msf::BenchmarkStatus::Failed) c.errorMessage = "boom";
    return c;
}

msf::BenchmarkRun makeRun(const std::string& runId) {
    msf::BenchmarkRun r;
    r.runId = runId;
    r.suiteId = "suite-1";
    r.sourceRoot = "D:/Media";
    r.sourceRootLabel = "Media";
    r.sourceRootId = "rid";
    r.datasetFingerprint = "fp";
    r.buildVersion = "0.9.4.43";
    r.startedAt = "2026-09-30T00:00:00Z";
    r.completedAt = "2026-09-30T00:01:00Z";
    r.mediaScope = msf::MediaScope::All;
    return r;
}

void appendRaw(const std::string& path, const std::string& text) {
    std::ofstream o(msf::path_from_utf8(path), std::ios::binary | std::ios::app);
    o << text;
}

} // namespace

int main() {
    std::printf("S3 benchmark journal selfcheck\n\n");
    const fs::path dir = scratchJournal();
    const std::string j1 = msf::path_to_utf8(dir / "j1.jsonl");

    // ---- write + ordering ----------------------------------------------------
    {
        msf::BenchmarkJournalWriter w;
        chk(w.open(j1), "journal writer opens");
        const auto run = makeRun("run-1");
        chk(w.writeRunStarted(run), "run_started written");
        chk(w.writeCaseComplete(run, makeCase("c1", "D:/Media/a.jpg", {
                 mode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 2.0),
                 mode(msf::GpuBackendKind::Cpu,  msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 3.0),
                 mode(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cuda, msf::BenchmarkStatus::Success, 5.0),
             })), "case 1 committed");
        chk(w.writeCaseComplete(run, makeCase("c2", "D:/Media/b.jpg", {
                 mode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 1.0),
                 mode(msf::GpuBackendKind::Cpu,  msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success, 1.0),
                 mode(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cuda, msf::BenchmarkStatus::Success, 1.0),
             })), "case 2 committed");
        w.writeRunFinished(run);
        // 1 run_started + (3 mode_result + 1 case_complete) x2 + 1 run_finished
        chk(w.recordCount() == 10, "10 records: 1 start + 4 + 4 + 1 finish");
        w.close();

        // Ordering on disk: all of case 1's modes, then its commit, then case 2.
        const auto r = msf::replayJournal(j1);
        chk(r.runStarted && r.runFinished, "replay sees run_started and run_finished");
        chk(!r.runCancelled, "  not cancelled");
        chk(r.cases.size() == 2, "  two committed cases");
        chk(r.cases[0].caseId == "c1" && r.cases[1].caseId == "c2", "  commit order preserved");
        chk(r.incompleteCases.empty(), "  no incomplete cases");
        chk(!r.fatal, "  journal is not fatal");
        chk(r.anomalies.empty(), "  no anomalies on a clean journal");
        chk(r.cases[0].modes.size() == 3, "  each case restores three mode results");
    }

    // ---- per-mode state is restored, not just the aggregate ------------------
    {
        const auto r = msf::replayJournal(j1);
        const auto& c = r.cases[0];
        chk(c.status == msf::BenchmarkStatus::Success, "case aggregate restored");
        chk(c.modes[0].requestedMode == msf::GpuBackendKind::Auto, "  mode 0 requested AUTO");
        chk(c.modes[2].requestedMode == msf::GpuBackendKind::Cuda, "  mode 2 requested CUDA (gpu-max)");
        chk(c.modes[1].elapsedMs == 3.0, "  per-mode elapsed restored");
        chk(c.modes[2].effectiveMode == msf::GpuBackendKind::Cuda, "  effective mode restored");
    }

    // ---- cancellation is replayed faithfully ---------------------------------
    {
        const std::string p = msf::path_to_utf8(dir / "cancel.jsonl");
        msf::BenchmarkJournalWriter w;
        w.open(p);
        const auto run = makeRun("run-c");
        w.writeRunStarted(run);
        w.writeCaseComplete(run, makeCase("c1", "D:/Media/a.jpg", {
                 mode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Success,   2.0),
                 mode(msf::GpuBackendKind::Cpu,  msf::GpuBackendKind::Cpu,  msf::BenchmarkStatus::Cancelled, 0.0),
                 mode(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cuda, msf::BenchmarkStatus::Skipped,   0.0),
             }));
        w.close();

        const auto r = msf::replayJournal(p);
        chk(r.cases.size() == 1, "cancelled case is still committed (it reached its commit point)");
        const auto& c = r.cases[0];
        chk(c.status == msf::BenchmarkStatus::Cancelled, "  case Cancelled");
        chk(c.modes[0].status == msf::BenchmarkStatus::Success,   "  AUTO Success");
        chk(c.modes[1].status == msf::BenchmarkStatus::Cancelled, "  CPU Cancelled");
        chk(c.modes[2].status == msf::BenchmarkStatus::Skipped,   "  GPU-max Skipped");
        chk(c.modes[2].started == false, "  Skipped mode is not marked started");
    }

    // ---- truncated final line is discarded -----------------------------------
    {
        const std::string p = msf::path_to_utf8(dir / "trunc.jsonl");
        {
            msf::BenchmarkJournalWriter w;
            w.open(p);
            const auto run = makeRun("run-t");
            w.writeRunStarted(run);
            w.writeCaseComplete(run, makeCase("c1", "D:/Media/a.jpg", {
                mode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu, msf::BenchmarkStatus::Success, 1.0)}));
            w.close();
        }
        const std::size_t before = msf::readFileIfExists(p).size();
        appendRaw(p, "{\"journalSchemaVersion\":1,\"eventType\":\"case_comp");  // interrupted write
        const auto r = msf::replayJournal(p);
        chk(r.committedCount() == 1, "records before the truncated tail are still used");
        chk(r.anomalies.size() == 1, "  one anomaly reported");
        chk(r.anomalies[0].kind == msf::JournalAnomalyKind::TruncatedTail, "  classified as a truncated tail");
        chk(!r.fatal, "  a truncated tail is not fatal corruption");
        chk(r.runStarted, "  the run identity survived recovery");
    }

    // ---- a case with modes but no commit is INCOMPLETE, not committed -------
    {
        const std::string p = msf::path_to_utf8(dir / "incomplete.jsonl");
        {
            msf::BenchmarkJournalWriter w;
            w.open(p);
            const auto run = makeRun("run-i");
            w.writeRunStarted(run);
            w.writeCaseComplete(run, makeCase("c1", "D:/Media/a.jpg", {
                mode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu, msf::BenchmarkStatus::Success, 1.0),
                mode(msf::GpuBackendKind::Cpu,  msf::GpuBackendKind::Cpu, msf::BenchmarkStatus::Success, 1.0),
                mode(msf::GpuBackendKind::Cuda, msf::GpuBackendKind::Cuda, msf::BenchmarkStatus::Success, 1.0),
            }));
            // case 2 gets its mode records, then the process dies before commit.
            w.writeCaseComplete(run, makeCase("c2", "D:/Media/b.jpg", {
                mode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu, msf::BenchmarkStatus::Success, 1.0),
                mode(msf::GpuBackendKind::Cpu,  msf::GpuBackendKind::Cpu, msf::BenchmarkStatus::Success, 1.0),
            }));
            w.close();
            // Simulate the crash: drop the case_complete line that closes c2,
            // leaving only its mode records.
            const std::string blob = msf::readFileIfExists(p);
            const std::size_t lastCommit = blob.rfind("\"eventType\":\"case_complete\"");
            if (lastCommit != std::string::npos) {
                const std::size_t lineStart = blob.rfind('\n', lastCommit);
                std::error_code e2;
                fs::resize_file(msf::path_from_utf8(p), lineStart + 1, e2);
            }
        }
        const auto r = msf::replayJournal(p);
        chk(r.committedCount() == 1, "only the fully committed case counts");
        chk(r.incompleteCount() == 1, "the uncommitted case is reported separately");
        chk(r.incompleteCases.size() == 1 && r.incompleteCases[0].caseId == "c2",
            "  and it is the right case");
        chk(r.incompleteCases[0].committed == false, "  marked not committed");
        chk(r.incompleteCases[0].modes.size() == 2, "  its already-written mode records are preserved");
    }

    // ---- duplicate identical record is idempotent ---------------------------
    {
        const std::string p = msf::path_to_utf8(dir / "dup_same.jsonl");
        {
            msf::BenchmarkJournalWriter w;
            w.open(p);
            const auto run = makeRun("run-d");
            w.writeRunStarted(run);
            w.writeCaseComplete(run, makeCase("c1", "D:/Media/a.jpg", {
                mode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu, msf::BenchmarkStatus::Success, 1.0)}));
            w.close();
        }
        // Re-append the exact same commit bytes: a retried write.
        const std::string blob = msf::readFileIfExists(p);
        const std::size_t commitAt = blob.find("\"eventType\":\"case_complete\"");
        const std::size_t commitStart = blob.rfind('\n', commitAt) + 1;
        appendRaw(p, blob.substr(commitStart));

        const auto r = msf::replayJournal(p);
        chk(r.committedCount() == 1, "an identical duplicate does not create a second case");
        bool sawDup = false;
        for (const auto& a : r.anomalies)
            if (a.kind == msf::JournalAnomalyKind::DuplicateIdentical) sawDup = true;
        chk(sawDup, "  the duplicate is reported as an identical duplicate");
        chk(!r.fatal, "  and it is not fatal");
    }

    // ---- duplicate recordId with a DIFFERENT payload is an anomaly ----------
    {
        const std::string p = msf::path_to_utf8(dir / "dup_conflict.jsonl");
        {
            msf::BenchmarkJournalWriter w;
            w.open(p);
            const auto run = makeRun("run-x");
            w.writeRunStarted(run);
            w.writeCaseComplete(run, makeCase("c1", "D:/Media/a.jpg", {
                mode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu, msf::BenchmarkStatus::Success, 1.0)}));
            w.close();
        }
        appendRaw(p, "{\"journalSchemaVersion\":1,\"eventType\":\"case_complete\","
                     "\"recordId\":\"case_complete:run-x:c1\",\"suiteId\":\"suite-1\","
                     "\"runId\":\"run-x\",\"caseId\":\"c1\",\"timestamp\":\"2026-09-30T00:09:00Z\","
                     "\"path\":\"D:/Media/DIFFERENT.jpg\",\"media\":\"Image\",\"status\":\"FAILED\","
                     "\"elapsedMs\":999.0,\"errorMessage\":\"tampered\"}\n");
        const auto r = msf::replayJournal(p);
        chk(r.committedCount() == 1, "a conflicting duplicate does not create a second case");
        bool sawConflict = false;
        for (const auto& a : r.anomalies)
            if (a.kind == msf::JournalAnomalyKind::DuplicateConflicting) sawConflict = true;
        chk(sawConflict, "  reported as a conflicting duplicate");
        chk(r.cases[0].path == "D:/Media/a.jpg", "  the FIRST record is kept; history is not overwritten");
        chk(!r.fatal, "  and it is reported rather than auto-repaired");
    }

    // ---- mid-file corruption is fatal, not auto-repaired --------------------
    {
        const std::string p = msf::path_to_utf8(dir / "corrupt.jsonl");
        {
            msf::BenchmarkJournalWriter w;
            w.open(p);
            const auto run = makeRun("run-c2");
            w.writeRunStarted(run);
            w.writeCaseComplete(run, makeCase("c1", "D:/Media/a.jpg", {
                mode(msf::GpuBackendKind::Auto, msf::GpuBackendKind::Cpu, msf::BenchmarkStatus::Success, 1.0)}));
            w.close();
        }
        std::string blob = msf::readFileIfExists(p);
        const std::size_t at = blob.find("\"eventType\":\"case_complete\"");
        blob.insert(at, "this is not a record\n");
        msf::writeFileAtomic(p, blob);

        const auto r = msf::replayJournal(p);
        chk(r.fatal, "mid-file corruption is reported as fatal");
        chk(r.anomalies.size() == 1, "  one anomaly");
        chk(r.anomalies[0].kind == msf::JournalAnomalyKind::MidFileCorruption, "  classified as corruption");
        chk(r.cases.empty(), "  no records are guessed past the corruption");
    }

    // ---- summary is derived from the journal ---------------------------------
    {
        const auto r = msf::replayJournal(j1);
        const std::string s = msf::buildSummaryJson(r);
        chk(s.find("\"derivedFrom\":\"runs.jsonl\"") != std::string::npos,
            "summary declares the journal as its source");
        chk(s.find("\"committedCases\":2") != std::string::npos, "summary counts committed cases");
        chk(s.find("D:/Media/a.jpg") != std::string::npos, "summary carries per-case identity");
        chk(s.find("authoritative") != std::string::npos,
            "summary states that the journal is authoritative");
    }

    // ---- empty / missing journal --------------------------------------------
    {
        const auto r = msf::replayJournal(msf::path_to_utf8(dir / "nope.jsonl"));
        chk(r.cases.empty() && !r.fatal, "a missing journal replays to an empty, non-fatal result");
        const std::string empty = msf::path_to_utf8(dir / "empty.jsonl");
        msf::writeFileAtomic(empty, "");
        const auto r2 = msf::replayJournal(empty);
        chk(r2.cases.empty() && r2.anomalies.empty(), "an empty journal has no anomalies");
    }

    // ---- build provenance: gitCommit (additive, schema 1 unchanged) ----------
    //
    // S6 needs commit-level cross-build comparison, and buildVersion alone cannot
    // separate two commits of the same version. The field is additive, so these
    // checks pin three things at once: a known value is stored, an absent one is
    // stored as an explicit "unknown", and a journal written before the field
    // existed still replays.
    {
        // 1. a known short hash is written and read back
        const std::string p = msf::path_to_utf8(dir / "git-known.jsonl");
        msf::BenchmarkJournalWriter w;
        chk(w.open(p), "provenance journal writer opens");
        auto run = makeRun("run-git-known");
        run.gitCommit = "eb5ef90";
        chk(w.writeRunStarted(run), "run_started with a known gitCommit written");
        w.close();

        const std::string blob = msf::readFileIfExists(p);
        chk(blob.find("\"gitCommit\":\"eb5ef90\"") != std::string::npos,
            "the record carries the supplied short commit id");
        const auto r = msf::replayJournal(p);
        chk(r.gitCommit == "eb5ef90", "replay reads the gitCommit back");

        // 2. no provenance available is stored as an explicit state, not empty
        const std::string p2 = msf::path_to_utf8(dir / "git-unknown.jsonl");
        msf::BenchmarkJournalWriter w2;
        chk(w2.open(p2), "unknown-provenance writer opens");
        auto run2 = makeRun("run-git-unknown");
        run2.gitCommit.clear();   // the caller had none, e.g. a git-less build
        chk(w2.writeRunStarted(run2), "run_started with no provenance written");
        w2.close();

        const std::string blob2 = msf::readFileIfExists(p2);
        chk(blob2.find("\"gitCommit\":\"unknown\"") != std::string::npos,
            "an absent provenance is recorded as the literal \"unknown\"");
        chk(blob2.find("\"gitCommit\":\"\"") == std::string::npos,
            "an absent provenance is never recorded as an empty string");
        chk(msf::replayJournal(p2).gitCommit == "unknown",
            "replay reads the unknown provenance back");

        // 3. a journal written before the field existed still replays
        const std::string p3 = msf::path_to_utf8(dir / "git-legacy.jsonl");
        appendRaw(p3,
            "{\"journalSchemaVersion\":1,\"eventType\":\"run_started\","
            "\"recordId\":\"run_started:run-legacy\",\"suiteId\":\"suite-1\","
            "\"runId\":\"run-legacy\",\"buildVersion\":\"0.9.4.43\","
            "\"startedAt\":\"2026-09-30T00:00:00\",\"filesStarted\":0}\n");
        const auto r3 = msf::replayJournal(p3);
        chk(r3.runStarted && !r3.fatal, "a pre-provenance run_started still replays");
        chk(r3.buildVersion == "0.9.4.43", "  and its buildVersion is still read");
        chk(r3.gitCommit.empty(), "  and gitCommit stays empty rather than being invented");

        // 4. the schema version is unchanged: this is an additive field
        chk(blob.find("\"journalSchemaVersion\":1") != std::string::npos,
            "journalSchemaVersion stays 1 for an additive field");
        chk(msf::kBenchmarkJournalSchemaVersion == 1,
            "the schema constant is still 1");

        // 5. summary regeneration is unaffected by the new field
        appendRaw(p3,
            "{\"journalSchemaVersion\":1,\"eventType\":\"run_finished\","
            "\"recordId\":\"run_finished:run-legacy\",\"runId\":\"run-legacy\","
            "\"status\":\"SUCCESS\",\"filesStarted\":0,\"filesCompleted\":0,"
            "\"filesRemaining\":0,\"completedAt\":\"2026-09-30T00:01:00\","
            "\"completionReason\":\"completed\"}\n");
        const std::string summary = msf::buildSummaryJson(msf::replayJournal(p3));
        chk(summary.find("\"derivedFrom\":\"runs.jsonl\"") != std::string::npos,
            "summary still regenerates from a journal whose run_started has no gitCommit");
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf("\nbenchmark_journal_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}