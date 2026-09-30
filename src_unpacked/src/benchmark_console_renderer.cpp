#include "benchmark_console_renderer.h"

#include <algorithm>
#include <cstdio>
#include <ostream>
#include <sstream>
#include <utility>

namespace msf {
namespace {

// Display geometry. These are deliberately fixed rather than measured: the
// renderer must not depend on terminal state to produce the same bytes for the
// same input.
constexpr std::size_t kRuleWidth = 80;
constexpr std::size_t kLabelColumn = 28;   // history file label
constexpr std::size_t kSequenceDigits = 3;
constexpr std::size_t kMinValueWidth = 8; // never abbreviate a value below this
constexpr std::size_t kSequenceColumn = 3;

const char* const kSeparator = " | ";

// Status text. S2 owns the vocabulary, so these are S2's names uppercased for
// display. No other status word is ever produced: there is no RUNNING, STARTED,
// PENDING or ESTIMATED, because S2 does not report those for a completed case.
std::string statusLabel(BenchmarkStatus s) {
    std::string n = benchmarkStatusName(s);
    for (char& c : n) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return n;
}

// Fixed-precision milliseconds. Kept separate from statusLabel so a missing
// measurement and a zero measurement can never print the same way.
std::string formatMs(double ms) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f ms", ms);
    return std::string(buf);
}

std::string formatSecondsFromMs(double ms) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f s", ms / 1000.0);
    return std::string(buf);
}

std::string padRight(const std::string& s, std::size_t width) {
    if (s.size() >= width) return s;
    return s + std::string(width - s.size(), ' ');
}

std::string padLeft(const std::string& s, std::size_t width) {
    if (s.size() >= width) return s;
    return std::string(width - s.size(), ' ') + s;
}

std::string sequenceLabel(std::size_t sequence) {
    char buf[32];
    if (sequence >= 1000) {
        std::snprintf(buf, sizeof(buf), "%zu", sequence);
    } else {
        std::snprintf(buf, sizeof(buf), "%0*zu", static_cast<int>(kSequenceDigits), sequence);
    }
    return std::string(buf);
}

// Human readable size for the CURRENT FILE block. Absent sizeBytes means the
// size was not observed, which is shown by omitting the field entirely rather
// than by printing 0 B.
std::string formatSizeBytes(std::size_t bytes) {
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    const double kb = static_cast<double>(bytes) / 1024.0;
    char buf[48];
    if (mb >= 1.0)      std::snprintf(buf, sizeof(buf), "%.2f MB", mb);
    else if (kb >= 1.0) std::snprintf(buf, sizeof(buf), "%.2f KB", kb);
    else                std::snprintf(buf, sizeof(buf), "%zu B", bytes);
    return std::string(buf);
}

// "IMG 12/640  VID 3/207", built only from counts the caller supplied. A media
// count pair that is only half known is omitted rather than completed with 0.
//
// `mediaSplit` selects between the two real forms the caller may have supplied:
// the per-media split, or the shorter run-level pair. Neither is derived.
std::string formatFiles(const ConsoleProgress& p, bool mediaSplit) {
    std::string out;
    if (mediaSplit) {
        auto add = [&out](const char* tag, const std::optional<std::size_t>& done,
                          const std::optional<std::size_t>& total) {
            if (!done || !total) return;
            if (!out.empty()) out += "  ";
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s %zu/%zu", tag, *done, *total);
            out += buf;
        };
        add("IMG", p.imagesCompleted, p.imagesTotal);
        add("VID", p.videosCompleted, p.videosTotal);
    }

    // Fall back to the run-level pair when no usable media split was supplied.
    if (out.empty() && p.filesCompleted && p.filesTotal) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "FILES %zu/%zu", *p.filesCompleted, *p.filesTotal);
        out += buf;
    }
    return out;
}

struct Field {
    std::string label;
    std::string value;

    // Only a value that means something when shortened is allowed to be
    // shortened. A mode order, a counter or an identity string is either shown
    // intact or dropped: "AUTO -> CPU...U-MAX" misstates the execution order,
    // which is the one thing the header exists to guarantee.
    bool abbreviate = false;
};

