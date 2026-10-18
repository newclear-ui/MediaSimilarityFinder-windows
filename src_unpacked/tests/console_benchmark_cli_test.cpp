// S5 Console benchmark orchestration unit test.
//
// The orchestration layer is the one place where a CLI value becomes a storage
// path or an identity, so these checks concentrate on the decisions that are
// cheap to get wrong and expensive to discover late: the suite id format, the
// path-traversal refusal, the collision suffix, and the mode order label.
//
// A real run is not simulated here. Cancellation and the journal sequence are
// S2/S3 contracts and are covered by benchmark_core_test and
// benchmark_integration_test, which drive the same seam this file's
// isCancelled wiring feeds.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "benchmark_console_renderer.h"
#include "benchmark_journal.h"  // replayJournal(), to confirm the recovered summary source
#include "benchmark_store.h"  // benchmarkSuitePaths(), to build the colliding suite dir
#include "console_benchmark_cli.h"

namespace {

int gChecks = 0;
int gFails = 0;

void chk(bool ok, const std::string& what) {
    ++gChecks;
    std::printf(ok ? "  [ok] %s\n" : "  [F] %s\n", what.c_str());
    if (!ok) ++gFails;
}

}  // namespace

int main() {
    using namespace msf;

    std::printf("--- suite id format (brief 9-E) ---\n");
    {
        chk(consoleSuiteIdStamp(2026, 10, 1, 4, 32, 1) == "20261001-0432-01",
            "YYYYMMDD-HHMM-SS format");
        chk(consoleSuiteIdStamp(2026, 1, 2, 3, 4, 5) == "20260102-0304-05",
            "single digit fields are zero padded");
        chk(consoleSuiteIdStamp(2026, 10, 1, 4, 32, 1).size() == 16,
            "stamp is a fixed width (8 date + 1 + 4 time + 1 + 2 seconds)");
    }

    std::printf("--- suite id safety (path traversal refusal) ---\n");
    {
        // benchmarkSuitePaths() concatenates the id straight into a directory
        // name, so anything that could change the path must be refused here.
        chk(consoleSuiteIdIsSafe("TEST-SUITE-001"), "plain id accepted");
        chk(consoleSuiteIdIsSafe("20261001-0432-01"), "generated id shape accepted");
        chk(consoleSuiteIdIsSafe("a.b_c-1"), "dots, underscores and dashes accepted");

        chk(!consoleSuiteIdIsSafe(""), "empty id refused");
        chk(!consoleSuiteIdIsSafe(".."), "dot-dot refused");
        chk(!consoleSuiteIdIsSafe("."), "dot refused");
        chk(!consoleSuiteIdIsSafe(".hidden"), "leading dot refused");
        chk(!consoleSuiteIdIsSafe("..\\..\\evil"), "windows traversal refused");
        chk(!consoleSuiteIdIsSafe("../../evil"), "posix traversal refused");
        chk(!consoleSuiteIdIsSafe("a/b"), "separator refused");
        chk(!consoleSuiteIdIsSafe("a\\b"), "backslash refused");
        chk(!consoleSuiteIdIsSafe("C:evil"), "drive syntax refused");
        chk(!consoleSuiteIdIsSafe("a b"), "space refused");
        chk(!consoleSuiteIdIsSafe("a\nb"), "control character refused");
        chk(!consoleSuiteIdIsSafe(std::string(200, 'a')), "absurd length refused");
    }

    std::printf("--- suite id resolution ---\n");
    {
        // A temporary storage root so the collision walk can be exercised without
        // touching the product's real benchmark storage.
        const auto root = std::filesystem::temp_directory_path() / "msf_s5_suiteid_test";
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        const std::string appdata = root.string();
        std::string err;

        chk(resolveConsoleSuiteId("EXPLICIT", "20261001-0432-01", appdata, err) == "EXPLICIT",
            "an explicit id is used unchanged");

        err.clear();
        chk(resolveConsoleSuiteId("../evil", "20261001-0432-01", appdata, err).empty(),
            "an unsafe explicit id is refused");
        chk(err.find("invalid --suite") != std::string::npos,
            "  and the reason names the option");

        err.clear();
        const std::string first = resolveConsoleSuiteId("", "20261001-0432-01", appdata, err);
        chk(first == "20261001-0432-01", "a free stamp is used as-is");
        chk(err.empty(), "no error for a free stamp");

        // Occupy that directory so the next resolution has to walk to the suffix.
        std::filesystem::create_directories(
            std::filesystem::path(benchmarkSuitePaths(appdata, first).suiteDir), ec);

        err.clear();
        const std::string second = resolveConsoleSuiteId("", "20261001-0432-01", appdata, err);
        chk(second == "20261001-0432-01-2", "an occupied stamp walks to the -2 variant");
        chk(err.empty(), "no error when a suffix is found");

        std::filesystem::create_directories(
            std::filesystem::path(benchmarkSuitePaths(appdata, second).suiteDir), ec);
        const std::string third = resolveConsoleSuiteId("", "20261001-0432-01", appdata, err);
        chk(third == "20261001-0432-01-3", "and then to -3");

        // The chosen id must be one the storage layer will accept, otherwise the
        // journal would record an id that is not the directory in use.
        chk(consoleSuiteIdIsSafe(first) && consoleSuiteIdIsSafe(second) && consoleSuiteIdIsSafe(third),
            "every generated id is storage-safe");
    }

    std::printf("--- mode order label ---\n");
    {
        chk(consoleModeOrderLabel({GpuBackendKind::Auto, GpuBackendKind::Cpu, GpuBackendKind::Cuda})
                == "AUTO -> CPU -> GPU-MAX",
            "the contract order is shown in full");
        chk(consoleModeOrderLabel({GpuBackendKind::Auto}) == "AUTO", "a single mode is labelled");
        chk(consoleModeOrderLabel({}) .empty(), "no modes yields no label rather than a guess");

        // The label must reflect the order it is given, because that is the order
        // the runner will use. It must not re-sort behind the caller's back.
        chk(consoleModeOrderLabel({GpuBackendKind::Cuda, GpuBackendKind::Auto})
                == "GPU-MAX -> AUTO",
            "the caller's order is echoed, not re-sorted");
    }

    std::printf("--- renderer integration surface ---\n");
    {
        // The orchestrator labels the header with the renderer's own mode names,
        // so a mode the renderer cannot name must not appear in the label either.
        chk(consoleModeOrderLabel({GpuBackendKind::Auto, GpuBackendKind::Cpu, GpuBackendKind::Cuda})
                == std::string("AUTO -> CPU -> ") + consoleModeLabel(GpuBackendKind::Cuda),
            "the orchestrator and renderer agree on mode names");
    }

    std::printf("--- benchmark recovery (0.9.4.126) ---\n");
    {
        // A scratch storage root so recovery is exercised without touching the
        // product's real benchmark storage. The journal below is the smallest
        // shape replayJournal() accepts: one run_started plus its terminal
        // record (field spellings follow benchmark_journal_test's fixtures).
        const auto root = std::filesystem::temp_directory_path() / "msf_s5_recover_test";
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        const std::string appdata = root.string();

        msf::ConsoleBenchmarkRecoverOptions bad{};
        chk(msf::runConsoleBenchmarkRecover(bad) != 0,
            "an empty request is refused, not guessed");
        bad.storageRoot = appdata;
        chk(msf::runConsoleBenchmarkRecover(bad) != 0,
            "  a missing suite id is refused");
        bad.suiteId = "../evil";
        chk(msf::runConsoleBenchmarkRecover(bad) != 0,
            "  a traversal suite id is refused");

        msf::ConsoleBenchmarkRecoverOptions missing{appdata, "NOPE", ""};
        chk(msf::runConsoleBenchmarkRecover(missing) != 0,
            "a suite with no journal is refused");

        // A corrupt interior line is fatal by journal contract, so recovery
        // must fail rather than publish a summary from half the evidence.
        {
            const auto paths = msf::benchmarkSuitePaths(appdata, "CORRUPT");
            std::filesystem::create_directories(
                std::filesystem::path(paths.suiteDir), ec);
            std::ofstream out(msf::path_from_utf8(paths.runsJsonl),
                              std::ios::binary | std::ios::trunc);
            out << "{\"journalSchemaVersion\":1,\"eventType\":\"run_started\","
                   "\"recordId\":\"run_started:r1\",\"suiteId\":\"CORRUPT\","
                   "\"runId\":\"r1\",\"buildVersion\":\"0.9.4.126\","
                   "\"startedAt\":\"2026-10-18T00:00:00\",\"filesStarted\":1}\n"
                   "this is not json\n";
            out.close();
            msf::ConsoleBenchmarkRecoverOptions opt{appdata, "CORRUPT", ""};
            chk(msf::runConsoleBenchmarkRecover(opt) != 0,
                "a journal with a corrupt interior line is refused");
            chk(!std::filesystem::exists(msf::path_from_utf8(paths.summaryJson), ec),
                "  and no summary is published from it");
        }

        // The happy path: a finished run replays into summary.json without any
        // scan and without the session ever being opened.
        {
            const auto paths = msf::benchmarkSuitePaths(appdata, "RECOVER");
            std::filesystem::create_directories(
                std::filesystem::path(paths.suiteDir), ec);
            std::ofstream out(msf::path_from_utf8(paths.runsJsonl),
                              std::ios::binary | std::ios::trunc);
            out << "{\"journalSchemaVersion\":1,\"eventType\":\"run_started\","
                   "\"recordId\":\"run_started:r1\",\"suiteId\":\"RECOVER\","
                   "\"runId\":\"r1\",\"buildVersion\":\"0.9.4.126\","
                   "\"startedAt\":\"2026-10-18T00:00:00\",\"filesStarted\":1}\n"
                   "{\"journalSchemaVersion\":1,\"eventType\":\"run_finished\","
                   "\"recordId\":\"run_finished:r1\",\"runId\":\"r1\","
                   "\"status\":\"SUCCESS\",\"filesStarted\":1,\"filesCompleted\":1,"
                   "\"filesRemaining\":0,\"completedAt\":\"2026-10-18T00:01:00\","
                   "\"completionReason\":\"completed\"}\n";
            out.close();
            msf::ConsoleBenchmarkRecoverOptions opt{appdata, "RECOVER", ""};
            chk(msf::runConsoleBenchmarkRecover(opt) == 0,
                "a finished journal recovers to exit 0");
            const std::string sum = msf::readFileIfExists(paths.summaryJson);
            chk(!sum.empty(), "  summary.json is written");
            chk(sum.find("\"derivedFrom\":\"runs.jsonl\"") != std::string::npos,
                "  the summary declares the journal as its source");
            chk(sum.find("\"runFinished\":true") != std::string::npos,
                "  the summary carries the replayed terminal state");
        }

        // A live run owns the journal stream: recovery over a held lock would
        // publish a view the run's own finalize immediately supersedes.
        {
            const auto paths = msf::benchmarkSuitePaths(appdata, "BUSY");
            std::filesystem::create_directories(
                std::filesystem::path(paths.suiteDir), ec);
            {
                std::ofstream out(msf::path_from_utf8(paths.runsJsonl),
                                  std::ios::binary | std::ios::trunc);
                out << "\n";
            }
            msf::BenchmarkSuiteLock held;
            chk(held.acquire(paths.lockFile), "test holds the suite lock");
            msf::ConsoleBenchmarkRecoverOptions opt{appdata, "BUSY", ""};
            chk(msf::runConsoleBenchmarkRecover(opt) != 0,
                "recovery under a held lock is refused");
        }

        std::filesystem::remove_all(root, ec);
    }

    std::printf("\nconsole_benchmark_cli_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
