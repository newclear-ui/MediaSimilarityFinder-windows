#pragma once
// S5 Console benchmark renderer. Display only.
//
// The renderer turns data that S2/S3 already produced into terminal lines. It
// executes nothing, opens no suite, writes no journal and touches no
// filesystem, which is what makes it unit testable without a real benchmark.
//
// The central rule of this file is that absence of data must stay visible as
// absence. S2 exposes no live per-file callback, so there is no running file,
// no running mode and no elapsed time for work in flight. The presentation
// types below therefore use std::optional for every measured or reported value,
// and an absent value is omitted from the output instead of being replaced by a
// zero, a dash or a guess. Reusing S2's own enums (GpuBackendKind,
// BenchmarkStatus, MediaKind) rather than declaring display-only copies is what
// keeps the renderer from being able to name a state the engine never produced.

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "benchmark_core.h"  // GpuBackendKind, BenchmarkStatus, MediaKind
#include "gpu_backend.h"

namespace msf {

// ---------------------------------------------------------------------------
// Presentation model
// ---------------------------------------------------------------------------
//
// These are presentation models. They carry no execution, scanning, journal,
// storage, cancellation or resource-policy responsibility: whoever assembles a
// ConsolePresentationInput is responsible for the data, and this renderer is
// responsible only for how it looks.

// Display name for a requested benchmark mode.
//
// This is not gpuBackendKindName(): that returns the backend name, so the
// GPU-max mode would read "CUDA". S2 already means GPU-max by Cuda
// (benchmark_core.h), so the benchmark display name is spelled out here while
// the backend name keeps its own meaning.
const char* consoleModeLabel(GpuBackendKind mode); // "AUTO" / "CPU" / "GPU-MAX"

// One completed mode execution for one case. `elapsedMs` is absent when the
// mode produced no measurement, which is distinct from a measured 0.0.
struct ConsoleModeRow {
    GpuBackendKind mode = GpuBackendKind::Auto;

    // The backend S2 actually resolved this request to. Optional because an
    // unobserved resolution must stay unshown rather than be guessed from the
    // requested mode.
    std::optional<GpuBackendKind> effectiveMode;

    // Straight from S2. There is deliberately no way to express a running state
    // here, because S2 does not report one for work in flight.
    BenchmarkStatus status = BenchmarkStatus::Skipped;

    // Absent when the mode produced no measurement, which is not the same as a
    // measured 0.0.
    std::optional<double> elapsedMs;
};

// One case. `label` is whatever the caller has actually observed, typically the
// file name; the renderer abbreviates it for display and never rewrites the
// caller's value.
struct ConsoleCaseRow {
    std::size_t sequence = 0;  // 1-based completion order
    std::string label;
    MediaKind media = MediaKind::Unknown;
    std::optional<std::size_t> sizeBytes;
    std::vector<ConsoleModeRow> modes;
};

// Progress counts. Every count is optional because S2 reports a run-level total
// and completion count; a per-media split is only present when the caller had a
// complete file list to divide. The renderer formats, it never derives.
struct ConsoleProgress {
    std::optional<std::size_t> filesTotal;
    std::optional<std::size_t> filesCompleted;
    std::optional<std::size_t> imagesTotal, imagesCompleted;
    std::optional<std::size_t> videosTotal, videosCompleted;
};

// Terminal summary. All counts are optional; `statusBanner` is a short caller
// supplied line such as the run status, and is omitted when not supplied.
struct ConsoleSummary {
    std::optional<std::size_t> cases, success, failed, cancelled, skipped;
    std::optional<double> elapsedMs;
    std::optional<std::string> statusBanner;
};

// Everything one screen is rendered from.
struct ConsolePresentationInput {
    std::string target;  // raw source root, never abbreviated in place
    std::string scopeLabel;
    std::string modeOrderLabel;  // e.g. "AUTO -> CPU -> GPU-MAX"

    std::optional<std::string> cpuResource;
    std::optional<std::string> gpu;
    std::optional<unsigned> distance;
    std::optional<std::string> suiteId;
    std::optional<std::string> build;
    std::optional<std::string> git;

    ConsoleProgress progress;

    // Most recently COMPLETED case, used for the CURRENT FILE block. It is
    // never the in-flight case, because S2 cannot report that.
    std::optional<ConsoleCaseRow> recentCase;

    std::vector<ConsoleCaseRow> history;

    std::optional<ConsoleSummary> summary;
};

// ---------------------------------------------------------------------------
// Line formatting
// ---------------------------------------------------------------------------
//
// These are pure string builders with no stream, no clock and no environment
// dependency, so the same input always yields the same bytes.

// Middle-ellipsis abbreviation for display only.
//
// Keeps a head and a tail so both the drive and the file name stay readable.
// Returns the input unchanged when it already fits. `maxWidth` of 0 or less
// yields an empty string rather than a broken one.
std::string consoleMiddleEllipsis(const std::string& text, std::size_t maxWidth);

// The fixed header block: a title row, a rule, up to three field rows, and a
// closing rule. Rows are never wrapped; long values are abbreviated to fit the
// requested width. Fields that were not supplied are omitted from their row, and
// a row whose fields are all absent is omitted entirely.
std::string consoleHeaderBlock(const ConsolePresentationInput& in, std::size_t width);

// The CURRENT FILE block for the most recently completed case, or an empty
// string when no case has completed yet.
std::string consoleCurrentFileBlock(const ConsoleCaseRow* recent);

// One compact history line. Only the modes actually present are listed.
std::string consoleHistoryLine(const ConsoleCaseRow& row);

// The final or partial summary block, or an empty string when there is none.
std::string consoleSummaryBlock(const ConsoleSummary& summary);

// ---------------------------------------------------------------------------
// Sink
// ---------------------------------------------------------------------------

// How the renderer writes. Both paths render the same presentation input to the
// same content; only the cursor handling differs.
enum class ConsoleOutputKind {
    // Repaint the fixed region in place with ANSI cursor control.
    Interactive,
    // Append lines and never emit a cursor control sequence. Used for pipes,
    // redirection and CI, where a repaint would corrupt the record.
    LineOriented,
};

// Small stateful writer around a std::ostream. The stream is the only sink, so
// a future --log file sink is just a different ostream and needs no renderer
// change.
//
// The stream is not owned and is not closed here.
class BenchmarkConsoleRenderer {
public:
    // `terminalWidth` is used only to abbreviate. A width of 0 means "do not
    // abbreviate", which is the right default for a pipe.
    BenchmarkConsoleRenderer(std::ostream& out,
                             ConsoleOutputKind kind,
                             std::size_t terminalWidth = 0);

    // Writes the fixed header. In Interactive mode a later call repaints the
    // same region instead of scrolling new lines down the terminal.
    void writeHeader(const ConsolePresentationInput& in);

    // Writes the CURRENT FILE block and appends the case to the history. This is
    // the only entry point that adds to the scrollback, so a caller that has
    // nothing new to report simply does not call it.
    void writeCompletedCase(const ConsoleCaseRow& row);

    // Writes the summary. Interactive mode first restores the fixed region so the
    // summary is not left underneath a repainted header.
    void writeSummary(const ConsoleSummary& summary);

    // Number of lines the fixed region occupies, for the cursor arithmetic in
    // Interactive mode. Always 0 for LineOriented, which never rewinds.
    int fixedRegionLines() const { return fixedLines_; }

    ConsoleOutputKind kind() const { return kind_; }
    std::size_t terminalWidth() const { return width_; }

private:
    std::ostream& out_;
    ConsoleOutputKind kind_;
    std::size_t width_;
    int fixedLines_ = 0;
};

}  // namespace msf
