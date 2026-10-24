// Crash-diagnostics regression (0.9.4.62, handler hardening in 0.9.4.65).
//
// The two consecutive 0xC0000409 fail-fast crashes left no product record:
// ScanWorker::run caught only std::exception, Qt messages went nowhere, and
// the heartbeat could not distinguish a slow walk from a hang. This covers:
//   1. installQtMessageLog() records Qt warnings to a file (deterministic,
//      own temp path; restores the default handler afterwards).
//   2. A non-standard exception in the scan worker takes the catch-all path:
//      partial results are checkpointed and failed() is emitted instead of
//      terminating the process (driven by the MSF_TEST_THROW_NONSTD seam;
//      production never sets it). SEH faults still crash (not caught here),
//      which is why this test can assert survival at all.
//   3. (0.9.4.65) A throw inside the failure handler itself still reports
//      failed() instead of terminating: the 2026-10-07 dump proved the fault
//      stack sits in the handler's persist path (abort out of run(), a Qt
//      slot). Driven by MSF_TEST_THROW_PERSIST on top of the NONSTD seam;
//      production never sets either.
#include "mainwindow.h"
#include "scan_worker.h"
#include "index_manager.h" // 0.9.4.153: cancel/exception baseline assertions
#include "database.h"      // 0.9.4.153: cancel/exception baseline assertions
#include "path_utils.h"    // 0.9.4.153: cancel/exception baseline assertions
#include "analyze_mode.h"  // 0.9.4.153: Live mode for the forceFullPass proof
#include <QCoreApplication>
#include <QElapsedTimer> // 0.9.4.153: cancel-after-change poll
#include <QStandardPaths> // 0.9.4.153: forceFullPass log tail
#include <QFile>
#include <QTextStream>
#include <filesystem>
#include <fstream>   // 0.9.4.153: cancel fixture mtime touch
#include <QFileInfo> // 0.9.4.153: backend log tail offset
#include <iostream>
#include <thread>  // 0.9.4.153: cancel-after-change wind-down
#include <mutex>   // 0.9.4.153: cancel-after-change message capture
#include <atomic>  // 0.9.4.153: cancel-after-change flags
#include <chrono>  // 0.9.4.153: cancel-after-change poll

namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << "\n"; }
    else       { std::cout << "  [ok] " << what << "\n"; }
}

// 0.9.4.153: minimal valid 24-bit BMP writer for the cancel/exception
// baseline fixtures.
bool writeBmp(const std::filesystem::path& p, unsigned seed) {
    QFile f(QString::fromStdString(msf::path_to_utf8(p)));
    if (!f.open(QIODevice::WriteOnly)) return false;
    const int w = 8, h = 8, row = w * 3, img = row * h, fs = 54 + img;
    unsigned char hd[54] = {0};
    hd[0] = 'B'; hd[1] = 'M';
    hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
    hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
    hd[26] = 1; hd[28] = 24;
    hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
    if (f.write((const char*)hd, 54) != 54) return false;
    unsigned s = seed * 2654435761u + 1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            s = s * 1103515245u + 12345u;
            const char v = (char)((((s >> 16) & 0xFF) > 127) ? 255 : 0);
            if (f.write(&v, 1) != 1 || f.write(&v, 1) != 1 || f.write(&v, 1) != 1) return false;
        }
    return true;
}

// 0.9.4.153: resolve the index DB file for an (appDir, root) pair.
QString indexDbFile(const std::filesystem::path& appDir, const std::filesystem::path& root) {
    msf::IndexPaths ipx;
    if (!msf::IndexManager::resolve(msf::path_from_utf8(appDir.string()),
                                    msf::path_from_utf8(root.string()), ipx))
        return QString();
    return QString::fromStdString(msf::path_to_utf8(ipx.database));
}

// 0.9.4.153: read the completeness baseline stamp (empty = force full pass).
QString dbBaseline(const QString& dbFile) {
    msf::Database b;
    if (!b.open(dbFile.toStdString()) || !b.initialize()) return QStringLiteral("!open");
    const std::string v = b.matchBaseline();
    b.close();
    return QString::fromStdString(v);
}

