#include "command_line.h"

#include <cstring>

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
            o.mode = CommandMode::Scan;
            o.scanRoot = argv[i + 1];
            ++i;  // consume the folder
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
        // belonging to later stages (--benchmark, --mode, --suite, ...) land
        // here on purpose, so that help text and reality stay in agreement.
        return makeError("unknown option: " + a);
    }

    // --media without --scan is accepted but has no consumer in S1, because S1
    // has no other headless entry point yet. It is not an error, so that
    // "--media all" stays valid for a future caller, but nothing claims to apply
    // it during a GUI run.
    return o;
}

} // namespace msf
