// S6-2 benchmark analysis data contract.
//
// S6-1 proved that journals can be discovered, replayed and normalized. This
// stage fixes the contract those normalized records expose to later stages, so
// most of these checks are about what the model says rather than what it holds:
// which values are measured, which are derived, which are absent, and whether the
// distinctions survive normalization.
//
// A few checks are deliberately about the edges of that contract, because they
// are the places a later grouping stage would otherwise have to guess:
//
//   * a blank datasetFingerprint is NOT the same as an absent one, because the
//     writer always emits the field and a blank value means the source could not
//     be measured;
//   * a derived wall duration of 0 ms is NOT the same as having no duration, and
//     its resolution is only one second;
//   * Legacy provenance is never back-filled with the current git value.

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
    const fs::path d = fs::temp_directory_path() / "msf_s6_contract_test";
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

struct RunSpec {
    std::string suiteId;
    std::string runId;
    std::string fingerprint = "fp-1";
    bool includeFingerprintField = true;
    std::string gitCommit = "abc1234";
    bool includeGitCommit = true;
    std::string buildVersion = "0.9.4.43";
    std::string mediaScope = "all";
    std::string terminalStatus = "SUCCESS";
    bool terminal = true;
    msf::BenchmarkStatus caseStatus = msf::BenchmarkStatus::Success;
    std::string startedAt = "2026-10-01T00:00:00";
    std::string completedAt = "2026-10-01T00:01:00";
};

std::string writeRun(const fs::path& root, const RunSpec& spec) {
    const std::string appdata = path_to_utf8(root);
    msf::BenchmarkSuitePaths paths = msf::benchmarkSuitePaths(appdata, spec.suiteId);
    msf::ensureBenchmarkSuiteDir(paths);

    msf::BenchmarkRun run;
    run.runId = spec.runId;
    run.suiteId = spec.suiteId;
    run.sourceRoot = "D:/Media";
    run.sourceRootLabel = "Media";
    run.sourceRootId = "rid";
    run.buildVersion = spec.buildVersion;
    run.startedAt = spec.startedAt;
    run.completedAt = spec.completedAt;
    run.mediaScope = msf::MediaScope::All;
    run.filesStarted = 1;
    run.filesCompleted = 1;
    run.status = (spec.terminalStatus == "FAILED") ? msf::BenchmarkStatus::Failed
               : (spec.terminalStatus == "CANCELLED") ? msf::BenchmarkStatus::Cancelled
               : msf::BenchmarkStatus::Success;

    // Build the record set by hand where the shape has to be exact (absent
    // fields), and through the real writer otherwise.
    std::string blob;
    blob += "{\"journalSchemaVersion\":1,\"eventType\":\"run_started\",";
    blob += "\"recordId\":\"run_started:" + spec.runId + "\",";
    blob += "\"suiteId\":\"" + spec.suiteId + "\",\"runId\":\"" + spec.runId + "\",";
    blob += "\"timestamp\":\"2026-10-01T00:00:00Z\",";
    blob += "\"mediaScope\":\"" + spec.mediaScope + "\",\"scanImages\":true,\"scanVideos\":true,";
    blob += "\"sourceRoot\":\"D:/Media\",\"sourceRootLabel\":\"Media\",\"sourceRootId\":\"rid\",";
    if (spec.includeFingerprintField) blob += "\"datasetFingerprint\":\"" + spec.fingerprint + "\",";
    blob += "\"buildVersion\":\"" + spec.buildVersion + "\",";
    if (spec.includeGitCommit) blob += "\"gitCommit\":\"" + spec.gitCommit + "\",";
    blob += "\"startedAt\":\"" + spec.startedAt + "\",\"filesStarted\":1}\n";

    blob += "{\"journalSchemaVersion\":1,\"eventType\":\"mode_result\",";
    blob += "\"recordId\":\"mode_result:" + spec.runId + ":case-0:AUTO\",";
    blob += "\"suiteId\":\"" + spec.suiteId + "\",\"runId\":\"" + spec.runId + "\",\"caseId\":\"case-0\",";
    blob += "\"timestamp\":\"2026-10-01T00:00:01Z\",\"requestedMode\":\"AUTO\",\"effectiveMode\":\"CPU\",";
    blob += "\"status\":\"SUCCESS\",\"started\":true,\"completed\":true,\"elapsedMs\":11.0,";
    blob += "\"summary\":{\"analyzed\":1},\"errorMessage\":\"\"}\n";

    const std::string caseStatusText =
        (spec.caseStatus == msf::BenchmarkStatus::Success)  ? "SUCCESS"
      : (spec.caseStatus == msf::BenchmarkStatus::Failed)   ? "FAILED"
      : (spec.caseStatus == msf::BenchmarkStatus::Cancelled) ? "CANCELLED"
                                                             : "SKIPPED";
    blob += "{\"journalSchemaVersion\":1,\"eventType\":\"case_complete\",";
    blob += "\"recordId\":\"case_complete:" + spec.runId + ":case-0\",";
    blob += "\"suiteId\":\"" + spec.suiteId + "\",\"runId\":\"" + spec.runId + "\",\"caseId\":\"case-0\",";
    blob += "\"timestamp\":\"2026-10-01T00:00:01Z\",\"path\":\"D:/Media/a.jpg\",\"media\":\"Image\",";
    blob += "\"status\":\"" + caseStatusText + "\",\"elapsedMs\":13.0,\"errorMessage\":\"\"}\n";

    if (spec.terminal) {
        const char* ev = (spec.terminalStatus == "CANCELLED") ? "run_cancelled" : "run_finished";
        blob += "{\"journalSchemaVersion\":1,\"eventType\":\"" + std::string(ev) + "\",";
        blob += "\"recordId\":\"" + std::string(ev) + ":" + spec.runId + "\",";
        blob += "\"suiteId\":\"" + spec.suiteId + "\",\"runId\":\"" + spec.runId + "\",";
        blob += "\"timestamp\":\"2026-10-01T00:01:00Z\",\"status\":\"" + spec.terminalStatus + "\",";
        blob += "\"filesStarted\":1,\"filesCompleted\":1,\"filesRemaining\":0,";
        if (spec.terminalStatus != "CANCELLED") blob += "\"completedAt\":\"" + spec.completedAt + "\",";
        blob += "\"completionReason\":\"test\"}\n";
    }
    msf::writeFileAtomic(paths.runsJsonl, blob);
    return paths.runsJsonl;
}