bool dbSetBaseline(const QString& dbFile, const char* v) {
    msf::Database b;
    if (!b.open(dbFile.toStdString()) || !b.initialize()) return false;
    const bool ok = b.setMatchBaseline(v);
    b.close();
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto d = fs::temp_directory_path() / "msf_crash_diag_test";
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);

    // ---- 1. Qt message file sink. ----
    const QString logPath = QString::fromStdString((d / "qt.log").string());
    installQtMessageLog(logPath);
    const char* marker = "crash-diag-probe-0.9.4.62";
    qWarning("%s", marker);
    qInstallMessageHandler(nullptr); // restore default
    {
        QFile f(logPath);
        check(f.open(QIODevice::ReadOnly), "message log file created");
        const QString body = QString::fromUtf8(f.readAll());
        check(body.contains(marker), "warning text recorded in the log");
        check(body.contains("[WARNING]"), "severity tag recorded in the log");
    }

    // ---- 2. Non-standard exception becomes failed(), not terminate. ----
    {
        qputenv("MSF_TEST_THROW_NONSTD", "1");
        ScanWorker w(QString::fromStdString((d / "media").string()),
                     QString::fromStdString(d.string()),
                     8, 50, 50, false);
        bool failedSeen = false;
        QString failedMsg;
        QObject::connect(&w, &ScanWorker::failed, &w,
                         [&](const QString& m) { failedSeen = true; failedMsg = m; },
                         Qt::DirectConnection);
        fs::create_directories(d / "media", ec);
        w.run(); // would terminate the process without the catch-all
        check(failedSeen, "failed() emitted for a non-standard exception");
        check(failedMsg.contains("non-standard"), "failure reason names the cause");
        qunsetenv("MSF_TEST_THROW_NONSTD");
    }

    // ---- 3. Throw inside the handler still reports failed(). ----
    {
        qputenv("MSF_TEST_THROW_NONSTD", "1");
        qputenv("MSF_TEST_THROW_PERSIST", "1");
        ScanWorker w(QString::fromStdString((d / "media").string()),
                     QString::fromStdString(d.string()),
                     8, 50, 50, false);
        bool failedSeen = false;
        QString failedMsg;
        QObject::connect(&w, &ScanWorker::failed, &w,
                         [&](const QString& m) { failedSeen = true; failedMsg = m; },
                         Qt::DirectConnection);
        w.run(); // would terminate the process without the 0.9.4.65 guard:
                 // persistMatchesSnapshot throws inside the catch-all handler
        check(failedSeen, "failed() emitted when the handler's persist throws");
        check(failedMsg.contains("non-standard"), "original failure reason kept");
        qunsetenv("MSF_TEST_THROW_NONSTD");
        qunsetenv("MSF_TEST_THROW_PERSIST");
    }

    // ---- 4. (0.9.4.151) A failed match persist fails loudly. ----
    // persistMatchesSnapshot() used to ignore saveMatches()'s return, so a
    // failed save still reported success while the DB kept the old set.
    // MSF_TEST_FAIL_PERSIST injects the failure (production never sets it):
    // the worker must emit failed(), never results()+finished().
    // 0.9.4.153: the final-persist failure also invalidates the completeness
    // baseline (file rows are committed with new fingerprints while pairs
    // keep the old rows), so the next A/AB scan full-verifies.
    {
        qputenv("MSF_TEST_FAIL_PERSIST", "1");
        ScanWorker w(QString::fromStdString((d / "media").string()),
                     QString::fromStdString(d.string()),
                     8, 50, 50, false);
        bool failedSeen = false;
        QString failedMsg;
        bool finishedSeen = false;
        QObject::connect(&w, &ScanWorker::failed, &w,
                         [&](const QString& m) { failedSeen = true; failedMsg = m; },
                         Qt::DirectConnection);
        QObject::connect(&w, &ScanWorker::finished, &w,
                         [&](const QString&) { finishedSeen = true; },
                         Qt::DirectConnection);
        fs::create_directories(d / "media", ec);
        const QString db4 = indexDbFile(d, d / "media");
        check(!db4.isEmpty(), "persist-fail index resolved");
        check(dbSetBaseline(db4, "1.5.1"), "persist-fail baseline pre-stamped");
        w.run();
        check(failedSeen, "failed() emitted when the final match persist fails");
        check(failedMsg.contains("match index save failed"), "failure reason names the cause");
        check(!finishedSeen, "no success finish after a failed persist");
        check(dbBaseline(db4).isEmpty(), "persist failure invalidated the baseline");
        qunsetenv("MSF_TEST_FAIL_PERSIST");
    }

    // ---- 5. (0.9.4.153 P1-B) Cancel after re-analysis approval invalidates
    // the baseline but stays a cancel (never a failure). File rows are
    // committed with new fingerprints while the pruned pair set is stored,
    // so the pre-scan baseline would wrongly vouch for an incomplete set.
    {
        const auto mdir = d / "cancel_media";
        fs::create_directories(mdir, ec);
        bool fixturesOk = true;
        for (int i = 0; i < 6; ++i) {
            const std::string a = "c" + std::to_string(i) + "_a.bmp";
            const std::string b = "c" + std::to_string(i) + "_b.bmp";
            fixturesOk &= writeBmp(mdir / a, 100 + (unsigned)i);
            fixturesOk &= writeBmp(mdir / b, 100 + (unsigned)i);
        }
        check(fixturesOk, "cancel fixtures written");
        const QString mroot = QString::fromStdString(mdir.string());
        const QString mapp = QString::fromStdString(d.string());
        // Baseline run: full scan completes and stamps the baseline.
        // Live (A) mode: the post-cancel proof needs the targeted path, where
        // only a forced full pass recomputes unchanged files.
        {
            ScanWorker w(mroot, mapp, 8, 50, 50, false);
            w.setAnalyzeMode(msf::AnalyzeMode::Live);
            bool fin = false;
            QObject::connect(&w, &ScanWorker::finished, &w,
                             [&](const QString&) { fin = true; }, Qt::DirectConnection);
            w.run();
            check(fin, "cancel baseline scan finished");
        }
        const QString dbC = indexDbFile(d, mdir);
        check(!dbC.isEmpty(), "cancel index resolved");
        check(dbBaseline(dbC) == QStringLiteral("1.5.1"), "cancel baseline stamped");
        // Change one member, then cancel right after the change is approved
        // (deterministic: poll the worker instead of racing a sleep).
        {
            std::ofstream f(mdir / "c0_a.bmp", std::ios::binary | std::ios::trunc);
            f.close();
            if (!writeBmp(mdir / "c0_a.bmp", 777)) check(false, "cancel change write");
            fs::last_write_time(mdir / "c0_a.bmp", fs::file_time_type::clock::now(), ec);
        }
        std::atomic<bool> cancelled{false};
        std::atomic<bool> failedSeen{false};
        QString cancelMsg;
        std::mutex cancelMu;
        {
            ScanWorker w(mroot, mapp, 8, 50, 50, false);
            w.setAnalyzeMode(msf::AnalyzeMode::Live);
            QObject::connect(&w, &ScanWorker::finished, &w,
                             [&](const QString& m) {
                                 std::lock_guard<std::mutex> g(cancelMu);
                                 cancelled = true; cancelMsg = m;
                             }, Qt::DirectConnection);
            QObject::connect(&w, &ScanWorker::failed, &w,
                             [&](const QString&) { failedSeen = true; }, Qt::DirectConnection);
            std::thread t([&w] { w.run(); });
            bool sawChange = false;
            QElapsedTimer et; et.start();
            while (et.elapsed() < 30000 && !cancelled.load()) {
                if (w.testChangedCount() > 0) { sawChange = true; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            check(sawChange, "cancel change approved before cancel");
            w.cancel();
            t.join();
            std::lock_guard<std::mutex> g(cancelMu);
            check(cancelled && cancelMsg.startsWith("CANCELLED"), "cancel reported, not failure");
            check(!failedSeen, "cancel not misclassified as failure");
        }
        check(dbBaseline(dbC).isEmpty(), "cancel invalidated the baseline");
        // Next production scan must full-verify (observe the engine log),
        // restamp the baseline, drop the changed pair and keep the rest.
        const QString logPath =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/msf_scan.log");
        const qint64 logBefore = QFileInfo(logPath).size();
        {
            ScanWorker w(mroot, mapp, 8, 50, 50, false);
            w.setAnalyzeMode(msf::AnalyzeMode::Live); // forceFullPass only gates A/AB
            bool fin = false;
            QObject::connect(&w, &ScanWorker::finished, &w,
                             [&](const QString&) { fin = true; }, Qt::DirectConnection);
            w.run();
            check(fin, "post-cancel scan finished");
        }
        bool fullPass = false;
        {
            QFile lf(logPath);
            if (lf.open(QIODevice::ReadOnly)) {
                if (logBefore > 0) lf.seek(logBefore);
                fullPass = QString::fromUtf8(lf.readAll()).contains(QStringLiteral("matchBaseline=forceFullPass"));
            }
        }
        check(fullPass, "post-cancel scan forced full verification");
        check(dbBaseline(dbC) == QStringLiteral("1.5.1"), "post-cancel baseline restamped");
        {
            msf::Database b;
            check(b.open(dbC.toStdString()) && b.initialize(), "post-cancel index open");
            int c0 = 0, c1 = 0;
            for (const auto& m : b.loadMatches()) {
                const bool c0m = (m.left.find("c0_") != std::string::npos ||
                                  m.right.find("c0_") != std::string::npos);
                const bool c1m = (m.left.find("c1_a") != std::string::npos ||
                                  m.right.find("c1_a") != std::string::npos);
                if (c0m) ++c0; if (c1m) ++c1;
            }
            b.close();
            check(c0 == 0, "post-cancel changed pair dropped");
            check(c1 >= 1, "post-cancel untouched pair kept");
        }
    }

    // ---- 6. (0.9.4.153 P1-B) std::exception after changes invalidates the
    // baseline. MSF_TEST_THROW_PERSIST makes the final persist throw (the
    // std::exception path with a non-empty changed set); production never
    // sets it. Short fixture scan: only the final persist fires.
    {
        const auto mdir = d / "throw_media";
        fs::create_directories(mdir, ec);
        bool fixturesOk = writeBmp(mdir / "t_a.bmp", 31) && writeBmp(mdir / "t_b.bmp", 31);
        check(fixturesOk, "throw fixtures written");
        const QString mroot = QString::fromStdString(mdir.string());
        const QString mapp = QString::fromStdString(d.string());
        {
            ScanWorker w(mroot, mapp, 8, 50, 50, false);
            bool fin = false;
            QObject::connect(&w, &ScanWorker::finished, &w,
                             [&](const QString&) { fin = true; }, Qt::DirectConnection);
            w.run();
            check(fin, "throw baseline scan finished");
        }
        const QString dbT = indexDbFile(d, mdir);
        check(!dbT.isEmpty(), "throw index resolved");
        check(dbBaseline(dbT) == QStringLiteral("1.5.1"), "throw baseline stamped");
        // Change the pair, then fail the final persist with a throw.
        if (!writeBmp(mdir / "t_a.bmp", 32)) check(false, "throw change write");
        fs::last_write_time(mdir / "t_a.bmp", fs::file_time_type::clock::now(), ec);
        qputenv("MSF_TEST_THROW_PERSIST", "1");
        bool failedSeen = false;
        QString failedMsg;
        bool finishedSeen = false;
        {
            ScanWorker w(mroot, mapp, 8, 50, 50, false);
            QObject::connect(&w, &ScanWorker::failed, &w,
                             [&](const QString& m) { failedSeen = true; failedMsg = m; },
                             Qt::DirectConnection);
            QObject::connect(&w, &ScanWorker::finished, &w,
                             [&](const QString&) { finishedSeen = true; },
                             Qt::DirectConnection);
            w.run();
        }
        qunsetenv("MSF_TEST_THROW_PERSIST");
        check(failedSeen, "failed() emitted for the std::exception path");
        check(failedMsg.contains("MSF_TEST_THROW_PERSIST"), "throw reason kept");
        check(!finishedSeen, "no success finish on the exception path");
        check(dbBaseline(dbT).isEmpty(), "exception invalidated the baseline");
    }

    // ---- 7. (0.9.4.154 D1) A surviving scan_dirty marker forces one full
    // verification pass. The marker stands in for a forced kill (process
    // death skips every settled-exit cleanup, leaving exactly this DB
    // state): baseline valid, marker set, one file changed.
    {
        const auto mdir = d / "dirty_media";
        fs::create_directories(mdir, ec);
        bool fixturesOk = writeBmp(mdir / "k_a.bmp", 41) && writeBmp(mdir / "k_b.bmp", 41) &&
                           writeBmp(mdir / "l_a.bmp", 42) && writeBmp(mdir / "l_b.bmp", 42);
        check(fixturesOk, "dirty fixtures written");
        const QString mroot = QString::fromStdString(mdir.string());
        const QString mapp = QString::fromStdString(d.string());
        // Baseline run: completes, stamps the baseline, leaves no marker.
        {
            ScanWorker w(mroot, mapp, 8, 50, 50, false);
            w.setAnalyzeMode(msf::AnalyzeMode::Live);
            bool fin = false;
            QObject::connect(&w, &ScanWorker::finished, &w,
                             [&](const QString&) { fin = true; }, Qt::DirectConnection);
            w.run();
            check(fin, "dirty baseline scan finished");
        }
        const QString dbD = indexDbFile(d, mdir);
        check(!dbD.isEmpty(), "dirty index resolved");
        check(dbBaseline(dbD) == QStringLiteral("1.5.1"), "dirty baseline stamped");
        {
            msf::Database b;
            check(b.open(dbD.toStdString()) && b.initialize(), "dirty index open");
            check(!b.scanDirty(), "no marker after clean scan");
            b.close();
        }
        // Simulate the post-kill DB state: baseline intact, marker set.
        {
            msf::Database b;
            check(b.open(dbD.toStdString()) && b.initialize(), "dirty index reopen");
            check(b.setScanDirty(true), "crash marker planted");
            b.close();
        }
        if (!writeBmp(mdir / "k_a.bmp", 43)) check(false, "dirty change write");
        fs::last_write_time(mdir / "k_a.bmp", fs::file_time_type::clock::now(), ec);
        const QString logPath =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/msf_scan.log");
        const qint64 logBefore = QFileInfo(logPath).size();
        // The next A/AB scan must full-verify despite the valid baseline.
        {
            ScanWorker w(mroot, mapp, 8, 50, 50, false);
            w.setAnalyzeMode(msf::AnalyzeMode::Live);
            bool fin = false;
            QObject::connect(&w, &ScanWorker::finished, &w,
                             [&](const QString&) { fin = true; }, Qt::DirectConnection);
            w.run();
            check(fin, "post-crash scan finished");
        }
        bool fullPass = false;
        {
            QFile lf(logPath);
            if (lf.open(QIODevice::ReadOnly)) {
                if (logBefore > 0) lf.seek(logBefore);
                const QString tail = QString::fromUtf8(lf.readAll());
                fullPass = tail.contains(QStringLiteral("matchBaseline=forceFullPass")) &&
                           tail.contains(QStringLiteral("dirty=1"));
            }
        }
        check(fullPass, "crash marker forced full verification");
        check(dbBaseline(dbD) == QStringLiteral("1.5.1"), "baseline restamped after crash recovery");
        {
            msf::Database b;
            check(b.open(dbD.toStdString()) && b.initialize(), "dirty index reopened");
            check(!b.scanDirty(), "marker cleared after clean scan");
            b.close();
            int kPairs = 0, lPairs = 0;
            msf::Database b2;
            if (b2.open(dbD.toStdString()) && b2.initialize()) {
                for (const auto& m : b2.loadMatches()) {
                    const std::string lf = std::filesystem::path(msf::path_from_utf8(m.left)).filename().string();
                    const bool touched = lf == "k_a.bmp" || lf == "k_b.bmp";
                    if (touched) ++kPairs;
                    else {
                        const std::string rf = std::filesystem::path(msf::path_from_utf8(m.right)).filename().string();
                        if (rf == "k_a.bmp" || rf == "k_b.bmp") ++kPairs;
                        else ++lPairs;
                    }
                }
                b2.close();
            }
            check(kPairs == 0, "crashed-then-changed pair dropped");
            check(lPairs >= 1, "untouched pair kept after crash recovery");
        }
    }

    fs::remove_all(d, ec);
    std::cout << "crash_diagnostics_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
