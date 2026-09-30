// S5 Console benchmark renderer unit test.
//
// The renderer is the only S5 piece that can be tested exhaustively without a
// real benchmark, and its whole purpose is to be honest about what it was
// given. So most of these checks are negative ones: given a presentation input
// that lacks a value, the output must not contain an invented one.
//
// Nothing here runs media, opens a suite or writes a journal. The renderer is
// fed presentation structs directly, which is exactly how S5-3 will feed it.

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "benchmark_console_renderer.h"

namespace {

int gChecks = 0;
int gFails = 0;

void chk(bool ok, const std::string& what) {
    ++gChecks;
    if (ok) {
        std::printf("  [ok] %s\n", what.c_str());
    } else {
        ++gFails;
        std::printf("  [F] %s\n", what.c_str());
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool hasAnsi(const std::string& s) {
    return s.find('\x1b') != std::string::npos;
}

std::string upper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}

// One mode row that succeeded with a real measurement.
msf::ConsoleModeRow doneMode(msf::GpuBackendKind mode, double ms) {
    msf::ConsoleModeRow row;
    row.mode = mode;
    row.effectiveMode = (mode == msf::GpuBackendKind::Cuda) ? msf::GpuBackendKind::Cuda
                                                            : msf::GpuBackendKind::Cpu;
    row.status = msf::BenchmarkStatus::Success;
    row.elapsedMs = ms;
    return row;
}

msf::ConsoleCaseRow imageCase(std::size_t sequence, const std::string& name) {
    msf::ConsoleCaseRow row;
    row.sequence = sequence;
    row.label = name;
    row.media = msf::MediaKind::Image;
    row.sizeBytes = 4820000;
    row.modes.push_back(doneMode(msf::GpuBackendKind::Auto, 12.41));
    row.modes.push_back(doneMode(msf::GpuBackendKind::Cpu, 18.08));
    row.modes.push_back(doneMode(msf::GpuBackendKind::Cuda, 7.32));
    return row;
}

// A fully populated header input, used as the base for negative checks so a
// missing value can be removed from a known-good input.
msf::ConsolePresentationInput fullInput() {
    msf::ConsolePresentationInput in;
    in.target = "D:\\Media\\TestSet";
    in.scopeLabel = "ALL";
    in.modeOrderLabel = "AUTO -> CPU -> GPU-MAX";
    in.cpuResource = std::string("Balanced 55%");
    in.gpu = std::string("CUDA");
    in.distance = 8;
    in.suiteId = std::string("20260930-0801-01");
    in.build = std::string("0.9.4.43");
    in.git = std::string("22c3ac9");

    in.progress.filesTotal = 847;
    in.progress.filesCompleted = 16;
    in.progress.imagesTotal = 640;
    in.progress.imagesCompleted = 12;
    in.progress.videosTotal = 207;
    in.progress.videosCompleted = 3;
    return in;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace msf;

    // Visual inspection path: renders one populated screen so a human can read
    // the layout. It asserts nothing and is excluded from the check count, so
    // running it can never change a pass or fail.
    if (argc > 1 && std::string(argv[1]) == "--dump") {
        auto in = fullInput();
        std::ostringstream os;
        BenchmarkConsoleRenderer r(os, ConsoleOutputKind::LineOriented, 0);
        r.writeHeader(in);
        for (int i = 1; i <= 3; ++i) {
            ConsoleCaseRow row = imageCase(static_cast<std::size_t>(i),
                                           (i == 3) ? "a_rather_long_video_file_name.mp4"
                                                     : ("image_000" + std::to_string(i) + ".jpg"));
            row.media = (i == 3) ? MediaKind::Video : MediaKind::Image;
            r.writeCompletedCase(row);
        }
        ConsoleSummary s;
        s.cases = 847; s.success = 845; s.failed = 2; s.cancelled = 0; s.skipped = 0;
        s.elapsedMs = 123456.0;
        s.statusBanner = std::string("BENCHMARK COMPLETE");
        r.writeSummary(s);
        std::fputs(os.str().c_str(), stdout);
        std::printf("\n--- narrow header (width 60) ---\n");
        std::fputs(consoleHeaderBlock(in, 60).c_str(), stdout);
        return 0;
    }

    std::printf("--- header: fields that were supplied ---\n");
    {
        const std::string h = consoleHeaderBlock(fullInput(), 0);
        chk(contains(h, "Target : D:\\Media\\TestSet"), "target is shown");
        chk(contains(h, "Scope : ALL"), "scope is shown");
        chk(contains(h, "IMG 12/640"), "image progress is shown");
        chk(contains(h, "VID 3/207"), "video progress is shown");
        chk(contains(h, "AUTO -> CPU -> GPU-MAX"), "mode order is shown");
        chk(contains(h, "CPU : Balanced 55%"), "cpu resource is shown");
        chk(contains(h, "GPU : CUDA"), "gpu is shown");
        chk(contains(h, "Distance : 8"), "distance is shown");
        chk(contains(h, "Suite ID : 20260930-0801-01"), "suite id is shown");
        chk(contains(h, "Build : 0.9.4.43"), "build is shown");
        chk(contains(h, "Git : 22c3ac9"), "git is shown");
    }

    std::printf("--- narrow width: no wrap, and identity fields survive ---\n");
    {
        auto in = fullInput();
        in.target = "D:\\Media\\Very\\Long\\Path\\To\\Some\\Deep\\Dataset\\Root";
        const std::size_t narrow = 60;
        const std::string h = consoleHeaderBlock(in, narrow);

        // No line may exceed the terminal width: the contract forbids wrapping.
        bool over = false;
        std::size_t start = 0;
        while (start < h.size()) {
            const std::size_t nl = h.find('\n', start);
            const std::size_t len = (nl == std::string::npos) ? h.size() - start : nl - start;
            if (len > narrow) over = true;
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        chk(!over, "no header line exceeds the requested width");

        // A shortened execution order would misstate the contract, so the mode
        // order is dropped-never-abbreviated: it is absent or whole.
        const bool modeWhole = contains(h, "AUTO -> CPU -> GPU-MAX");
        const bool modeMangled = contains(h, "...");
        chk(modeWhole || !modeMangled, "the mode order is never abbreviated");

        // An abbreviated version or commit id reads as a different identity, so
        // those fields are dropped before anything else is shortened.
        chk(!contains(h, "Build :"), "build is dropped at narrow width rather than abbreviated");
        chk(!contains(h, "Git :"), "git is dropped at narrow width rather than abbreviated");

        // A broken counter is not a smaller fact, so the run-level count is used.
        chk(!contains(h, "IMG 12/"), "media counters are not abbreviated into nonsense");
        chk(contains(h, "D:\\Media"), "the head of the target is still shown");
    }

    std::printf("--- header: three fixed rows, no wrapping ---\n");
    {
        const std::string h = consoleHeaderBlock(fullInput(), 0);
        // title + rule + at most three field rows + rule
        std::size_t lines = 0;
        for (char c : h) {
            if (c == '\n') ++lines;
        }
        chk(lines <= 6, "header occupies at most 6 lines (title, 2 rules, 3 rows)");
        chk(h.find("MediaSimilarityFinder Benchmark") == 0, "header starts with the title");
    }

    std::printf("--- header: a value that was NOT supplied is not invented ---\n");
    {
        auto in = fullInput();
        in.git.reset();
        const std::string h = consoleHeaderBlock(in, 0);
        chk(!contains(h, "Git"), "absent git field is omitted, not filled with a guess");

        in = fullInput();
        in.cpuResource.reset();
        in.gpu.reset();
        const std::string h2 = consoleHeaderBlock(in, 0);
        chk(!contains(h2, "CPU :"), "absent cpu resource is omitted");
        chk(!contains(h2, "GPU :"), "absent gpu is omitted");

        in = fullInput();
        in.progress.imagesCompleted.reset();
        const std::string h3 = consoleHeaderBlock(in, 0);
        chk(!contains(h3, "IMG"), "half-known media progress is omitted, not completed with 0");

        in = fullInput();
        in.distance.reset();
        const std::string h4 = consoleHeaderBlock(in, 0);
        chk(!contains(h4, "Distance"), "absent distance is omitted");
    }

    std::printf("--- header: middle ellipsis ---\n");
    {
        const std::string longPath = "D:\\Media\\Very\\Long\\Path\\To\\Some\\Deep\\Dataset\\Root";
        chk(consoleMiddleEllipsis("short", 40) == "short", "a value that fits is unchanged");
        chk(consoleMiddleEllipsis(longPath, 40).size() == 40, "abbreviated value fits the budget");
        chk(contains(consoleMiddleEllipsis(longPath, 40), "..."), "abbreviation uses a visible marker");
        chk(consoleMiddleEllipsis(longPath, 40).find("D:\\") == 0, "head of the path is kept");
        chk(contains(consoleMiddleEllipsis(longPath, 40), "Root"), "tail of the path is kept");
        chk(consoleMiddleEllipsis("abcdef", 2) == "ab", "budget smaller than the marker is handled");
        chk(consoleMiddleEllipsis("abc", 0).empty(), "zero budget yields an empty string");

        // The caller's value must survive: abbreviation is a display concern.
        std::string original = longPath;
        auto in = fullInput();
        in.target = longPath;
        const std::string h = consoleHeaderBlock(in, 60);
        chk(original == longPath, "original target string is not modified");
        chk(in.target == longPath, "the input's target is not modified in place");
        chk(h.find(longPath) == std::string::npos, "narrow header shows an abbreviated target");
    }

    std::printf("--- mode results: only real S2 statuses ---\n");
    {
        const auto row = imageCase(12, "image_0012.jpg");
        const std::string block = consoleCurrentFileBlock(&row);
        chk(contains(block, "CURRENT FILE"), "current file block is labelled");
        chk(contains(block, "image_0012.jpg"), "recently completed case is named");
        chk(contains(block, "AUTO"), "AUTO row is shown");
        chk(contains(block, "CPU "), "CPU row is shown");
        chk(contains(block, "GPU-MAX"), "GPU-MAX row is shown");
        chk(contains(block, "12.41 ms"), "auto elapsed is shown");
        chk(contains(block, "18.08 ms"), "cpu elapsed is shown");
        chk(contains(block, "7.32 ms"), "gpu-max elapsed is shown");
        // S2's own status vocabulary, not the mockup's DONE/RUNNING wording: the
        // brief requires reusing S2 states so the renderer cannot drift into a
        // parallel set of words.
        chk(contains(block, "SUCCESS"), "success is shown with S2's status name");

        // The core A1 rule.
        const std::string up = upper(block);
        chk(!contains(up, "RUNNING"), "no RUNNING is invented for a completed case");
        chk(!contains(up, "STARTED"), "no STARTED is invented");
        chk(!contains(up, "PENDING"), "no PENDING is invented");
        chk(!contains(up, "ESTIMATED"), "no ESTIMATED is invented");
        chk(!contains(up, "ETA"), "no ETA is displayed");
        chk(!contains(block, "123/1000"), "no invented current-file counter");
    }

    std::printf("--- mode results: non-success and missing measurements ---\n");
    {
        ConsoleCaseRow row = imageCase(13, "image_0013.mp4");
        row.media = MediaKind::Video;
        ConsoleModeRow failed;
        failed.mode = GpuBackendKind::Cuda;
        failed.status = BenchmarkStatus::Failed;
        row.modes[2] = failed;

        const std::string block = consoleCurrentFileBlock(&row);
        chk(contains(upper(block), "FAILED"), "a real failure is shown by its S2 status");
        chk(!contains(block, "7.32 ms"), "a failed mode does not keep a stale measurement");
        chk(contains(block, "Video"), "video media is labelled");

        ConsoleModeRow skipped;
        skipped.mode = GpuBackendKind::Cuda;
        skipped.status = BenchmarkStatus::Skipped;
        row.modes[2] = skipped;
        const std::string block2 = consoleCurrentFileBlock(&row);
        chk(contains(upper(block2), "SKIPPED"), "skipped is shown as SKIPPED, not as a missing time");

        ConsoleModeRow noClock;
        noClock.mode = GpuBackendKind::Cpu;
        noClock.status = BenchmarkStatus::Success;
        row.modes[1] = noClock;
        const std::string block3 = consoleCurrentFileBlock(&row);
        chk(contains(block3, "SUCCESS"), "success without a measurement still shows SUCCESS");
        chk(!contains(block3, "0.00 ms"), "a missing measurement is not printed as 0.00 ms");
    }

    std::printf("--- current file: nothing completed yet ---\n");
    {
        chk(consoleCurrentFileBlock(nullptr).empty(), "no current file block before any completion");
        const std::string h = consoleHeaderBlock(fullInput(), 0);
        chk(contains(h, "CURRENT") == false, "the header does not invent a current file");
    }

    std::printf("--- history: compact one line per case ---\n");
    {
        const auto row = imageCase(1, "image01.jpg");
        const std::string line = consoleHistoryLine(row);
        chk(contains(line, "001"), "history line carries the completion sequence");
        chk(contains(line, "image01.jpg"), "history line carries the case label");
        chk(contains(line, "AUTO"), "history line lists AUTO");
        chk(contains(line, "CPU"), "history line lists CPU");
        chk(contains(line, "GPU-MAX"), "history line lists GPU-MAX");
        chk(contains(line, "12.41ms"), "history line shows the measurement compactly");
        chk(line.find('\n') == std::string::npos, "history line is exactly one line");

        // Only the modes actually present are listed.
        ConsoleCaseRow partial = imageCase(2, "image02.jpg");
        partial.modes.pop_back();
        partial.modes.pop_back();
        const std::string line2 = consoleHistoryLine(partial);
        chk(contains(line2, "AUTO"), "partial history keeps the mode it has");
        chk(!contains(line2, "GPU-MAX"), "partial history does not list a mode it does not have");
    }

    std::printf("--- summary ---\n");
    {
        ConsoleSummary s;
        s.cases = 847;
        s.success = 845;
        s.failed = 2;
        s.cancelled = 0;
        s.skipped = 0;
        s.elapsedMs = 123456.0;
        s.statusBanner = std::string("BENCHMARK COMPLETE");

        const std::string b = consoleSummaryBlock(s);
        chk(contains(b, "SUMMARY"), "summary block is labelled");
        chk(contains(b, "Cases    : 847"), "cases are shown");
        chk(contains(b, "Success  : 845"), "success count is shown");
        chk(contains(b, "Failed   : 2"), "failed count is shown");
        chk(contains(b, "Cancelled: 0"), "cancelled count is shown");
        chk(contains(b, "Skipped  : 0"), "skipped count is shown");
        chk(contains(b, "123.46 s"), "elapsed is shown in seconds");
        chk(contains(b, "BENCHMARK COMPLETE"), "status banner is shown");

        ConsoleSummary partial;
        const std::string b2 = consoleSummaryBlock(partial);
        chk(contains(b2, "SUMMARY"), "a partial summary still has the block label");
        chk(!contains(b2, "Cases"), "an absent count is omitted rather than printed as 0");
        chk(!contains(b2, "Status"), "an absent status banner is omitted");
    }

    std::printf("--- non-TTY: line oriented, no cursor control ---\n");
    {
        std::ostringstream os;
        BenchmarkConsoleRenderer r(os, ConsoleOutputKind::LineOriented, 0);
        auto in = fullInput();
        r.writeHeader(in);
        const auto row = imageCase(1, "image01.jpg");
        r.writeCompletedCase(row);
        ConsoleSummary s;
        s.cases = 1;
        s.success = 1;
        r.writeSummary(s);

        const std::string out = os.str();
        chk(!hasAnsi(out), "line-oriented output contains no ANSI sequence");
        chk(contains(out, "Target :"), "header is written");
        chk(contains(out, "CURRENT FILE"), "current file block is written");
        chk(contains(out, "image01.jpg"), "history line is written");
        chk(contains(out, "SUMMARY"), "summary is written");
        chk(r.fixedRegionLines() == 0, "line-oriented mode never reserves a repaint region");
    }

    std::printf("--- TTY: ANSI region repaint ---\n");
    {
        std::ostringstream os;
        BenchmarkConsoleRenderer r(os, ConsoleOutputKind::Interactive, 0);
        auto in = fullInput();
        r.writeHeader(in);
        const int afterFirst = r.fixedRegionLines();
        chk(afterFirst > 0, "interactive mode reserves the header region");

        std::ostringstream os2;
        BenchmarkConsoleRenderer r2(os2, ConsoleOutputKind::Interactive, 0);
        r2.writeHeader(in);
        r2.writeHeader(in);  // a progress update repaints instead of scrolling
        const std::string out = os2.str();
        chk(hasAnsi(out), "interactive output uses ANSI cursor control");
        chk(out.find("\x1b[") != std::string::npos, "a cursor sequence is emitted");
        chk(r2.fixedRegionLines() == afterFirst, "region size does not grow when repainting");
    }

    std::printf("--- purity: same input, same bytes ---\n");
    {
        const auto a = fullInput();
        auto b = fullInput();
        b.recentCase = imageCase(5, "a.jpg");
        auto c = fullInput();
        c.recentCase = imageCase(5, "a.jpg");
        chk(consoleHeaderBlock(a, 0) == consoleHeaderBlock(a, 0), "header rendering is deterministic");
        chk(consoleHistoryLine(*b.recentCase) == consoleHistoryLine(*c.recentCase),
            "history rendering is deterministic for equal input");
        chk(consoleMiddleEllipsis(b.target, 30) == consoleMiddleEllipsis(c.target, 30),
            "abbreviation is deterministic");
    }

    std::printf("--- CPU FB: rejected, must never appear ---\n");
    {
        const std::string h = consoleHeaderBlock(fullInput(), 0);
        const auto row = imageCase(1, "x.jpg");
        const std::string cur = consoleCurrentFileBlock(&row);
        const std::string hist = consoleHistoryLine(row);
        ConsoleSummary s;
        s.cases = 1;
        const std::string sum = consoleSummaryBlock(s);
        for (const std::string* s2 : {&h, &cur, &hist, &sum}) {
            const std::string u = upper(*s2);
            chk(!contains(u, "CPU FB"), "no CPU FB indicator in renderer output");
            chk(!contains(u, "FALLBACK"), "no fallback indicator in renderer output");
        }
    }

    std::printf("--- rendering never claims in-flight work ---\n");
    {
        // A presentation input with progress but no completed case must not imply
        // that a specific file is being processed right now.
        auto in = fullInput();
        in.progress.filesCompleted = 16;
        std::ostringstream os;
        BenchmarkConsoleRenderer r(os, ConsoleOutputKind::LineOriented, 0);
        r.writeHeader(in);
        const std::string out = os.str();
        chk(contains(out, "IMG 12/640"), "observed aggregate progress is still shown");
        chk(!contains(out, "CURRENT FILE"), "no current file is claimed without a completed case");
    }

    std::printf("\nconsole_renderer_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
