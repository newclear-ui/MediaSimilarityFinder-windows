#include "benchmark_journal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "benchmark.h"    // BenchmarkRecorder::escapeJson
#include "path_utils.h"

namespace msf {
namespace {

// Both delegate to the shared helpers in benchmark_store, so the Console journal,
// the Console suite.json and the GUI snapshots cannot drift into three different
// escaping or timestamp implementations. These wrappers only shorten call sites.
std::string jstr(const std::string& s) { return benchmarkJsonString(s); }
std::string jbool(bool b) { return benchmarkJsonBool(b); }

// Deterministic record ids. Determinism is what makes a repeated commit
// idempotent instead of duplicated.
std::string modeRecordId(const std::string& runId, const std::string& caseId, GpuBackendKind m) {
    return "mode_result:" + runId + ":" + caseId + ":" + gpuBackendKindName(m);
}
std::string caseRecordId(const std::string& runId, const std::string& caseId) {
    return "case_complete:" + runId + ":" + caseId;
}

std::string isoNow() { return benchmarkNowStamp(); }
BenchmarkStatus statusFromName(const std::string& s) {
    if (s == "SUCCESS")   return BenchmarkStatus::Success;
    if (s == "FAILED")    return BenchmarkStatus::Failed;
    if (s == "CANCELLED") return BenchmarkStatus::Cancelled;
    return BenchmarkStatus::Skipped;
}
std::string statusName(BenchmarkStatus s) { return benchmarkStatusName(s); }

GpuBackendKind modeFromName(const std::string& s) {
    if (s == "CUDA") return GpuBackendKind::Cuda;
    if (s == "CPU")  return GpuBackendKind::Cpu;
    return GpuBackendKind::Auto;
}

MediaKind mediaFromName(const std::string& s) {
    if (s == "Image") return MediaKind::Image;
    if (s == "Video") return MediaKind::Video;
    return MediaKind::Unknown;
}
std::string mediaName(MediaKind k) {
    switch (k) {
        case MediaKind::Image: return "Image";
        case MediaKind::Video: return "Video";
        default: return "Unknown";
    }
}

std::string summaryBody(const BenchmarkScanSummary& s) {
    std::ostringstream o;
    o << "{\"scanned\":" << s.scanned
      << ",\"added\":" << s.added
      << ",\"modified\":" << s.modified
      << ",\"unchanged\":" << s.unchanged
      << ",\"removed\":" << s.removed
      << ",\"analyzed\":" << s.analyzed
      << ",\"candidates\":" << s.candidates
      << ",\"groups\":" << s.groups
      << ",\"indexedVideos\":" << s.indexedVideos
      << ",\"videoCandidatePairs\":" << s.videoCandidatePairs << "}";
    return o.str();
}

} // namespace

// ---------------------------------------------------------------------------
// Minimal flat-JSON readers
// ---------------------------------------------------------------------------
//
// The journal writes flat objects, so a full parser would be dead weight. These
// locate a top-level "key":value pair and decode a JSON string (handling the
// escapes escapeJson can emit) or a bare number/true/false.
//
// They are not a general JSON reader: a nested value will be returned raw. That
// is acceptable because no journal record nests, and keeping the surface small
// keeps the recovery path auditable.

namespace {
const char* findKey(const std::string& line, const std::string& key) {
    const std::string pat = "\"" + key + "\":";
    const std::size_t at = line.find(pat);
    if (at == std::string::npos) return nullptr;
    return line.c_str() + at + pat.size();
}
} // namespace

bool jsonFieldString(const std::string& line, const std::string& key, std::string& out) {
    const char* p = findKey(line, key);
    if (!p || *p != '"') return false;
    ++p;
    std::string s;
    while (*p && *p != '"') {
        if (*p == '\\') {
            ++p;
            switch (*p) {
                case 'n': s.push_back('\n'); break;
                case 't': s.push_back('\t'); break;
                case 'r': s.push_back('\r'); break;
                case '"': s.push_back('"'); break;
                case '\\': s.push_back('\\'); break;
                default: s.push_back(*p); break;
            }
        } else {
            s.push_back(*p);
        }
        ++p;
    }
    if (*p != '"') return false;   // unterminated: the line is truncated
    out = std::move(s);
    return true;
}

bool jsonFieldNumber(const std::string& line, const std::string& key, double& out) {
    const char* p = findKey(line, key);
    if (!p) return false;
    char* end = nullptr;
    const double v = std::strtod(p, &end);
    if (end == p) return false;
    out = v;
    return true;
}

bool jsonFieldBool(const std::string& line, const std::string& key, bool& out) {
    const char* p = findKey(line, key);
    if (!p) return false;
    if (std::strncmp(p, "true", 4) == 0)  { out = true;  return true; }
    if (std::strncmp(p, "false", 5) == 0) { out = false; return true; }
    return false;
}

const char* journalEventTypeName(JournalEventType t) {
    switch (t) {
        case JournalEventType::RunStarted:   return "run_started";
        case JournalEventType::ModeResult:   return "mode_result";
        case JournalEventType::CaseComplete: return "case_complete";
        case JournalEventType::RunFinished:  return "run_finished";
        case JournalEventType::RunCancelled: return "run_cancelled";
    }
    return "run_started";
}

bool journalEventTypeFromName(const std::string& s, JournalEventType& out) {
    if (s == "run_started")   { out = JournalEventType::RunStarted;   return true; }
    if (s == "mode_result")   { out = JournalEventType::ModeResult;   return true; }
    if (s == "case_complete") { out = JournalEventType::CaseComplete; return true; }
    if (s == "run_finished")  { out = JournalEventType::RunFinished;  return true; }
    if (s == "run_cancelled") { out = JournalEventType::RunCancelled; return true; }
    return false;
}

std::string journalTimestamp() { return isoNow(); }

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

BenchmarkJournalWriter::~BenchmarkJournalWriter() { close(); }

bool BenchmarkJournalWriter::open(const std::string& runsJsonlPath) {
    close();
    path_ = runsJsonlPath;
    out_.open(path_from_utf8(path_), std::ios::binary | std::ios::app);
    if (!out_.is_open()) return false;
    out_ << std::unitbuf;   // per-record durability, which is the granularity that matters
    records_ = 0;
    return true;
}

void BenchmarkJournalWriter::close() {
    if (out_.is_open()) { out_.flush(); out_.close(); }
}

bool BenchmarkJournalWriter::flush() {
    if (!out_.is_open()) return false;
    out_.flush();
    return out_.good();
}

bool BenchmarkJournalWriter::appendLine(const std::string& line) {
    if (!out_.is_open()) return false;
    out_ << line << "\n";
    if (!out_.good()) return false;
    ++records_;
    return true;
}

bool BenchmarkJournalWriter::writeRunStarted(const BenchmarkRun& run) {
    std::ostringstream o;
    o << "{\"journalSchemaVersion\":" << kBenchmarkJournalSchemaVersion
      << ",\"eventType\":\"run_started\""
      << ",\"recordId\":" << jstr("run_started:" + run.runId)
      << ",\"suiteId\":" << jstr(run.suiteId)
      << ",\"runId\":" << jstr(run.runId)
      << ",\"timestamp\":" << jstr(isoNow())
      << ",\"mediaScope\":" << jstr(msf::mediaScopeName(run.mediaScope))
      << ",\"scanImages\":" << jbool(run.scanImages)
      << ",\"scanVideos\":" << jbool(run.scanVideos)
      << ",\"sourceRoot\":" << jstr(run.sourceRoot)
      << ",\"sourceRootLabel\":" << jstr(run.sourceRootLabel)
      << ",\"sourceRootId\":" << jstr(run.sourceRootId)
      << ",\"datasetFingerprint\":" << jstr(run.datasetFingerprint)
      << ",\"buildVersion\":" << jstr(run.buildVersion)
      // Build provenance, additive to schema 1. S5 decision D defines the value as
      // the short commit id when git was usable at configure time and the literal
      // "unknown" otherwise, so an unavailable provenance is recorded as an
      // explicit state rather than as an empty claim or a silent omission.
      // Read back by S6 for commit-level cross-build comparison; the replay parser
      // treats a missing field as "this record predates provenance", which is what
      // keeps pre-existing journals readable.
      << ",\"gitCommit\":" << jstr(run.gitCommit.empty() ? std::string("unknown")
                                                          : run.gitCommit)
      << ",\"startedAt\":" << jstr(run.startedAt)
      << ",\"filesStarted\":" << run.filesStarted << "}";
    return appendLine(o.str());
}

bool BenchmarkJournalWriter::writeCaseComplete(const BenchmarkRun& run,
                                               const BenchmarkCaseResult& c) {
    // Per-mode records first, in the order S2 produced them, then the commit
    // marker. The marker is what makes the case committed during recovery.
    for (const auto& m : c.modeResults) {
        std::ostringstream o;
        o << "{\"journalSchemaVersion\":" << kBenchmarkJournalSchemaVersion
          << ",\"eventType\":\"mode_result\""
          << ",\"recordId\":" << jstr(modeRecordId(run.runId, c.caseId, m.requestedMode))
          << ",\"suiteId\":" << jstr(run.suiteId)
          << ",\"runId\":" << jstr(run.runId)
          << ",\"caseId\":" << jstr(c.caseId)
          << ",\"timestamp\":" << jstr(isoNow())
          << ",\"requestedMode\":" << jstr(gpuBackendKindName(m.requestedMode))
          << ",\"effectiveMode\":" << jstr(gpuBackendKindName(m.effectiveMode))
          << ",\"status\":" << jstr(statusName(m.status))
          << ",\"started\":" << jbool(m.started)
          << ",\"completed\":" << jbool(m.completed)
          << ",\"elapsedMs\":" << m.elapsedMs
          << ",\"summary\":" << summaryBody(m.summary)
          << ",\"errorMessage\":" << jstr(m.errorMessage)
          << "}";
        if (!appendLine(o.str())) return false;
    }
    std::ostringstream o;
    o << "{\"journalSchemaVersion\":" << kBenchmarkJournalSchemaVersion
      << ",\"eventType\":\"case_complete\""
      << ",\"recordId\":" << jstr(caseRecordId(run.runId, c.caseId))
      << ",\"suiteId\":" << jstr(run.suiteId)
      << ",\"runId\":" << jstr(run.runId)
      << ",\"caseId\":" << jstr(c.caseId)
      << ",\"timestamp\":" << jstr(isoNow())
      << ",\"path\":" << jstr(c.path)
      << ",\"media\":" << jstr(mediaName(c.media))
      << ",\"status\":" << jstr(statusName(c.status))
      << ",\"elapsedMs\":" << c.elapsedMs
      << ",\"errorMessage\":" << jstr(c.errorMessage)
      << "}";
    return appendLine(o.str());
}

bool BenchmarkJournalWriter::writeRunFinished(const BenchmarkRun& run) {
    std::ostringstream o;
    o << "{\"journalSchemaVersion\":" << kBenchmarkJournalSchemaVersion
      << ",\"eventType\":\"run_finished\""
      << ",\"recordId\":" << jstr("run_finished:" + run.runId)
      << ",\"suiteId\":" << jstr(run.suiteId)
      << ",\"runId\":" << jstr(run.runId)
      << ",\"timestamp\":" << jstr(isoNow())
      << ",\"status\":" << jstr(statusName(run.status))
      << ",\"filesStarted\":" << run.filesStarted
      << ",\"filesCompleted\":" << run.filesCompleted
      << ",\"filesRemaining\":" << run.filesRemaining
      << ",\"completedAt\":" << jstr(run.completedAt)
      << ",\"completionReason\":" << jstr("completed") << "}";
    return appendLine(o.str());
}

bool BenchmarkJournalWriter::writeRunCancelled(const BenchmarkRun& run, const std::string& reason) {
    std::ostringstream o;
    o << "{\"journalSchemaVersion\":" << kBenchmarkJournalSchemaVersion
      << ",\"eventType\":\"run_cancelled\""
      << ",\"recordId\":" << jstr("run_cancelled:" + run.runId)
      << ",\"suiteId\":" << jstr(run.suiteId)
      << ",\"runId\":" << jstr(run.runId)
      << ",\"timestamp\":" << jstr(isoNow())
      << ",\"status\":" << jstr(statusName(BenchmarkStatus::Cancelled))
      << ",\"filesStarted\":" << run.filesStarted
      << ",\"filesCompleted\":" << run.filesCompleted
      << ",\"filesRemaining\":" << run.filesRemaining
      << ",\"completionReason\":" << jstr(reason) << "}";
    return appendLine(o.str());
}

// ---------------------------------------------------------------------------
// Replay / recovery
// ---------------------------------------------------------------------------

JournalReplay replayJournal(const std::string& runsJsonlPath,
                            const std::string& runIdFilter) {
    JournalReplay r;
    const std::string blob = readFileIfExists(runsJsonlPath);
    if (blob.empty()) return r;

    // A record is a line terminated by '\n'. A trailing fragment with no newline
    // is an interrupted write: it is discarded and never guessed at.
    std::vector<std::string> lines;
    std::size_t pos = 0;
    bool truncatedTail = false;
    while (pos < blob.size()) {
        const std::size_t nl = blob.find('\n', pos);
        if (nl == std::string::npos) { truncatedTail = true; break; }
        lines.push_back(blob.substr(pos, nl - pos));
        pos = nl + 1;
    }
    if (truncatedTail) {
        JournalAnomaly a;
        a.kind = JournalAnomalyKind::TruncatedTail;
        a.detail = "final line had no terminating newline; discarded";
        a.lineNumber = lines.size();
        r.anomalies.push_back(a);
    }

    // recordId -> canonical line, for idempotency.
    std::map<std::string, std::string> seen;

    // Modes seen per case, until (and unless) a commit arrives. The key is
    // (runId, caseId), not caseId alone: a suite journal accumulates several runs,
    // and the same file yields the same caseId in every one of them. Keying on
    // caseId alone would merge one run's pending modes into another's and then
    // commit them as a single case.
    auto pendingKey = [](const std::string& runId, const std::string& caseId) {
        return runId + '\x1f' + caseId;
    };
    std::map<std::string, ReplayedCase> pending;
    std::vector<std::string> pendingOrder;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty()) continue;

