// User-facing summary breakdown regression (0.9.4.48+).
//
// The Detailed Logs dialog shows per-kind scanned/analyzed/throughput plus the
// duplicate groups/files/pairs split. Groups and files are different things
// (clusters vs members) and must be counted separately. This drives the real
// MediaSearchEngine production scan path and pins the partition identities:
//
//   imgScanned + vidScanned == scanned
//   imgAnalyzed + vidAnalyzed == analyzed
//   imgGroups + vidGroups == total distinct clusters
//   imgDupFiles + vidDupFiles == total files participating in any pair
//   imgPairs + vidPairs == total verified pairs
//
// Fixture: two identical BMPs (one duplicate pair, one group of two members)
// plus one distinct BMP. No videos, so every vid* field must be exactly zero.
#include "media_search_engine.h"
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

void bmp(const std::filesystem::path& p, unsigned char seed) {
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
    const auto root = fs::temp_directory_path() / "msf_summary_breakdown";
    const auto app  = fs::temp_directory_path() / "msf_summary_breakdown_app";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::remove_all(app, ec);
    fs::create_directories(root, ec);
    fs::create_directories(app, ec);

    bmp(root / "a.bmp", 0);
    bmp(root / "b.bmp", 0);   // byte-identical to a.bmp: one pair, one group
    bmp(root / "c.bmp", 77);  // distinct content: scanned but unmatched

    msf::MediaSearchEngine e;
    if (!e.openIndexForRoot(root.string(), app.string())) {
        std::cerr << "openIndexForRoot failed\n";
        return 2;
    }
    const auto r = e.scan(root.string(), 8, nullptr);

    check(r.scanned == 3, "scanned=3");
    check(r.analyzed == 3, "analyzed=3");
    check(r.imgScanned == 3, "imgScanned=3");
    check(r.imgAnalyzed == 3, "imgAnalyzed=3");
    check(r.vidScanned == 0, "vidScanned=0 (no videos)");
    check(r.vidAnalyzed == 0, "vidAnalyzed=0 (no videos)");
    check(r.imgScanned + r.vidScanned == r.scanned, "scanned partitions by kind");
    check(r.imgAnalyzed + r.vidAnalyzed == r.analyzed, "analyzed partitions by kind");

    // One duplicate pair (a,b), one group of two, two member files.
    check(r.imgPairs == 1, "imgPairs=1 for the identical pair");
    check(r.vidPairs == 0, "vidPairs=0");
    check(r.imgPairs + r.vidPairs == r.groups, "pairs partition the total pair count");
    check(r.imgGroups == 1, "imgGroups=1 for the single cluster");
    check(r.vidGroups == 0, "vidGroups=0");
    check(r.imgDupFiles == 2, "imgDupFiles=2 (both members, not the group count)");
    check(r.vidDupFiles == 0, "vidDupFiles=0");
    check(r.imgDupFiles != r.imgGroups, "duplicate files and groups are different things");

    // Telemetry mirrors the engine so the GUI summary and the JSON agree.
    const std::string js = e.telemetryJson();
    check(js.find("\"imagePairs\":1") != std::string::npos, "telemetry carries imagePairs=1");
    check(js.find("\"imageGroups\":1") != std::string::npos, "telemetry carries imageGroups=1");
    check(js.find("\"imageDuplicateFiles\":2") != std::string::npos, "telemetry carries imageDuplicateFiles=2");
    check(js.find("\"videoPairs\":0") != std::string::npos, "telemetry carries videoPairs=0");
    check(js.find("\"breakdownState\":\"measured\"") != std::string::npos, "breakdown is measured");
    // Phase-A telemetry fields. finishedAt exists and is not before startedAt
    // (same localTimeStr convention, so lexicographic compare is valid).
    // totalScannedBytes equals images.bytes + videos.bytes. Series keeps tMs
    // and carries an absolute wallTime anchor. Fingerprint duration/bytes are
    // present. System RAM is captured for low-memory assessment.
    {
        const auto sAt = js.find("\"startedAt\":\"");
        const auto fAt = js.find("\"finishedAt\":\"");
        check(sAt != std::string::npos && fAt != std::string::npos, "startedAt and finishedAt exist");
        if (sAt != std::string::npos && fAt != std::string::npos) {
            const std::string s = js.substr(sAt + 13, 19);
            const std::string f = js.substr(fAt + 14, 19);
            check(s <= f, "finishedAt is not before startedAt");
        }
        check(js.find("\"cancelledDuring\":\"\"") != std::string::npos, "no phase claimed on a normal run");
        check(js.find("\"totalScannedBytes\"") != std::string::npos, "totalScannedBytes present");
        // Series entries are positional arrays [tMs,cpuProc,cpuSys,memMB,gpu,
        // ioReadBps,ioWriteBps,"wallTime"]. The 8th element is the absolute
        // timestamp; there is no "wallTime": key by design.
        {
            const auto sp = js.find("\"series\":[[");
            bool hasWall = false;
            if (sp != std::string::npos) {
                // An 8-element entry has 7 commas before its closing bracket.
                // Look for ,"YYYY- pattern: quote, comma, quote, 4 digits, dash.
                for (std::size_t i = sp; i + 8 < js.size(); ++i) {
                    if (js[i] == ',' && js[i+1] == '"' &&
                        js[i+2] >= '0' && js[i+2] <= '9' &&
                        js[i+6] == '-') { hasWall = true; break; }
                    if (js[i] == ']' && i > sp + 12) break;
                }
            }
            check(hasWall, "series carries absolute wallTime");
        }
        check(js.find("\"durationMs\"") != std::string::npos, "fingerprint durationMs present");
        check(js.find("\"bytesRead\"") != std::string::npos, "fingerprint bytesRead present");
        check(js.find("\"memSystemMB\"") != std::string::npos, "system RAM captured");
    }

    e.close();
    fs::remove_all(root, ec);
    fs::remove_all(app, ec);

    std::cout << "summary_breakdown_selfcheck="
              << (gOk ? "ok" : "FAILED") << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
