#include "command_line.h"

#include <cstring>
#include <set>

#include "benchmark_core.h"   // benchmarkContractModes(): the canonical mode order
#include "benchmark_store.h"  // benchmarkModeDirName(): the one "auto"/"cpu"/"gpu-max" spelling

namespace msf {
namespace {

CommandLineOptions makeError(const std::string& message) {
    CommandLineOptions o;
    o.mode = CommandMode::Error;
    o.errorMessage = message;
    o.exitCode = kCommandLineErrorExitCode;
    return o;
}

// A value is a flag only if it starts with '-'. This is what lets
// "--scan --media" fail with "missing folder" instead of silently treating
// "--media" as a directory name.
bool looksLikeOption(const char* s) {
    return s && s[0] == '-' && s[1] != '\0';
}

// Resolves a --mode list. Returns false and fills `error` on a malformed value.
//
// Three rules, all fixed by the S5 brief:
//   * the token spelling comes from benchmarkModeDirName(), so the CLI cannot
//     drift from the names the storage layer already uses;
//   * the input order does not matter, the result is always S2's canonical order;
//   * duplicates and unknown names are rejected rather than silently ignored.
//
// Matching is case sensitive on purpose: --media already accepts only exact
// lowercase "images"/"videos"/"all", and inventing a second case policy here
// would make the CLI inconsistent with itself.
bool parseModeList(const std::string& value, std::vector<GpuBackendKind>& out, std::string& error) {
    const std::vector<GpuBackendKind>& canonical = benchmarkContractModes();

    std::set<GpuBackendKind> wanted;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = value.find(',', start);
        const std::string token =
            value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

        if (token.empty()) {
            error = "--mode has an empty entry: " + value;
            return false;
        }
        bool matched = false;
        for (GpuBackendKind m : canonical) {
            if (token == benchmarkModeDirName(m)) {
                if (!wanted.insert(m).second) {
                    error = "--mode contains a duplicate: " + token;
                    return false;
                }
                matched = true;
                break;
            }
        }
        if (!matched) {
            error = "invalid --mode value: " + token + " (expected auto, cpu or gpu-max)";
            return false;
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    // Emit in canonical order regardless of how the user typed them.
    out.clear();
    for (GpuBackendKind m : canonical) {
        if (wanted.count(m)) out.push_back(m);
    }
    return true;
}

} // namespace

const char* mediaScopeName(MediaScope s) {
    switch (s) {
        case MediaScope::Images: return "images";
        case MediaScope::Videos: return "videos";
        case MediaScope::All:    return "all";
    }
    return "all";
}

CommandLineOptions parseCommandLine(int argc, const char* const* argv) {
    CommandLineOptions o;

    // No arguments at all is the normal GUI launch and must stay that way.
    if (argc <= 1) return o;

    // --mode carries one comma list, so a second occurrence would silently discard
    // the first one. Remember that it was seen rather than trusting the result
    // value, because a repeated default is indistinguishable from an unset option.
    bool sawModeOption = false;
    bool sawSuiteOption = false;
    bool sawLogDirOption = false;
    bool sawLogOption = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!arg) continue;
        const std::string a(arg);

        if (a == "--help" || a == "-h")    { o.mode = CommandMode::Help;    return o; }
        if (a == "--version" || a == "-v")  { o.mode = CommandMode::Version; return o; }
        if (a == "--smoke")                 { o.mode = CommandMode::Smoke;   return o; }

        if (a == "--scan") {
            // The folder is mandatory. A following flag is not a folder, and
            // silently accepting one would turn a typo into a wrong scan root.
            if (i + 1 >= argc || looksLikeOption(argv[i + 1]))
                return makeError("--scan requires a folder argument");
            if (o.mode == CommandMode::Scan)
                return makeError("--scan may be given only once");
            // Order-independent guard: --benchmark checks for a pending --scan,
            // and this checks for a pending --benchmark.
            if (o.mode == CommandMode::Benchmark)
                return makeError("--scan and --benchmark cannot be combined");
            o.mode = CommandMode::Scan;
            o.scanRoot = argv[i + 1];
            ++i;  // consume the folder
            continue;
        }

        // ---- S5 benchmark entry ------------------------------------------------
        if (a == "--benchmark") {
            if (i + 1 >= argc || looksLikeOption(argv[i + 1]))
                return makeError("--benchmark requires a folder argument");
            if (o.mode == CommandMode::Benchmark)
                return makeError("--benchmark may be given only once");
            // A scan and a benchmark are the same execution path with different
            // outputs, so asking for both would leave "which one wins" undefined.
            if (o.mode == CommandMode::Scan)
                return makeError("--scan and --benchmark cannot be combined");
            if (argv[i + 1][0] == '\0')
                return makeError("--benchmark requires a non-empty folder argument");
            o.mode = CommandMode::Benchmark;
            o.benchmarkRoot = argv[i + 1];
            // --mode defaults to all three, in S2's canonical order. The default is
            // applied here rather than at construction time so that an explicit
            // --mode given before --benchmark is not overwritten by it.
            if (!sawModeOption) o.benchModes = benchmarkContractModes();
            ++i;  // consume the folder
            continue;
        }

        if (a == "--mode") {
            // Accepted only once: the brief fixes a single comma list rather than
            // a repeatable flag, so a repeated --mode is a mistake worth naming.
            if (sawModeOption)
                return makeError("--mode may be given only once");
            if (i + 1 >= argc || looksLikeOption(argv[i + 1]))
                return makeError("--mode requires a value: auto, cpu or gpu-max");
            std::string modeError;
            std::vector<GpuBackendKind> modes;
            if (!parseModeList(argv[i + 1], modes, modeError)) return makeError(modeError);
            sawModeOption = true;
            o.benchModes = std::move(modes);
            ++i;  // consume the value
            continue;
        }

        if (a == "--suite" || a == "--log-dir" || a == "--log") {
            // Same single-occurrence rule as --mode: each of these overrides a
            // storage or sink location, so a second one would lose the first value
            // without saying anything.
            bool* seen = (a == "--suite") ? &sawSuiteOption
                      : (a == "--log-dir") ? &sawLogDirOption
                                          : &sawLogOption;
            if (*seen) return makeError(a + " may be given only once");
            if (i + 1 >= argc || looksLikeOption(argv[i + 1]))
                return makeError(a + " requires a value");
            if (argv[i + 1][0] == '\0')
                return makeError(a + " requires a non-empty value");
            *seen = true;
            if (a == "--suite")   o.suiteId = argv[i + 1];
            if (a == "--log-dir") o.logDir  = argv[i + 1];
            if (a == "--log")     o.logFile = argv[i + 1];
            ++i;  // consume the value
            continue;
        }

        if (a == "--media") {
            if (i + 1 >= argc)
                return makeError("--media requires a value: images, videos or all");
            const std::string v(argv[i + 1]);
            if (v == "images")      o.scope = MediaScope::Images;
            else if (v == "videos") o.scope = MediaScope::Videos;
            else if (v == "all")    o.scope = MediaScope::All;
            else return makeError("invalid --media value: " + v
                                  + " (expected images, videos or all)");
            ++i;  // consume the value
            continue;
        }

        // Anything else is an error rather than something to ignore. Options
        // belonging to later stages (--resource, --cpu-percent, ...) land here on
        // purpose, so that help text and reality stay in agreement.
        return makeError("unknown option: " + a);
    }

    // --media without --scan is accepted but has no consumer in S1, because S1
    // has no other headless entry point yet. It is not an error, so that
    // "--media all" stays valid for a future caller, but nothing claims to apply
    // it during a GUI run.
    return o;
}

} // namespace msf
