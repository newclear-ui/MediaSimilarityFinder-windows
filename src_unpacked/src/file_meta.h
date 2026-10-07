// File metadata probing (P4: 0.9.4.70). Display metadata (dimensions,
// duration) for the detail pane and file lists. Backend-owned: the GUI must
// not run decoders or spawn probes (directive §3). QtCore/std only.
#pragma once
#include <string>

namespace msf {

struct FileMeta {
    int width = 0;
    int height = 0;
    double duration = 0.0;
    bool ok = false;
};

// Last-resort dimensions via ffprobe (no decode). Windowless spawn through
// captureSilent; cached by the caller, so at most one spawn per path ever.
bool ffprobeSize(const std::string& path, int& w, int& h);

} // namespace msf