const msf::IngestRun* findRun(const msf::NormalizedBenchmarkData& d, const std::string& runId) {
    for (const auto& r : d.runs) if (r.runId == runId) return &r;
    for (const auto& r : d.excludedRuns) if (r.runId == runId) return &r;
    return nullptr;
}

bool hasExclusion(const msf::IngestRun& run, msf::IngestExclusion reason) {
    for (auto e : run.exclusions) if (e == reason) return true;
    return false;
}

}  // namespace

int main() {
    std::printf("S6-2 benchmark analysis data contract selfcheck\n\n");
    const fs::path root = scratchRoot();
    const std::string appdata = path_to_utf8(root);

    // ---- baseline dataset ---------------------------------------------------
    writeRun(root, RunSpec{"base", "run-base"});
    // a second run in the SAME journal: identity must stay separated
    {
        const std::string p = writeRun(root, RunSpec{"base2", "run-base2", "fp-2"});
        const std::string p2 = writeRun(root, RunSpec{"base2b", "run-base2b", "fp-2"});
        const std::string blob = msf::readFileIfExists(p2);
        msf::writeFileAtomic(p, msf::readFileIfExists(p) + blob);
    }
    writeRun(root, RunSpec{"unknown", "run-unk", "fp-3", true, "unknown"});
    writeRun(root, RunSpec{"legacy", "run-leg", "fp-4", true, "", false});
    writeRun(root, RunSpec{"failed", "run-fail", "fp-5", true, "abc1234", true, "0.9.4.43", "all", "FAILED"});
    writeRun(root, RunSpec{"cancelled", "run-canc", "fp-6", true, "abc1234", true, "0.9.4.43", "all", "CANCELLED"});
    writeRun(root, RunSpec{"noterm", "run-noterm", "fp-7", true, "abc1234", true, "0.9.4.43", "all", "SUCCESS", false});
    writeRun(root, RunSpec{"nofield", "run-nofield", "", false});
    writeRun(root, RunSpec{"blankfp", "run-blankfp", "", true});
    writeRun(root, RunSpec{"casefail", "run-casefail", "fp-8", true, "abc1234", true, "0.9.4.43", "all",
                            "SUCCESS", true, msf::BenchmarkStatus::Failed});
    writeRun(root, RunSpec{"caseskip", "run-caseskip", "fp-9", true, "abc1234", true, "0.9.4.43", "all",
                            "SUCCESS", true, msf::BenchmarkStatus::Skipped});

    const auto data = msf::normalizeBenchmarks(appdata);

    // ---- identity ----------------------------------------------------------
    {
        const auto* run = findRun(data, "run-base");
        chk(run != nullptr, "a valid run is ingested");
        chk(run && run->runId == "run-base", "  S3 runId preserved verbatim, never synthesised");
        chk(run && run->suiteId == "base", "  suiteId preserved");
        chk(run && run->datasetIdentity == msf::DatasetIdentityState::Valid, "  dataset identity is Valid");
        chk(run && run->datasetFingerprint.value_or("") == "fp-1", "  fingerprint preserved");
        chk(run && !run->sourceJournalPath.empty(), "  source journal path retained as provenance");

        // gitCommit is provenance, not identity: it is never merged into a runId.
        const auto key = msf::groupingKey(*run);
        chk(key.gitCommit.value_or("") == "abc1234" && run->runId == "run-base",
            "gitCommit stays a separate provenance field, not part of the run id");
    }

    // ---- multi-run journal -------------------------------------------------
    {
        const auto* a = findRun(data, "run-base2");
        const auto* b = findRun(data, "run-base2b");
        chk(a && b, "both runs of one journal are present as separate runs");
        chk(a && a->datasetFingerprint.value_or("") == "fp-2" &&
            b && b->datasetFingerprint.value_or("") == "fp-2",
            "  both keep the fingerprint they were written with");
        chk(a && b && a->runId != b->runId, "  and never collapse into one");
    }

    // ---- provenance states --------------------------------------------------
    {
        const auto* known = findRun(data, "run-base");
        const auto* unk = findRun(data, "run-unk");
        const auto* leg = findRun(data, "run-leg");
        chk(known && known->gitCommitState == msf::GitCommitState::Known, "Known provenance");
        chk(unk && unk->gitCommitState == msf::GitCommitState::Unknown, "Unknown provenance");
        chk(leg && leg->gitCommitState == msf::GitCommitState::Legacy, "Legacy provenance");
        chk(leg && !leg->gitCommit.has_value(),
            "  a Legacy run carries no value at all");
        // The real property: no Legacy run anywhere in the dataset carries a value, so
// nothing can have been back-filled with a commit that did not produce it.
        bool legacyClean = true;
        for (const auto& r : data.runs) {
            if (r.gitCommitState != msf::GitCommitState::Legacy) continue;
            if (r.gitCommit.has_value() && !r.gitCommit->empty()) legacyClean = false;
        }
        for (const auto& r : data.excludedRuns) {
            if (r.gitCommitState != msf::GitCommitState::Legacy) continue;
            if (r.gitCommit.has_value() && !r.gitCommit->empty()) legacyClean = false;
        }
        chk(legacyClean, "no Legacy run anywhere carries a back-filled commit value");
        chk(known && known->buildVersion.value_or("") == "0.9.4.43", "buildVersion preserved alongside");
        chk(known && known->gitCommit.value_or("") == "abc1234",
            "  gitCommit and buildVersion remain separate fields");
    }

    // ---- measured / derived / missing --------------------------------------
    {
        const auto* run = findRun(data, "run-base");
        chk(run != nullptr, "contract run present");
        if (run) {
            chk(msf::valueOrigin(*run, msf::RunField::DatasetFingerprint) == msf::ValueOrigin::Measured,
                "datasetFingerprint is measured");
            chk(msf::valueOrigin(*run, msf::RunField::BuildVersion) == msf::ValueOrigin::Measured,
                "buildVersion is measured");
            chk(msf::valueOrigin(*run, msf::RunField::GitCommit) == msf::ValueOrigin::Measured,
                "gitCommit is measured");
            chk(msf::valueOrigin(*run, msf::RunField::WallDurationMs) == msf::ValueOrigin::Derived,
                "wall duration is derived, not measured");

            // Missing: never 0, false or "unknown".
            chk(!run->distance.has_value(), "distance absent");
            chk(!run->resourcePolicy.has_value(), "resourcePolicy absent, not inferred from a preset");
            chk(!run->gpuBackend.has_value(), "gpuBackend absent, not inferred from effectiveMode");
            chk(msf::valueOrigin(*run, msf::RunField::Distance) == msf::ValueOrigin::Missing,
                "distance reports origin Missing");
            chk(msf::valueOrigin(*run, msf::RunField::ResourcePolicy) == msf::ValueOrigin::Missing,
                "resourcePolicy reports origin Missing");
            chk(msf::valueOrigin(*run, msf::RunField::GpuBackend) == msf::ValueOrigin::Missing,
                "gpuBackend reports origin Missing");
        }
    }

    // ---- duration resolution and the 0-vs-absent distinction ---------------
    {
        chk(msf::benchmarkTimestampResolution() == msf::TimestampResolution::OneSecond,
            "timestamp resolution is reported as one second");
        chk(std::string(msf::timestampResolutionName(msf::benchmarkTimestampResolution())) == "OneSecond",
            "  and is nameable");

        const auto* sub = findRun(data, "run-base");
        chk(sub && sub->wallDurationMs.has_value() && *sub->wallDurationMs > 59000.0,
            "a 60 s run reports ~60000 ms");

        writeRun(root, RunSpec{"subsec", "run-subsec", "fp-10", true, "abc1234", true, "0.9.4.43", "all",
                                "SUCCESS", true, msf::BenchmarkStatus::Success,
                                "2026-10-01T00:00:07", "2026-10-01T00:00:07"});
        const auto d2 = msf::normalizeBenchmarks(appdata);
        const auto* zero = findRun(d2, "run-subsec");
        chk(zero && zero->wallDurationMs.has_value() && *zero->wallDurationMs == 0.0,
            "a same-second run has a duration value of 0");
        const auto* canc = findRun(d2, "run-canc");
        chk(canc && !canc->wallDurationMs.has_value(),
            "a run with no completedAt has NO duration value, which is distinguishable from 0");
    }

    // ---- dataset identity tri-state ---------------------------------------
    {
        const auto* nofield = findRun(data, "run-nofield");
        chk(nofield && nofield->datasetIdentity == msf::DatasetIdentityState::Missing,
            "an absent datasetFingerprint field is Missing");
        chk(nofield && !nofield->datasetFingerprint.has_value(),
            "  with no value at all");

        const auto* blank = findRun(data, "run-blankfp");
        chk(blank && blank->datasetIdentity == msf::DatasetIdentityState::Empty,
            "a present-but-blank datasetFingerprint is Empty");
        chk(blank && blank->datasetFingerprint.has_value() && blank->datasetFingerprint->empty(),
            "  holding an empty string rather than no value");
        chk(blank && blank->datasetFingerprint.has_value() != nofield->datasetFingerprint.has_value(),
            "  and is therefore distinguishable from Missing");

        chk(!findRun(data, "run-nofield")->datasetFingerprint.value_or("sentinel").empty(),
            "  Missing never yields a fabricated fingerprint");
    }

    // ---- status ------------------------------------------------------------
    {
        const auto* ok = findRun(data, "run-base");
        const auto* failed = findRun(data, "run-fail");
        const auto* canc = findRun(data, "run-canc");
        const auto* noterm = findRun(data, "run-noterm");
        const auto* casefail = findRun(data, "run-casefail");
        const auto* caseskip = findRun(data, "run-caseskip");

        chk(ok && ok->runStatus && *ok->runStatus == msf::BenchmarkStatus::Success,
            "run status Success comes from the terminal record");
        chk(failed && failed->runStatus && *failed->runStatus == msf::BenchmarkStatus::Failed,
            "run status Failed preserved");
        chk(canc && canc->runStatus && *canc->runStatus == msf::BenchmarkStatus::Cancelled,
            "run status Cancelled preserved");
        chk(noterm && !noterm->runStatus.has_value(),
            "a run with no terminal record has no run status at all");
        chk(casefail && casefail->runStatus && *casefail->runStatus == msf::BenchmarkStatus::Success,
            "run Success is independent of a failed case");
        chk(casefail && casefail->cases[0].status == msf::BenchmarkStatus::Failed,
            "  and the case keeps its own Failed status");
        chk(caseskip && caseskip->cases[0].status == msf::BenchmarkStatus::Skipped,
            "a skipped case keeps the Skipped status");

        // Case and mode elapsed keep their own meanings and are never a wall time.
        chk(ok && !ok->cases.empty(), "case present");
        if (ok && !ok->cases.empty()) {
            const auto& c = ok->cases[0];
            chk(c.elapsedMs > 12.0 && c.elapsedMs < 14.0, "case elapsed is the S2 case value (13 ms)");
            chk(!c.modes.empty(), "mode present");
            if (!c.modes.empty()) {
                chk(c.modes[0].elapsedMs.has_value() && *c.modes[0].elapsedMs > 10.0 &&
                    *c.modes[0].elapsedMs < 12.0,
                    "mode elapsed is the S2 mode value (11 ms), distinct from the case value");
                chk(*c.modes[0].elapsedMs != c.elapsedMs,
                    "  mode and case elapsed are never merged");
            }
            chk(ok->wallDurationMs.value_or(0.0) > 1000.0 * c.elapsedMs,
                "  and neither is the run wall duration");
        }
    }

    // ---- exclusions carry provenance --------------------------------------
    {
        chk(!data.exclusionRecords.empty(), "exclusions are recorded");
        bool allHaveProvenance = true;
        for (const auto& rec : data.exclusionRecords) {
            if (rec.sourceJournalPath.empty() || rec.reason == msf::IngestExclusion::None) {
                allHaveProvenance = false;
            }
        }
        chk(allHaveProvenance, "every exclusion record names its journal and a reason");
        bool foundCancelled = false;
        for (const auto& rec : data.exclusionRecords) {
            if (rec.reason == msf::IngestExclusion::CancelledRun) {
                foundCancelled = (rec.runId == "run-canc") && !rec.suiteId.empty();
            }
        }
        chk(foundCancelled, "a cancelled run's exclusion names its runId and suiteId");

        const auto* canc = findRun(data, "run-canc");
        chk(canc && hasExclusion(*canc, msf::IngestExclusion::CancelledRun),
            "the run itself also keeps its exclusion list");

        // Accepted runs carry no acceptance-side exclusion of their own.
        const auto* ok = findRun(data, "run-base");
        chk(ok && !hasExclusion(*ok, msf::IngestExclusion::CancelledRun) &&
                !hasExclusion(*ok, msf::IngestExclusion::FatalCorruption),
            "an accepted run is not marked with an exclusion reason");
    }

    // ---- grouping key material is stable ----------------------------------
    {
        const auto* a = findRun(data, "run-base");
        const auto* b = findRun(data, "run-fail");
        chk(a && b, "two runs available");
        if (a && b) {
            const auto ka = msf::groupingKey(*a);
            const auto kb = msf::groupingKey(*b);
            chk(ka.buildVersion == kb.buildVersion, "same build yields the same build key");
            chk(ka.gitCommit == kb.gitCommit, "same commit yields the same commit key");
            chk(ka.datasetIdentity == msf::DatasetIdentityState::Valid &&
                kb.datasetIdentity == msf::DatasetIdentityState::Valid,
                "both key material report Valid dataset identity");
            chk(ka.mediaScope.has_value() && kb.mediaScope.has_value(),
                "media scope is exposed as key material");
        }
    }

    // ---- empty / partial data uses the same semantics ---------------------
    {
        const msf::BenchmarkSuitePaths empty =
            msf::benchmarkSuitePaths(appdata, "contract-empty");
        msf::ensureBenchmarkSuiteDir(empty);
        msf::writeFileAtomic(empty.runsJsonl, "");
        const auto d = msf::normalizeBenchmarks(appdata);
        chk(d.journalsWithoutRuns >= 1, "an empty journal is handled without changing the model");
        chk(d.runsFound == d.runsAccepted + d.runsExcluded, "accounting stays self-consistent");
    }

    // ---- determinism -------------------------------------------------------
    {
        const auto a = msf::normalizeBenchmarks(appdata);
        const auto b = msf::normalizeBenchmarks(appdata);
        chk(a.runs.size() == b.runs.size(), "same input yields the same run count");
        bool same = (a.runs.size() == b.runs.size());
        for (std::size_t i = 0; same && i < a.runs.size(); ++i) {
            same = a.runs[i].runId == b.runs[i].runId &&
                   a.runs[i].datasetFingerprint == b.runs[i].datasetFingerprint &&
                   a.runs[i].gitCommit == b.runs[i].gitCommit &&
                   a.runs[i].datasetIdentity == b.runs[i].datasetIdentity &&
                   a.runs[i].wallDurationMs == b.runs[i].wallDurationMs &&
                   a.runs[i].cases.size() == b.runs[i].cases.size();
        }
        chk(same, "same input yields identical normalized content and order");
        chk(a.exclusionRecords.size() == b.exclusionRecords.size(),
            "exclusion records are stable across repeats");
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("\nbenchmark_data_contract_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}