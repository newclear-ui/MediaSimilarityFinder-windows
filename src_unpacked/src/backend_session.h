// BackendSession (P3a: 0.9.4.69). The reusable backend engine-room: owns the
// scan QThread, ScanWorker, and MediaMonitor, and speaks the same signal set
// the GUI consumes. LoopbackBackendClient is a thin forwarder over it (same
// process); the P3 backend process drives it behind the IPC codec. One
// implementation, no drift between tested and shipped paths.
//
// Threading: the worker lives on its QThread, this object on the creating
// (GUI, or backend-main) thread. pause()/resume()/cancel() stay synchronous
// direct calls — a queued slot could never fire while run() occupies the
// worker thread. Types here are msf-core/QtCore only (no widgets), so this
// compiles into msf_core and the Qt6::Core-only backend executable.
#pragma once
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <memory>
#include <string>
#include <vector>

#include "monitor.h"
#include "scan_worker.h"
#include "file_meta.h"
#include "thumbnail_store.h"
#include "backend_thumb.h"


// Pushed engine snapshot (Type C).
struct SessionStatus {
    qulonglong analyzed = 0;
    qulonglong unchanged = 0; // P4: valid index reused without analysis
    bool gpuActive = false;
    bool gpuAvailable = false;
    qulonglong gpuDone = 0;
};

// Pushed monitor snapshot (Type C). Subset of msf::MonitorStatus actually
// shown; kept flat so the IPC codec stays trivial.
struct SessionMonitorStatus {
    bool running = false;
    int loadState = 0;
    double cpuPercent = 0.0;
    double memoryPercent = 0.0;
    double gpuPercent = -1.0;
    qulonglong analyzed = 0;
    qulonglong pending = 0;
};

Q_DECLARE_METATYPE(SessionStatus)
Q_DECLARE_METATYPE(SessionMonitorStatus)
Q_DECLARE_METATYPE(msf::MonitorEvent)

class BackendSession : public QObject {
    Q_OBJECT
public:
    explicit BackendSession(QObject* parent = nullptr);
    ~BackendSession() override;

    void startScan(const QString& root, const QString& appDir, int distance,
                   int cpu, int gpuPercent, bool gpuEnabled,
                   bool scanImages, bool scanVideos,
                   const QSet<QString>& ignored, bool detailedLog,
                   int cpuMode, int strategy, bool testMode = false,
                   int analyzeMode = 0);
    // Live policy refresh (P4): monitor applies immediately; the stored
    // engine policy applies to the next scan (worker pool sizing is only
    // safe at scan start). Reports through onPolicyApplied().
    void updateResourcePolicy(int cpuMode, int cpuPercent, int gpuPercent,
                              int strategy, bool gpuEnabled);
    void pause();
    void resume();
    void cancel();
    void shutdown();
    void startMonitor(const QStringList& watchRoots, const QStringList& compareRoots,
                      const QString& appDir, double thresholdPercent,
                      int stableSeconds, int pollSeconds, bool gpuEnabled,
                      const msf::ResourcePolicy& policy);
    void stopMonitor();
    void setMonitorPolicy(const msf::ResourcePolicy& policy);
    void refreshMonitor();
    // Thumbnail serving through the store (JPEG end-to-end). Callers decode
    // (loopback via QImage) or forward bytes (backend_main base64) — never
    // re-encode. desiredMaxDim caps decode work, not the served size.
    msf::JpegThumb requestThumb(const QString& path, bool isVideo, int desiredMaxDim = 256);
    QVector<GuiFile> requestFiles();
    std::string telemetryJsonForTest() const;
    // Display metadata for the detail pane (P4): engine records first, then
    // one Backend-side decode chain (video info / dimensionsFast / ffprobe).
    // Synchronous here; the IPC boundary makes it async for the GUI.
    msf::FileMeta requestFileMeta(const std::string& path);
    // True while a scan worker exists and has not reported terminal state.
    // Used by the backend server's bounded shutdown wait.
    bool scanActive() const { return scanning_; }
    bool monitorRunning() const;
    SessionStatus lastStatus() const { return lastStatus_; }
    SessionMonitorStatus lastMonitorStatus() const { return lastMonStatus_; }

signals:
    void progress(int pct, QString path);
    void progressCount(qulonglong done, qulonglong total);
    void walkedCount(qulonglong n);
    void fingerprintProgress(qulonglong files, qulonglong bytes, QString path);
    void targetCount(qulonglong n);
    void listingProgress(std::size_t n);
    void matchesBatch(QVector<LiveMatch> batch);
    void quickLoaded(int n);
    void revalidated(int kept, int dropped);
    void revalidateProgress(qulonglong done, qulonglong total);
    void results(QVector<GuiFile> files, QStringList matchRows);
    void telemetryReady(QString json);
    void finished(QString msg);
    void failed(QString msg);
    void statusSnapshot(SessionStatus st);
    void monitorEvent(msf::MonitorEvent ev);
    void monitorSnapshot(SessionMonitorStatus st);
    // Session lifecycle for STATE events (READY/SCANNING/PAUSED/CANCELLING/
    // SHUTTING_DOWN). The loopback ignores it; the backend server forwards it.
    void stateChanged(QString state);
    // Policy application report (P4): partial=true means monitor-applied now,
    // engine side deferred to the next scan. Never silent.
    void policyApplied(bool partial, int mode, int cpuPercent, int gpuPercent,
                       int strategy, bool gpuEnabled);

private slots:
    void onMatchesArrived();
    void onResults(QVector<GuiFile> files, QStringList matchRows);
    void onFinished(QString msg);
    void onFailed(QString msg);

private:
    void drainToGui();
    void pushStatusSnapshot();
    void teardownWorker();
    static SessionMonitorStatus convertStatus(const msf::MonitorStatus& s);

    QThread* thread_ = nullptr;
    ScanWorker* worker_ = nullptr;
    std::unique_ptr<msf::MediaMonitor> monitor_;
    // 0.9.4.84: pushes the engine counters (analyzed/unchanged/gpu) on a timer
    // during a scan. Previously the snapshot was pushed only when matches
    // arrived, so an incremental rescan that finds no new matches left the live
    // "index complete" counter at 0.
    QTimer* statusTimer_ = nullptr;
    bool scanning_ = false;
    msf::ThumbnailStore thumbs_;
    QString lastAppDir_;
    QString lastRoot_;
    SessionStatus lastStatus_;
    SessionMonitorStatus lastMonStatus_;
};
