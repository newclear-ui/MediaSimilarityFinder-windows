// Crash-frontier durability regression (0.9.4.76).
//
// Admission skeleton rows (fingerprint 0, analysis not failed) must be
// committed BEFORE batch work starts, so a mid-batch failure/kill leaves a
// visible frontier that the next scan retries through the pendingAnalysis
// rule. Provenance: a real 0xC0000005 backend crash with zero completed files
// left zero fingerprint-0 rows, making the poison batch invisible.
//
// Phase 1 faults deterministically mid-batch (MSF_TEST_THROW_BATCH, after the
// entry checkpoint) and asserts the frontier survives in a fresh engine
// handle. Phase 2 rescans cleanly and asserts full analysis with no frontier
// left behind. QCoreApplication suffices (no GUI).
#include "scan_worker.h"
#include "media_search_engine.h"
#include "database.h"
#include "index_manager.h"
#include "path_utils.h"
#include <QCoreApplication>
#include <QObject>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << std::endl; }
    else       { std::cout << "  [ok] " << what << std::endl; }
}

// Minimal 8x8 24-bit BMP. Identical files hash identically -> distance 0,
// so 8 copies form 28 pairs (same fixture shape as scan_streaming).
void bmp(const std::filesystem::path& p) {
    std::ofstream f(p, std::ios::binary);
    const int w = 8, h = 8, row = w * 3, img = row * h, fs = 54 + img;
    unsigned char hd[54] = {0};
    hd[0] = 'B'; hd[1] = 'M';
    hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
    hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
    hd[26] = 1; hd[28] = 24;
    hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
    f.write((const char*)hd, 54);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned char v = (x >= 4) ? 255 : 0;
            f.put((char)v); f.put((char)v); f.put((char)v);
        }
}

struct Frontier {
    bool valid = false;
    long total = 0;
    long unanalyzed = 0; // fingerprint 0, analysis NOT failed: crash frontier
    long failed = 0;
};

Frontier readFrontier(const std::string& appDir, const std::string& root) {
    Frontier out;
    msf::IndexPaths paths;
    if (!msf::IndexManager::resolve(msf::path_from_utf8(appDir), msf::path_from_utf8(root), paths))
        return out;
    msf::Database db;
    if (!db.open(msf::path_to_utf8(paths.database)) || !db.initialize()) return out;
    for (const auto& x : db.all()) {
        ++out.total;
        if (x.fingerprint == 0 && !x.analysisFailed) ++out.unanalyzed;
        if (x.analysisFailed) ++out.failed;
    }
    out.valid = true;
    return out;
}

bool parseCompletion(const QString& message, qulonglong& scanned, qulonglong& analyzed) {
    const QStringList parts = message.split(QLatin1Char('|'));
    if (parts.size() < 6 || parts.first().startsWith(QStringLiteral("CANCELLED"))) return false;
    bool ok = false;
    scanned = parts[1].toULongLong(&ok);
    if (!ok) return false;
    analyzed = parts[2].toULongLong(&ok);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto work = fs::temp_directory_path() / "msf_crash_frontier";
    fs::remove_all(work, ec);
    fs::create_directories(work / "media", ec);
    fs::create_directories(work / "appdir", ec);
    for (int i = 0; i < 8; ++i)
        bmp(work / "media" / ("dup" + std::to_string(i) + ".bmp"));
    const std::string root = (work / "media").string();
    const std::string ad = (work / "appdir").string();

    // Phase 1: deterministic mid-batch failure AFTER the entry checkpoint.
    // The worker must report failure (not die), and the admission frontier
    // must survive in the committed index.
    qputenv("MSF_TEST_THROW_BATCH", "1");
    bool failedSeen = false;
    {
        ScanWorker w(QString::fromStdString(root), QString::fromStdString(ad),
                     8, 50, 50, false, true, false);
        QObject::connect(&w, &ScanWorker::failed, [&](const QString&) { failedSeen = true; });
        w.run();
    }
    qunsetenv("MSF_TEST_THROW_BATCH");
    check(failedSeen, "mid-batch fault reported as failure, process alive");
    const Frontier f1 = readFrontier(ad, root);
    check(f1.valid, "frontier readable after mid-batch failure");
    check(f1.total == 8, "all 8 admitted files recorded");
    check(f1.unanalyzed == 8, "unanalyzed frontier committed (fingerprint 0, not failed)");
    check(f1.failed == 0, "no analysis failures recorded");
    if (!gOk) return 1;

    // Phase 2: clean rescan retries the frontier via pendingAnalysis and
    // completes everything with nothing left behind.
    bool finishedSeen = false;
    QString finishedMsg;
    {
        ScanWorker w(QString::fromStdString(root), QString::fromStdString(ad),
                     8, 50, 50, false, true, false);
        QObject::connect(&w, &ScanWorker::finished, [&](const QString& m) {
            finishedSeen = true;
            finishedMsg = m;
        });
        w.run();
    }
    check(finishedSeen, "rescan finished");
    qulonglong scanned = 0, analyzed = 0;
    check(parseCompletion(finishedMsg, scanned, analyzed), "rescan completion parsed");
    check(scanned == 8 && analyzed == 8, "frontier fully reprocessed, not skipped");
    const Frontier f2 = readFrontier(ad, root);
    check(f2.valid && f2.unanalyzed == 0, "no frontier left after clean rescan");
    msf::MediaSearchEngine e2;
    check(e2.openIndexForRoot(root, ad), "index reopens after frontier rescan");
    check((long)e2.loadMatches().size() >= 28, "all 28 pairs indexed");

    fs::remove_all(work, ec);
    std::cout << "crash_frontier_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
