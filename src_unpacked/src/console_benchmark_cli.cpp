#include "console_benchmark_cli.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>
#include <functional>

#include "benchmark_console_renderer.h"
#include "benchmark_core.h"
#include "benchmark_session.h"
#include "benchmark_store.h"
#include "dataset_fingerprint.h"
#include "msf_build_version.h"
#include "path_utils.h"
#include "resource_policy.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

namespace msf {
namespace {

// ---------------------------------------------------------------------------
// Terminal detection
// ---------------------------------------------------------------------------
//
// Used only to choose between cursor repaint and plain line output, and to learn
// the width for abbreviation. It never changes what the renderer is told: both
// paths format the same presentation input.

bool consoleIsInteractive() {
#ifdef _WIN32
    const intptr_t raw = _get_osfhandle(_fileno(stdout));
    if (raw <= 0) return false;
    const DWORD t = GetFileType(reinterpret_cast<HANDLE>(raw));
    return t == FILE_TYPE_CHAR;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

std::size_t consoleTerminalWidth() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(out, &info)) return 0;
    const int w = info.srWindow.Right - info.srWindow.Left + 1;
    return w > 0 ? static_cast<std::size_t>(w) : 0;
#else
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------
//
// A console control handler runs on its own thread, so the flag it sets is the
// only state shared with the benchmark thread. It is atomic, and the runner
// polls it through BenchmarkRequest::isCancelled, which is S2's existing
// cancellation contract. No polling thread is introduced here.
//
// Returning TRUE tells Windows the event was handled, so the process is not torn
// down. The run then ends through S2's normal cancellation path and S3 writes its
// own terminal record; nothing is written by hand.
std::atomic<bool> g_cancelRequested{false};

#ifdef _WIN32
BOOL WINAPI consoleCtrlHandler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_cancelRequested.store(true);
            return TRUE;
        default:
            return FALSE;
    }
}
#endif

// Installs the handler for its own lifetime, so nothing else is left holding it.
class ConsoleCtrlScope {
public:
#ifdef _WIN32
    ConsoleCtrlScope() { active_ = (SetConsoleCtrlHandler(consoleCtrlHandler, TRUE) != FALSE); }
    ~ConsoleCtrlScope() {
        if (active_) SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);
    }
    bool installed() const { return active_; }
#else
    ConsoleCtrlScope() = default;
    bool installed() const { return false; }
#endif
private:
    bool active_ = false;
};

// ---------------------------------------------------------------------------
// Progress projection
// ---------------------------------------------------------------------------
//
// Progress is a count of cases that S2 reported as finished. Nothing here is
// estimated: A1 forbids a live file callback, so the renderer only ever receives
// completed cases plus the totals the run itself reported.
struct ConsoleProgressState {
    std::size_t filesTotal = 0;
    std::size_t filesCompleted = 0;
    std::size_t imagesTotal = 0;
    std::size_t imagesCompleted = 0;
    std::size_t videosTotal = 0;
    std::size_t videosCompleted = 0;
    std::size_t sequence = 0;
};

ConsoleCaseRow toCaseRow(const BenchmarkCaseResult& c, std::size_t sequence) {
    ConsoleCaseRow row;
    row.sequence = sequence;
    // The file name rather than the full path: the header already shows the root,
    // and a full path would need abbreviating on every history line.
    const std::size_t slash = c.path.find_last_of("/\\");
    row.label = (slash == std::string::npos) ? c.path : c.path.substr(slash + 1);
    row.media = c.media;
    if (c.sizeBytes > 0) row.sizeBytes = c.sizeBytes;
    row.modes.reserve(c.modeResults.size());
    for (const auto& m : c.modeResults) {
        ConsoleModeRow mr;
        mr.mode = m.requestedMode;
        mr.effectiveMode = m.effectiveMode;
        mr.status = m.status;
        // Only a measurement S2 actually took is offered to the renderer. A mode
        // that never ran has no elapsed time, and printing 0.0 would read as
        // "instantly fast" rather than "not run".
        if (m.started) mr.elapsedMs = m.elapsedMs;
        row.modes.push_back(std::move(mr));
    }
    return row;
}

