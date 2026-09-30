// S1 command line parser unit test.
//
// The parser is the only part of S1 that can be tested exhaustively without
// launching the application, and it is where the "invalid input must not be
// ignored" rule lives. These checks are deliberately blunt: each one asserts a
// decision, not an implementation detail.

#include <cstdio>
#include <string>
#include <vector>

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
        const auto unknown = parse({"--benchmark", "D"});
        chk(unknown.mode == msf::CommandMode::Error, "unknown option --benchmark -> Error");
        chk(unknown.errorMessage.find("unknown option") != std::string::npos,
            "  message names the unknown option");
    }
    {
        // Options reserved for later stages must not be accepted yet, otherwise
        // --help and reality would drift apart.
        for (const char* opt : {"--mode", "--suite", "--resource", "--cpu-percent",
                                "--log-dir", "--log"}) {
            const auto r = parse({opt, "x"});
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
