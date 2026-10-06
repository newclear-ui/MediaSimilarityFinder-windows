// Generation-scope + terminal-state regression (0.9.4.59 follow-up).
//
// D. Image-only scans never touch video sampling_generation (complete and
//    cancelled cases). The stamp requires in-scope video admission evidence;
//    scanVideos=false must not write it, not even vacuously.
// E. Ignored stale videos block promotion: a complete scan with an ignored
//    pre-existing video row keeps the old generation; unignoring later
//    re-analyzes via the regrid safety net instead of reusing stale rows.
// F. Stamp write failure is conservative: the scan stays valid and complete,
//    the old generation is kept, and the next scan safely regrids. The
//    failure never fails or rolls back the search results.
// G. Deterministic DB error with the cancel flag set: the scan takes the real
//    failure path (rollback, telemetry failed=true, cancelled=false), never
//    the live-flag-inferred Cancelled. Decode failure and DB failure are
//    different outcomes and are asserted separately.
//
// Deterministic, no timing races: stops fire from synchronous hooks with
// exact admission counts; DB faults come from sqlite3 RAISE(ABORT,) triggers
// installed while the engine is closed (same seam style as the existing
// analysis_failure_state_test). ffmpeg lavfi clips; SKIP if unavailable.
#include "database.h"
#include "media_search_engine.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sqlite3.h>

namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << "\n"; }
    else       { std::cout << "  [ok] " << what << "\n"; }
}

bool genClip(const std::filesystem::path& out, int seconds, int variant) {
    const int w = 160 + variant * 16, h = 90 + variant * 8;
    const std::string cmd = std::string("ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc=size=") +
        std::to_string(w) + "x" + std::to_string(h) + ":rate=10:duration=" + std::to_string(seconds) +
        " -c:v mpeg4 -pix_fmt yuv420p \"" + out.string() + "\"";
    return std::system(cmd.c_str()) == 0;
}

void pgm(const std::filesystem::path& p, int high) {
    std::ofstream f(p, std::ios::binary);
    f << "P5\n64 64\n255\n";
    for (int i = 0; i < 4096; i++) f.put((char)((i % 64) < 32 ? high : 20));
}

