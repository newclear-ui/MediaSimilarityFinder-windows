// Stop-drain regression (0.9.4.58).
//
// Stop means "read no new files", not "discard work already read". Before this
// test, cancelling mid-walk dropped the admitted-but-unanalyzed image batch,
// so stopping a long scan kept zero new index rows. Now the pending image
// batch drains to completion on cancel (bounded: at most one batch), while
// videos keep drop behavior (single video decode is unbounded).
//
// Deterministic, no timing: cancel is set from the synchronous walked hook
// after 3 admissions. Default gpuBatch (256) far exceeds the 10-file fixture,
// so no mid-walk batch fires; without the drain, analyzed would be 0. Exact
// counts throughout: 3 admitted/drained, then rescan reuses exactly those 3
// as unchanged and analyzes exactly the remaining 7 fresh.
#include "media_search_engine.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

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

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << "\n"; }
    else       { std::cout << "  [ok] " << what << "\n"; }
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "msf_cancel_drain";
    const auto app = fs::temp_directory_path() / "msf_cancel_drain_app";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::remove_all(app, ec);
    fs::create_directories(root, ec);
    fs::create_directories(app, ec);
    for (int i = 0; i < 10; ++i)
        bmp(root / ("d" + std::to_string(i) + ".bmp"), (unsigned char)(i * 7));

    msf::MediaSearchEngine e;
    if (!e.openIndexForRoot(root.string(), app.string())) {
        std::cerr << "openIndexForRoot failed\n";
        return 2;
    }
    msf::ScanControl c;
    // Cancel after 3 admissions. walked() fires synchronously per admitted
    // file inside processOne, so at least 3 images sit unanalyzed in the
    // batch when the consumer loop notices the cancel.
    c.walked = [&](std::size_t n) {
        if (n >= 3) c.cancel.store(true);
    };
    const auto r = e.scan(root.string(), 8, &c);
    check(!r.completed, "cancelled scan reports completed=false");
    // Exact: the single-threaded consumer admits precisely the 3 files whose
    // processOne ran before the loop observes the cancel; the trailing batch
    // then drains all of them. No timing involved.
    check(r.analyzed == 3, "admitted images drain to analysis despite cancel (exact)");
    check(r.scanned == 3, "exactly 3 admissions happened before the stop");
    check(r.imgAnalyzed == 3, "all drained analysis is image analysis");
    // Drained rows are real index rows: the rescan must reuse exactly those 3
    // as unchanged and analyze exactly the remaining 7 fresh — proving the
    // first scan's work was actually reused, not merely present.
    {
        msf::MediaSearchEngine e2;
        if (!e2.openIndexForRoot(root.string(), app.string())) {
            std::cerr << "reopen failed\n";
            return 3;
        }
        const auto r2 = e2.scan(root.string(), 8, nullptr);
        check(r2.completed, "rescan completes");
        check(r2.unchanged == 3, "drained rows reused as unchanged (exact)");
        check(r2.analyzed == 7, "remaining files analyzed fresh (exact)");
        e2.close();
    }
    e.close();
    fs::remove_all(root, ec);
    fs::remove_all(app, ec);

    std::cout << "cancel_drain_selfcheck="
              << (gOk ? "ok" : "FAILED") << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
