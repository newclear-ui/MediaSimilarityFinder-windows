#pragma once
// S1 Console Entry Foundation ??command line parsing.
// S5 extends this with the benchmark entry point; the parsing rules stay here so
// they can be unit tested without launching the application. This file performs
// no I/O and includes no Qt headers: the GUI executable stays the only thing that
// decides whether a window is ever created.
//
// S1 scope was entry only. S5 adds --benchmark/--mode/--suite/--log-dir/--log,
// which are parsed here but NOT executed here: main.cpp wiring is a later stage.
// Options still belonging to later stages (--resource, --cpu-percent) remain
// unparsed and are reported as unknown options, so --help can never advertise
// something that does not exist.

#include <string>
#include <vector>

#include "gpu_backend.h"  // GpuBackendKind: S2's benchmark mode type, reused as is

namespace msf {

// Which entry path the process should take. Gui is the default so that running
// the executable with no arguments behaves exactly as it always has.
enum class CommandMode {
    Gui,       // no arguments: existing GUI
    Help,      // --help / -h
    Version,   // --version / -v
    Smoke,     // --smoke (existing headless GUI smoke hook, unchanged behaviour)
    Scan,      // --scan <folder>: headless scan, no MainWindow
    Benchmark, // --benchmark <folder>: headless benchmark, no MainWindow (S5)
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

    // --- S5 benchmark entry ------------------------------------------------
    std::string benchmarkRoot;  // valid when mode == Benchmark

    // The modes to execute, already normalised into S2's canonical order
    // (AUTO -> CPU-only -> GPU-max). The CLI token is "gpu-max"; it maps onto
    // GpuBackendKind::Cuda, which is what S2 already means by GPU-max. The vector
    // is handed to BenchmarkRunner::run unchanged, so this layer never reorders
    // anything: it only reports which subset the user picked.
    std::vector<GpuBackendKind> benchModes;

    std::string suiteId;        // --suite, empty when auto-generated later
    std::string logDir;         // --log-dir: benchmark durable storage root override
    std::string logFile;        // --log: human-readable renderer output sink

    // True when a MainWindow must be created. Only Gui and Smoke qualify; Smoke
    // builds the window offscreen, which is the pre-existing CI hook.
    // Benchmark deliberately does not qualify: the Console path never builds a
    // window, so it also never needs the Qt platform plugin.
    bool needsMainWindow() const { return mode == CommandMode::Gui || mode == CommandMode::Smoke; }
};

// Parses argv. Never throws and never exits; the caller decides what to print.
// `argv` is expected to include argv[0] at index 0, matching main()'s signature.
CommandLineOptions parseCommandLine(int argc, const char* const* argv);

// Exit code used for every argument error, so callers and scripts have one
// stable value to test for.
constexpr int kCommandLineErrorExitCode = 2;

} // namespace msf