        std::string evName, recordId;
        if (!jsonFieldString(line, "eventType", evName) || !jsonFieldString(line, "recordId", recordId)) {
            // A COMPLETE line that does not parse is interior corruption. This is
            // deliberately fatal: silently skipping an interior record would drop
            // evidence that the journal exists to preserve.
            JournalAnomaly a;
            a.kind = JournalAnomalyKind::MidFileCorruption;
            a.detail = "complete line is not a readable journal record";
            a.lineNumber = i + 1;
            r.anomalies.push_back(a);
            r.fatal = true;
            return r;
        }
        JournalEventType ev;
        if (!journalEventTypeFromName(evName, ev)) {
            JournalAnomaly a;
            a.kind = JournalAnomalyKind::MidFileCorruption;
            a.detail = "unknown eventType: " + evName;
            a.lineNumber = i + 1;
            r.anomalies.push_back(a);
            r.fatal = true;
            return r;
        }

        auto sit = seen.find(recordId);
        if (sit != seen.end()) {
            if (sit->second == line) {
                JournalAnomaly a;   // idempotent re-emission: ignore
                a.kind = JournalAnomalyKind::DuplicateIdentical;
                a.detail = recordId;
                a.lineNumber = i + 1;
                r.anomalies.push_back(a);
                continue;
            }
            JournalAnomaly a;       // same id, different payload: report, keep the first
            a.kind = JournalAnomalyKind::DuplicateConflicting;
            a.detail = recordId;
            a.lineNumber = i + 1;
            r.anomalies.push_back(a);
            continue;
        }
        seen[recordId] = line;