// Counts media totals over a discovered file list. This is arithmetic over files
// S2 already enumerated: no hashing, no content read, no second scan pass.
void countMediaTotals(const std::vector<BenchmarkFileItem>& files,
                      ConsoleProgressState& s) {
    for (const auto& f : files) {
        if (f.media == MediaKind::Image) ++s.imagesTotal;
        else if (f.media == MediaKind::Video) ++s.videosTotal;
    }
    s.filesTotal = files.size();
}

// "Balanced 55%" style label for the header, built from the policy object itself
// so the displayed number is the number the executor will use.
std::string resourceLabel(const ResourcePolicy& p) {
    const char* name = "Custom";
    switch (p.mode) {
        case ResourceMode::Maximum:  name = "Maximum";  break;
        case ResourceMode::High:     name = "High";     break;
        case ResourceMode::Balanced: name = "Balanced"; break;
        case ResourceMode::Gaming:   name = "Gaming";   break;
        case ResourceMode::Custom:   name = "Custom";   break;
    }
    return std::string(name) + " " + std::to_string(p.cpuPercent) + "%";
}

}  // namespace

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

std::string consoleSuiteIdStamp(int year, int month, int day,
                                int hour, int minute, int second) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d-%02d",
                  year, month, day, hour, minute, second);
    return std::string(buf);
}

bool consoleSuiteIdIsSafe(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    // No separators, no dots, no drive or device syntax, no control characters.
    // benchmarkSuitePaths() concatenates the id straight into a directory name, so
    // anything that could change the path is refused here rather than escaped. The
    // storage layer keeps S3's behaviour unchanged.
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) return false;
    }
    if (id == "." || id == "..") return false;
    if (id.front() == '.') return false;  // would hide the suite directory
    return true;
}

std::string consoleModeOrderLabel(const std::vector<GpuBackendKind>& modes) {
    std::string out;
    for (const auto m : modes) {
        if (!out.empty()) out += " -> ";
        out += consoleModeLabel(m);
    }
    return out;
}

