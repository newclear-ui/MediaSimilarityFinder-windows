// Analysis-failure state regression (0.9.4.46 DEFECT-A / DEFECT-B).
//
// Before this test, a file whose decode/analysis produced no fingerprint was
// still reported as a successful `added`/`modified` index entry and then
// re-queued as `modified` with analyzed=0 on every later scan, forever:
//   run1: scanned=1 added=1     analyzed=0
//   run2: scanned=1 modified=1 analyzed=0   <- identical, forever
// That was found on the real dataset (a YCbCr LZW TIFF that WIC cannot decode),
// but the cause is not TIFF-specific: it is the absence of a state for
// "registered, analysis attempted, no fingerprint". This test reproduces the
// state transition with a small deterministic fixture instead of that file.
//
// Fixture: a valid 8x8 24-bit BMP that analyzes, plus a BMP whose header claims
// a 64x64 image while carrying no pixel data, so WIC's GetFrame(0) cannot yield
// a frame. That is a decode failure by construction, with no external file.
//
// This drives the real MediaSearchEngine production scan path
// (openIndexForRoot + scan) on both the CPU and the GPU tree; it does not disable
// the GPU and does not use a helper-only path.
#include "media_search_engine.h"
#include "index_manager.h"
#include "database.h"
#include <sqlite3.h>
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

// Minimal 8x8 24-bit BMP, same construction the image verify suite uses.
void goodBmp(const std::filesystem::path& p) {
    const int w = 8, h = 8;
    std::ofstream f(p, std::ios::binary);
    const int row = ((w * 3 + 3) / 4) * 4, img = row * h, fs = 54 + img;
    unsigned char hd[54] = {0};
    hd[0] = 'B'; hd[1] = 'M';
    hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
    hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
    hd[26] = 1; hd[28] = 24;
    hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
    f.write((const char*)hd, 54);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned char px[3] = { (unsigned char)(x * 30), (unsigned char)(y * 30), 40 };
            f.write((const char*)px, 3);
        }
}

// A structurally valid BMP header that claims 64x64 pixels with bfSizeBits
// pointing past the end of the file, and carries no pixel data at all.
// Deterministic decode failure: the frame cannot be produced.
void undecodableBmp(const std::filesystem::path& p) {
    const int w = 64, h = 64;
    std::ofstream f(p, std::ios::binary);
    const int row = ((w * 3 + 3) / 4) * 4, img = row * h, fs = 54 + img;
    unsigned char hd[54] = {0};
    hd[0] = 'B'; hd[1] = 'M';
    hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
    hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
    hd[26] = 1; hd[28] = 24;
    hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
    // Header only: every pixel byte the header promises is absent.
    f.write((const char*)hd, 54);
}

// scanned == added + modified + unchanged + failed must hold exactly.
void accounting(const msf::SearchReport& r, const char* where) {
    const std::size_t sum = r.added + r.modified + r.unchanged + r.failed;
    if (sum != r.scanned)
        std::cerr << "  accounting broken at " << where << ": scanned=" << r.scanned
                  << " added=" << r.added << " modified=" << r.modified
                  << " unchanged=" << r.unchanged << " failed=" << r.failed << "\n";
    check(sum == r.scanned, where);
}

} // namespace

