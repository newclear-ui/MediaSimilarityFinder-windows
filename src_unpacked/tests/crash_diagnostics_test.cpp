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
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <filesystem>
#include <iostream>

namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << "\n"; }
    else       { std::cout << "  [ok] " << what << "\n"; }
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

    fs::remove_all(d, ec);
    std::cout << "crash_diagnostics_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
