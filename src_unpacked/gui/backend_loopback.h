// LoopbackBackendClient (P2: 0.9.4.68, rehomed P3a: 0.9.4.69).
//
// BackendClient implemented in-process on top of the real BackendCore. Since
// P3a this is a thin forwarder over BackendSession (src/backend_session.*):
// the same session implementation the P3 backend process drives, so tested
// and shipped paths cannot drift. This is NOT a fake.
#pragma once
#include "backend_client.h"

#include <QObject>
#include <memory>

#include "backend_session.h"

class LoopbackBackendClient : public BackendClient {
    Q_OBJECT
public:
    explicit LoopbackBackendClient(QObject* parent = nullptr);
    ~LoopbackBackendClient() override = default;

    void startScan(const BackendScanConfig& cfg) override;
    void ensureRunning() override {} // loopback needs no process
    void pause() override { session_->pause(); }
    void resume() override { session_->resume(); }
    void cancel() override { session_->cancel(); }
    void shutdown() override { session_->shutdown(); }
    void startMonitor(const QStringList& watchRoots, const QStringList& compareRoots,
                      const QString& appDir, double thresholdPercent,
                      int stableSeconds, int pollSeconds, bool gpuEnabled,
                      const msf::ResourcePolicy& policy) override;
    void stopMonitor() override { session_->stopMonitor(); }
    void setMonitorPolicy(const msf::ResourcePolicy& policy) override;
    void refreshMonitor() override { session_->refreshMonitor(); }
    void requestThumb(const QString& path, const QSize& size, bool isVideo,
                      quint64 requestId) override;
    QVector<BackendFile> requestFiles() override;
    void requestFileMeta(const QString& path, quint64 requestId) override;
    BackendStatus lastStatus() const override;
    BackendMonitorStatus lastMonitorStatus() const override;
    std::string telemetryJsonForTest() const override;

private:
    std::unique_ptr<BackendSession> session_;
};