        std::string runId, caseId;
        jsonFieldString(line, "runId", runId);
        jsonFieldString(line, "caseId", caseId);

        // A suite journal holds every run that used the suite; a filtered replay
        // looks at one run only. Filtering happens after the integrity checks
        // above, so corruption in an unrelated run still surfaces as fatal.
        if (!runIdFilter.empty() && runId != runIdFilter) continue;

        switch (ev) {
            case JournalEventType::RunStarted: {
                r.runStarted = true;
                r.runId = runId;
                jsonFieldString(line, "suiteId", r.suiteId);
                jsonFieldString(line, "buildVersion", r.buildVersion);
                // Optional: a journal written before the field existed simply has
                // no value here, and that is reported as empty rather than guessed.
                jsonFieldString(line, "gitCommit", r.gitCommit);
                jsonFieldString(line, "startedAt", r.startedAt);
                break;
            }
            case JournalEventType::RunFinished: {
                r.runFinished = true;
                jsonFieldString(line, "completionReason", r.completionReason);
                break;
            }
            case JournalEventType::RunCancelled: {
                r.runFinished = true;
                r.runCancelled = true;
                jsonFieldString(line, "completionReason", r.completionReason);
                break;
            }
            case JournalEventType::ModeResult: {
                ReplayedModeResult m;
                m.recordId = recordId;
                std::string s;
                if (jsonFieldString(line, "requestedMode", s)) m.requestedMode = modeFromName(s);
                if (jsonFieldString(line, "effectiveMode", s)) m.effectiveMode = modeFromName(s);
                if (jsonFieldString(line, "status", s))     m.status = statusFromName(s);
                jsonFieldBool(line, "started", m.started);
                jsonFieldBool(line, "completed", m.completed);
                jsonFieldNumber(line, "elapsedMs", m.elapsedMs);
                jsonFieldString(line, "errorMessage", m.errorMessage);
                // The summary is written as a nested object; read the few fields
                // that matter for identity of a mode result. Counts are
                // informational for recovery, so a flat scan is sufficient and
                // avoids pretending the flat reader can walk nested objects.
                BenchmarkScanSummary sum;
                double v = 0.0;
                const std::size_t sp = line.find("\"summary\":{");
                if (sp != std::string::npos) {
                    const std::string sub = line.substr(sp);
                    if (jsonFieldNumber(sub, "scanned", v))  sum.scanned  = static_cast<std::size_t>(v);
                    if (jsonFieldNumber(sub, "analyzed", v)) sum.analyzed = static_cast<std::size_t>(v);
                }
                m.summary = sum;
                const std::string key = pendingKey(runId, caseId);
                auto it = pending.find(key);
                if (it == pending.end()) {
                    it = pending.emplace(key, ReplayedCase{}).first;
                    it->second.caseId = caseId;
                    pendingOrder.push_back(key);
                }
                it->second.modes.push_back(std::move(m));
                break;
            }
            case JournalEventType::CaseComplete: {
                ReplayedCase c;
                c.caseId = caseId;
                std::string s;
                if (jsonFieldString(line, "path", c.path)) {}
                if (jsonFieldString(line, "media", s)) c.media = mediaFromName(s);
                if (jsonFieldString(line, "status", s)) c.status = statusFromName(s);
                jsonFieldNumber(line, "elapsedMs", c.elapsedMs);
                jsonFieldString(line, "errorMessage", c.errorMessage);
                c.committed = true;
                // Carry the modes that preceded this commit, verbatim.
                const std::string key = pendingKey(runId, caseId);
                auto it = pending.find(key);
                if (it != pending.end()) {
                    c.modes = std::move(it->second.modes);
                    pending.erase(it);
                    pendingOrder.erase(std::remove(pendingOrder.begin(), pendingOrder.end(), key),
                                      pendingOrder.end());
                }
                r.cases.push_back(std::move(c));
                break;
            }
        }
    }

    // Anything still pending had modes but no commit: an incomplete transaction.
    for (const auto& key : pendingOrder) {
        auto it = pending.find(key);
        if (it == pending.end()) continue;
        ReplayedCase c = it->second;
        c.committed = false;
        r.incompleteCases.push_back(std::move(c));
    }
    return r;
}

