// BackendSupervisor (P3: 0.9.4.69). BackendClient implemented over a real
// OS process: spawns MediaSimilarityFinderBackend --backend, speaks the
// line-oriented JSON protocol (src/backend_ipc.*), and enforces the
// supplemental-directive hard rules:
//
// - G3: no synchronous wait anywhere on the live path. Kill escalation and
//   backoff run on QTimer; the only bounded synchronous wait is shutdown()
//   on the application-exit path (documented exception, no event pumping).
// - G4: the scan never blocks on IPC. Qt buffers stdin writes; MATCHES are
//   chunked backend-side and never dropped (lossless); progress is
//   coalesced by arrival (latest wins downstream).
// - G5: never spawn while the old backend is alive: terminate() → 3s grace
//   → kill() → 5s verify, all asynchronous, spawn only after exit verified.
// - G6: the backend lives in a Win32 Job Object with KILL_ON_JOB_CLOSE, so
//   a dead GUI cannot orphan it. The backend additionally self-exits on
//   control-channel loss.
// - Bounded restart (default 3 attempts, 2s/5s/10s backoff) then FAILED.
//   Success is proven by HELLO_ACK + READY, never by spawn alone.
//
// MainWindow talks to this object exactly like the loopback client. Monitor
// configuration is re-sent after every restart (ambient, not a session).
#pragma once
#include "backend_client.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>
#include <deque>

struct SupervisorOptions {
    int readyTimeoutMs = 15000;
    int healthTimeoutMs = 10000;
    int maxAttempts = 3;
    QList<int> backoffMs = {2000, 5000, 10000};
    int killGraceMs = 3000;
    int killWaitMs = 5000;
    int shutdownWaitMs = 3000;
    int malformedLimit = 10;
};

class BackendSupervisor : public BackendClient {
    Q_OBJECT
public:
    explicit BackendSupervisor(QObject* parent = nullptr,
                               const SupervisorOptions& opt = SupervisorOptions{});
    ~BackendSupervisor() override;

    // BackendClient (same contract as loopback; transport underneath).
    void ensureRunning() override;
    void startScan(const BackendScanConfig& cfg) override;
    void pause() override;
    void resume() override;
    void cancel() override;
    void shutdown() override;
    void startMonitor(const QStringList& watchRoots, const QStringList& compareRoots,
                      const QString& appDir, double thresholdPercent,
                      int stableSeconds, int pollSeconds, bool gpuEnabled,
                      const msf::ResourcePolicy& policy) override;
    void stopMonitor() override;
    void setMonitorPolicy(const msf::ResourcePolicy& policy) override;
    void updateResourcePolicy(const ExecutionPolicy& exec) override;
    void refreshMonitor() override {} // snapshots stream from the backend ticker
    void requestThumb(const QString& path, const QSize& size, bool isVideo,
                      quint64 requestId) override;
    void requestFileMeta(const QString& path, quint64 requestId) override;
    QVector<BackendFile> requestFiles() override { return lastFiles_; }
    BackendStatus lastStatus() const override { return lastStatus_; }
    BackendMonitorStatus lastMonitorStatus() const override { return lastMonStatus_; }
    std::string telemetryJsonForTest() const override { return {}; } // loopback-only
    qint64 backendPid() const override;
    bool separateProcess() const override { return true; }

    // Acceptance introspection (E2E tests): current supervisor phase.
    QString phase() const { return phase_; }

private slots:
    void onStarted();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void onStdout();
    void onStderr();
    void onHealthTick();
    void onEscalationTimeout();
    void onRestartTimeout();
    void terminateAndRespawn(const QString& reason); // watchdog path: async kill escalation

private:
    enum class ProcState { Down, Starting, Ready, Failed, ShuttingDown };

    void spawn();
    void sendMonitorStart(); // re-sends stored monitor config (also after restart)
    void handleMalformed(const QString& reason); // reject accounting, restarts the faulty backend past the limit
    void sendCommand(const QString& type, const QJsonObject& payload, quint64 requestId = 0);
    void handleLine(const QByteArray& line);
    void dispatchEvent(const QString& type, const QJsonObject& payload, quint64 requestId);
    void enterUnexpectedExit(const QString& reason, int exitCode);
    void scheduleRestart();
    void enterFailed(const QString& reason);
    void setPhase(const QString& phase) { phase_ = phase; }
    void assignJobObject();
    bool processAlive() const;
    void emitThumbReady(quint64 requestId, const QJsonObject& payload);

    SupervisorOptions opt_;
    QProcess* proc_ = nullptr;
    QTimer healthTimer_;
    QTimer escTimer_;
    QTimer restartTimer_;
    QByteArray stdoutBuf_;
    QByteArray stderrBuf_;
    QString nonce_;
    quint64 sequenceOut_ = 0;
    quint64 sequenceIn_ = 0;
    int malformed_ = 0;
    qint64 lastHealthMs_ = 0;
    qint64 spawnMs_ = 0;
    int attempts_ = 0; // restarts used (initial spawn is not an attempt)
    int escStage_ = 0; // kill-escalation stage: 0 = terminate grace, 1 = kill wait
    // 0.9.4.78: kill proven ineffective against this process instance. Terminal
    // latch: enterFailed stops re-arming escalation, and ensureRunning refuses
    // to stack a new backend on the live one (G5) until it is gone.
    bool killExhausted_ = false;
    // 0.9.4.78: health-timeout strikes. One missed window is usually a match
    // burst saturating the pipe/GUI, not death: kill only on 3 consecutive.
    int healthMisses_ = 0;
    bool explicitShutdown_ = false;
    bool backendReady_ = false;
    bool scanning_ = false;
    QString phase_ = QStringLiteral("Down");
    ProcState procState_ = ProcState::Down;
    // Monitor re-establishment across restarts (ambient configuration).
    bool monitorActive_ = false;
    QStringList monWatch_;
    QStringList monCompare_;
    QString monAppDir_;
    double monThreshold_ = 90.0;
    int monStable_ = 3;
    int monPoll_ = 2;
    bool monGpu_ = true;
    msf::ResourcePolicy monPolicy_;
    // Snapshot caches for the synchronous getters.
    BackendStatus lastStatus_;
    BackendMonitorStatus lastMonStatus_;
    QVector<BackendFile> lastFiles_;
    // RESULTS_PAGE assembly.
    QVector<BackendFile> assemblingFiles_;
    QStringList assemblingRows_;
    int assemblingPages_ = -1;
#ifdef _WIN32
    void* job_ = nullptr; // HANDLE, void* to keep windows.h out of the header
#endif
};
