#include "benchmark_data_mining.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>

#include "benchmark_journal.h"   // replayJournal, jsonFieldString
#include "benchmark_store.h"    // benchmarkSuitePaths, readFileIfExists
#include "path_utils.h"

namespace fs = std::filesystem;

namespace msf {
namespace {

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

// One candidate journal plus the non-fatal reasons it cannot be used, if any.
struct JournalCandidate {
    std::string path;
    IngestExclusion unusable = IngestExclusion::None;
};

// Enumerates <root>/Benchmark/Console/suite-*/runs.jsonl.
//
// The directory names come from benchmarkSuitePaths(), so the S3 path policy
// stays the single owner and this layer cannot drift from it. Anything that is
// not a regular file, or that cannot be read, is kept as a candidate with a
// recorded reason rather than being skipped silently.
std::vector<JournalCandidate> discoverJournals(const std::string& applicationDataRoot) {
    std::vector<JournalCandidate> found;

    const BenchmarkSuitePaths base =
        benchmarkSuitePaths(applicationDataRoot, std::string());   // gives consoleRoot
    const fs::path consoleRoot = path_from_utf8(base.consoleRoot);

    std::error_code ec;
    if (!fs::is_directory(consoleRoot, ec)) return found;

    std::vector<fs::path> suiteDirs;
    for (fs::directory_iterator it(consoleRoot, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_directory(ec)) continue;
        suiteDirs.push_back(it->path());
    }
    // Deterministic regardless of directory enumeration order.
    std::sort(suiteDirs.begin(), suiteDirs.end());

    for (const auto& suiteDir : suiteDirs) {
        const fs::path journal = suiteDir / "runs.jsonl";
        std::error_code fec;
        const bool isFile = fs::is_regular_file(journal, fec);
        if (!isFile) {
            // A suite directory with no journal is normal for an abandoned or
            // locked run, so it is recorded as an empty journal rather than an
            // error, and it still contributes to the discovered count.
            found.push_back({path_to_utf8(journal), IngestExclusion::EmptyJournal});
            continue;
        }
        found.push_back({path_to_utf8(journal), IngestExclusion::None});
    }
    return found;
}

// ---------------------------------------------------------------------------
// Run enumeration
// ---------------------------------------------------------------------------

// What a run's journal records contribute to the normalized model, collected in
// one pass so the journal is read once.
//
// completedAt is gathered here because S3's replayJournal() deliberately exposes
// only a subset of the terminal record: it fills completionReason but leaves
// JournalReplay::completedAt empty, even though run_finished does write the
// field. Reading it with S3's own flat field reader is therefore required, and is
// not a second parser: no judgement is made about the value here.
struct RunStartInfo {
    std::string runId;
    std::string datasetFingerprint;
    std::string mediaScope;
    std::string completedAt;
};

// Lists the runs a journal contains, with their run_started provenance and the
// terminal timestamp.
//
// A suite journal accumulates every run that used the suite, and replayJournal()
// returns one run at a time, so the run list has to be established before it can
// be replayed per run.
//
// This is NOT a parser and makes no judgement: it uses S3's own minimal flat
// field readers to locate run_started records and the terminal record's
// completedAt, and copies four string fields out of them. Every integrity,
// commit and anomaly decision is still made by replayJournal() below. The dataset
// fingerprint is read verbatim and is never recomputed.
std::vector<RunStartInfo> listRuns(const std::string& journalPath) {
    std::map<std::string, RunStartInfo> byRun;

    std::ifstream in(path_from_utf8(journalPath), std::ios::binary);
    if (!in) return {};

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::string ev, runId;
        if (!jsonFieldString(line, "eventType", ev)) continue;
        if (!jsonFieldString(line, "runId", runId)) continue;
        if (runId.empty()) continue;

        auto it = byRun.find(runId);
        const bool isNew = (it == byRun.end());
        if (isNew) it = byRun.emplace(runId, RunStartInfo{}).first;

        if (ev == "run_started") {
            it->second.runId = runId;
            jsonFieldString(line, "datasetFingerprint", it->second.datasetFingerprint);
            jsonFieldString(line, "mediaScope", it->second.mediaScope);
        } else if (ev == "run_finished" || ev == "run_cancelled") {
            jsonFieldString(line, "completedAt", it->second.completedAt);
        }
    }

