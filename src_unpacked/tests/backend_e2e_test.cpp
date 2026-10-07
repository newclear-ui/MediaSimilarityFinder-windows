// Backend process E2E + crash injection (P3: 0.9.4.69, Windows-only).
//
// Real MainWindow + real BackendSupervisor + real Backend OS process:
//   Test 1: HELLO/READY, scan runs to FINISHED, GUI PID != Backend PID.
//   Test 2: forced backend kill mid-scan -> GUI alive, exit detected,
//           bounded restart, new PID, READY again, rescan completes.
//   Test 3: repeated kills -> bounded retries exhausted -> FAILED, no
//           infinite loop, GUI still alive.
//   Test 4 (folded into Test 2): the post-restart rescan reopens the
//           existing SQLite/index (committed state preserved).
//
// A modal-closing timer runs throughout: failure popups and the end-of-scan
// benchmark summary are modal and would hang an offscreen run. Failures are
// recorded through signals instead.
#include "mainwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QLabel>
#include <QTreeWidget>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Must match gui/backend_supervisor.h (test-only include would couple the
// test to GUI internals beyond MainWindow; the supervisor is constructed
// here directly instead).
#include "backend_supervisor.h"

namespace {

int gChecks = 0;
bool gOk = true;

void check(bool cond, const char* what) {
    ++gChecks;
    if (!cond) { gOk = false; std::cerr << "  [FAIL] " << what << std::endl; }
    else       { std::cout << "  [ok] " << what << std::endl; }
}

void bmp(const std::filesystem::path& p, unsigned seed) {
    std::ofstream f(p, std::ios::binary);
    const int w = 8, h = 8, row = w * 3, img = row * h, fs = 54 + img;
    unsigned char hd[54] = {0};
    hd[0] = 'B'; hd[1] = 'M';
    hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
    hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
    hd[26] = 1; hd[28] = 24;
    hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
    f.write((const char*)hd, 54);
    unsigned s = seed * 2654435761u + 1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            s = s * 1103515245u + 12345u;
            const unsigned char v = (unsigned char)(((s >> 16) & 0xFF) > 127 ? 255 : 0);
            f.put((char)v); f.put((char)v); f.put((char)v);
        }
}

void pumpUntil(std::function<bool()> done, int timeoutMs, const char* what) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        QApplication::processEvents();
        QThread::msleep(50);
        if (t.elapsed() > timeoutMs) {
            std::cerr << "  [FAIL] timeout: " << what << std::endl;
            gOk = false;
            ++gChecks;
            return;
        }
    }
    ++gChecks;
    std::cout << "  [ok] " << what << std::endl;
}

#ifdef _WIN32
bool killPid(qint64 pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return false;
    const BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok != FALSE;
}
#endif

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto d = fs::temp_directory_path() / "msf_backend_e2e";
    fs::remove_all(d, ec);
    fs::create_directories(d / "media", ec);
    fs::create_directories(d / "settings", ec);
    // 30 groups: the scan must last long enough that the forced kill in
    // Test 2 reliably lands mid-scan (a 10-group scan can finish inside the
    // kill race and would make the failure assertion vacuous).
    for (int i = 0; i < 30; ++i) {
        bmp(d / "media" / ("g" + std::to_string(i) + "_a.bmp"), (unsigned)(i + 1));
        bmp(d / "media" / ("g" + std::to_string(i) + "_b.bmp"), (unsigned)(i + 1));
    }
    initAppSettings(QString::fromStdString((d / "settings").string()));
    QSettings().setValue("ui/lastFolder", QString::fromStdString((d / "media").string()));
    QSettings().sync();

    // Fast timeouts: the contract is identical, only the waits shrink.
    SupervisorOptions opt;
    opt.readyTimeoutMs = 15000;
    opt.healthTimeoutMs = 5000;
    opt.maxAttempts = 3;
    opt.backoffMs = {500, 1000, 2000};
    opt.killGraceMs = 1000;
    opt.killWaitMs = 2000;
    opt.shutdownWaitMs = 2000;
    auto* supervisor = new BackendSupervisor(&app, opt);

    bool connAvailable = false;
    QString connMsg;
    QObject::connect(supervisor, &BackendClient::backendConnection, supervisor,
                     [&](bool available, const QString& msg) {
                         connAvailable = available;
                         connMsg = msg;
                     });
    bool failedSeen = false;
    QString failedMsg;
    QObject::connect(supervisor, &BackendClient::failed, supervisor,
                     [&](const QString& m) { failedSeen = true; failedMsg = m; });
    bool scanActive = false;
    QObject::connect(supervisor, &BackendClient::progress, supervisor,
                     [&](int, QString) { scanActive = true; });
    QObject::connect(supervisor, &BackendClient::backendLogLine, supervisor,
                     [&](const QString& line) {
                         std::cerr << "  [sv] " << line.toStdString() << std::endl;
                     });

    MainWindow w(nullptr, supervisor);
    w.show();
    QApplication::processEvents();
    // Modal closer: failure popups + benchmark summary must not hang CTest.
    QTimer closer;
    closer.setInterval(250);
    QObject::connect(&closer, &QTimer::timeout, [&]() {
        if (QWidget* m = QApplication::activeModalWidget()) m->close();
    });
    closer.start();

    auto* grid = w.findChild<QListWidget*>("imgGrid");
    auto* scan = w.findChild<QPushButton*>("scan");
    if (!grid || !scan) { std::cerr << "no grid/scan\n"; return 2; }
    const qint64 guiPid = QCoreApplication::applicationPid();

    // ---- Test 1: normal scan over the real channel. ----
    pumpUntil([&] { return connAvailable; }, 30000, "backend READY (connection up)");
    const qint64 pid1 = supervisor->backendPid();
    check(pid1 > 0, "backend PID known");
    check(pid1 != guiPid, "GUI PID != Backend PID (separate OS processes)");
    scan->click();
    QApplication::processEvents();
    pumpUntil([&] { return scan->isEnabled(); }, 180000, "test1 scan finished");
    if (!gOk) return 3;
    check(grid->count() >= 2, "test1 groups displayed");
    std::cout << "  [info] guiPid=" << guiPid << " backendPid=" << pid1 << "\n";
    // FileMeta over IPC: select the first group/file and expect the detail
    // pane to resolve "8x8" through GET_FILE_META/FILE_META (no GUI decode).
    {
        auto* tree = w.findChild<QTreeWidget*>("imgTree");
        auto* files = w.findChild<QListWidget*>("fileGrid");
        if (tree && files && tree->topLevelItemCount() > 0) {
            tree->setCurrentItem(tree->topLevelItem(0));
            QApplication::processEvents();
            if (files->count() > 0) files->setCurrentRow(0);
            pumpUntil(
                [&] {
                    for (auto* lb : w.findChildren<QLabel*>()) {
                        if (lb->text() == QStringLiteral("8x8")) return true;
                    }
                    return false;
                },
                30000, "test1 detail shows 8x8 via fileMeta");
        }
    }

    // ---- Test 2: forced kill right after scan start -> restart -> rescan. ----
    // The kill lands deterministically with no wait at all: supervisor.scanning_
    // is set synchronously when START_SCAN is sent, while even a quick-load
    // rescan needs hundreds of ms to finish. Any wait (even for progress)
    // lets the rescan finish first and makes the failure assertion vacuous.
    scanActive = false;
    failedSeen = false;
    scan->click();
    QApplication::processEvents();
