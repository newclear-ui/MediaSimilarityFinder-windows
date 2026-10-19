// Backend process E2E + crash injection (P3: 0.9.4.69, Windows-only).
//
// Real MainWindow + real BackendSupervisor + real Backend OS process:
//   Test 1: HELLO/READY, scan runs to FINISHED, GUI PID != Backend PID.
//   Test 2: forced backend kill mid-scan -> GUI alive, exit detected,
//           bounded restart, new PID, READY again, scan auto-resumes once.
//   Test 3: repeated kills -> bounded retries exhausted -> FAILED, no
//           infinite loop, GUI still alive.
//   Test 4 (folded into Test 2): the post-restart rescan reopens the
//           existing SQLite/index (committed state preserved).
//   Test 4 (0.9.4.111, G4): a 3 s consumer stall mid-scan (no event pumping,
//           the pipe fills, the backend blocks in flush) must lose nothing:
//           no event gaps, every group delivered, no deadlock, bounded
//           backend memory.
//   Test 5 (0.9.4.111, G6): graceful shutdown is bounded AND prompt — the
//           old state() polling could not observe the exit without an event
//           loop and burned the full shutdownWaitMs on every app exit.
//   Test 6 (0.9.4.111, G6): parent (GUI) death must not orphan the backend
//           (Job Object KILL_ON_JOB_CLOSE), proven against a WEDGED backend
//           whose stdin-EOF self-exit cannot run; a --no-job control proves
//           the job is load-bearing.
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
#include <psapi.h>
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

bool pidAlive(qint64 pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

quint64 rssMB(qint64 pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return 0;
    PROCESS_MEMORY_COUNTERS pmc{};
    const bool ok = K32GetProcessMemoryInfo(h, &pmc, sizeof(pmc));
    CloseHandle(h);
    return ok ? (quint64)(pmc.WorkingSetSize / (1024 * 1024)) : 0;
}
#else
bool pidAlive(qint64) { return false; }
quint64 rssMB(qint64) { return 0; }
#endif

qint64 waitPidFile(const std::filesystem::path& f, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        QApplication::processEvents();
        std::ifstream in(f);
        qint64 pid = -1;
        if (in >> pid) return pid;
        QThread::msleep(50);
    }
    return -1;
}

// 0.9.4.111 (G6): helper process for the parent-death E2E. It spawns a WEDGED
// backend (never enters its event loop, so SHUTDOWN and the stdin-EOF
// self-exit are both blocked), reports the backend PID, and then spins until
// the parent kills it with TerminateProcess — no destructors, no shutdown:
// a GUI crash. MSF_TEST_BACKEND_WEDGE is inherited by the spawned backend.
int runGuiHelper(int argc, char** argv, const QStringList& raw) {
    QCoreApplication app(argc, argv);
    const int i = raw.indexOf(QStringLiteral("--gui-helper"));
    const QString pidFile = (i >= 0 && i + 1 < raw.size()) ? raw[i + 1] : QString();
    if (pidFile.isEmpty()) return 2;
    const bool noJob = raw.contains(QStringLiteral("--no-job"));
    qputenv("MSF_TEST_BACKEND_WEDGE", "1");
    SupervisorOptions opt;
    opt.readyTimeoutMs = 60000; // the parent kills us long before escalation
    opt.jobObjectEnabled = !noJob;
    BackendSupervisor sup(nullptr, opt);
    sup.ensureRunning();
    qint64 pid = -1;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 20000) {
        QCoreApplication::processEvents();
        pid = sup.backendPid();
        if (pid > 0) break;
        QThread::msleep(50);
    }
    if (pid <= 0) return 3;
    {
        std::ofstream f(pidFile.toStdString(), std::ios::trunc);
        f << pid;
    }
    std::cout << "helper backend pid=" << pid << std::endl;
    return app.exec(); // spin; the parent will TerminateProcess this process
}

} // namespace