std::string joinFields(const std::vector<Field>& fields) {
    std::string out;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) out += kSeparator;
        out += fields[i].label;
        out += " : ";
        out += fields[i].value;
    }
    return out;
}

// Rendered width of a row, which is what has to fit the terminal.
std::size_t rowRenderedWidth(const std::vector<Field>& fields) {
    std::size_t total = 0;
    for (const auto& f : fields) total += f.label.size() + 3 + f.value.size();
    if (fields.size() > 1) total += (fields.size() - 1) * std::string(kSeparator).size();
    return total;
}

// Drops the named label from a row if it is present.
void dropField(std::vector<Field>& fields, const char* label) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].label == label) {
            fields.erase(fields.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

// Makes a row fit `width`, in three escalating steps.
//
//  1. Abbreviate the values that were marked abbreviate (only Target does this).
//  2. As a last resort drop trailing fields, so the row can never wrap.
//
// Dropping is preferred over mangling because every field in a header row is a
// fact, and a missing fact is recoverable while a corrupted one is not.
bool fitRow(std::vector<Field>& fields, std::size_t width) {
    while (rowRenderedWidth(fields) > width) {
        std::size_t victim = fields.size();
        std::size_t longest = kMinValueWidth;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].abbreviate && fields[i].value.size() > longest) {
                longest = fields[i].value.size();
                victim = i;
            }
        }

        if (victim == fields.size()) {
            // Nothing left to shorten: drop the last field instead. Target is
            // always first, so the leftmost, most identifying field survives.
            if (fields.size() <= 1) return false;
            fields.pop_back();
            continue;
        }

        std::string& value = fields[victim].value;
        value = consoleMiddleEllipsis(value, std::max<std::size_t>(kMinValueWidth, value.size() - 1));
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------

const char* consoleModeLabel(GpuBackendKind mode) {
    switch (mode) {
        case GpuBackendKind::Cuda: return "GPU-MAX";
        case GpuBackendKind::Cpu:  return "CPU";
        default:                   return "AUTO";
    }
}

std::string consoleMiddleEllipsis(const std::string& text, std::size_t maxWidth) {
    if (maxWidth == 0) return std::string();
    if (text.size() <= maxWidth) return text;

    // ASCII "..." rather than U+2026: this text also goes to a file sink and to
    // a Korean Windows console, where a non-ASCII ellipsis is not reliably
    // renderable in the active code page.
    static const std::string kEllipsis = "...";
    if (maxWidth <= kEllipsis.size()) return text.substr(0, maxWidth);

    const std::size_t keep = maxWidth - kEllipsis.size();
    // Keep the head a little longer than the tail: a path is recognised by its
    // beginning, while the tail carries the file name that distinguishes rows.
    const std::size_t head = keep - keep / 3;
    const std::size_t tail = keep - head;
    return text.substr(0, head) + kEllipsis + text.substr(text.size() - tail);
}

std::string consoleHeaderBlock(const ConsolePresentationInput& in, std::size_t width) {
    const std::size_t rowWidth = (width == 0) ? kRuleWidth : width;

    // The three field rows are fixed by the terminal rendering contract: Target
    // / Scope / progress, then Mode / CPU / GPU, then Distance / Suite / Build /
    // Git. A row whose fields are all absent is dropped rather than printed
    // empty.
    std::vector<Field> row1;
    auto buildRow1 = [&in](bool mediaSplit) {
        std::vector<Field> r;
        // Target is the one field the rendering contract explicitly allows to be
        // abbreviated, because a long source root is expected and a shortened
        // path still shows both the root and the leaf.
        if (!in.target.empty())     r.push_back({"Target", in.target, true});
        if (!in.scopeLabel.empty()) r.push_back({"Scope", in.scopeLabel, false});
        const std::string files = formatFiles(in.progress, mediaSplit);
        if (!files.empty()) r.push_back({"Files", files, false});
        return r;
    };
    row1 = buildRow1(true);
    // An abbreviated counter ("IMG 12/...207") is not a smaller fact, it is a
    // broken one. If the per-media split cannot fit, prefer the run-level pair,
    // which is also observed.
    if (rowRenderedWidth(row1) > rowWidth) {
        std::vector<Field> relaxed = buildRow1(false);
        if (rowRenderedWidth(relaxed) < rowRenderedWidth(row1)) row1 = relaxed;
    }

    std::vector<Field> row2;
    if (!in.modeOrderLabel.empty()) row2.push_back({"Mode", in.modeOrderLabel});
    if (in.cpuResource)           row2.push_back({"CPU", *in.cpuResource});
    if (in.gpu)                   row2.push_back({"GPU", *in.gpu});

    std::vector<Field> row3;
    if (in.distance) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%u", *in.distance);
        row3.push_back({"Distance", buf});
    }
    if (in.suiteId) row3.push_back({"Suite ID", *in.suiteId});
    if (in.build)   row3.push_back({"Build", *in.build});
    if (in.git)     row3.push_back({"Git", *in.git});

    std::vector<std::string> rows;
    for (std::vector<Field>* row : {&row1, &row2, &row3}) {
        if (row->empty()) continue;

        // Build and Git are the droppable header fields: an abbreviated version
        // or commit id reads as a different identity, so the row loses the field
        // before it shortens anything. Both are preferred to keep, so they go
        // only when the row genuinely does not fit.
        if (rowRenderedWidth(*row) > rowWidth) dropField(*row, "Git");
        if (rowRenderedWidth(*row) > rowWidth) dropField(*row, "Build");

        fitRow(*row, rowWidth);
        rows.push_back(joinFields(*row));
    }

    const std::string rule(rowWidth, '=');

    std::ostringstream os;
    os << "MediaSimilarityFinder Benchmark\n" << rule << '\n';
    for (const auto& r : rows) os << r << '\n';
    os << rule << '\n';
    return os.str();
}