std::string buildSummaryJson(const JournalReplay& r) {
    // Case totals are counted from the per-case statuses rather than assumed, so
    // the numbers describe what the journal actually contains.
    std::size_t success = 0, failed = 0, cancelled = 0, skipped = 0;
    double totalElapsedMs = 0.0;
    for (const auto& c : r.cases) {
        switch (c.status) {
            case BenchmarkStatus::Success:   ++success;   break;
            case BenchmarkStatus::Failed:    ++failed;    break;
            case BenchmarkStatus::Cancelled: ++cancelled; break;
            case BenchmarkStatus::Skipped:   ++skipped;   break;
        }
        totalElapsedMs += c.elapsedMs;
    }

    std::ostringstream o;
    o << "{\"journalSchemaVersion\":" << kBenchmarkJournalSchemaVersion
      << ",\"derivedFrom\":\"runs.jsonl\""
      << ",\"runId\":" << jstr(r.runId)
      << ",\"suiteId\":" << jstr(r.suiteId)
      << ",\"runStarted\":" << jbool(r.runStarted)
      << ",\"runFinished\":" << jbool(r.runFinished)
      << ",\"runCancelled\":" << jbool(r.runCancelled)
      << ",\"committedCases\":" << r.cases.size()
      << ",\"incompleteCases\":" << r.incompleteCases.size()
      << ",\"successCases\":" << success
      << ",\"failedCases\":" << failed
      << ",\"cancelledCases\":" << cancelled
      << ",\"skippedCases\":" << skipped
      << ",\"totalElapsedMs\":" << totalElapsedMs
      << ",\"anomalies\":" << r.anomalies.size()
      << ",\"note\":" << jstr("summary is regenerated from the journal and may omit per-file evidence; the journal is authoritative")
      << ",\"cases\":[";
    for (std::size_t i = 0; i < r.cases.size(); ++i) {
        const auto& c = r.cases[i];
        if (i) o << ",";
        o << "{\"caseId\":" << jstr(c.caseId)
          << ",\"path\":" << jstr(c.path)
          << ",\"status\":" << jstr(statusName(c.status))
          << ",\"elapsedMs\":" << c.elapsedMs
          << ",\"modes\":[";
        for (std::size_t k = 0; k < c.modes.size(); ++k) {
            const auto& m = c.modes[k];
            if (k) o << ",";
            o << "{\"requestedMode\":" << jstr(gpuBackendKindName(m.requestedMode))
              << ",\"effectiveMode\":" << jstr(gpuBackendKindName(m.effectiveMode))
              << ",\"status\":" << jstr(statusName(m.status))
              << ",\"elapsedMs\":" << m.elapsedMs << "}";
        }
        o << "]}";
    }
    o << "]}\n";
    return o.str();
}

} // namespace msf
