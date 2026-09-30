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
#include <string>

#include "benchmark_console_renderer.h"
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

    std::printf("\nconsole_benchmark_cli_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
