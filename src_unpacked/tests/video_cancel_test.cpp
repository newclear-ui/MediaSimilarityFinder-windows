// Video-build cancel regression (0.9.4.78). VideoFingerprintEngine::build()
// runs unbounded sweeps (minutes on long videos) with no per-file progress,
// so a Stop landing mid-build used to wait for the whole sweep. The build now
// takes an optional cooperative cancel flag, polled at entry and between
// frames. Deterministic: a pre-cancelled flag must fail fast, an uncancelled
// build of the same file must succeed.
#include "video_fingerprint.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

double nowMs() {
    return (double)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto d = fs::temp_directory_path() / "msf_video_cancel";
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    const std::string clip = (d / "clip.mp4").string();
    // 8s testsrc, same generator shape as video_scan_e2e_test.
    const std::string gen = std::string("ffmpeg -hide_banner -loglevel error -y -f lavfi"
        " -i testsrc=size=160x90:rate=10:duration=8 -c:v mpeg4 -pix_fmt yuv420p \"") + clip + "\"";
    if (std::system(gen.c_str()) != 0 || !fs::exists(clip, ec)) {
        std::cerr << "ffmpeg fixture generation failed (environment)\n";
        return 2;
    }
    msf::VideoFingerprintEngine engine;
    // Baseline: uncancelled build succeeds (fixture validity, not speed).
    {
        msf::VideoFingerprint vf;
        if (!engine.build(clip, vf)) { std::cerr << "uncancelled build failed\n"; return 3; }
        if (vf.hashes.empty()) { std::cerr << "empty fingerprint\n"; return 4; }
        std::cout << "  [ok] uncancelled build succeeds\n";
    }
    // Pre-cancelled flag: must fail fast without decoding the sweep.
    {
        msf::VideoFingerprint vf;
        std::atomic<bool> cancelled{true};
        const double t0 = nowMs();
        const bool ok = engine.build(clip, vf, nullptr, nullptr, nullptr, &cancelled);
        const double dt = nowMs() - t0;
        if (ok) { std::cerr << "cancelled build unexpectedly succeeded\n"; return 5; }
        if (dt > 5000.0) { std::cerr << "cancelled build took too long: " << dt << "ms\n"; return 6; }
        std::cout << "  [ok] pre-cancelled build fails fast (" << dt << "ms)\n";
    }
    fs::remove_all(d, ec);
    std::cout << "video_cancel_selfcheck=ok checks=4\n";
    return 0;
}
