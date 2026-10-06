// Stop/Cancel responsiveness regression (0.9.4.47+).
//
// Before this test, MediaSearchEngine::scan() ran computeDatasetFingerprint()
// at startup with no cancellation hook. That function reads EVERY file fully,
// so on a large dataset Stop did nothing until hundreds of gigabytes had been
// hashed: the GUI showed "stopping" while disk I/O continued for 10+ minutes.
//
// This drives the real production scan path (openIndexForRoot + scan) and
// verifies, deterministically (no timing assertions):
//   1. a pre-set cancel makes the fingerprint return state="cancelled",
//      not "measured" (old code ignored cancel and measured anyway);
//   2. a pre-set cancel makes engine.scan return completed=false;
//   3. the DB is intact and reopenable after a cancelled scan;
//   4. a normal scan afterwards still completes (cancel did not corrupt state).
// Promptness is measured manually, not asserted: with cancel pre-set the
// fingerprint must not read the files at all.
#include "dataset_fingerprint.h"
#include "media_search_engine.h"
#include <chrono>
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

// Deterministic binary payload. Same bytes every run so content is stable.
// Written as a valid 8x8 24-bit BMP so the engine actually scans it (.bin is
// not a supported media extension and would be skipped by the walker).
void blob(const std::filesystem::path& p, unsigned char seed) {
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
            const unsigned char px[3] = {
                (unsigned char)((x * 30 + seed) & 0xFF),
                (unsigned char)((y * 30) & 0xFF), 40 };
            f.write((const char*)px, 3);
        }
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "msf_cancel_fp";
    const auto app  = fs::temp_directory_path() / "msf_cancel_fp_app";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::remove_all(app, ec);
    fs::create_directories(root, ec);
    fs::create_directories(app, ec);

    // 20 small valid BMPs. Cancel is pre-set, so the new code must not read
    // at all; the old code would hash everything first.
    for (int i = 0; i < 20; ++i)
        blob(root / ("f" + std::to_string(i) + ".bmp"), (unsigned char)i);

    // ---- 1. fingerprint honors a pre-set cancel. ----
    {
        std::atomic_bool cancel{true};
        const auto t0 = std::chrono::steady_clock::now();
        const auto fp = msf::computeDatasetFingerprint(root.string(), &cancel);
        const auto ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        check(fp.state == "cancelled", "pre-set cancel yields state=cancelled");
        check(fp.fingerprint.empty(), "a cancelled fingerprint carries no hash");
        check(fp.fileCount == 0, "a cancelled fingerprint counts no files");
        std::cout << "  [info] cancelled fingerprint took " << ms << " ms\n";
    }

    // ---- 2. without cancel the same dataset measures normally. ----
    {
        const auto fp = msf::computeDatasetFingerprint(root.string());
        check(fp.state == "measured", "no cancel yields state=measured");
        check(fp.fileCount == 20, "all files counted");
        check(fp.fingerprint.size() == 64, "64-hex manifest hash present");
    }

    // ---- 3. engine scan with cancel pre-set returns incomplete, promptly. ----
    {
        msf::MediaSearchEngine e;
        if (!e.openIndexForRoot(root.string(), app.string())) {
            std::cerr << "openIndexForRoot failed\n";
            return 2;
        }
        msf::ScanControl c;
        c.cancel.store(true);
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = e.scan(root.string(), 8, &c);
        const auto ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        check(!r.completed, "cancelled scan reports completed=false");
        check(r.scanned == 0 || r.analyzed == 0,
              "cancelled scan analyzes nothing");
        std::cout << "  [info] cancelled scan took " << ms << " ms\n";
        // Cancellation phase is recorded: pre-set cancel lands during the
        // fingerprint stage, before any walk. The JSON must say so.
        const std::string js = e.telemetryJson();
        check(js.find("\"cancelledDuring\":\"fingerprint\"") != std::string::npos,
              "cancelledDuring=fingerprint for a pre-set cancel");
        check(js.find("\"cancelled\":true") != std::string::npos,
              "cancelled flag is true");
        e.close();
    }

    // ---- 4. the DB is intact and a normal scan still completes. ----
    {
        msf::MediaSearchEngine e;
        if (!e.openIndexForRoot(root.string(), app.string())) {
            std::cerr << "reopen failed\n";
            return 3;
        }
        const auto r = e.scan(root.string(), 8, nullptr);
        check(r.completed, "a normal scan completes after a cancelled one");
        check(r.scanned == 20, "all files seen");
        check(r.analyzed == 20, "all files analyzed");
        e.close();
    }

    // ---- 5. fingerprint progress fires live (the fixed blackout). ----
    {
        std::size_t lastN = 0;
        std::uint64_t lastB = 0;
        std::size_t calls = 0;
        const auto fp = msf::computeDatasetFingerprint(
            root.string(), nullptr,
            [&](std::size_t n, std::uint64_t b, const std::string&) {
                ++calls;
                if (n > lastN) lastN = n;
                if (b > lastB) lastB = b;
            });
        check(fp.state == "measured", "progress run still measures");
        check(calls == 20, "progress fired once per hashed file");
        check(lastN == 20, "progress file count reaches the total");
        check(lastB > 0, "progress byte count is nonzero");
        check(lastB == fp.totalBytes, "progress bytes converge to the total");
    }

    // ---- 6. telemetry-off scan skips the fingerprint (no wasted I/O). ----
    // Fresh app dir so this is a first scan: everything must analyze while
    // the fingerprint stays untouched.
    {
        const auto app6 = fs::temp_directory_path() / "msf_cancel_fp_app6";
        fs::remove_all(app6, ec);
        fs::create_directories(app6, ec);
        msf::MediaSearchEngine e;
        if (!e.openIndexForRoot(root.string(), app6.string())) {
            std::cerr << "reopen2 failed\n";
            return 4;
        }
        msf::ScanControl c;
        c.telemetryEnabled = false;
        const auto r = e.scan(root.string(), 8, &c);
        check(r.completed, "telemetry-off scan completes");
        check(r.analyzed == 20, "telemetry-off scan still analyzes");
        const std::string js = e.telemetryJson();
        check(js.find("\"state\":\"not_available\"") != std::string::npos,
              "telemetry-off leaves the fingerprint not_available");
        e.close();
        fs::remove_all(app6, ec);
    }

    fs::remove_all(root, ec);
    fs::remove_all(app, ec);

    std::cout << "cancel_fingerprint_selfcheck="
              << (gOk ? "ok" : "FAILED") << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
