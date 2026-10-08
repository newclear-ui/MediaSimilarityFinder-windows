// BackendClient interface (P2: 0.9.4.68).
//
// The GUI talks to the search backend through this interface only: commands
// go down, events come up. LoopbackBackendClient implements it in-process on
// top of the real BackendCore (ScanWorker + MediaSearchEngine +
// MediaMonitor); the P3 real client will implement the same contract over
// the IPC channel, so every type here is a plain value struct with no
// QObject, no widget, no native handle, and no C++ object pointer.
//
// Discipline (supplemental directive G2/G3):
// - Commands are fire-and-forget. There is deliberately NO synchronous wait
//   API: no waitFor*, no blocking read, no requestAndWait.
// - Tick/hot-path values travel as pushed snapshots (statusSnapshot,
//   monitorSnapshot). The GUI caches them and never polls the backend.
// - Request/response pairs (thumbnail, detail) carry requestId +
//   generation; the GUI drops stale responses.
#pragma once
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstddef>
#include <string>

#include "resource_policy.h" // msf::ResourcePolicy: plain config data (brief §10 payload class)

// Execution policy (directive §9–14, P4): two independent axes cross the
// boundary with identical meaning. Backend never reinterprets them:
// - cpuMode/cpuPercent: ResourceMode aggressiveness + worker budget.
// - strategy: 0=AUTO (scheduler auto-splits CPU/GPU), 1=CPU_ONLY (GPU lane
//   forced off backend-side), 2=GPU_MAX (GPU allowed; scheduler auto-splits,
//   no share boost exists in code — documented, not silently upgraded).
// - gpuEnabled: effective GPU switch after strategy enforcement.
struct ExecutionPolicy {
    int cpuMode = 3; // ResourceMode::Balanced
    int cpuPercent = 55;
    int gpuPercent = 60;
    int strategy = 0;
    bool gpuEnabled = true;
};
// Scan configuration. Plain values copied from the existing UI model at
// startScan time (brief §10: reuse the existing model, no new schema).
// exec carries the two-axis policy; the scalar cpu/gpu fields stay for
// backward-compatible readers.
struct BackendScanConfig {
    QString root;
    QString appDir;
    int distance = 8;
    int cpu = 50;
    int gpuPercent = 50;
    bool gpuEnabled = false;
    bool scanImages = true;
    bool scanVideos = true;
    QSet<QString> ignored;
    bool detailedLog = true;
    // Test Mode (0.9.4.75): run the full scan pipeline inside an isolated
    // scratch index directory. The production index/database is never opened
    // for writing by a test scan.
    bool testMode = false;
    ExecutionPolicy exec;
};

// One match for MATCHES_BATCH. Same shape as LiveMatch, encodable later.
struct BackendMatch {
    QString left;
    QString right;
    double percent = 0.0;
    int kind = 1;
};

// One indexed file for the results event. Same shape as GuiFile.
struct BackendFile {
    QString path;
    qulonglong size = 0;
    QString fpHex;
    double duration = 0.0;
};

// Thumbnail pixels from the backend (Type B). Raw RGBA plus dimensions;
// the GUI builds the display QIcon. P3 encodes this struct for IPC; the
// fields stay identical so the contract does not change.
struct ThumbResult {
    QByteArray rgba;
    int width = 0;
    int height = 0;
    bool ok = false;
};

// Display metadata for the detail pane and file lists (Type B, async like
// thumbnails: requestFileMeta + fileMetaReady; stale arrivals dropped).
struct FileMetaResult {
    QString path;
    int width = 0;
    int height = 0;
    double duration = 0.0;
    bool ok = false;
};

// Pushed engine snapshot (Type C). Updated by statusSnapshot; the GUI tick
// reads lastStatus() and never touches the engine. backendCpu/backendRss
// are the WORKING process's own numbers (Backend in production, GUI process
// under loopback) — the only CPU/RAM the summary panel shows (P4: one
// meaning, no flip-flop).
struct BackendStatus {
    qulonglong analyzed = 0;
    qulonglong unchanged = 0; // P4: valid index reused (Index Complete = analyzed + unchanged)
    bool gpuActive = false;
    bool gpuAvailable = false;
    qulonglong gpuDone = 0;
    double backendCpu = 0.0;
    qulonglong backendRssMB = 0;
};

// Monitor snapshot (Type C). Mirrors the fields updateMonitorStatus shows.
struct BackendMonitorStatus {
    bool running = false;
    int loadState = 0; // msf::LoadState as int (Idle/Light/Busy/Heavy/Critical)
    double cpuPercent = 0.0;
    double memoryPercent = 0.0;
    double gpuPercent = -1.0;
    qulonglong analyzed = 0;
    qulonglong pending = 0;
};

// One monitor match for the monitor event.
struct BackendMonitorMatch {
    QString newPath;
    QString existingPath;
    double percent = 0.0;
};

// Monitor event types, same order as msf::MonitorEvent::Type
// (Detected/Deferred/Match/Error/Started/Stopped). Compared as ints across
// the future process boundary; keep the order in sync.
enum class BackendMonitorEventType { Detected, Deferred, Match, Error, Started, Stopped };