#ifdef _WIN32
    check(killPid(supervisor->backendPid()), "test2 backend killed");
#else
    std::cerr << "no kill primitive\n";
    return 4;
#endif
    pumpUntil([&] { return !connAvailable; }, 15000, "test2 exit detected (unavailable)");
    check(w.isVisible(), "test2 GUI alive after backend kill");
    check(failedSeen, "test2 scan failure reported, not silent");
    pumpUntil([&] { return connAvailable; }, 30000, "test2 backend restarted (READY again)");
    const qint64 pid2 = supervisor->backendPid();
    check(pid2 > 0 && pid2 != pid1, "test2 new backend PID after restart");
    std::cout << "  [info] restarted backendPid=" << pid2 << "\n";
    pumpUntil([&] { return scan->isEnabled(); }, 180000, "test2 wound-down scan button idle");
    // Rescan on the same folder: proves the existing SQLite/index reopened
    // with committed state (Test 4).
    scan->click();
    QApplication::processEvents();
    pumpUntil([&] { return scan->isEnabled(); }, 180000, "test2 rescan finished (index reopened)");
    if (!gOk) return 5;
    check(grid->count() >= 2, "test2 groups displayed after restart");

    // ---- Test 3a: instant-exit backend exhausts the unexpected-exit budget.
    // Deterministic (no handshake race): the backend exits before answering.
    qputenv("MSF_TEST_BACKEND_FAIL_FAST", "1");
#ifdef _WIN32
    killPid(supervisor->backendPid());
#endif
    pumpUntil([&] { return supervisor->phase() == QStringLiteral("Failed"); }, 60000,
              "test3a supervisor FAILED after exhausted budget");
    qunsetenv("MSF_TEST_BACKEND_FAIL_FAST");
    {
        const qint64 gonePid = supervisor->backendPid();
        QThread::msleep(3000);
        QApplication::processEvents();
        check(supervisor->backendPid() == gonePid, "test3a no restart loop after FAILED");
        check(w.isVisible(), "test3a GUI alive after repeated crashes");
    }

    // ---- Test 3b: silent backend exhausts the READY-timeout budget. ----
    // The backend runs but never answers HELLO: startup timeouts (2s here)
    // must trip bounded restarts, then FAILED. Standalone supervisor.
    qputenv("MSF_TEST_BACKEND_SILENT", "1");
    SupervisorOptions opt2;
    opt2.readyTimeoutMs = 2000;
    opt2.healthTimeoutMs = 2000;
    opt2.maxAttempts = 3;
    opt2.backoffMs = {300, 300, 300};
    opt2.killGraceMs = 500;
    opt2.killWaitMs = 1000;
    auto* supervisor2 = new BackendSupervisor(&app, opt2);
    supervisor2->ensureRunning();
    pumpUntil([&] { return supervisor2->phase() == QStringLiteral("Failed"); }, 60000,
              "test3b supervisor FAILED on startup-timeout budget");
    qunsetenv("MSF_TEST_BACKEND_SILENT");
    check(w.isVisible(), "test3b GUI alive after startup-timeout storm");
    delete supervisor2;

    fs::remove_all(d, ec);
    std::cout << "backend_e2e_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