std::string resolveConsoleSuiteId(const std::string& requested,
                                  const std::string& stamp,
                                  const std::string& applicationDataRoot,
                                  std::string& errorOut) {
    if (!requested.empty()) {
        if (!consoleSuiteIdIsSafe(requested)) {
            errorOut =
                "invalid --suite value: expected letters, digits, '-', '_' or '.'";
            return std::string();
        }
        return requested;
    }

    if (stamp.empty() || !consoleSuiteIdIsSafe(stamp)) {
        errorOut = "could not build a suite id from the current time";
        return std::string();
    }

    // Brief 짠9-E: uniqueness is the S3 identity rule's call, so a generated id
    // walks to the next free "-2", "-3", ... variant instead of overwriting an
    // existing suite. The chosen id is returned, so the journal records exactly
    // the directory that is used.
    std::error_code ec;
    for (int suffix = 1; suffix < 10000; ++suffix) {
        const std::string candidate =
            (suffix == 1) ? stamp : stamp + "-" + std::to_string(suffix);
        const std::string dir =
            benchmarkSuitePaths(applicationDataRoot, candidate).suiteDir;
        if (!std::filesystem::exists(path_from_utf8(dir), ec)) return candidate;
    }
    errorOut = "could not find a free suite id";
    return std::string();
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

int runConsoleBenchmark(const ConsoleBenchmarkOptions& opt) {
    if (opt.sourceRoot.empty()) {
        std::cerr << "Error: --benchmark requires a folder argument\n";
        return kCommandLineErrorExitCode;
    }
    if (opt.modes.empty()) {
        std::cerr << "Error: --benchmark needs at least one mode\n";
        return kCommandLineErrorExitCode;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(path_from_utf8(opt.sourceRoot), ec)) {
        std::cerr << "Error: benchmark target folder does not exist: "
                  << opt.sourceRoot << "\n";
        return 1;
    }

    const std::string sourceRoot = path_to_utf8(path_from_utf8(opt.sourceRoot));
    // --log-dir replaces the storage root; empty keeps the portable application
    // directory, which is where the product already keeps its settings and index.
    const std::string applicationDataRoot =
        opt.storageRootOverride.empty() ? opt.applicationDirectory
                                        : opt.storageRootOverride;

    // ---- Suite and run identity --------------------------------------------
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif

    const int year = utc.tm_year + 1900;
    const int month = utc.tm_mon + 1;
    const int day = utc.tm_mday;
    const int hour = utc.tm_hour;
    const int minute = utc.tm_min;
    const int second = utc.tm_sec;

    std::string suiteError;
    const std::string suiteId =
        resolveConsoleSuiteId(opt.suiteId, consoleSuiteIdStamp(year, month, day, hour, minute, second),
                              applicationDataRoot, suiteError);
    if (suiteId.empty()) {
        std::cerr << "Error: " << suiteError << "\n";
        return kCommandLineErrorExitCode;
    }

    // The suite's runtime tree, lock and journal path all derive from the run id,
    // so it must exist before the session opens. S2's own counter restarts at 1 in
    // every process and would collide with an earlier run in the same suite.
    const std::string runId = "run-" + consoleSuiteIdStamp(year, month, day, hour, minute, second);

    // ---- Dataset fingerprint ------------------------------------------------
    // The product's own canonical value, computed exactly as the GUI computes it.
    // No new hash and no new serialisation: an unavailable or failed root leaves
    // the fingerprint empty, which is the existing contract.
    const std::string fingerprint = computeDatasetFingerprint(sourceRoot).fingerprint;

    // ---- Sinks --------------------------------------------------------------
    // The renderer writes to a std::ostream, so --log is only a different stream.
    // It stays a display artifact: the journal and summary.json come from S3 and
    // are never produced from these bytes.
    std::ofstream logStream;
    std::ostream* sink = &std::cout;
    if (!opt.logFile.empty()) {
        logStream.open(path_from_utf8(opt.logFile), std::ios::out | std::ios::trunc);
        if (!logStream.is_open()) {
            std::cerr << "Error: could not open --log file: " << opt.logFile << "\n";
            return 1;
        }
        sink = &logStream;
    }

    // A pipe, a redirect or a file sink is not a terminal, so cursor control would
    // only corrupt the record. Detected rather than assumed.
    const bool interactive = (sink == &std::cout) && consoleIsInteractive();
    BenchmarkConsoleRenderer renderer(
        *sink, interactive ? ConsoleOutputKind::Interactive : ConsoleOutputKind::LineOriented,
        interactive ? consoleTerminalWidth() : 0);

    // ---- S3 session ---------------------------------------------------------
    BenchmarkSessionConfig cfg;
    cfg.suiteId = suiteId;
    cfg.runId = runId;
    cfg.applicationDataRoot = applicationDataRoot;
    cfg.sourceRoot = sourceRoot;
    cfg.sourceRootLabel = sanitizeSourceLabel(sourceRoot);
    cfg.sourceRootId = shortRootId(sourceRoot);
    cfg.datasetFingerprint = fingerprint;
    cfg.buildVersion = MSF_BUILD_VERSION;
    cfg.mediaScope = opt.mediaScope;

    BenchmarkSession session(cfg);
    std::string sessionError;
    if (!session.open(sessionError)) {
        std::cerr << "Error: could not start the benchmark suite: " << sessionError << "\n";
        if (session.isBusy()) {
            std::cerr << "       another benchmark already holds this suite's lock\n";
        }
        return 1;
    }

    // ---- S2 request ---------------------------------------------------------
    BenchmarkRequest request;
    request.sourceRoot = sourceRoot;
    request.applicationDirectory = opt.applicationDirectory;
    request.buildVersion = MSF_BUILD_VERSION;
    request.mediaScope = opt.mediaScope;
    request.distance = opt.distance;
    request.suiteId = suiteId;
    request.runId = runId;
    // S3 journal provenance, from the same generated value the Console header
    // already displays. Reusing it here means the journal and the screen can
    // never disagree, and no second git lookup exists: nothing is executed here,
    // the binary's own build provenance is simply recorded.
    request.gitCommit = MSF_BUILD_GIT;
    // Brief 짠13: the console default is the same Balanced policy the product uses,
    // produced by the one existing policy factory. No console-only policy engine
    // and no second reading of a preset.
    request.resourcePolicy = make_policy(ResourceMode::Balanced);

    ConsoleCtrlScope ctrl;
    g_cancelRequested.store(false);
    request.isCancelled = [] { return g_cancelRequested.load(); };

    // ---- Presentation -------------------------------------------------------
    ConsoleProgressState progress;
    std::optional<ConsoleCaseRow> recentCase;
    bool wroteHeader = false;

    auto buildPresentation = [&]() {
        ConsolePresentationInput in;
        in.target = opt.sourceRoot;
        in.scopeLabel = [&] {
            // mediaScopeName() is the CLI/journal token spelling (lowercase) and is
            // reused verbatim elsewhere; the header just presents it in caps.
            std::string s = mediaScopeName(opt.mediaScope);
            for (char& c : s) {
                if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            }
            return s;
        }();
        in.modeOrderLabel = consoleModeOrderLabel(opt.modes);
        in.cpuResource = resourceLabel(*request.resourcePolicy);
        in.gpu = std::string(request.resourcePolicy->gpuEnabled
                                 ? gpuBackendKindName(GpuBackendKind::Cuda)
                                 : "OFF");
        in.distance = opt.distance;
        in.suiteId = suiteId;
        in.build = std::string(MSF_BUILD_VERSION);
        // S5 decision D: whatever the build system resolved, including the literal
        // "unknown". Never substituted here.
        in.git = std::string(MSF_BUILD_GIT);
        in.progress.filesTotal = progress.filesTotal;
        in.progress.filesCompleted = progress.filesCompleted;
        in.progress.imagesTotal = progress.imagesTotal;
        in.progress.imagesCompleted = progress.imagesCompleted;
        in.progress.videosTotal = progress.videosTotal;
        in.progress.videosCompleted = progress.videosCompleted;
        in.recentCase = recentCase;
        return in;
    };

    // Progress and history are observation only and write nothing. The journal
    // records for a case are written by S3's own hook inside the session.
    //
    // These are kept as named functions rather than being installed on the request
    // directly, because the session takes ownership of the request's hooks (see
    // below) and they have to be re-composed after attach().
    std::function<void(const BenchmarkRun&)> observeRunStarted = [&](const BenchmarkRun& run) {
        // S2's own total wins over the pre-counted one: it is what the run will
        // actually process.
        progress.filesTotal = run.filesStarted;
    };

    std::function<void(const BenchmarkCaseResult&)> observeCaseComplete =
        [&](const BenchmarkCaseResult& c) {
            // A1: only a COMPLETED case is reported. There is deliberately no
            // file-start hook, so the in-flight file is never named.
            ConsoleCaseRow row = toCaseRow(c, ++progress.sequence);
            ++progress.filesCompleted;
            if (c.media == MediaKind::Image) ++progress.imagesCompleted;
            else if (c.media == MediaKind::Video) ++progress.videosCompleted;

            recentCase = row;
            if (!wroteHeader) {
                renderer.writeHeader(buildPresentation());
                wroteHeader = true;
            }
            renderer.writeCompletedCase(row);
            if (interactive) renderer.writeHeader(buildPresentation());
        };

    ProductionBenchmarkExecutor executor(fingerprint);

    // Media totals need the discovered list, which the runner enumerates
    // internally. Counting them here means one extra directory enumeration before
    // the run: no content hashing and no analysis, so it is negligible next to the
    // per-file scans, and it is the only way to show the IMG/VID split the header
    // contract asks for. A file added or removed between the two enumerations
    // would make the split differ from the run's own total, which is why the
    // run-level count from onRunStarted stays the authoritative one.
    countMediaTotals(executor.discover(request), progress);

    session.attach(request);

    // attach() moves the request's hooks aside and installs the session's journal
    // writers, and only puts them back in detach(). A caller that wants to observe
    // the same events has to wrap whatever attach() installed, which is what these
    // three wrappers do. S3 is left exactly as it is, and the journal write stays
    // first, so the durable evidence never depends on the display layer running.
    {
        const auto sessionStarted = request.onRunStarted;
        request.onRunStarted = [sessionStarted, &observeRunStarted](const BenchmarkRun& run) {
            if (sessionStarted) sessionStarted(run);
            observeRunStarted(run);
        };
    }
    {
        const auto sessionCase = request.onCaseComplete;
        request.onCaseComplete =
            [sessionCase, &observeCaseComplete](const BenchmarkCaseResult& c) {
                if (sessionCase) sessionCase(c);
                observeCaseComplete(c);
            };
    }

    BenchmarkRunner runner(executor);

    const auto wallStart = std::chrono::steady_clock::now();
    BenchmarkRun result;
    try {
        // The single invocation, identical to the GUI's. Mode order, per-file
        // order, aggregation and cancellation are all S2's.
        result = runner.run(request, opt.modes);
    } catch (const std::exception& e) {
        std::cerr << "Error: benchmark failed: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Error: benchmark failed with an unknown error\n";
        return 1;
    }
    const double wallMs = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - wallStart)
                              .count();

    if (!wroteHeader) {
        renderer.writeHeader(buildPresentation());
        wroteHeader = true;
    }

    // ---- Summary ------------------------------------------------------------
    // Counts come from the run's own case results, which S2 produced. Nothing is
    // recomputed and nothing is inferred. Elapsed is the wall clock this process
    // measured around the run, not a sum of per-case times, so it is labelled by
    // what it is rather than presented as a benchmark measurement.
    ConsoleSummary summary;
    summary.cases = result.cases.size();
    std::size_t success = 0, failed = 0, cancelled = 0, skipped = 0;
    for (const auto& c : result.cases) {
        switch (c.status) {
            case BenchmarkStatus::Success:   ++success; break;
            case BenchmarkStatus::Failed:    ++failed; break;
            case BenchmarkStatus::Cancelled: ++cancelled; break;
            case BenchmarkStatus::Skipped:   ++skipped; break;
        }
    }
    summary.success = success;
    summary.failed = failed;
    summary.cancelled = cancelled;
    summary.skipped = skipped;
    summary.statusBanner = std::string(benchmarkStatusName(result.status));
    summary.elapsedMs = wallMs;

    renderer.writeSummary(summary);

    // ---- Where things landed ------------------------------------------------
    *sink << "\nSuite ID : " << suiteId
          << "\nRun ID   : " << runId
          << "\nJournal  : " << session.runsJsonlPath()
          << "\nSummary  : " << session.summaryPath()
          << "\nRecords  : " << session.recordsWritten()
          << "\nCtrl+C   : " << (ctrl.installed() ? "cancellation wired" : "no console handler")
          << "\nWall     : " << wallMs / 1000.0 << " s"
          << "\n";
    sink->flush();
    if (logStream.is_open()) logStream.close();

    // Cancellation shares the failure exit code because the CLI has no separate
    // one and the stage rules forbid inventing one. It stays distinguishable where
    // it matters: the run status in the journal, the summary banner, stderr, and
    // the partial journal left on disk.
    if (result.status == BenchmarkStatus::Cancelled) {
        std::cerr << "benchmark cancelled; partial journal kept at "
                  << session.runsJsonlPath() << "\n";
        return 1;
    }
    if (result.status == BenchmarkStatus::Failed) {
        std::cerr << "benchmark failed\n";
        return 1;
    }
    return 0;
}

}  // namespace msf
