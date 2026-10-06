// Video cancellation regression (directive 0.9.4.58 follow-up).
//
// Cancelled != Failed for videos, matching the image drain rule:
//   - a user Stop during video analysis must NOT set failed / roll back;
//   - futures already completed at (or after) the stop persist with real
//     fingerprints and are reused as unchanged by the next scan;
//   - genuine decode failures keep failure semantics (analysisFailed row,
//     counted failed, settled, never retried, scan still completes).
//   - the sampling-generation stamp (which gates video rescan-reuse) is
//     written on cancel only when every pre-existing video row was admitted
//     this scan; rows this scan never reached must still regrid next time.
//
// Deterministic, no timing races: stops fire from synchronous hooks
// (per-harvest progress, per-admission walked), so admission counts are
// exact on any worker count. ffmpeg lavfi testsrc clips are real decodable
// videos (same pattern as color_thumb_test). If ffmpeg is unavailable the
// test SKIPs instead of failing.
#include "database.h"
#include "media_search_engine.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << "\n"; }
    else       { std::cout << "  [ok] " << what << "\n"; }
}

bool genClip(const std::filesystem::path& out, int seconds, int variant) {
    // Distinct sizes per clip so fixtures are not byte-identical.
    const int w = 160 + variant * 16, h = 90 + variant * 8;
    const std::string cmd = std::string("ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc=size=") +
        std::to_string(w) + "x" + std::to_string(h) + ":rate=10:duration=" + std::to_string(seconds) +
        " -c:v mpeg4 -pix_fmt yuv420p \"" + out.string() + "\"";
    return std::system(cmd.c_str()) == 0;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const auto rootA = fs::temp_directory_path() / "msf_vcancel_a";
    const auto appA = fs::temp_directory_path() / "msf_vcancel_a_app";
    const auto rootB = fs::temp_directory_path() / "msf_vcancel_b";
    const auto appB = fs::temp_directory_path() / "msf_vcancel_b_app";
    std::error_code ec;
    for (const auto& d : {rootA, appA, rootB, appB}) { fs::remove_all(d, ec); fs::create_directories(d, ec); }

    static constexpr int kClips = 6;
    for (int i = 0; i < kClips; ++i) {
        if (!genClip(rootA / ("v" + std::to_string(i) + ".mp4"), 2, i)) {
            std::cout << "[SKIP] ffmpeg unavailable; video fixtures cannot be generated\n";
            return 0;
        }
    }
    // Scenario B: 2 corrupt clips (invalid bytes) + 2 valid clips.
    for (int i = 0; i < 2; ++i) {
        std::ofstream f(rootB / ("bad" + std::to_string(i) + ".mp4"), std::ios::binary);
        f << "not a video container, decode must fail deterministically";
    }
    for (int i = 0; i < 2; ++i) {
        if (!genClip(rootB / ("ok" + std::to_string(i) + ".mp4"), 1, i)) {
            std::cout << "[SKIP] ffmpeg unavailable; video fixtures cannot be generated\n";
            return 0;
        }
    }

    // ---- Scenario A: stop during video analysis is Cancelled, not Failed.
    std::size_t firstAnalyzed = 0;
    {
        msf::MediaSearchEngine e;
        if (!e.openIndexForRoot(rootA.string(), appA.string())) {
            std::cerr << "openIndexForRoot failed\n";
            return 2;
        }
        msf::ScanControl c;
        // Fires synchronously per harvested video, after that video is
        // persisted. At least one valid result is stored before the stop
        // is observed, on any worker count.
        c.progress = [&](std::size_t, std::size_t, const std::string&) {
            c.cancel.store(true);
        };
        const auto r = e.scan(rootA.string(), 8, &c);
        firstAnalyzed = r.analyzed;
        check(!r.completed, "cancelled video scan reports completed=false");
        check(r.failed == 0, "cancel records zero failures (Cancelled != Failed)");
        check(r.analyzed >= 1, "at least one completed video persists despite stop");
        check(r.vidAnalyzed == r.analyzed, "video-only fixture: all analysis is video");
        const std::string js = e.telemetryJson();
        check(js.find("\"cancelled\":true") != std::string::npos, "telemetry marks cancelled");
        check(js.find("\"failed\":false") != std::string::npos, "telemetry marks not-failed");
        e.close();
    }
    // Durability + reuse: reopen (new engine object) and rescan without
    // cancel. Everything persisted in scenario A must come back unchanged;
    // the remainder is analyzed fresh. Exact split, not a >= bound.
    {
        msf::MediaSearchEngine e2;
        if (!e2.openIndexForRoot(rootA.string(), appA.string())) {
            std::cerr << "reopen failed\n";
            return 3;
        }
        const auto r2 = e2.scan(rootA.string(), 8, nullptr);
        check(r2.completed, "rescan after cancel completes");
        check(r2.failed == 0, "rescan records zero failures");
        check(r2.unchanged == firstAnalyzed, "persisted videos reused as unchanged (exact)");
        check(r2.analyzed == (std::size_t)kClips - firstAnalyzed, "remainder analyzed fresh (exact)");
        e2.close();
    }

    // ---- Scenario B: genuine decode failure keeps failure semantics.
    {
        msf::MediaSearchEngine e;
        if (!e.openIndexForRoot(rootB.string(), appB.string())) {
            std::cerr << "openIndexForRoot failed (B)\n";
            return 4;
        }
        const auto r = e.scan(rootB.string(), 8, nullptr);
        check(r.completed, "scan with corrupt clips still completes");
        check(r.failed == 2, "corrupt clips counted failed (exact)");
        check(r.analyzed == 2, "valid clips analyzed (exact)");
        check(r.unchanged == 0, "first scan reuses nothing");
        e.close();
    }
    {
        msf::MediaSearchEngine e2;
        if (!e2.openIndexForRoot(rootB.string(), appB.string())) {
            std::cerr << "reopen failed (B)\n";
            return 5;
        }
        const auto r2 = e2.scan(rootB.string(), 8, nullptr);
        check(r2.completed, "rescan after failures completes");
        check(r2.analyzed == 0, "settled failures are not re-analyzed");
        check(r2.failed == 0, "rescan records no new failures");
        check(r2.unchanged == 4, "valid + settled-failure rows reused (exact)");
        e2.close();
    }

    // ---- Scenario C: the generation stamp is conditional, not blind.
    // A cancelled scan stamps only when every pre-existing video row was
    // admitted. Here 6 of 8 old rows are never reached, so no stamp may be
    // written even though 2 videos were admitted: the unreached rows must
    // still regrid next scan. The stored "0" is forced directly to simulate
    // a legacy unstamped DB (fresh rows, old stamp).
    const auto rootC = fs::temp_directory_path() / "msf_vcancel_c";
    const auto appC = fs::temp_directory_path() / "msf_vcancel_c_app";
    fs::remove_all(rootC, ec); fs::remove_all(appC, ec);
    fs::create_directories(rootC, ec); fs::create_directories(appC, ec);
    static constexpr int kClipsC = 8;
    for (int i = 0; i < kClipsC; ++i) {
        if (!genClip(rootC / ("w" + std::to_string(i) + ".mp4"), 1, i)) {
            std::cout << "[SKIP] ffmpeg unavailable; video fixtures cannot be generated\n";
            return 0;
        }
    }
    const std::string dbC = (appC / "index.sqlite").string();
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbC)) { std::cerr << "openIndex failed (C1)\n"; return 6; }
        const auto r = e.scan(rootC.string(), 8, nullptr);
        check(r.completed, "C1 completes and analyzes all clips");
        check(r.analyzed == (std::size_t)kClipsC, "C1 analyzes every clip (exact)");
        e.close();
    }
    {   // Simulate a legacy DB: valid current rows, generation never stamped.
        msf::Database db;
        if (!db.open(dbC)) { std::cerr << "db open failed (C poke)\n"; return 7; }
        if (!db.setSamplingGeneration("0")) { std::cerr << "stamp reset failed\n"; return 7; }
        db.close();
    }
    {
        msf::MediaSearchEngine e2;
        if (!e2.openIndex(dbC)) { std::cerr << "openIndex failed (C2)\n"; return 8; }
        msf::ScanControl c;
        // Exactly 2 admissions, then stop: 6 old rows are never reached.
        // Single-threaded consumer + synchronous hook = deterministic.
        c.walked = [&](std::size_t n) {
            if (n >= 2) c.cancel.store(true);
        };
        const auto r = e2.scan(rootC.string(), 8, &c);
        check(!r.completed, "C2 cancelled scan reports completed=false");
        check(r.scanned == 2, "C2 admits exactly 2 videos before the stop");
        e2.close();
    }
    {   // No stamp: unreached old rows exist.
        msf::Database db;
        if (!db.open(dbC)) { std::cerr << "db open failed (C check)\n"; return 9; }
        check(db.samplingGeneration() == "0", "no generation stamp when old rows unreached");
        db.close();
    }
    {
        msf::MediaSearchEngine e3;
        if (!e3.openIndex(dbC)) { std::cerr << "openIndex failed (C3)\n"; return 10; }
        const auto r = e3.scan(rootC.string(), 8, nullptr);
        check(r.completed, "C3 completes");
        check(r.analyzed == (std::size_t)kClipsC, "C3 re-analyzes everything (regrid, exact)");
        check(r.unchanged == 0, "C3 reuses nothing while unstamped");
        e3.close();
    }
    fs::remove_all(rootC, ec); fs::remove_all(appC, ec);

    for (const auto& d : {rootA, appA, rootB, appB}) fs::remove_all(d, ec);
    std::cout << "video_cancel_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
