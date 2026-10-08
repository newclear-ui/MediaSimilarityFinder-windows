// Test Mode isolation regression (0.9.4.75).
//
// A Test Mode scan must use the same ScanWorker/MediaSearchEngine pipeline as
// production, but inside a fresh per-run scratch application directory. This
// drives three real BackendSession scans over four identical BMPs:
// baseline production, a warm production repeat, and a Test Mode run. It then
// asserts that the Test Mode run fully reprocesses the files while the
// production index.sqlite/video_cache.sqlite/metadata.json bytes are unchanged.
#include "backend_session.h"
#include "index_manager.h"
#include "path_utils.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QObject>
#include <QTimer>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) {
        gOk = false;
        std::cerr << "  [FAIL] " << what << std::endl;
    } else {
        std::cout << "  [ok] " << what << std::endl;
    }
}

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
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char v = (x >= 4) ? 255 : 0;
            f.put((char)v); f.put((char)v); f.put((char)v);
        }
    }
}

struct ScanOutcome {
    bool finished = false;
    bool failed = false;
    QString message;
};

ScanOutcome runSessionScan(BackendSession& session, const QString& root, const QString& appDir,
                           bool testMode) {
    ScanOutcome out;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(120000);
    QObject::connect(&session, &BackendSession::finished, &loop,
                     [&](const QString& message) {
                         out.finished = true;
                         out.message = message;
                         loop.quit();
                     });
    QObject::connect(&session, &BackendSession::failed, &loop,
                     [&](const QString& message) {
                         out.failed = true;
                         out.message = message;
                         loop.quit();
                     });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { loop.quit(); });
    session.startScan(root, appDir, 8, 55, 60, false, true, false, {}, true, 3, 0, testMode);
    timeout.start();
    loop.exec();
    return out;
}

bool parseCompletion(const QString& message, qulonglong& scanned, qulonglong& analyzed,
                     qulonglong& unchanged) {
    const QStringList parts = message.split(QLatin1Char('|'));
    if (parts.size() < 6 || parts.first().startsWith(QStringLiteral("CANCELLED"))) return false;
    bool ok = false;
    scanned = parts[1].toULongLong(&ok);
    if (!ok) return false;
    analyzed = parts[2].toULongLong(&ok);
    if (!ok) return false;
    unchanged = parts[3].toULongLong(&ok);
    return ok;
}

QByteArray fileSha256(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result().toHex();
}

struct ProductionSnapshot {
    bool valid = false;
    QByteArray database;
    QByteArray videoCache;
    QByteArray metadata;
};

ProductionSnapshot snapshotProduction(const QString& appDir, const QString& root) {
    ProductionSnapshot snapshot;
    msf::IndexPaths paths;
    if (!msf::IndexManager::resolve(msf::path_from_utf8(appDir.toStdString()),
                                    msf::path_from_utf8(root.toStdString()), paths)) {
        return snapshot;
    }
    QDir dir;
    if (!dir.exists(QString::fromStdString(paths.database.string()))) return snapshot;
    snapshot.database = fileSha256(paths.database);
    snapshot.videoCache = fileSha256(paths.videoCache);
    snapshot.metadata = fileSha256(paths.metadata);
    snapshot.valid = !snapshot.database.isEmpty() && !snapshot.metadata.isEmpty();
    return snapshot;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto work = fs::temp_directory_path() / "msf_test_mode";
    fs::remove_all(work, ec);
    fs::create_directories(work / "media", ec);
    fs::create_directories(work / "app", ec);
    for (int i = 0; i < 4; ++i) {
        bmp(work / "media" / ("dup" + std::to_string(i) + ".bmp"));
    }
    const QString root = QString::fromStdString((work / "media").string());
    const QString appDir = QString::fromStdString((work / "app").string());

    BackendSession baseline(&app);
    const ScanOutcome first = runSessionScan(baseline, root, appDir, false);
    baseline.shutdown();
    check(first.finished && !first.failed, "baseline production scan finished");
    qulonglong scanned = 0, analyzed = 0, unchanged = 0;
    check(parseCompletion(first.message, scanned, analyzed, unchanged), "baseline completion message parsed");
    check(scanned == 4 && analyzed == 4, "baseline fully processed the cold index");
    if (!gOk) return 1;

    BackendSession warm(&app);
    const ScanOutcome second = runSessionScan(warm, root, appDir, false);
    warm.shutdown();
    check(second.finished && !second.failed, "warm production repeat finished");
    qulonglong warmAnalyzed = 0, warmUnchanged = 0, warmScanned = 0;
    check(parseCompletion(second.message, warmScanned, warmAnalyzed, warmUnchanged),
          "warm completion message parsed");
    check(warmScanned == 4 && warmUnchanged == 4, "warm repeat reused the stored index");
    const ProductionSnapshot before = snapshotProduction(appDir, root);
    check(before.valid, "production index snapshot readable");
    if (!gOk) return 2;

    BackendSession probe(&app);
    const ScanOutcome test = runSessionScan(probe, root, appDir, true);
    probe.shutdown();
    check(test.finished && !test.failed, "Test Mode scan finished");
    qulonglong testScanned = 0, testAnalyzed = 0, testUnchanged = 0;
    check(parseCompletion(test.message, testScanned, testAnalyzed, testUnchanged),
          "Test Mode completion message parsed");
    check(testScanned == 4 && testAnalyzed == 4 && testUnchanged == 0,
          "Test Mode fully reprocessed despite the existing index");
    const ProductionSnapshot after = snapshotProduction(appDir, root);
    check(after.valid, "production index snapshot readable after Test Mode");
    check(after.database == before.database, "Test Mode left production index.sqlite unchanged");
    check(after.videoCache == before.videoCache, "Test Mode left production video_cache.sqlite unchanged");
    check(after.metadata == before.metadata, "Test Mode left production metadata.json unchanged");

    const QString scratchRoot = QDir(appDir).filePath(QStringLiteral("TestMode"));
    QDir scratch(scratchRoot);
    check(scratch.exists(), "Test Mode created an isolated scratch directory");
    bool scratchIndexFound = false;
    if (scratch.exists()) {
        QDirIterator it(scratchRoot, QStringList() << QStringLiteral("index.sqlite"), QDir::Files,
                        QDirIterator::Subdirectories);
        scratchIndexFound = it.hasNext();
    }
    check(scratchIndexFound, "Test Mode wrote its index inside scratch, not production");

    fs::remove_all(work, ec);
    std::cout << "test_mode_selfcheck=" << (gOk ? "ok" : "FAILED") << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