bool sqliteExec(const std::string& dbPath, const char* sql) {
    sqlite3* h = nullptr;
    if (sqlite3_open(dbPath.c_str(), &h) != SQLITE_OK) {
        if (h) sqlite3_close(h);
        return false;
    }
    char* err = nullptr;
    const int rc = sqlite3_exec(h, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    sqlite3_close(h);
    return rc == SQLITE_OK;
}

std::string readGeneration(const std::string& dbPath) {
    msf::Database db;
    if (!db.open(dbPath)) return "<open-failed>";
    const std::string v = db.samplingGeneration();
    db.close();
    return v;
}

bool setGeneration(const std::string& dbPath, const std::string& v) {
    msf::Database db;
    if (!db.open(dbPath)) return false;
    const bool ok = db.setSamplingGeneration(v);
    db.close();
    return ok;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    std::error_code ec;

    // ---- D. image-only scans never touch the video generation. ----
    const auto rootD = fs::temp_directory_path() / "msf_genscope_d";
    const auto appD = fs::temp_directory_path() / "msf_genscope_d_app";
    fs::remove_all(rootD, ec); fs::remove_all(appD, ec);
    fs::create_directories(rootD, ec); fs::create_directories(appD, ec);
    for (int i = 0; i < 4; ++i) {
        if (!genClip(rootD / ("v" + std::to_string(i) + ".mp4"), 1, i)) {
            std::cout << "[SKIP] ffmpeg unavailable\n";
            return 0;
        }
    }
    pgm(rootD / "a.jpg", 220); pgm(rootD / "b.jpg", 200);
    const std::string dbD = (appD / "index.sqlite").string();
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbD)) { std::cerr << "openIndex failed (D1)\n"; return 2; }
        const auto r = e.scan(rootD.string(), 8, nullptr);
        check(r.completed, "D1 full-scope scan completes");
        check(r.analyzed == 6, "D1 analyzes all media (exact)");
        e.close();
    }
    if (!setGeneration(dbD, "0")) { std::cerr << "stamp reset failed (D)\n"; return 2; }
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbD)) { std::cerr << "openIndex failed (D2)\n"; return 3; }
        msf::ScanControl c;
        c.scanVideos = false; c.scanImages = true;
        const auto r = e.scan(rootD.string(), 8, &c);
        check(r.completed, "D2 image-only scan completes");
        check(r.vidScanned == 0, "D2 admits no video");
        check(r.scanned == 2, "D2 sees exactly the images");
        check(r.unchanged == 2, "D2 reuses the images");
        check(r.analyzed == 0, "D2 analyzes nothing new");
        check(readGeneration(dbD) == "0", "D2 leaves the video generation untouched");
        e.close();
    }
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbD)) { std::cerr << "openIndex failed (D3)\n"; return 4; }
        msf::ScanControl c;
        c.scanVideos = false; c.scanImages = true;
        c.cancel.store(true);
        const auto r = e.scan(rootD.string(), 8, &c);
        check(!r.completed, "D3 cancelled image-only scan reports completed=false");
        check(readGeneration(dbD) == "0", "D3 leaves the video generation untouched");
        e.close();
    }
    fs::remove_all(rootD, ec); fs::remove_all(appD, ec);

    // ---- E. ignored stale videos block promotion; unignore regrids. ----
    const auto rootE = fs::temp_directory_path() / "msf_genscope_e";
    const auto appE = fs::temp_directory_path() / "msf_genscope_e_app";
    fs::remove_all(rootE, ec); fs::remove_all(appE, ec);
    fs::create_directories(rootE, ec); fs::create_directories(appE, ec);
    for (int i = 0; i < 3; ++i) {
        if (!genClip(rootE / ("v" + std::to_string(i) + ".mp4"), 1, i)) {
            std::cout << "[SKIP] ffmpeg unavailable\n";
            return 0;
        }
    }
    const std::string dbE = (appE / "index.sqlite").string();
    std::string victim;
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbE)) { std::cerr << "openIndex failed (E1)\n"; return 5; }
        const auto r = e.scan(rootE.string(), 8, nullptr);
        check(r.completed, "E1 full scan completes");
        check(r.analyzed == 3, "E1 analyzes every clip (exact)");
        for (const auto& f : e.files())
            if (f.path.find("v0.") != std::string::npos) victim = f.path;
        e.close();
    }
    if (victim.empty()) { std::cerr << "victim path not found (E)\n"; return 5; }
    if (!setGeneration(dbE, "0")) { std::cerr << "stamp reset failed (E)\n"; return 5; }
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbE)) { std::cerr << "openIndex failed (E2)\n"; return 6; }
        msf::ScanControl c;
        c.ignoredPaths.insert(victim);
        const auto r = e.scan(rootE.string(), 8, &c);
        check(r.completed, "E2 scan with one ignored video completes");
        check(r.scanned == 2, "E2 admits exactly the non-ignored videos");
        check(r.analyzed == 2, "E2 analyzes exactly the non-ignored videos");
        check(readGeneration(dbE) == "0", "E2 does not promote past the ignored stale row");
        e.close();
    }
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbE)) { std::cerr << "openIndex failed (E3)\n"; return 7; }
        const auto r = e.scan(rootE.string(), 8, nullptr);
        check(r.completed, "E3 unignored scan completes");
        check(r.analyzed == 3, "E3 re-analyzes everything via regrid (exact)");
        check(r.unchanged == 0, "E3 reuses nothing while unstamped");
        e.close();
    }
    fs::remove_all(rootE, ec); fs::remove_all(appE, ec);

    // ---- F. stamp write failure keeps a valid scan and the old value. ----
    const auto rootF = fs::temp_directory_path() / "msf_genscope_f";
    const auto appF = fs::temp_directory_path() / "msf_genscope_f_app";
    fs::remove_all(rootF, ec); fs::remove_all(appF, ec);
    fs::create_directories(rootF, ec); fs::create_directories(appF, ec);
    for (int i = 0; i < 2; ++i) {
        if (!genClip(rootF / ("v" + std::to_string(i) + ".mp4"), 1, i)) {
            std::cout << "[SKIP] ffmpeg unavailable\n";
            return 0;
        }
    }
    const std::string dbF = (appF / "index.sqlite").string();
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbF)) { std::cerr << "openIndex failed (F0)\n"; return 8; }
        e.close();
    }
    if (!sqliteExec(dbF, "CREATE TRIGGER no_gen_ins BEFORE INSERT ON meta "
                         "WHEN NEW.key='sampling_generation' "
                         "BEGIN SELECT RAISE(ABORT,'injected meta failure'); END;") ||
        !sqliteExec(dbF, "CREATE TRIGGER no_gen_upd BEFORE UPDATE ON meta "
                         "WHEN NEW.key='sampling_generation' "
                         "BEGIN SELECT RAISE(ABORT,'injected meta failure'); END;")) {
        std::cerr << "trigger install failed (F)\n";
        return 8;
    }
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbF)) { std::cerr << "openIndex failed (F1)\n"; return 9; }
        const auto r = e.scan(rootF.string(), 8, nullptr);
        check(r.completed, "F1 scan completes despite the stamp failure");
        check(r.analyzed == 2, "F1 analyzes every clip (exact)");
        check(readGeneration(dbF) == "0", "F1 keeps the old generation value");
        const std::string js = e.telemetryJson();
        check(js.find("\"failed\":false") != std::string::npos, "F1 records no failure");
        e.close();
    }
    if (!sqliteExec(dbF, "DROP TRIGGER no_gen_ins;") ||
        !sqliteExec(dbF, "DROP TRIGGER no_gen_upd;")) {
        std::cerr << "trigger drop failed (F)\n";
        return 9;
    }
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbF)) { std::cerr << "openIndex failed (F2)\n"; return 10; }
        const auto r = e.scan(rootF.string(), 8, nullptr);
        check(r.completed, "F2 rescan completes");
        check(r.analyzed == 2, "F2 regrids on the kept old value (exact)");
        check(r.unchanged == 0, "F2 reuses nothing while unstamped");
        check(readGeneration(dbF) == "3", "F2 stamp recovers once writable");
        e.close();
    }
    fs::remove_all(rootF, ec); fs::remove_all(appF, ec);

    // ---- G. DB error with the cancel flag set is Failed, not Cancelled. ----
    const auto rootG = fs::temp_directory_path() / "msf_genscope_g";
    const auto appG = fs::temp_directory_path() / "msf_genscope_g_app";
    fs::remove_all(rootG, ec); fs::remove_all(appG, ec);
    fs::create_directories(rootG, ec); fs::create_directories(appG, ec);
    for (int i = 0; i < 3; ++i) {
        if (!genClip(rootG / ("v" + std::to_string(i) + ".mp4"), 1, i)) {
            std::cout << "[SKIP] ffmpeg unavailable\n";
            return 0;
        }
    }
    const std::string dbG = (appG / "index.sqlite").string();
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbG)) { std::cerr << "openIndex failed (G0)\n"; return 11; }
        e.close();
    }
    if (!sqliteExec(dbG, "CREATE TRIGGER no_files BEFORE INSERT ON files "
                         "BEGIN SELECT RAISE(ABORT,'injected files failure'); END;")) {
        std::cerr << "trigger install failed (G)\n";
        return 11;
    }
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbG)) { std::cerr << "openIndex failed (G1)\n"; return 12; }
        msf::ScanControl c;
        c.walked = [&](std::size_t n) {
            if (n >= 1) c.cancel.store(true);
        };
        const auto r = e.scan(rootG.string(), 8, &c);
        check(!r.completed, "G1 failing scan reports completed=false");
        check(r.scanned == 1, "G1 reaches the upsert on the first admission");
        check(r.failed == 0, "G1 counts no analysis failures (DB failure is not decode failure)");
        const std::string js = e.telemetryJson();
        check(js.find("\"failed\":true") != std::string::npos, "G1 telemetry marks failed");
        check(js.find("\"cancelled\":false") != std::string::npos, "G1 telemetry does not mark cancelled");
        e.close();
    }
    {   // Rollback evidence: nothing from the failed scan survived.
        msf::Database db;
        if (!db.open(dbG)) { std::cerr << "db open failed (G check)\n"; return 12; }
        check(db.all().empty(), "G1 rolled back: no rows survived");
        db.close();
    }
    if (!sqliteExec(dbG, "DROP TRIGGER no_files;")) {
        std::cerr << "trigger drop failed (G)\n";
        return 12;
    }
    {
        msf::MediaSearchEngine e;
        if (!e.openIndex(dbG)) { std::cerr << "openIndex failed (G2)\n"; return 13; }
        const auto r = e.scan(rootG.string(), 8, nullptr);
        check(r.completed, "G2 retry completes after the fault is removed");
        check(r.analyzed == 3, "G2 analyzes everything fresh (exact)");
        e.close();
    }
    fs::remove_all(rootG, ec); fs::remove_all(appG, ec);

    std::cout << "generation_scope_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