std::string consoleCurrentFileBlock(const ConsoleCaseRow* recent) {
    // Nothing has completed yet, so there is no current file to name. S2 cannot
    // report the in-flight case, and this block must not fall back to one.
    if (!recent) return std::string();

    std::ostringstream os;
    os << "CURRENT FILE\n";
    os << std::string(kRuleWidth, '-') << '\n';

    std::ostringstream meta;
    meta << "  " << sequenceLabel(recent->sequence) << "  "
         << padRight(consoleMiddleEllipsis(recent->label, kLabelColumn), kLabelColumn);
    switch (recent->media) {
        case MediaKind::Image: meta << "  Image"; break;
        case MediaKind::Video: meta << "  Video"; break;
        default:               break;  // unknown media is simply not labelled
    }
    if (recent->sizeBytes) meta << "  " << formatSizeBytes(*recent->sizeBytes);
    os << meta.str() << '\n';

    for (const auto& mode : recent->modes) {
        std::string line = "  " + padRight(consoleModeLabel(mode.mode), 8);
        if (mode.effectiveMode) {
            line += padRight(gpuBackendKindName(*mode.effectiveMode), 11);
        } else {
            line += std::string(11, ' ');  // not observed: leave the column empty
        }
        line += padRight(statusLabel(mode.status), 10);
        line += mode.elapsedMs ? padLeft(formatMs(*mode.elapsedMs), 12) : std::string();
        // Trailing padding is dropped so the block has no invisible content.
        while (!line.empty() && line.back() == ' ') line.pop_back();
        os << line << '\n';
    }
    os << std::string(kRuleWidth, '-') << '\n';
    return os.str();
}

