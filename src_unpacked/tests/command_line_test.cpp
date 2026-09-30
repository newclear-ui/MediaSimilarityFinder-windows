// S1 command line parser unit test, extended by S5 with the benchmark entry.
//
// The parser is the only part that can be tested exhaustively without launching
// the application, and it is where the "invalid input must not be ignored" rule
// lives. These checks are deliberately blunt: each one asserts a decision, not an
// implementation detail.

#include <cstdio>
#include <string>
#include <vector>

#include "benchmark_core.h"   // benchmarkContractModes(), the canonical mode order
#include "benchmark_store.h"  // benchmarkModeDirName(), the canonical mode spelling
#include "command_line.h"

namespace {

int gChecks = 0, gFails = 0;
void chk(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) { ++gFails; std::printf("  [F] %s\n", what.c_str()); }
    else      { std::printf("  [ok] %s\n", what.c_str()); }
}

// Builds an argv array including argv[0], matching main()'s calling convention.
msf::CommandLineOptions parse(const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    argv.push_back("MediaSimilarityFinder.exe");
    for (const auto& a : args) argv.push_back(a.c_str());
    return msf::parseCommandLine(static_cast<int>(argv.size()), argv.data());
}

} // namespace

int main() {
    std::printf("S1 command line parser selfcheck\n\n");

    // ---- GUI is the default and must not change ----------------------------
    chk(parse({}).mode == msf::CommandMode::Gui, "no arguments -> GUI");
    chk(parse({}).needsMainWindow(), "  GUI needs a MainWindow");
    chk(parse({}).scope == msf::MediaScope::All, "  default scope is all");

    // ---- pre-existing short forms must keep working ------------------------
    chk(parse({"--help"}).mode == msf::CommandMode::Help, "--help -> Help");
    chk(parse({"-h"}).mode == msf::CommandMode::Help, "-h -> Help");
    chk(parse({"--version"}).mode == msf::CommandMode::Version, "--version -> Version");
    chk(parse({"-v"}).mode == msf::CommandMode::Version, "-v -> Version");
    chk(parse({"--smoke"}).mode == msf::CommandMode::Smoke, "--smoke -> Smoke");
    chk(parse({"--smoke"}).needsMainWindow(),
        "  --smoke still builds the window offscreen (pre-existing behaviour)");

    // ---- --scan ------------------------------------------------------------
    {
        const auto o = parse({"--scan", "D:\\Media"});
        chk(o.mode == msf::CommandMode::Scan, "--scan <folder> -> Scan");
        chk(o.scanRoot == "D:\\Media", "  folder captured");
        chk(!o.needsMainWindow(), "  Scan must NOT create a MainWindow");
        chk(o.scope == msf::MediaScope::All, "  default scope all");
    }

    // ---- --media -----------------------------------------------------------
    {
        const auto i = parse({"--scan", "D", "--media", "images"});
        chk(i.mode == msf::CommandMode::Scan && i.scope == msf::MediaScope::Images,
            "--media images -> Images");
        const auto v = parse({"--scan", "D", "--media", "videos"});
        chk(v.mode == msf::CommandMode::Scan && v.scope == msf::MediaScope::Videos,
            "--media videos -> Videos");
        const auto a = parse({"--scan", "D", "--media", "all"});
        chk(a.mode == msf::CommandMode::Scan && a.scope == msf::MediaScope::All,
            "--media all -> All");
        chk(parse({"--media", "images"}).scope == msf::MediaScope::Images,
            "--media without --scan is still parsed");
    }

    // ---- errors: never silently ignored ------------------------------------
    {
        const auto bad = parse({"--scan", "D", "--media", "invalid"});
        chk(bad.mode == msf::CommandMode::Error, "--media invalid -> Error");
        chk(bad.exitCode != 0, "  non-zero exit code");
        chk(!bad.errorMessage.empty(), "  error message present");
        chk(!bad.needsMainWindow(), "  no MainWindow on error");
    }
    {
        const auto noFolder = parse({"--scan"});
        chk(noFolder.mode == msf::CommandMode::Error, "--scan without folder -> Error");
        const auto followed = parse({"--scan", "--media", "all"});
        chk(followed.mode == msf::CommandMode::Error, "--scan followed by a flag -> Error");
        chk(followed.errorMessage.find("folder") != std::string::npos,
            "  message mentions the missing folder");
    }
    {
        const auto noValue = parse({"--scan", "D", "--media"});
        chk(noValue.mode == msf::CommandMode::Error, "--media without value -> Error");
    }
    {
        // S5 implemented --benchmark, so it is no longer an unknown option. The
        // still-reserved options below remain rejected, otherwise --help and
        // reality would drift apart.
        const auto b = parse({"--benchmark", "D"});
        chk(b.mode == msf::CommandMode::Benchmark, "--benchmark is now a known option");
        const auto unknown = parse({"--nope"});
        chk(unknown.mode == msf::CommandMode::Error, "a genuinely unknown option -> Error");
        chk(unknown.errorMessage.find("unknown option") != std::string::npos,
            "  message names the unknown option");
    }
    {
        // Options reserved for later stages must not be accepted yet, otherwise
        // --help and reality would drift apart. --benchmark/--mode/--suite/
        // --log-dir/--log left this list in S5.
        for (const char* opt : {"--resource", "--cpu-percent"}) {
            const auto r = parse({"--benchmark", "D", opt, "x"});
            chk(r.mode == msf::CommandMode::Error,
                std::string("not-yet-implemented option is rejected: ") + opt);
        }
    }
    {
        const auto dup = parse({"--scan", "A", "--scan", "B"});
        chk(dup.mode == msf::CommandMode::Error, "duplicate --scan -> Error");
    }
    {
        // A help or version flag wins immediately, matching the pre-existing
        // behaviour where those were handled before anything else.
        chk(parse({"--scan", "D", "--help"}).mode == msf::CommandMode::Help,
            "--help after other arguments still shows help");
    }

    // =====================================================================
    // S5 benchmark entry
    // =====================================================================
    {
        // Renders a mode list as the same "auto,cpu,gpu-max" spelling the CLI
        // accepts, so an expectation can be read without a translation table.
        auto modesText = [](const std::vector<msf::GpuBackendKind>& v) {
            std::string s;
            for (auto m : v) { if (!s.empty()) s += ","; s += msf::benchmarkModeDirName(m); }
            return s;
        };
        const std::string all = modesText(msf::benchmarkContractModes());

        // ---- the basic entry ------------------------------------------------
        {
            const auto o = parse({"--benchmark", "D:\\Media"});
            chk(o.mode == msf::CommandMode::Benchmark, "--benchmark <folder> -> Benchmark");
            chk(o.benchmarkRoot == "D:\\Media", "  target folder captured");
            chk(!o.needsMainWindow(), "  Benchmark must NOT create a MainWindow");
            chk(modesText(o.benchModes) == all, "  --mode defaults to auto,cpu,gpu-max");
            chk(o.suiteId.empty() && o.logDir.empty() && o.logFile.empty(),
                "  suite/log/log-dir default to empty");
            chk(o.scope == msf::MediaScope::All, "  default media scope is all");
        }

        // ---- single, two and three modes ------------------------------------
        {
            chk(modesText(parse({"--benchmark", "D", "--mode", "auto"}).benchModes) == "auto",
                "--mode auto -> auto");
            chk(modesText(parse({"--benchmark", "D", "--mode", "cpu"}).benchModes) == "cpu",
                "--mode cpu -> cpu");
            chk(modesText(parse({"--benchmark", "D", "--mode", "gpu-max"}).benchModes) == "gpu-max",
                "--mode gpu-max -> gpu-max");
            chk(modesText(parse({"--benchmark", "D", "--mode", "auto,cpu"}).benchModes) == "auto,cpu",
                "--mode auto,cpu -> two modes");
            chk(modesText(parse({"--benchmark", "D", "--mode", "cpu,gpu-max"}).benchModes) ==
                    "cpu,gpu-max",
                "--mode cpu,gpu-max -> two modes");
            chk(modesText(parse({"--benchmark", "D", "--mode", "auto,cpu,gpu-max"}).benchModes) == all,
                "--mode with all three is accepted");
        }

        // ---- input order never becomes execution order ----------------------
        {
            chk(modesText(parse({"--benchmark", "D", "--mode", "gpu-max,auto"}).benchModes) ==
                    "auto,gpu-max",
                "--mode gpu-max,auto normalises to auto,gpu-max");
            chk(modesText(parse({"--benchmark", "D", "--mode", "gpu-max,cpu,auto"}).benchModes) == all,
                "--mode gpu-max,cpu,auto normalises to canonical order");
        }

        // ---- rejected mode lists --------------------------------------------
        {
            const auto dup = parse({"--benchmark", "D", "--mode", "auto,auto"});
            chk(dup.mode == msf::CommandMode::Error, "--mode auto,auto -> Error (duplicate)");
            chk(dup.errorMessage.find("duplicate") != std::string::npos, "  message says duplicate");
            chk(dup.exitCode == msf::kCommandLineErrorExitCode, "  uses the shared error exit code");

            const auto unknownMode = parse({"--benchmark", "D", "--mode", "auto,foo"});
            chk(unknownMode.mode == msf::CommandMode::Error, "--mode auto,foo -> Error (unknown)");
            chk(unknownMode.errorMessage.find("foo") != std::string::npos, "  message names the token");

            chk(parse({"--benchmark", "D", "--mode", ""}).mode == msf::CommandMode::Error,
                "--mode with an empty value -> Error");
            chk(parse({"--benchmark", "D", "--mode", ","}).mode == msf::CommandMode::Error,
                "--mode ',' -> Error");
            chk(parse({"--benchmark", "D", "--mode", "auto,"}).mode == msf::CommandMode::Error,
                "--mode 'auto,' trailing comma -> Error");
            chk(parse({"--benchmark", "D", "--mode"}).mode == msf::CommandMode::Error,
                "--mode without a value -> Error");
            chk(parse({"--benchmark", "D", "--mode", "--suite", "x"}).mode == msf::CommandMode::Error,
                "--mode followed by a flag -> Error, not a mode named --suite");
            // Case handling follows the existing --media convention: exact
            // lowercase only, no new case policy invented for --mode.
            chk(parse({"--benchmark", "D", "--mode", "AUTO"}).mode == msf::CommandMode::Error,
                "--mode AUTO -> Error (case sensitive like --media)");
        }

        // ---- suite / log-dir / log are preserved verbatim -------------------
        {
            const auto o = parse({"--benchmark", "D", "--suite", "20260930-0801-01",
                                  "--log-dir", "D:\\BenchmarkLogs",
                                  "--log", "D:\\BenchmarkLogs\\console.txt"});
            chk(o.suiteId == "20260930-0801-01", "--suite value preserved");
            chk(o.logDir == "D:\\BenchmarkLogs", "--log-dir value preserved");
            chk(o.logFile == "D:\\BenchmarkLogs\\console.txt", "--log value preserved");
            for (const char* opt : {"--suite", "--log-dir", "--log"}) {
                chk(parse({"--benchmark", "D", opt}).mode == msf::CommandMode::Error,
                    std::string(opt) + " without a value -> Error");
                chk(parse({"--benchmark", "D", opt, ""}).mode == msf::CommandMode::Error,
                    std::string(opt) + " with an empty value -> Error");
                chk(parse({"--benchmark", "D", opt, "--media", "all"}).mode ==
                        msf::CommandMode::Error,
                    std::string(opt) + " followed by a flag -> Error");
                // A second occurrence would drop the first value without saying so,
                // so each of these is single-occurrence like --mode.
                chk(parse({"--benchmark", "D", opt, "first", opt, "second"}).mode ==
                        msf::CommandMode::Error,
                    std::string(opt) + " given twice -> Error");
            }
        }

        // ---- --mode is single-occurrence, not just single-value --------------
        // "--mode auto --mode cpu" is the shape the brief calls out, and it must be
        // rejected as a repeat rather than quietly keeping only the last list.
        {
            chk(parse({"--benchmark", "D", "--mode", "auto", "--mode", "cpu"}).mode ==
                    msf::CommandMode::Error,
                "--mode auto --mode cpu -> Error");
            chk(parse({"--benchmark", "D", "--mode", "auto", "--mode", "auto"}).mode ==
                    msf::CommandMode::Error,
                "--mode auto --mode auto -> Error");
        }

        // ---- option order must not change the result ------------------------
        // The all-three default is filled in where --benchmark is handled, so an
        // explicit --mode given first has to survive that. These pairs assert the
        // same outcome for both orders.
        {
            const auto before = parse({"--benchmark", "D", "--mode", "auto"});
            const auto after  = parse({"--mode", "auto", "--benchmark", "D"});
            chk(before.benchModes == after.benchModes,
                "--mode auto --benchmark D and the reverse agree");
            chk(before.benchModes.size() == 1 &&
                    before.benchModes[0] == msf::GpuBackendKind::Auto,
                "--mode before --benchmark is not overwritten by the all-three default");
            const auto defBefore = parse({"--mode", "gpu-max,auto", "--benchmark", "D"});
            chk(defBefore.benchModes.size() == 2 &&
                    defBefore.benchModes[0] == msf::GpuBackendKind::Auto &&
                    defBefore.benchModes[1] == msf::GpuBackendKind::Cuda,
                "--mode before --benchmark is still canonicalized to AUTO, GPU-max");
        }

        // ---- --benchmark argument errors ------------------------------------
        {
            chk(parse({"--benchmark"}).mode == msf::CommandMode::Error,
                "--benchmark without a folder -> Error");
            chk(parse({"--benchmark", "--media", "all"}).mode == msf::CommandMode::Error,
                "--benchmark followed by a flag -> Error");
            chk(parse({"--benchmark", ""}).mode == msf::CommandMode::Error,
                "--benchmark with an empty folder -> Error");
            chk(parse({"--benchmark", "A", "--benchmark", "B"}).mode == msf::CommandMode::Error,
                "duplicate --benchmark -> Error");
            chk(parse({"--scan", "A", "--benchmark", "B"}).mode == msf::CommandMode::Error,
                "--scan and --benchmark cannot be combined");
            chk(parse({"--benchmark", "A", "--scan", "B"}).mode == msf::CommandMode::Error,
                "  in either order");
        }

        // ---- --media is reused unchanged for benchmark ----------------------
        {
            const auto i = parse({"--benchmark", "D:\\Media", "--media", "images"});
            chk(i.mode == msf::CommandMode::Benchmark && i.scope == msf::MediaScope::Images,
                "--benchmark ... --media images -> Images");
            chk(parse({"--benchmark", "D", "--media", "videos"}).scope == msf::MediaScope::Videos,
                "--media videos with --benchmark");
            chk(parse({"--benchmark", "D", "--media", "all"}).scope == msf::MediaScope::All,
                "--media all with --benchmark");
            chk(parse({"--benchmark", "D", "--media", "sideways"}).mode == msf::CommandMode::Error,
                "an invalid --media value is still rejected for --benchmark");
        }

        // ---- options before --benchmark are still honoured ------------------
        {
            const auto o = parse({"--suite", "s1", "--benchmark", "D", "--mode", "auto"});
            chk(o.mode == msf::CommandMode::Benchmark && o.suiteId == "s1" &&
                    o.benchModes.size() == 1,
                "benchmark options work in any order");
            chk(parse({"--benchmark", "D", "--help"}).mode == msf::CommandMode::Help,
                "--help after --benchmark still shows help");
        }

        // ---- determinism ----------------------------------------------------
        {
            const auto a = parse({"--benchmark", "D", "--mode", "gpu-max,auto"});
            const auto b = parse({"--benchmark", "D", "--mode", "gpu-max,auto"});
            chk(modesText(a.benchModes) == modesText(b.benchModes) &&
                    a.benchmarkRoot == b.benchmarkRoot,
                "benchmark parsing is deterministic");
        }
    }

    // ---- exit code is a single stable value --------------------------------
    chk(msf::kCommandLineErrorExitCode != 0, "argument errors use a non-zero exit code");

    // ---- scope naming ------------------------------------------------------
    chk(std::string(msf::mediaScopeName(msf::MediaScope::Images)) == "images", "scope name images");
    chk(std::string(msf::mediaScopeName(msf::MediaScope::Videos)) == "videos", "scope name videos");
    chk(std::string(msf::mediaScopeName(msf::MediaScope::All)) == "all", "scope name all");

    // ---- determinism -------------------------------------------------------
    {
        const auto a = parse({"--scan", "D", "--media", "videos"});
        const auto b = parse({"--scan", "D", "--media", "videos"});
        chk(a.mode == b.mode && a.scanRoot == b.scanRoot && a.scope == b.scope,
            "parsing is deterministic");
    }

    std::printf("\ncli_parser_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
