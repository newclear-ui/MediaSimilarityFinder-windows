// S6-1 benchmark data mining: ingestion and normalization.
//
// The layer under test has one job: hand real S3 journals to S3's own replay and
// project the result into an analysis-ready model without inventing anything.
// Most of these checks therefore assert absence: that a value the journal did
// not carry stays absent, and that a run S3 rejected is counted rather than
// silently dropped.
//
// No journal is written by hand except where a recovery state cannot be produced
// any other way (a corrupt line, a truncated tail). Everything else goes through
// the real BenchmarkJournalWriter, so the fixtures are the same bytes S3 writes.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "benchmark_core.h"
#include "benchmark_data_mining.h"
#include "benchmark_journal.h"
#include "benchmark_store.h"
#include "path_utils.h"

namespace fs = std::filesystem;

namespace {

// The path helpers live in namespace msf; the anonymous namespace below is the
// global one, so bring the two used helpers into scope once instead of
// qualifying every call site.
using msf::path_from_utf8;
using msf::path_to_utf8;

int gChecks = 0;
int gFails = 0;

void chk(bool ok, const std::string& what) {
    ++gChecks;
    if (ok) std::printf("  [ok] %s\n", what.c_str());
    else { ++gFails; std::printf("  [F] %s\n", what.c_str()); }
}

fs::path scratchRoot() {
    const fs::path d = fs::temp_directory_path() / "msf_s6_ingest_test";
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

// Writes one journal under <root>/Benchmark/Console/suite-<id>/runs.jsonl using
// the real S3 writer, so the fixture bytes are exactly what S3 produces.
struct JournalSpec {
    std::string suiteId;
    std::string runId = "run-1";
    std::string fingerprint = "fp-A";
    std::string gitCommit = "abc1234";
    std::string buildVersion = "0.9.4.43";
    std::string mediaScope = "all";
    int cases = 2;
    bool withGitCommit = true;
    bool cancelled = false;
    bool terminal = true;
};

std::string writeJournal(const fs::path& root, const JournalSpec& spec) {
    const std::string appdata = path_to_utf8(root);
    msf::BenchmarkSuitePaths paths = msf::benchmarkSuitePaths(appdata, spec.suiteId);
    msf::ensureBenchmarkSuiteDir(paths);

    msf::BenchmarkRun run;
    run.runId = spec.runId;
    run.suiteId = spec.suiteId;
    run.sourceRoot = "D:/Media";
    run.sourceRootLabel = "Media";
    run.sourceRootId = "rid";
    run.datasetFingerprint = spec.fingerprint;
    run.buildVersion = spec.buildVersion;
    if (spec.withGitCommit) run.gitCommit = spec.gitCommit;
    run.startedAt = "2026-10-01T00:00:00";
    run.completedAt = "2026-10-01T00:01:00";
    run.mediaScope = msf::MediaScope::All;
    run.status = spec.cancelled ? msf::BenchmarkStatus::Cancelled : msf::BenchmarkStatus::Success;
    run.filesStarted = static_cast<std::size_t>(spec.cases);
    run.filesCompleted = static_cast<std::size_t>(spec.cases);

    msf::BenchmarkJournalWriter w;
    w.open(paths.runsJsonl);
    w.writeRunStarted(run);

    for (int i = 0; i < spec.cases; ++i) {
        msf::BenchmarkCaseResult c;
        c.caseId = "case-" + std::to_string(i);
        c.path = "D:/Media/file" + std::to_string(i) + ".jpg";
        c.media = msf::MediaKind::Image;
        c.elapsedMs = 12.0 + i;
        c.status = msf::BenchmarkStatus::Success;
        c.started = true;
        c.completed = true;
        msf::BenchmarkModeResult m;
        m.requestedMode = msf::GpuBackendKind::Auto;
        m.effectiveMode = msf::GpuBackendKind::Cpu;
        m.status = msf::BenchmarkStatus::Success;
        m.started = true;
        m.completed = true;
        m.elapsedMs = 12.0 + i;
        c.modeResults.push_back(m);
        w.writeCaseComplete(run, c);
    }

    if (spec.terminal) {
        if (spec.cancelled) w.writeRunCancelled(run, "cancelled by test");
        else w.writeRunFinished(run);
    }
    w.close();
    return paths.runsJsonl;
}

// A journal as it existed before the provenance field did: a real pre-provenance
// run_started written by hand. The current writer always emits the field (as a
// commit id or as "unknown"), so a legacy journal cannot be produced any other
// way, and it is exactly the shape the 29 pre-existing journals still have.
std::string writeLegacyJournal(const fs::path& root, const std::string& suiteId,
                               const std::string& runId) {
    const msf::BenchmarkSuitePaths paths =
        msf::benchmarkSuitePaths(path_to_utf8(root), suiteId);
    msf::ensureBenchmarkSuiteDir(paths);
    std::string blob;
    blob += "{\"journalSchemaVersion\":1,\"eventType\":\"run_started\",";
    blob += "\"recordId\":\"run_started:" + runId + "\",";
    blob += "\"suiteId\":\"" + suiteId + "\",";
    blob += "\"runId\":\"" + runId + "\",";
    blob += "\"timestamp\":\"2026-10-01T00:00:00Z\",";
    blob += "\"mediaScope\":\"all\",\"scanImages\":true,\"scanVideos\":true,";
    blob += "\"sourceRoot\":\"D:/Media\",\"sourceRootLabel\":\"Media\",\"sourceRootId\":\"rid\",";
    blob += "\"datasetFingerprint\":\"fp-L\",\"buildVersion\":\"0.9.4.43\",";
    blob += "\"startedAt\":\"2026-10-01T00:00:00\",\"filesStarted\":1}\n";
    blob += "{\"journalSchemaVersion\":1,\"eventType\":\"mode_result\",";
    blob += "\"recordId\":\"mode_result:" + runId + ":case-0:AUTO\",";
    blob += "\"suiteId\":\"" + suiteId + "\",\"runId\":\"" + runId + "\",\"caseId\":\"case-0\",";
    blob += "\"timestamp\":\"2026-10-01T00:00:01Z\",\"requestedMode\":\"AUTO\",";
    blob += "\"effectiveMode\":\"CPU\",\"status\":\"SUCCESS\",\"started\":true,";
    blob += "\"completed\":true,\"elapsedMs\":12.0,";
    blob += "\"summary\":{\"scanned\":0},\"errorMessage\":\"\"}\n";
    blob += "{\"journalSchemaVersion\":1,\"eventType\":\"case_complete\",";
    blob += "\"recordId\":\"case_complete:" + runId + ":case-0\",";
    blob += "\"suiteId\":\"" + suiteId + "\",\"runId\":\"" + runId + "\",\"caseId\":\"case-0\",";
    blob += "\"timestamp\":\"2026-10-01T00:00:01Z\",\"path\":\"D:/Media/file0.jpg\",";
    blob += "\"media\":\"Image\",\"status\":\"SUCCESS\",\"elapsedMs\":12.0,\"errorMessage\":\"\"}\n";
    blob += "{\"journalSchemaVersion\":1,\"eventType\":\"run_finished\",";
    blob += "\"recordId\":\"run_finished:" + runId + "\",";
    blob += "\"suiteId\":\"" + suiteId + "\",\"runId\":\"" + runId + "\",";
    blob += "\"timestamp\":\"2026-10-01T00:01:00Z\",\"status\":\"SUCCESS\",";
    blob += "\"filesStarted\":1,\"filesCompleted\":1,\"filesRemaining\":0,";
    blob += "\"completedAt\":\"2026-10-01T00:01:00\",\"completionReason\":\"completed\"}\n";
    msf::writeFileAtomic(paths.runsJsonl, blob);
    return paths.runsJsonl;
}

void appendRaw(const std::string& path, const std::string& text) {
    std::ofstream o(path_from_utf8(path), std::ios::binary | std::ios::app);
    o << text;
}

std::size_t tallyOf(const msf::IngestResult& r, msf::IngestExclusion reason) {
    for (const auto& t : r.exclusions) if (t.reason == reason) return t.count;
    return 0;
}

bool hasExclusion(const msf::IngestRun& run, msf::IngestExclusion reason) {
    for (auto e : run.exclusions) if (e == reason) return true;
    return false;
}

const msf::IngestRun* findRun(const msf::IngestResult& r, const std::string& runId) {
    for (const auto& run : r.runs) if (run.runId == runId) return &run;
    for (const auto& run : r.excludedRuns) if (run.runId == runId) return &run;
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("S6-1 benchmark data mining ingestion selfcheck\n\n");

    // Read-only diagnostic pass against a real benchmark storage root, for
    // verifying the layer against journals that the product actually wrote
    // rather than only against the fixtures below. Asserts nothing and is
    // excluded from the check count, so running it can never change a result.
    if (argc > 1) {
        const auto live = msf::ingestBenchmarks(argv[1]);
        std::printf("LIVE  journals=%zu unreadable=%zu withoutRuns=%zu\n",
                    live.journalsDiscovered, live.journalsUnreadable, live.journalsWithoutRuns);
        std::printf("LIVE  runsFound=%zu accepted=%zu excluded=%zu commitless=%zu anyFatal=%d\n",
                    live.runsFound, live.runsAccepted, live.runsExcluded,
                    live.commitlessCases, (int)live.anyFatal);
        std::size_t legacy = 0, known = 0, unknown = 0, withDuration = 0;
        for (const auto& run : live.runs) {
            switch (run.gitCommitState) {
                case msf::GitCommitState::Legacy:  ++legacy; break;
                case msf::GitCommitState::Unknown: ++unknown; break;
                case msf::GitCommitState::Known:   ++known; break;
            }
            if (run.wallDurationMs) ++withDuration;
        }
        std::printf("LIVE  provenance: legacy=%zu known=%zu unknown=%zu  withDuration=%zu\n",
                    legacy, known, unknown, withDuration);
        std::printf("LIVE  -- accepted (first 8) --\n");
        for (std::size_t i = 0; i < live.runs.size() && i < 8; ++i) {
            const auto& run = live.runs[i];
            std::printf("  %-26s %-10s cases=%zu fp=%-10.10s build=%-9s git=%-7s dur=%s\n",
                        run.runId.c_str(), msf::ingestRunClassName(run.runClass), run.cases.size(),
                        run.datasetFingerprint ? run.datasetFingerprint->c_str() : "(none)",
                        run.buildVersion ? run.buildVersion->c_str() : "(none)",
                        msf::gitCommitStateName(run.gitCommitState),
                        run.wallDurationMs ? std::to_string((long long)*run.wallDurationMs).c_str() : "(none)");
        }
        std::printf("LIVE  -- exclusions --\n");
        for (const auto& t : live.exclusions) {
            std::printf("  %-26s %zu\n", msf::ingestExclusionName(t.reason), t.count);
        }
        const auto again = msf::ingestBenchmarks(argv[1]);
        bool same = (live.runs.size() == again.runs.size());
        for (std::size_t i = 0; same && i < live.runs.size(); ++i) {
            same = (live.runs[i].runId == again.runs[i].runId) &&
                   (live.runs[i].sourceJournalPath == again.runs[i].sourceJournalPath);
        }
        std::printf("LIVE  determinism: %s\n", same ? "IDENTICAL" : "DIFFERS");
        return 0;
    }

    const fs::path root = scratchRoot();
    const std::string appdata = path_to_utf8(root);

    // ---- empty root ---------------------------------------------------------
    {
        const auto r = msf::ingestBenchmarks(path_to_utf8(fs::temp_directory_path() / "msf_s6_absent_root"));
        chk(r.journalsDiscovered == 0, "a missing storage root yields no journals, not an error");
        chk(r.runs.empty() && r.runsAccepted == 0, "  and no runs");
    }

    // ---- valid journal: accepted --------------------------------------------
    {
        writeJournal(root, JournalSpec{"valid", "run-valid"});
        const auto r = msf::ingestBenchmarks(appdata);
        chk(r.journalsDiscovered == 1, "one journal discovered under Benchmark/Console");
        chk(r.runsFound == 1 && r.runsAccepted == 1, "a complete run is accepted");

        const auto* run = findRun(r, "run-valid");
        chk(run != nullptr, "the accepted run is present");
        if (run) {
            chk(run->runClass == msf::IngestRunClass::Complete, "  classified Complete");
            chk(run->datasetFingerprint.value_or("") == "fp-A", "  datasetFingerprint preserved verbatim");
            chk(run->buildVersion.value_or("") == "0.9.4.43", "  buildVersion preserved");
            chk(run->gitCommit.value_or("") == "abc1234", "  gitCommit preserved");
            chk(run->gitCommitState == msf::GitCommitState::Known, "  provenance state is Known");
            chk(run->mediaScope.value_or("") == "all", "  mediaScope preserved, not re-classified");
            chk(run->cases.size() == 2, "  two cases ingested");
            chk(!run->sourceJournalPath.empty(), "  provenance keeps the source journal path");
            chk(!run->suiteId.empty(), "  provenance keeps the suite id");

            // Derived wall duration, from startedAt/completedAt only.
            chk(run->wallDurationMs.has_value(), "  wall duration derived");
            if (run->wallDurationMs) {
                chk(*run->wallDurationMs > 59000.0 && *run->wallDurationMs < 61000.0,
                    "  and is about 60 s, distinct from the 12 ms case elapsed");
            }
            chk(run->cases[0].elapsedMs > 11.0 && run->cases[0].elapsedMs < 13.0,
                "  case elapsed is the S2 per-case value");
            const auto& m = run->cases[0].modes[0];
            chk(m.elapsedMs.has_value() && *m.elapsedMs > 11.0, "  measured mode keeps its elapsed");
        }
    }

    // ---- provenance: the three states are distinct --------------------------
    {
        writeLegacyJournal(root, "legacy", "run-legacy");
        writeJournal(root, JournalSpec{"unknown", "run-unknown", "fp-U", "unknown", "", "all", 1, true});

        const auto r = msf::ingestBenchmarks(appdata);
        const auto* legacy = findRun(r, "run-legacy");
        const auto* unknown = findRun(r, "run-unknown");
        chk(legacy && legacy->gitCommitState == msf::GitCommitState::Legacy,
            "a journal without the field is Legacy, not Unknown");
        chk(legacy && !legacy->gitCommit.has_value(),
            "  and carries no value rather than the current git commit");
        chk(unknown && unknown->gitCommitState == msf::GitCommitState::Unknown,
            "a recorded \"unknown\" is Unknown, not Legacy");
        chk(unknown && unknown->gitCommit.value_or("") == "unknown", "  and its value is preserved");
    }

    // ---- missing fields stay missing, never 0 / false / "unknown" ----------
    {
        const auto r = msf::ingestBenchmarks(appdata);
        const auto* run = findRun(r, "run-valid");
        chk(run && !run->distance.has_value(), "distance stays absent (not 0)");
        chk(run && !run->resourcePolicy.has_value(), "resourcePolicy stays absent");
        chk(run && !run->gpuBackend.has_value(), "gpuBackend stays absent");
    }

    // ---- empty fingerprint: excluded from comparison, still visible --------
    {
        writeJournal(root, JournalSpec{"nofp", "run-nofp", "", "abc1234", "", "all", 1});
        const auto r = msf::ingestBenchmarks(appdata);
        const auto* run = findRun(r, "run-nofp");
        chk(run != nullptr, "a run with no dataset fingerprint is still returned, not dropped");
        chk(run && !run->datasetFingerprint.has_value(), "  its fingerprint stays absent");
        chk(run && run->runClass == msf::IngestRunClass::Complete,
            "  its own status is still Complete (S6 classification is not a benchmark status)");
        chk(run && hasExclusion(*run, msf::IngestExclusion::NoDatasetFingerprint),
            "  and it is excluded from the analysable set for that reason");
        chk(tallyOf(r, msf::IngestExclusion::NoDatasetFingerprint) == 1,
            "  counted once in the exclusion tally");
    }

    // ---- cancellation: preserved, never a baseline -------------------------
    {
        writeJournal(root, JournalSpec{"cancel", "run-cancel", "fp-C", "abc1234", "", "all", 2, true, true});
        const auto r = msf::ingestBenchmarks(appdata);
        const auto* run = findRun(r, "run-cancel");
        chk(run != nullptr, "a cancelled run is ingested, not discarded");
        chk(run && run->runClass == msf::IngestRunClass::Cancelled, "  classified Cancelled");
        chk(run && hasExclusion(*run, msf::IngestExclusion::CancelledRun), "  excluded with a named reason");
        chk(run && !run->wallDurationMs.has_value(),
            "  no wall duration, because run_cancelled carries no completedAt");
        chk(run && run->cases.size() == 2, "  its committed cases are still preserved");
    }

    // ---- incomplete run: no terminal record ---------------------------------
    {
        writeJournal(root, JournalSpec{"partial", "run-partial", "fp-P", "abc1234", "", "all", 2, true, false, false});
        const auto r = msf::ingestBenchmarks(appdata);
        const auto* run = findRun(r, "run-partial");
        chk(run && run->runClass == msf::IngestRunClass::Incomplete, "a run with no terminal record is Incomplete");
        chk(run && hasExclusion(*run, msf::IngestExclusion::IncompleteRun), "  excluded as an incomplete run");
    }

    // ---- S3 recovery: truncated tail ---------------------------------------
    {
        const std::string p = writeJournal(root, JournalSpec{"trunc", "run-trunc", "fp-T", "abc1234", "", "all", 2});
        // An interrupted final write: a complete line with no terminating newline.
        appendRaw(p, "{\"journalSchemaVersion\":1,\"eventType\":\"case_comp");
        const auto r = msf::ingestBenchmarks(appdata);
        const auto* run = findRun(r, "run-trunc");
        chk(run != nullptr, "a truncated tail does not lose the run");
        chk(run && run->cases.size() == 2, "  the committed cases before it survive");
        chk(run && hasExclusion(*run, msf::IngestExclusion::TruncatedTail),
            "  the discarded tail is recorded as an S3 anomaly");
    }

    // ---- S3 recovery: commitless case --------------------------------------
    {
        const std::string p = writeJournal(root, JournalSpec{"commitless", "run-commitless", "fp-CL", "abc1234", "", "all", 1});
        // A mode record with no following case_complete: an uncommitted
        // transaction, which S3 must classify as incomplete rather than a case.
        appendRaw(p,
            "{\"journalSchemaVersion\":1,\"eventType\":\"mode_result\","
            "\"recordId\":\"mode_result:run-commitless:case-x:AUTO\",\"suiteId\":\"commitless\","
            "\"runId\":\"run-commitless\",\"caseId\":\"case-x\","
            "\"requestedMode\":\"AUTO\",\"effectiveMode\":\"CPU\",\"status\":\"SUCCESS\","
            "\"started\":true,\"completed\":true,\"elapsedMs\":5.0,"
            "\"summary\":{\"scanned\":0},\"errorMessage\":\"\"}\n");
        const auto r = msf::ingestBenchmarks(appdata);
        const auto* run = findRun(r, "run-commitless");
        chk(run != nullptr, "a journal with a commitless mode is still ingested");
        chk(run && run->cases.size() == 1, "  only the committed case is attributed");
        chk(r.commitlessCases >= 1, "  the commitless case is counted, not silently dropped");
        chk(run && hasExclusion(*run, msf::IngestExclusion::CommitlessCase), "  and recorded as an exclusion");
    }

    // ---- S3 recovery: duplicate and conflicting duplicate -------------------
    {
        const std::string p = writeJournal(root, JournalSpec{"dup", "run-dup", "fp-D", "abc1234", "", "all", 1});
        const std::string first = msf::readFileIfExists(p);
        // Re-emit the identical case_complete: idempotent, S3 ignores it.
        const std::string line = first.substr(first.rfind("case_complete") ? first.rfind("\n", first.rfind("case_complete") - 1) : 0);
        std::string caseLine;
        {
            std::size_t start = first.rfind("\n", first.find("case_complete"));
            start = (start == std::string::npos) ? 0 : start + 1;
            const std::size_t end = first.find('\n', start);
            caseLine = first.substr(start, end - start) + "\n";
        }
        appendRaw(p, caseLine);
        const auto r1 = msf::ingestBenchmarks(appdata);
        const auto* dup = findRun(r1, "run-dup");
        chk(dup && dup->cases.size() == 1, "an identical duplicate commit is ignored by S3");
        chk(dup && hasExclusion(*dup, msf::IngestExclusion::DuplicateIgnored), "  and reported as ignored");

        // Same recordId, different payload: reported, first record kept.
        std::string conflicting = caseLine;
        const std::size_t ep = conflicting.find("\"elapsedMs\":");
        if (ep != std::string::npos) {
            const std::size_t comma = conflicting.find(',', ep);
            conflicting.replace(ep, comma - ep, "\"elapsedMs\":999.0");
        }
        appendRaw(p, conflicting);
        const auto r2 = msf::ingestBenchmarks(appdata);
        const auto* conf = findRun(r2, "run-dup");
        chk(conf && hasExclusion(*conf, msf::IngestExclusion::ConflictingDuplicate),
            "a conflicting duplicate is reported as an anomaly");
        chk(conf && conf->cases.size() == 1, "  and the first record is retained");
    }

    // ---- S3 recovery: mid-file corruption is fatal --------------------------
    {
        const std::string p = writeJournal(root, JournalSpec{"corrupt", "run-corrupt", "fp-X", "abc1234", "", "all", 1});
        appendRaw(p, "{ this is not a readable record }\n");
        appendRaw(p, "{\"journalSchemaVersion\":1,\"eventType\":\"run_finished\",\"runId\":\"run-corrupt\","
                      "\"status\":\"SUCCESS\",\"filesStarted\":1,\"filesCompleted\":1,"
                      "\"filesRemaining\":0,\"completedAt\":\"2026-10-01T00:01:00\","
                      "\"completionReason\":\"completed\"}\n");
        const auto r = msf::ingestBenchmarks(appdata);
        const auto* run = findRun(r, "run-corrupt");
        chk(run && run->runClass == msf::IngestRunClass::Corrupt, "a corrupt interior line is Corrupt");
        chk(r.anyFatal, "  and the result reports fatal, so a caller can refuse partial data");
        chk(run && hasExclusion(*run, msf::IngestExclusion::FatalCorruption), "  recorded as an exclusion");
    }

    // ---- wall duration resolution is one second, and 0 is not "no time" ------
    {
        // S3 writes startedAt/completedAt without a fractional part, so a run
        // that finished inside the same second must surface as exactly 0 while
        // still being PRESENT. If this ever becomes indistinguishable from a
        // missing duration, a later stage could read it as an instant run.
        const std::string p = writeJournal(root, JournalSpec{"subsec", "run-subsec", "fp-S", "abc1234", "", "all", 1});
        std::string blob = msf::readFileIfExists(p);
        const std::size_t sp = blob.find("\"startedAt\":\"2026-10-01T00:00:00\"");
        chk(sp != std::string::npos, "fixture has the expected startedAt");
        if (sp != std::string::npos) {
            blob.replace(sp, std::string("\"startedAt\":\"2026-10-01T00:00:00\"").size(),
                         "\"startedAt\":\"2026-10-01T00:00:07\"");
            const std::size_t cp = blob.find("\"completedAt\":\"2026-10-01T00:01:00\"");
            if (cp != std::string::npos) {
                blob.replace(cp, std::string("\"completedAt\":\"2026-10-01T00:01:00\"").size(),
                             "\"completedAt\":\"2026-10-01T00:00:07\"");
            }
            msf::writeFileAtomic(p, blob);

            const auto r = msf::ingestBenchmarks(appdata);
            const auto* run = findRun(r, "run-subsec");
            chk(run && run->wallDurationMs.has_value(),
                "a same-second run still HAS a duration value");
            chk(run && run->wallDurationMs && *run->wallDurationMs == 0.0,
                "  reported as 0 ms, because the journal resolution is one second");
        }

        // And a run with no completedAt has NO value at all, which is the
        // distinguishable state.
        const auto r2 = msf::ingestBenchmarks(appdata);
        const auto* canc = findRun(r2, "run-cancel");
        chk(canc && !canc->wallDurationMs.has_value(),
            "a cancelled run has no duration value at all, distinct from 0");
    }

    // ---- multi-run journal: runs stay separated ----------------------------
    {
        // One suite journal that two runs appended to. Replaying it must yield
        // both runs rather than merging them, which is the S3-BUG regression guard.
        const std::string appdata2 = path_to_utf8(root);
        msf::BenchmarkSuitePaths paths = msf::benchmarkSuitePaths(appdata2, "multi");
        msf::ensureBenchmarkSuiteDir(paths);
        const std::string p1 = writeJournal(root, JournalSpec{"multiA", "run-multi-A", "fp-M1", "abc1234", "", "all", 1});
        const std::string p2 = writeJournal(root, JournalSpec{"multiB", "run-multi-B", "fp-M2", "def5678", "", "all", 1});
        // Append the second run's records into the FIRST journal, so one file
        // holds two runs exactly as a reused suite would.
        appendRaw(p1, msf::readFileIfExists(p2));

        const auto r = msf::ingestBenchmarks(appdata2);
        const auto* a = findRun(r, "run-multi-A");
        const auto* b = findRun(r, "run-multi-B");
        chk(a && b, "both runs of a shared journal are ingested");
        chk(a && a->datasetFingerprint.value_or("") == "fp-M1", "  each keeps its own dataset");
        chk(b && b->datasetFingerprint.value_or("") == "fp-M2", "  without being merged");
        chk(a && a->cases.size() == 1 && b && b->cases.size() == 1,
            "  and each keeps only its own cases");
    }

    // ---- empty journal -------------------------------------------------------
    {
        const msf::BenchmarkSuitePaths paths =
            msf::benchmarkSuitePaths(appdata, "empty-suite");
        msf::ensureBenchmarkSuiteDir(paths);
        msf::writeFileAtomic(paths.runsJsonl, "");
        const auto r = msf::ingestBenchmarks(appdata);
        chk(tallyOf(r, msf::IngestExclusion::EmptyJournal) >= 1, "an empty journal is counted, not an error");
    }

    // ---- a suite directory with no journal ----------------------------------
    {
        const msf::BenchmarkSuitePaths paths =
            msf::benchmarkSuitePaths(appdata, "no-journal-suite");
        msf::ensureBenchmarkSuiteDir(paths);
        const auto r = msf::ingestBenchmarks(appdata);
        chk(r.journalsDiscovered >= 8, "a suite directory without a journal is discovered, not fatal");
    }

    // ---- determinism --------------------------------------------------------
    {
        const auto a = msf::ingestBenchmarks(appdata);
        const auto b = msf::ingestBenchmarks(appdata);
        chk(a.runs.size() == b.runs.size(), "repeat ingestion yields the same run count");
        bool sameOrder = (a.runs.size() == b.runs.size());
        for (std::size_t i = 0; sameOrder && i < a.runs.size(); ++i) {
            sameOrder = (a.runs[i].runId == b.runs[i].runId) &&
                        (a.runs[i].suiteId == b.runs[i].suiteId) &&
                        (a.runs[i].sourceJournalPath == b.runs[i].sourceJournalPath);
        }
        chk(sameOrder, "repeat ingestion yields the same order and content");
        chk(a.runsFound == b.runsFound && a.runsExcluded == b.runsExcluded,
            "counts are stable across runs");
    }

    // ---- accounting is self-consistent -------------------------------------
    {
        const auto r = msf::ingestBenchmarks(appdata);
        chk(r.runsFound == r.runsAccepted + r.runsExcluded,
            "found == accepted + excluded");
        chk(r.runs.size() == r.runsAccepted, "the accepted vector matches the accepted count");
        chk(r.excludedRuns.size() == r.runsExcluded,
            "the excluded vector holds exactly the excluded runs");
        chk(r.journalsWithoutRuns >= 2,
            "journals with no run identity are counted separately, not as excluded runs");
        std::size_t totalExcl = 0;
        for (const auto& t : r.exclusions) totalExcl += t.count;
        chk(totalExcl > 0, "exclusions carry named reasons rather than vanishing");
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("\nbenchmark_data_mining_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}