std::string consoleHistoryLine(const ConsoleCaseRow& row) {
    std::ostringstream os;
    os << sequenceLabel(row.sequence) << "  "
       << padRight(consoleMiddleEllipsis(row.label, kLabelColumn), kLabelColumn);

    for (const auto& mode : row.modes) {
        std::string cell = consoleModeLabel(mode.mode);
        // A measurement is shown only for a successful mode. Any other real S2
        // status is shown by name instead, so a failure is never rendered as a
        // missing number.
        if (mode.status == BenchmarkStatus::Success && mode.elapsedMs) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), " %.2fms", *mode.elapsedMs);
            cell += buf;
        } else if (mode.status != BenchmarkStatus::Success) {
            cell += " " + statusLabel(mode.status);
        }
        os << "  " << padRight(cell, 17);
    }
    std::string line = os.str();
    while (!line.empty() && line.back() == ' ') line.pop_back();
    return line;
}

std::string consoleSummaryBlock(const ConsoleSummary& summary) {
    std::ostringstream os;
    os << "SUMMARY\n";

    auto addCount = [&os](const char* label, const std::optional<std::size_t>& v) {
        if (!v) return;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%zu\n", label, *v);
        os << buf;
    };

    if (summary.statusBanner) os << "  Status   : " << *summary.statusBanner << '\n';
    addCount("  Cases    : ", summary.cases);
    addCount("  Success  : ", summary.success);
    addCount("  Failed   : ", summary.failed);
    addCount("  Cancelled: ", summary.cancelled);
    addCount("  Skipped  : ", summary.skipped);
    if (summary.elapsedMs) os << "  Elapsed  : " << formatSecondsFromMs(*summary.elapsedMs) << '\n';
    return os.str();
}

// ---------------------------------------------------------------------------

BenchmarkConsoleRenderer::BenchmarkConsoleRenderer(std::ostream& out,
                                                 ConsoleOutputKind kind,
                                                 std::size_t terminalWidth)
    : out_(out), kind_(kind), width_(terminalWidth) {}

void BenchmarkConsoleRenderer::writeHeader(const ConsolePresentationInput& in) {
    // Repaint only in Interactive mode. A pipe or a redirect must never receive a
    // cursor sequence, because that would corrupt the recorded output.
    if (kind_ == ConsoleOutputKind::Interactive && fixedLines_ > 0) {
        out_ << "\x1b[" << fixedLines_ << "A";  // up over the previous region
    }
    if (kind_ == ConsoleOutputKind::Interactive) {
        out_ << "\x1b[J";  // erase from cursor down
    }

    const std::string block = consoleHeaderBlock(in, width_);
    out_ << block;

    // Count the lines just written so the next repaint can rewind exactly this
    // far and no further.
    fixedLines_ = 0;
    for (char c : block) {
        if (c == '\n') ++fixedLines_;
    }
}

void BenchmarkConsoleRenderer::writeCompletedCase(const ConsoleCaseRow& row) {
    // Scrollback first: history is append-only in both output kinds, so a case
    // that has already been printed is never overwritten by a repaint.
    const std::string history = consoleHistoryLine(row);
    out_ << history << '\n';

    // The CURRENT FILE block is the latest completed case, so it is rewritten
    // rather than appended in Interactive mode. In LineOriented mode it is
    // appended, which keeps a pipe readable without ANSI.
    if (kind_ == ConsoleOutputKind::Interactive && fixedLines_ > 0) {
        out_ << "\x1b[" << fixedLines_ << "A";
        out_ << "\x1b[J";
        const std::string block = consoleCurrentFileBlock(&row);
        out_ << block;
        fixedLines_ = 0;
        for (char c : block) {
            if (c == '\n') ++fixedLines_;
        }
    } else {
        out_ << consoleCurrentFileBlock(&row);
        fixedLines_ = 0;
    }
}

void BenchmarkConsoleRenderer::writeSummary(const ConsoleSummary& summary) {
    // Leave the fixed region before the summary, so the final block is not
    // written underneath a header that a later repaint would overwrite.
    if (kind_ == ConsoleOutputKind::Interactive && fixedLines_ > 0) {
        out_ << "\x1b[" << fixedLines_ << "A";
        out_ << "\x1b[J";
        fixedLines_ = 0;
    }
    out_ << consoleSummaryBlock(summary);
}

}  // namespace msf
