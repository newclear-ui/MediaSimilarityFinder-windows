#pragma once
// S1 Console Entry Foundation — command line parsing.
//
// Pure, dependency-free, and deliberately separated from main() so the parsing
// rules can be unit tested without launching the application. This file performs
// no I/O and includes no Qt headers: the GUI executable stays the only thing that
// decides whether a window is ever created.
//
// S1 scope is entry only. Options that belong to later stages (--benchmark,
// --mode, --suite, --resource, --cpu-percent, --log-dir, --log) are intentionally
// NOT parsed, and are reported as unknown options rather than silently accepted,
// so that --help can never advertise something that does not exist.

#include <string>
#include <vector>

namespace msf {

// Which entry path the process should take. Gui is the default so that running
// the executable with no arguments behaves exactly as it always has.
enum class CommandMode {
    Gui,       // no arguments: existing GUI
    Help,      // --help / -h
    Version,   // --version / -v
    Smoke,     // --smoke (existing headless GUI smoke hook, unchanged behaviour)
    Scan,      // --scan <folder>: headless scan, no MainWindow
    Error      // invalid arguments: report to stderr and exit non-zero
};

// --media selection. Maps onto the existing ScanControl::scanImages / scanVideos
// pair rather than introducing a parallel concept.
enum class MediaScope {
    Images,    // --media images
    Videos,    // --media videos
    All        // --media all (default)
};

const char* mediaScopeName(MediaScope s);

struct CommandLineOptions {
    CommandMode mode = CommandMode::Gui;
    MediaScope scope = MediaScope::All;
    std::string scanRoot;       // valid when mode == Scan
    std::string errorMessage;   // populated when mode == Error
    int exitCode = 0;           // valid when mode == Error

    // True when a MainWindow must be created. Only Gui and Smoke qualify; Smoke
    // builds the window offscreen, which is the pre-existing CI hook.
    bool needsMainWindow() const { return mode == CommandMode::Gui || mode == CommandMode::Smoke; }
};

// Parses argv. Never throws and never exits; the caller decides what to print.
// `argv` is expected to include argv[0] at index 0, matching main()'s signature.
CommandLineOptions parseCommandLine(int argc, const char* const* argv);

// Exit code used for every argument error, so callers and scripts have one
// stable value to test for.
constexpr int kCommandLineErrorExitCode = 2;

} // namespace msf
