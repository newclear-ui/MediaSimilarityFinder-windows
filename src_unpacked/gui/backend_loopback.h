// LoopbackBackendClient (P2: 0.9.4.68).
//
// BackendClient implemented in-process on top of the real BackendCore
// (ScanWorker + MediaSearchEngine + MediaMonitor). This is NOT a fake: the
// same engine/monitor code runs, only the OS process split is missing
// (arrives in P3). Existing search-oriented GUI tests keep exercising real
// behavior through it.
//
// Threading mirrors today's MainWindow wiring one-to-one: the worker lives
// on its QThread, this object lives on the GUI thread, worker signals reach
// it queued. pause()/resume()/cancel() stay synchronous direct calls — a
// queued slot could never fire while run() occupies the worker thread
// (see the preserved comment at the call sites).
#pragma once
#include "backend_client.h"

#include <QThread>
#include <QVector>
#include <memory>

// ScanWorker/MediaMonitor declarations (still owned here in P2; the P3 real
// client will own neither). mainwindow.h keeps declaring ScanWorker.
#include "mainwindow.h"
#include "monitor.h"

class LoopbackBackendClient : public BackendClient {
    Q_OBJECT
public:
    explicit LoopbackBackendClient(QObject* parent = nullptr);
    ~LoopbackBackendClient() override;

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
    void refreshMonitor() override;
    ThumbResult requestThumb(const QString& path, const QSize& size, bool isVideo,
                             quint64 requestId) override;
    QVector<BackendFile> requestFiles() override;
    BackendStatus lastStatus() const override { return lastStatus_; }
    BackendMonitorStatus lastMonitorStatus() const override { return lastMonStatus_; }
    std::string telemetryJsonForTest() const override;

private slots:
    void onMatchesArrived();
    void onProgress(int pct, QString path) { emit progress(pct, path); }
    void onProgressCount(qulonglong done, qulonglong total);
    void onWalkedCount(qulonglong n);
    void onFingerprintProgress(qulonglong files, qulonglong bytes, QString path);
    void onTargetCount(qulonglong n) { emit targetCount(n); }
    void onListingProgress(std::size_t n) { emit listingProgress(n); }
    void onQuickLoaded(int n) { emit quickLoaded(n); }
    void onRevalidated(int kept, int dropped) { emit revalidated(kept, dropped); }
    void onResults(QVector<GuiFile> files, QStringList matchRows);
    void onTelemetryReady(QString json) { emit telemetryReady(json); }
    void onFinished(QString msg) { emit finished(msg); }
    void onFailed(QString msg) { emit failed(msg); }

private:
    void pushStatusSnapshot();
    void teardownWorker();
    static BackendMonitorEvent convertEvent(const msf::MonitorEvent& e);
    static BackendMonitorStatus convertStatus(const msf::MonitorStatus& s);

    QThread* thread_ = nullptr;
    ScanWorker* worker_ = nullptr;
    std::unique_ptr<msf::MediaMonitor> monitor_;
    BackendStatus lastStatus_;
    BackendMonitorStatus lastMonStatus_;
};