int main(int argc, char** argv) {
    QStringList raw;
    for (int i = 1; i < argc; ++i) raw << QString::fromLocal8Bit(argv[i]);
    if (raw.contains(QStringLiteral("--gui-helper"))) return runGuiHelper(argc, argv, raw);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // 0.9.4.115 (D1): the load seam must exist BEFORE the first backend spawn
    // (QProcess children inherit the environment at spawn time only). Every
    // Load in this suite runs the slow load; only Test 1b performs one.
    qputenv("MSF_TEST_SLOW_LOAD_MS", "12000");
    QApplication app(argc, argv);
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto d = fs::temp_directory_path() / "msf_backend_e2e";
    fs::remove_all(d, ec);
    fs::create_directories(d / "media", ec);
    fs::create_directories(d / "settings", ec);
    // 300 groups (0.9.4.111): the scan must outlive the 3 s consumer stall in
    // Test 4 with enough match/progress bytes to fill the pipe, and must last
    // long enough that the forced kill in Test 2 reliably lands mid-scan
    // (a 10-group scan can finish inside the kill race and would make the
    // failure assertion vacuous).
    for (int i = 0; i < 300; ++i) {
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
    int finishedCount = 0;
    int gapLines = 0;     // 0.9.4.111 (G4): sequence gaps = dropped messages
    int batchEntries = 0; // 0.9.4.111 (G4): match entries delivered
    int backendExits = 0; // 0.9.4.115 (D1): unexpected backend exits
    QObject::connect(supervisor, &BackendClient::finished, supervisor,
                     [&](const QString&) { ++finishedCount; });
    QObject::connect(supervisor, &BackendClient::matchesBatch, supervisor,
                     [&](const QVector<BackendMatch>& b) { batchEntries += b.size(); });
    QObject::connect(supervisor, &BackendClient::progress, supervisor,
                     [&](int, QString) { scanActive = true; });
    QObject::connect(supervisor, &BackendClient::backendLogLine, supervisor,
                     [&](const QString& line) {
                         if (line.contains(QStringLiteral("event gap"))) ++gapLines;
                         if (line.contains(QStringLiteral("unexpected exit"))) ++backendExits;
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
    // 0.9.4.113 (B2): the settings value must reach the engine AND the record
    // the GUI receives. Fast(A) here; the telemetry's analyze.mode is the
    // same field the detailed-log summary displays.
    QSettings().setValue("ui/analyzeMode", 1);
    QSettings().sync();
    scan->click();
    QApplication::processEvents();
    pumpUntil([&] { return scan->isEnabled(); }, 180000, "test1 scan finished");
    if (!gOk) return 3;
    pumpUntil([&] { return w.testLastTelemetryJson().contains(QStringLiteral("\"mode\":\"A\"")); },
              15000, "test1 telemetry records Fast(A) from the settings value");
    check(grid->count() >= 2, "test1 groups displayed");
    check(finishedCount == 1, "test1 finished event observed");
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

    // ---- Test 1b (0.9.4.115, D1): Load must not block the backend's IPC
    // event thread. With the test seam the load outlives the health window;
    // the pre-fix synchronous load silenced HEALTH, tripped the health
    // timeout, and the backend was killed mid-load (field: exit code 0xf291 =
    // 62097) on real datasets. ----
    {
        auto* loadBtn = w.findChild<QPushButton*>("loadBtn");
        check(loadBtn != nullptr, "test1b load button found");
        bool loadedSeen = false;
        // Wait for the RESULTS answer (not quickLoaded): the GUI keeps
        // loading_ set until results arrives, and a scan click while loading_
        // is ignored — the later scan phases depend on the state being clear.
        QObject::connect(supervisor, &BackendClient::results, supervisor,
                         [&](const QVector<BackendFile>&, const QStringList&) { loadedSeen = true; });
        const int exitsBefore = backendExits;
        loadBtn->click();
        QApplication::processEvents();
        pumpUntil([&] { return loadedSeen; }, 60000,
                  "test1b slow load completed while the backend stayed alive");
        qunsetenv("MSF_TEST_SLOW_LOAD_MS");
        check(backendExits == exitsBefore, "test1b no backend restart during the load");
        // 0.9.4.132: the left summary must describe the loaded set (the field
        // report: every row stayed "-" after Load). The suite dataset holds
        // 300 pairs (600 files, 300 groups, 600 duplicate members).
        const auto sumText = [&](const char* name) {
            auto* l = w.findChild<QLabel*>(QString::fromLatin1(name));
            return l ? l->text() : QStringLiteral("<none>");
        };
        check(sumText("sumValTotal") == QStringLiteral("600"), "test1b summary total=600");
        check(sumText("sumValDone") == QStringLiteral("600"), "test1b summary read=600");
        check(sumText("sumValIndexed") == QStringLiteral("600"), "test1b summary indexed=600");
        check(sumText("sumValGroups") == QStringLiteral("300"), "test1b summary groups=300");
        check(sumText("sumValDup") == QStringLiteral("600"), "test1b summary dupFiles=600");
        check(sumText("sumValVerify") == QStringLiteral("- / 300"), "test1b summary stored pairs shown");
        check(sumText("sumValTime") != QStringLiteral("-"), "test1b summary load time shown");
    }

    // ---- Test 1c (0.9.4.134): Re-verify end-to-end. The backend re-reads the
    // duplicate members' hashes (no changes expected here), updates nothing,
    // and answers through results() plus the counter event; the backend must
    // stay alive and the view/summary must be repopulated. ----
    {
        auto* reverifyBtn = w.findChild<QPushButton*>("reverifyBtn");
        check(reverifyBtn != nullptr, "test1c reverify button found");
        bool reverifyResults = false;
        bool reverifyCounters = false;
        const auto resultsConn =
            QObject::connect(supervisor, &BackendClient::results, supervisor,
                             [&](const QVector<BackendFile>&, const QStringList&) {
                                 reverifyResults = true;
                             });
        const auto countersConn =
            QObject::connect(supervisor, &BackendClient::reverifyFinished, supervisor,
                             [&](qulonglong, qulonglong, qulonglong, qulonglong,
                                 qulonglong, qulonglong) { reverifyCounters = true; });
        const int exitsBefore2 = backendExits;
        reverifyBtn->click();
        QApplication::processEvents();
        pumpUntil([&] { return reverifyResults; }, 60000, "test1c reverify completed");
        pumpUntil([&] { return reverifyCounters; }, 5000, "test1c reverify counters delivered");
        QObject::disconnect(resultsConn);
        QObject::disconnect(countersConn);
        check(backendExits == exitsBefore2, "test1c no backend restart during re-verify");
        const auto sumText2 = [&](const char* name) {
            auto* l = w.findChild<QLabel*>(QString::fromLatin1(name));
            return l ? l->text() : QStringLiteral("<none>");
        };
        check(sumText2("sumValGroups") == QStringLiteral("300"), "test1c summary groups kept");
        check(sumText2("sumValVerify") == QStringLiteral("- / 300"), "test1c stored pairs kept");
    }

    // ---- Test 2: forced kill right after scan start -> restart -> auto-resume. ----
    // The kill lands deterministically with no wait at all: supervisor.scanning_
    // is set synchronously when START_SCAN is sent, while even a quick-load
    // rescan needs hundreds of ms to finish. Any wait (even for progress)
    // lets the rescan finish first and makes the failure assertion vacuous.
    scanActive = false;
    failedSeen = false;
    const int finishedBeforeCrash = finishedCount;
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
    pumpUntil([&] { return w.testAutoResumeTries() == 1; }, 30000,
              "test2 scan automatically reissued after READY");
    pumpUntil([&] { return finishedCount > finishedBeforeCrash; }, 180000,
              "test2 auto-resumed scan finished (index reopened)");
    check(scan->isEnabled(), "test2 scan button idle after auto-resume");
    if (!gOk) return 5;
    check(grid->count() >= 2, "test2 groups displayed after restart");

    // ---- Test 4 (0.9.4.111, G4): slow-consumer backpressure. ----
    // The GUI consumer stops pumping for 3 s mid-scan: QProcess cannot read
    // stdout, the pipe fills and the backend blocks in flush() while its
    // event queue holds the rest. Draining afterwards must deliver every
    // message: no sequence gaps (loss), no deadlock, all groups, bounded
    // backend memory.
    {
        const int rowsControl = grid->count();
        const int finishedBefore = finishedCount;
        gapLines = 0;
        batchEntries = 0;
        // 0.9.4.113 (B2): back to the default Stable(B) for this scan.
        QSettings().setValue("ui/analyzeMode", 0);
        QSettings().sync();
        const qint64 g4Pid = supervisor->backendPid();
        const quint64 rssBefore = rssMB(g4Pid);
        scan->click();
        QApplication::processEvents();
        QThread::msleep(3000); // consumer stall: no event processing at all
        const quint64 rssDuring = rssMB(g4Pid);
        pumpUntil([&] { return finishedCount > finishedBefore; }, 180000,
                  "test4 stalled-consumer scan finished after drain");
        const quint64 rssAfter = rssMB(g4Pid);
        std::cout << "  [info] test4 rssMB before=" << rssBefore << " during=" << rssDuring
                  << " after=" << rssAfter << " batchEntries=" << batchEntries << "\n";
        pumpUntil([&] { return w.testLastTelemetryJson().contains(QStringLiteral("\"mode\":\"B\"")); },
                  15000, "test4 telemetry records Stable(B) from the settings value");
        check(gapLines == 0, "test4 no backend event gaps (nothing dropped)");
        check(batchEntries > 0, "test4 match batches delivered after the stall");
        check(grid->count() == rowsControl, "test4 every group delivered after the stall");
        check(rssDuring < 512, "test4 backend memory bounded during the stall");
    }

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

    // ---- Test 3c (0.9.4.115, D2): a stale escalation timer must never kill a
    // freshly spawned backend. The 0.9.4.78 comment promised this, but spawn()
    // reset escStage_ while leaving escTimer_ armed, so the timer's next fire
    // took the stage-0 branch and KILLED the new backend ~killWait after spawn
    // with no reason logged (field: exit code 0xf291/62097 self-sustaining
    // loop). Invariant: every unexpected exit must be preceded, since its own
    // spawn, by a logged escalation reason. ----
    {
        qputenv("MSF_TEST_BACKEND_SILENT", "1");
        SupervisorOptions opt4;
        opt4.readyTimeoutMs = 4000;
        opt4.healthTimeoutMs = 4000;
        opt4.maxAttempts = 3;
        opt4.backoffMs = {200, 200, 200};
        opt4.killGraceMs = 300;
        opt4.killWaitMs = 2500;
        auto* supervisor4 = new BackendSupervisor(&app, opt4);
        std::vector<QString> events;
        QObject::connect(supervisor4, &BackendClient::backendLogLine, supervisor4,
                         [&](const QString& line) { events.push_back(line); });
        supervisor4->ensureRunning();
        pumpUntil([&] { return supervisor4->phase() == QStringLiteral("Failed"); }, 60000,
                  "test3c silent-budget supervisor FAILED");
        qunsetenv("MSF_TEST_BACKEND_SILENT");
        std::size_t exits = 0, violations = 0;
        bool reasonSinceSpawn = false;
        for (const auto& line : events) {
            if (line.contains(QStringLiteral("backend spawn"))) reasonSinceSpawn = false;
            else if (line.contains(QStringLiteral("READY timeout"))
                     || line.contains(QStringLiteral("watchdog:"))
                     || line.contains(QStringLiteral("FAILED"))
                     || line.contains(QStringLiteral("refuses to die"))) reasonSinceSpawn = true;
            else if (line.contains(QStringLiteral("unexpected exit"))) {
                ++exits;
                if (!reasonSinceSpawn) ++violations;
            }
        }
        std::cout << "  [info] test3c exits=" << exits << " reasonless=" << violations << "\n";
        check(exits >= 2, "test3c repeated escalations observed");
        check(violations == 0,
              "test3c every kill has its own logged escalation reason (no stale-timer kill)");
        delete supervisor4;
    }

    // ---- Test 5 (0.9.4.111, G6): graceful shutdown is bounded AND prompt. ----
    // Qt updates QProcess state only when the event loop dispatches
    // WinEventAct; the old exit path polled state() without an event loop,
    // so it could never observe the exit and burned the full shutdownWaitMs
    // on every app exit. The fix waits on the process handle directly.
    {
        SupervisorOptions opt3;
        opt3.readyTimeoutMs = 15000;
        opt3.healthTimeoutMs = 5000;
        opt3.maxAttempts = 1;
        opt3.backoffMs = {300};
        opt3.killGraceMs = 500;
        opt3.killWaitMs = 1000;
        opt3.shutdownWaitMs = 3000;
        auto* supervisor3 = new BackendSupervisor(&app, opt3);
        bool ready3 = false;
        QObject::connect(supervisor3, &BackendClient::backendConnection, supervisor3,
                         [&](bool up, const QString&) { if (up) ready3 = true; });
        supervisor3->ensureRunning();
        pumpUntil([&] { return ready3; }, 30000, "test5 backend READY for shutdown timing");
        const qint64 pid3 = supervisor3->backendPid();
        QElapsedTimer st;
        st.start();
        delete supervisor3; // destructor -> shutdown(): the app-exit path
        const qint64 shutdownMs = st.elapsed();
        std::cout << "  [info] test5 shutdown elapsed=" << shutdownMs
                  << " ms (budget " << opt3.shutdownWaitMs << ")\n";
        check(shutdownMs < 1500, "test5 graceful shutdown prompt (no fixed full-budget stall)");
        check(!pidAlive(pid3), "test5 backend PID gone after shutdown");
    }

    // ---- Test 6 (0.9.4.111, G6): a dead GUI must not orphan the backend. ----
    // The helper spawns a WEDGED backend (stdin-EOF self-exit blocked), then
    // the parent kills the helper with TerminateProcess — a GUI crash with no
    // shutdown path. The Job Object KILL_ON_JOB_CLOSE must reap the backend.
    // The --no-job control must show the orphan surviving, proving the job
    // (not some other path) is what kills it.
    {
        const auto pidFile = d / "helper_job.pid";
        QProcess helper;
        helper.setProgram(QCoreApplication::applicationFilePath());
        helper.setArguments({"--gui-helper", QString::fromStdString(pidFile.string())});
        helper.start();
        const qint64 helperBackend = waitPidFile(pidFile, 30000);
        check(helperBackend > 0, "test6 helper reported its backend PID");
        const qint64 helperPid = helper.processId();
        check(helperPid > 0 && pidAlive(helperBackend),
              "test6 wedged backend alive before parent death");
        killPid(helperPid); // simulate GUI crash: no shutdown, no destructors
        pumpUntil([&] { return !pidAlive(helperBackend); }, 10000,
                  "test6 backend terminated after parent death (job KILL_ON_JOB_CLOSE)");
        if (pidAlive(helperBackend)) killPid(helperBackend); // cleanup on failure
    }
    {
        const auto pidFile = d / "helper_nojob.pid";
        QProcess helper;
        helper.setProgram(QCoreApplication::applicationFilePath());
        helper.setArguments({"--gui-helper", QString::fromStdString(pidFile.string()),
                             "--no-job"});
        helper.start();
        const qint64 helperBackend = waitPidFile(pidFile, 30000);
        check(helperBackend > 0, "test6b no-job helper reported its backend PID");
        killPid(helper.processId());
        QThread::msleep(2000);
        QApplication::processEvents();
        check(pidAlive(helperBackend),
              "test6b control: no-job backend survives parent death (job is load-bearing)");
        check(killPid(helperBackend), "test6b orphan cleaned up explicitly");
        pumpUntil([&] { return !pidAlive(helperBackend); }, 10000,
                  "test6b orphan gone after explicit kill");
    }

    fs::remove_all(d, ec);
    std::cout << "backend_e2e_selfcheck=" << (gOk ? "ok" : "FAILED")
              << " checks=" << gChecks << "\n";
    return gOk ? 0 : 1;
}