// Reads the persisted row straight from the managed index so the assertions do
// not depend on a test-only accessor on the product class (same read-only pattern
// as video_cache_test). Returns false when the row cannot be read.
static bool readRow(const std::filesystem::path& dbPath, const std::string& leaf,
                    std::uint64_t& fingerprint, bool& analysisFailed) {
    sqlite3* raw = nullptr;
    if (sqlite3_open(dbPath.string().c_str(), &raw) != SQLITE_OK) { if (raw) sqlite3_close(raw); return false; }
    sqlite3_stmt* st = nullptr;
    const char* q = "SELECT fingerprint,analysis_failed FROM files WHERE path LIKE ?";
    if (sqlite3_prepare_v2(raw, q, -1, &st, nullptr) != SQLITE_OK) { sqlite3_close(raw); return false; }
    const std::string pattern = "%" + leaf;
    sqlite3_bind_text(st, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        fingerprint = (std::uint64_t)sqlite3_column_int64(st, 0);
        analysisFailed = sqlite3_column_int(st, 1) != 0;
        found = true;
    }
    sqlite3_finalize(st);
    sqlite3_close(raw);
    return found;
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / "msf_analysis_failure_state";
    const auto app  = std::filesystem::temp_directory_path() / "msf_analysis_failure_app";
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(app);
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(app);

    goodBmp(root / "good.bmp");
    undecodableBmp(root / "broken.bmp");

    std::cout << "scan path = MediaSearchEngine::openIndexForRoot + scan (production)\n";

    msf::MediaSearchEngine engine;
    if (!engine.openIndexForRoot(root.string(), app.string())) {
        std::cerr << "openIndexForRoot failed\n";
        return 2;
    }

    // ---- scan 1: first encounter. The failure must not look like a success. ----
    const auto r1 = engine.scan(root.string());
    check(r1.scanned == 2, "scan1 scanned=2");
    check(r1.analyzed == 1, "scan1 analyzed counts only the successful file");
    check(r1.added == 1, "scan1 added counts only the analyzed file");
    check(r1.modified == 0, "scan1 modified=0");
    check(r1.failed == 1, "scan1 reports the decode failure explicitly");
    check(r1.added + r1.modified <= r1.analyzed,
          "scan1 never reports more added+modified than actually analyzed");
    accounting(r1, "scan1 accounting");

    // ---- scan 2: must converge, not repeat as modified forever (DEFECT-B). ----
    const auto r2 = engine.scan(root.string());
    check(r2.modified == 0, "scan2 modified=0 (DEFECT-B: no permanent modified)");
    check(r2.added == 0, "scan2 added=0");
    check(r2.analyzed == 0, "scan2 analyzed=0");
    check(r2.unchanged == 2, "scan2 unchanged=2, so the report is settled");
    check(r2.failed == 0, "scan2 no new failure is invented for a settled row");
    accounting(r2, "scan2 accounting");

    // ---- scan 3 and 4: the report must be identical. Convergence is the point. ----
    const auto r3 = engine.scan(root.string());
    const auto r4 = engine.scan(root.string());
    check(r3.scanned == r4.scanned && r3.added == r4.added && r3.modified == r4.modified
              && r3.unchanged == r4.unchanged && r3.analyzed == r4.analyzed
              && r3.failed == r4.failed,
          "scan3 and scan4 report identical state");
    check(r3.modified == 0, "scan3 modified=0");
    check(r4.modified == 0, "scan4 modified=0");
    check(r3.candidates == r4.candidates && r3.groups == r4.groups,
          "candidate/group result is deterministic across settled rescans");

    // ---- a normal file keeps its existing rescan semantics. ----
    check(r2.unchanged == 2 && r2.analyzed == 0,
          "normal file converges to unchanged and is not re-analyzed");

    // ---- a failed file carries no fingerprint, so it cannot enter search. ----
    // Resolve the managed index the same way production does, then read the rows.
    msf::IndexPaths paths;
    const bool located = msf::IndexManager::resolve(app.string(), root.string(), paths);
    check(located, "managed index located through IndexManager");
    std::uint64_t brokenFp = 1, goodFp = 0;
    bool brokenFailed = false, goodFailed = true;
    if (located) {
        check(readRow(paths.database, "broken.bmp", brokenFp, brokenFailed),
              "the failed file is present as a known row");
        check(readRow(paths.database, "good.bmp", goodFp, goodFailed),
              "the analyzed file is present");
    }
    check(brokenFp == 0, "failed file has no fingerprint");
    check(brokenFailed, "failed file is recorded in the analysis-failed state");
    check(goodFp != 0, "analyzed file keeps its fingerprint");
    check(!goodFailed, "analyzed file is not in the failed state");

    // ---- a content change on the failed file re-queues it (not stuck forever). ----
    undecodableBmp(root / "broken.bmp");   // rewrite with a new mtime/size context
    goodBmp(root / "broken.bmp");          // now it becomes decodable
    const auto r5 = engine.scan(root.string());
    check(r5.modified == 1, "a changed file is re-analyzed once it becomes readable");
    check(r5.analyzed == 1, "the recovered file is analyzed");
    check(r5.failed == 0, "no failure is reported once analysis succeeds");

    engine.close();
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(app);

    std::cout << "analysis_failure_state_selfcheck="
              << (gOk ? "ok" : "FAILED") << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