    std::vector<RunStartInfo> runs;
    runs.reserve(byRun.size());
    for (auto& kv : byRun) {
        // Only a run that actually announced itself is a run. A journal holding
        // nothing but a terminal record carries no identity to analyse.
        if (kv.second.runId.empty()) continue;
        runs.push_back(kv.second);
    }
    // Sorted for determinism, independent of filesystem and line order.
    std::sort(runs.begin(), runs.end(),
              [](const RunStartInfo& a, const RunStartInfo& b) { return a.runId < b.runId; });
    return runs;
}

// ---------------------------------------------------------------------------
// Normalization
// ---------------------------------------------------------------------------

IngestModeResult normalizeMode(const ReplayedModeResult& m) {
    IngestModeResult out;
    out.requestedMode = m.requestedMode;
    out.effectiveMode = m.effectiveMode;
    out.status = m.status;
    out.started = m.started;
    out.completed = m.completed;
    // Only a mode S2 actually ran carries a measurement. A mode that never
    // started keeps no value at all, because 0.0 would be a claim.
    if (m.started) out.elapsedMs = m.elapsedMs;
    out.summary = m.summary;
    out.errorMessage = m.errorMessage;
    return out;
}

IngestCase normalizeCase(const ReplayedCase& c) {
    IngestCase out;
    out.caseId = c.caseId;
    out.path = c.path;
    out.media = c.media;
    out.status = c.status;
    out.elapsedMs = c.elapsedMs;
    out.modes.reserve(c.modes.size());
    for (const auto& m : c.modes) out.modes.push_back(normalizeMode(m));
    return out;
}

// Parses the S3 "%Y-%m-%dT%H:%M:%S" shape used by startedAt / completedAt.
//
// Deliberately minimal and deliberately not a timezone conversion: the value is
// local time, the two fields share one formatter, and this layer only takes their
// difference. A value it cannot read yields no duration rather than a guess.
bool parseLocalStamp(const std::string& s, std::tm& out) {
    if (s.size() < 19) return false;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return false;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = se;
    tm.tm_isdst = -1;   // let the platform resolve, so a DST jump is not invented
    out = tm;
    return true;
}

std::optional<double> wallDuration(const std::string& startedAt,
                                   const std::string& completedAt) {
    if (startedAt.empty() || completedAt.empty()) return std::nullopt;
    std::tm a{}, b{};
    if (!parseLocalStamp(startedAt, a) || !parseLocalStamp(completedAt, b)) return std::nullopt;
    const std::time_t ta = std::mktime(&a);
    const std::time_t tb = std::mktime(&b);
    if (ta == static_cast<std::time_t>(-1) || tb == static_cast<std::time_t>(-1)) return std::nullopt;
    const double ms = (static_cast<double>(tb) - static_cast<double>(ta)) * 1000.0;
    if (ms < 0.0) return std::nullopt;   // a negative duration is not a measurement
    return ms;
}

void tally(std::map<int, std::size_t>& counts, IngestExclusion reason) {
    if (reason == IngestExclusion::None) return;
    counts[static_cast<int>(reason)] += 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

const char* ingestRunClassName(IngestRunClass c) {
    switch (c) {
        case IngestRunClass::Complete:    return "Complete";
        case IngestRunClass::Cancelled:   return "Cancelled";
        case IngestRunClass::Incomplete:  return "Incomplete";
        case IngestRunClass::Corrupt:     return "Corrupt";
        case IngestRunClass::Unavailable: return "Unavailable";
    }
    return "Unavailable";
}

const char* ingestExclusionName(IngestExclusion e) {
    switch (e) {
        case IngestExclusion::None:                 return "none";
        case IngestExclusion::UnreadableJournal:    return "unreadable-journal";
        case IngestExclusion::EmptyJournal:         return "empty-journal";
        case IngestExclusion::FatalCorruption:      return "fatal-corruption";
        case IngestExclusion::IncompleteRun:        return "incomplete-run";
        case IngestExclusion::CancelledRun:         return "cancelled-run";
        case IngestExclusion::NoDatasetFingerprint: return "no-dataset-fingerprint";
        case IngestExclusion::TruncatedTail:        return "truncated-tail";
        case IngestExclusion::ConflictingDuplicate: return "conflicting-duplicate";
        case IngestExclusion::DuplicateIgnored:     return "duplicate-ignored";
        case IngestExclusion::CommitlessCase:       return "commitless-case";
    }
    return "none";
}

const char* gitCommitStateName(GitCommitState s) {
    switch (s) {
        case GitCommitState::Known:   return "Known";
        case GitCommitState::Unknown: return "Unknown";
        case GitCommitState::Legacy:  return "Legacy";
    }
    return "Legacy";
}

// ---------------------------------------------------------------------------
// Ingestion
// ---------------------------------------------------------------------------

IngestResult ingestBenchmarks(const std::string& applicationDataRoot) {
    IngestResult result;
    std::map<int, std::size_t> exclusionCounts;

    const std::vector<JournalCandidate> candidates = discoverJournals(applicationDataRoot);
    result.journalsDiscovered = candidates.size();



    for (const auto& cand : candidates) {
        if (cand.unusable == IngestExclusion::UnreadableJournal) {
            ++result.journalsUnreadable;
            tally(exclusionCounts, IngestExclusion::UnreadableJournal);
            continue;
        }

        const std::vector<RunStartInfo> runInfos = listRuns(cand.path);

        if (runInfos.empty()) {
            // A journal with no run_started has no run to analyse. This covers a
            // zero-byte file and a suite directory that never ran.
            //
            // It is a JOURNAL-level exclusion, not an excluded run: there is no
            // run identity to exclude, so counting it as one would break
            // "found == accepted + excluded". The reason is still tallied so the
            // journal does not disappear from the accounting.
            tally(exclusionCounts, IngestExclusion::EmptyJournal);
            ++result.journalsWithoutRuns;
            continue;
        }

        // One run at a time, and the replay is released before the next one so
        // peak memory tracks the largest single journal rather than the tree.
        for (const auto& info : runInfos) {
            const JournalReplay rep = replayJournal(cand.path, info.runId);

            IngestRun run;
            run.sourceJournalPath = cand.path;
            run.runId = rep.runId.empty() ? info.runId : rep.runId;
            run.suiteId = rep.suiteId;
            run.startedAt = rep.startedAt;
            run.completedAt = info.completedAt;

            if (!rep.buildVersion.empty()) run.buildVersion = rep.buildVersion;

            // Three distinct provenance states, never conflated. An empty value
            // means the journal predates the field, and the current git value is
            // deliberately NOT substituted for it.
            if (!rep.gitCommit.empty()) {
                run.gitCommit = rep.gitCommit;
                run.gitCommitState = (rep.gitCommit == "unknown") ? GitCommitState::Unknown
                                                                : GitCommitState::Known;
            } else {
                run.gitCommitState = GitCommitState::Legacy;
            }

            // The dataset fingerprint and media scope come from the run_started
            // record verbatim. S3 does not carry them into JournalReplay, and the
            // fingerprint is never recomputed here: an absent value stays absent
            // rather than becoming an empty identity.
            if (!info.datasetFingerprint.empty()) run.datasetFingerprint = info.datasetFingerprint;
            if (!info.mediaScope.empty()) run.mediaScope = info.mediaScope;

            // S3 recovery outcomes, recorded rather than reinterpreted. S3 already
            // decided what each one means; S6 only carries the reason so the
            // exclusion is visible instead of silent.
            for (const auto& a : rep.anomalies) {
                switch (a.kind) {
                    case JournalAnomalyKind::TruncatedTail:        run.exclusions.push_back(IngestExclusion::TruncatedTail); break;
                    case JournalAnomalyKind::MidFileCorruption:    run.exclusions.push_back(IngestExclusion::FatalCorruption); break;
                    case JournalAnomalyKind::DuplicateConflicting: run.exclusions.push_back(IngestExclusion::ConflictingDuplicate); break;
                    case JournalAnomalyKind::DuplicateIdentical:   run.exclusions.push_back(IngestExclusion::DuplicateIgnored); break;
                }
            }

            // Mode records without a commit are not attributed to any case. S3
            // already separated them; they are counted and left out.
            result.commitlessCases += rep.incompleteCount();
            if (rep.incompleteCount() > 0) run.exclusions.push_back(IngestExclusion::CommitlessCase);

            for (const auto& c : rep.cases) run.cases.push_back(normalizeCase(c));

            // Both sides come from this run's own records, not from JournalReplay:
            // startedAt is filled by replay, and completedAt is read separately
            // because replay leaves that field empty.
            run.wallDurationMs = wallDuration(run.startedAt, run.completedAt);

            // --- decide acceptability ---------------------------------------
            if (rep.fatal) {
                run.runClass = IngestRunClass::Corrupt;
                run.exclusions.push_back(IngestExclusion::FatalCorruption);
                result.anyFatal = true;
            } else if (!rep.runStarted) {
                run.runClass = IngestRunClass::Incomplete;
                run.exclusions.push_back(IngestExclusion::IncompleteRun);
            } else if (rep.runCancelled) {
                // Preserved, never deleted: a cancelled run is real evidence, it
                // just must not become a comparison baseline.
                run.runClass = IngestRunClass::Cancelled;
                run.exclusions.push_back(IngestExclusion::CancelledRun);
            } else if (!rep.runFinished) {
                run.runClass = IngestRunClass::Incomplete;
                run.exclusions.push_back(IngestExclusion::IncompleteRun);
            } else {
                run.runClass = IngestRunClass::Complete;
            }

            if (!run.datasetFingerprint) {
                run.exclusions.push_back(IngestExclusion::NoDatasetFingerprint);
            }

            ++result.runsFound;

            // A complete run with a dataset identity is the analysis input.
            // Everything else is kept in excludedRuns with its reasons.
            const bool analysable = (run.runClass == IngestRunClass::Complete) &&
                                    run.datasetFingerprint.has_value();
            if (analysable) {
                for (auto e : run.exclusions) tally(exclusionCounts, e);
                ++result.runsAccepted;
                result.runs.push_back(std::move(run));
            } else {
                for (auto e : run.exclusions) tally(exclusionCounts, e);
                ++result.runsExcluded;
                result.excludedRuns.push_back(std::move(run));
            }
        }
    }

    // Deterministic report ordering regardless of discovery order.
    result.runsAccepted = result.runs.size();
    result.excludedRuns.shrink_to_fit();
    for (const auto& kv : exclusionCounts) {
        result.exclusions.push_back({static_cast<IngestExclusion>(kv.first), kv.second});
    }
    std::sort(result.runs.begin(), result.runs.end(),
              [](const IngestRun& a, const IngestRun& b) {
                  if (a.suiteId != b.suiteId) return a.suiteId < b.suiteId;
                  return a.runId < b.runId;
              });
    std::sort(result.excludedRuns.begin(), result.excludedRuns.end(),
              [](const IngestRun& a, const IngestRun& b) {
                  if (a.sourceJournalPath != b.sourceJournalPath) return a.sourceJournalPath < b.sourceJournalPath;
                  return a.runId < b.runId;
              });
    return result;
}

}  // namespace msf