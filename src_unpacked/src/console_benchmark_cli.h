#pragma once
// S5 Console benchmark orchestration.
//
// SCOPE. This is the CLI wiring layer between the parsed options and the existing
// S2/S3 benchmark pipeline. It contains no scanning, no search and no result
// aggregation of its own:
//
//   CommandLineOptions
//        -> runConsoleBenchmark
//             -> BenchmarkSession (S3: lock, journal, summary)
//                  -> BenchmarkRequest
//                       -> BenchmarkRunner (S2: order, aggregation, cancel)
//                            -> ProductionBenchmarkExecutor -> product scan path
//                  -> BenchmarkConsoleRenderer (S5: display only)
//
// Everything below the session is reused unchanged. The single BenchmarkRunner
// invocation is the same one the GUI makes; only the caller differs.
//
// It is Qt free and takes the application directory as a parameter so it can be
// unit tested without an application object.

#include <string>
#include <vector>

#include "benchmark_core.h"  // GpuBackendKind, MediaScope
#include "command_line.h"     // MediaScope, kCommandLineErrorExitCode

namespace msf {

// Everything runConsoleBenchmark needs, already validated by the parser.
struct ConsoleBenchmarkOptions {
    std::string sourceRoot;
    std::string applicationDirectory;  // QCoreApplication::applicationDirPath()

    // --log-dir: benchmark durable storage root override. Empty means the
    // portable application directory, which is where the product already keeps
    // its settings and index. This is the storage root, NOT a text log location.
    std::string storageRootOverride;

    // --log: human-readable renderer output file sink. Empty means stdout. This
    // is a display artifact and is never handed to the journal or to summary.json.
    std::string logFile;

    // --suite: empty means generate one.
    std::string suiteId;

    MediaScope mediaScope = MediaScope::All;

    // Already canonicalised by the S5 parser. Never re-parsed from a string here.
    std::vector<GpuBackendKind> modes;

    unsigned distance = 8;
};

// ---------------------------------------------------------------------------
// Pure helpers, separated so they can be tested without running a benchmark
// ---------------------------------------------------------------------------

// "YYYYMMDD-HHMM-SS" from an already-fetched UTC timestamp, per the S5 brief's
// 짠9-E format. Split out from the resolution step so the format is testable
// without touching the filesystem or the clock.
std::string consoleSuiteIdStamp(int year, int month, int day,
                                int hour, int minute, int second);

// Whether `id` is safe to interpolate into a suite directory name.
//
// This is a CLI boundary check, not a storage change: benchmarkSuitePaths()
// concatenates the id straight into a path, so a value like "../../evil" or
// "a/b" would place the suite outside the storage root. The storage layer is left
// exactly as S3 defined it and the value is rejected here instead.
bool consoleSuiteIdIsSafe(const std::string& id);

// "AUTO -> CPU -> GPU-MAX" for the header. The label is built from the caller's
// already-ordered list, so it can only ever show the order that will be used.
std::string consoleModeOrderLabel(const std::vector<GpuBackendKind>& modes);

// Resolves the suite id to use.
//
// A caller-supplied id is returned unchanged, after the safety check, because the
// brief says the S3 identity rules decide uniqueness. An absent id is stamped
// from `stamp`, and on collision the next free "-2", "-3", ... variant is chosen,
// so the id the journal records is the id the directory actually uses.
std::string resolveConsoleSuiteId(const std::string& requested,
                                  const std::string& stamp,
                                  const std::string& applicationDataRoot,
                                  std::string& errorOut);

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

// Runs one Console benchmark and writes the renderer output.
//
// Returns a process exit code following the existing CLI convention:
//   0   the run completed
//   2   an argument the parser could not reject on its own (kCommandLineErrorExitCode)
//   1   the run failed, could not be started, OR was cancelled
//
// Cancellation deliberately shares 1 with failure rather than introducing a new
// code: the CLI already has no separate cancellation code, and the stage rules
// forbid inventing one. A cancelled run is distinguishable where it actually
// matters, in the run status the journal records and the summary banner, and the
// renderer prints it.
//
// No MainWindow is constructed on this path, ever.
int runConsoleBenchmark(const ConsoleBenchmarkOptions& options);

// ---------------------------------------------------------------------------
// Recovery (0.9.4.126)
// ---------------------------------------------------------------------------

// Everything runConsoleBenchmarkRecover needs. The storage root and the suite
// id locate the suite; runId scopes the replay (empty replays every run in
// the journal, per replayJournal()'s own contract).
struct ConsoleBenchmarkRecoverOptions {
    std::string storageRoot;  // --benchmark-recover <log-dir>
    std::string suiteId;      // --suite (mandatory, safety-checked)
    std::string runId;        // --run (empty = every run in the journal)
};

// Regenerates a suite's summary.json by replaying runs.jsonl, without
// scanning anything and without taking the suite lock.
//
// Returns a process exit code following the existing CLI convention:
//   0   summary.json regenerated (journal replay clean)
//   1   recovery refused or failed: unsafe suite id, missing journal, a live
//       run holds the suite lock, fatal journal corruption, or the summary
//       could not be written
//
// The journal is opened read-only and is never appended to: recovery is a
// view rebuild, so an interrupted suite keeps exactly the evidence it had.
// No MainWindow is constructed on this path, ever.
int runConsoleBenchmarkRecover(const ConsoleBenchmarkRecoverOptions& options);

}  // namespace msf