// Load states, same order as msf::LoadState (Idle/Light/Busy/Heavy/Critical).
enum class BackendLoadState { Idle, Light, Busy, Heavy, Critical };
struct BackendMonitorEvent {
    int type = 0;
    QString path;
    QString detail;
    QVector<BackendMonitorMatch> matches;
};

Q_DECLARE_METATYPE(BackendMatch)
Q_DECLARE_METATYPE(BackendFile)
Q_DECLARE_METATYPE(ThumbResult)
Q_DECLARE_METATYPE(BackendStatus)
Q_DECLARE_METATYPE(BackendMonitorStatus)
Q_DECLARE_METATYPE(BackendMonitorMatch)
Q_DECLARE_METATYPE(BackendMonitorEvent)
Q_DECLARE_METATYPE(FileMetaResult)
Q_DECLARE_METATYPE(QVector<BackendMatch>)
Q_DECLARE_METATYPE(QVector<BackendFile>)

class BackendClient : public QObject {
    Q_OBJECT
public:
    explicit BackendClient(QObject* parent = nullptr) : QObject(parent) {}
    ~BackendClient() override = default;

    // ---- Type A: fire-and-forget commands (never block, never wait) ----
    // ensureRunning boots the backend on first GUI show (supervisor spawns;
    // loopback is a no-op). Idempotent: safe to call on every show.
    virtual void ensureRunning() = 0;
    virtual void startScan(const BackendScanConfig& cfg) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void cancel() = 0;
    virtual void shutdown() = 0;
    virtual void startMonitor(const QStringList& watchRoots, const QStringList& compareRoots,
                              const QString& appDir, double thresholdPercent,
                              int stableSeconds, int pollSeconds, bool gpuEnabled,
                              const msf::ResourcePolicy& policy) = 0;
    virtual void stopMonitor() = 0;
    // Live policy refresh (Type A). The monitor keeps sampling under the new
    // policy; no restart, no state loss.
    virtual void setMonitorPolicy(const msf::ResourcePolicy& policy) = 0;
    // P4 live policy refresh: monitor applies immediately, engine side
    // applies at the next scan (worker sizing is scan-start-only). Answers
    // through the session policyApplied report (surfaced as a backend log
    // line: never silent, explicit partial flag).
    virtual void updateResourcePolicy(const ExecutionPolicy& exec) = 0;
    // Hint to push a fresh monitor snapshot (fire-and-forget; the snapshot
    // arrives via monitorSnapshot). Lets the GUI tick stay pull-free.
    virtual void refreshMonitor() = 0;

    // ---- Type B: request/response ----
    // Thumbnails are fire-and-forget requests; the answer arrives via
    // thumbReady (uniform async contract — even the loopback answers
    // asynchronously, so the GUI never depends on timing). requestId
    // correlates; the transport echoes it. The GUI drops stale arrivals
    // (size no longer wanted) and unknown ids.
    virtual void requestThumb(const QString& path, const QSize& size, bool isVideo,
                              quint64 requestId) = 0;
    // Indexed-file metadata for the detail pane (Type B). Served from the
    // last completed scan's file list snapshot (loopback answers inline from
    // the engine; same semantics: only finished-scan data).
    virtual QVector<BackendFile> requestFiles() = 0;
    // File metadata (Type B, async): dimensions + duration for paths the
    // engine may never have analyzed. Answers via fileMetaReady.
    virtual void requestFileMeta(const QString& path, quint64 requestId) = 0;

    // ---- Type C: cached snapshots (updated by signals, never polled) ----
    virtual BackendStatus lastStatus() const = 0;
    virtual BackendMonitorStatus lastMonitorStatus() const = 0;

    // Test-only hook (loopback implements it; the real client reports empty).
    virtual std::string telemetryJsonForTest() const { return {}; }
    // Acceptance hook: OS PID of the current backend process, -1 when there
    // is none (loopback: always -1, same process). Used by crash-injection
    // tests to prove GUI PID != Backend PID across restarts.
    virtual qint64 backendPid() const { return -1; }

signals:
    void progress(int pct, QString path);
    void progressCount(qulonglong done, qulonglong total);
    void walkedCount(qulonglong n);
    void fingerprintProgress(qulonglong files, qulonglong bytes, QString path);
    void targetCount(qulonglong n);
    void listingProgress(std::size_t n);
    void matchesBatch(QVector<BackendMatch> batch);
    void quickLoaded(int n);
    void revalidated(int kept, int dropped);
    void results(QVector<BackendFile> files, QStringList matchRows);
    void telemetryReady(QString json);
    void finished(QString msg);
    void failed(QString msg);
    void thumbReady(quint64 requestId, ThumbResult thumb);
    void fileMetaReady(quint64 requestId, FileMetaResult meta);
    void statusSnapshot(BackendStatus st);
    void monitorEvent(BackendMonitorEvent ev);
    void monitorSnapshot(BackendMonitorStatus st);
    // Backend availability for the UI guard (brief §15/§21): true = READY or
    // running normally; false with a reason while restarting/unavailable or
    // FAILED. Loopback never emits (always available).
    void backendConnection(bool available, QString message);
    // Backend stderr/diagnostics surfacing (the GUI appends these to
    // msf_scan.log; the backend never writes that file itself).
    void backendLogLine(const QString& line);
};
