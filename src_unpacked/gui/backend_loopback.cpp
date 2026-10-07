// LoopbackBackendClient implementation (P3a: 0.9.4.69). Thin forwarder over
// BackendSession: converts core/session types to the BackendClient contract
// types. No engine logic lives here.
#include "backend_loopback.h"

LoopbackBackendClient::LoopbackBackendClient(QObject* parent) : BackendClient(parent) {
    qRegisterMetaType<QVector<BackendMatch>>();
    qRegisterMetaType<QVector<BackendFile>>();
    qRegisterMetaType<ThumbResult>();
    qRegisterMetaType<BackendStatus>();
    qRegisterMetaType<BackendMonitorStatus>();
    qRegisterMetaType<BackendMonitorMatch>();
    qRegisterMetaType<BackendMonitorEvent>();
    session_ = std::make_unique<BackendSession>(this);
    // Same signal shapes where possible; conversion where the contract type
    // differs from the core type. Queued automatically across threads.
    connect(session_.get(), &BackendSession::progress, this, &BackendClient::progress);
    connect(session_.get(), &BackendSession::progressCount, this, &BackendClient::progressCount);
    connect(session_.get(), &BackendSession::walkedCount, this, &BackendClient::walkedCount);
    connect(session_.get(), &BackendSession::fingerprintProgress, this, &BackendClient::fingerprintProgress);
    connect(session_.get(), &BackendSession::targetCount, this, &BackendClient::targetCount);
    connect(session_.get(), &BackendSession::listingProgress, this, &BackendClient::listingProgress);
    connect(session_.get(), &BackendSession::quickLoaded, this, &BackendClient::quickLoaded);
    connect(session_.get(), &BackendSession::revalidated, this, &BackendClient::revalidated);
    connect(session_.get(), &BackendSession::telemetryReady, this, &BackendClient::telemetryReady);
    connect(session_.get(), &BackendSession::finished, this, &BackendClient::finished);
    connect(session_.get(), &BackendSession::failed, this, &BackendClient::failed);
    connect(session_.get(), &BackendSession::matchesBatch, this,
            [this](const QVector<LiveMatch>& batch) {
                QVector<BackendMatch> out;
                out.reserve(batch.size());
                for (const auto& m : batch) out.push_back({m.left, m.right, m.percent, m.kind});
                emit matchesBatch(out);
            });
    connect(session_.get(), &BackendSession::results, this,
            [this](const QVector<GuiFile>& files, const QStringList& rows) {
                QVector<BackendFile> out;
                out.reserve(files.size());
                for (const auto& f : files) out.push_back({f.path, f.size, f.fpHex, f.duration});
                emit results(out, rows);
            });
    connect(session_.get(), &BackendSession::statusSnapshot, this,
            [this](const SessionStatus& st) {
                emit statusSnapshot({st.analyzed, st.gpuActive, st.gpuAvailable, st.gpuDone});
            });
    connect(session_.get(), &BackendSession::monitorEvent, this,
            [this](const msf::MonitorEvent& e) {
                BackendMonitorEvent out;
                out.type = static_cast<int>(e.type);
                out.path = QString::fromStdString(e.path);
                out.detail = QString::fromStdString(e.detail);
                for (const auto& m : e.matches)
                    out.matches.push_back({QString::fromStdString(m.newPath),
                                           QString::fromStdString(m.existingPath), m.percent});
                emit monitorEvent(out);
            });
    connect(session_.get(), &BackendSession::monitorSnapshot, this,
            [this](const SessionMonitorStatus& s) {
                BackendMonitorStatus out;
                out.running = s.running;
                out.loadState = s.loadState;
                out.cpuPercent = s.cpuPercent;
                out.memoryPercent = s.memoryPercent;
                out.gpuPercent = s.gpuPercent;
                out.analyzed = s.analyzed;
                out.pending = s.pending;
                emit monitorSnapshot(out);
            });
}

void LoopbackBackendClient::startScan(const BackendScanConfig& cfg) {
    session_->startScan(cfg.root, cfg.appDir, cfg.distance, cfg.cpu, cfg.gpuPercent,
                        cfg.gpuEnabled, cfg.scanImages, cfg.scanVideos,
                        cfg.ignored, cfg.detailedLog);
}

void LoopbackBackendClient::startMonitor(const QStringList& watchRoots, const QStringList& compareRoots,
                                        const QString& appDir, double thresholdPercent,
                                        int stableSeconds, int pollSeconds, bool gpuEnabled,
                                        const msf::ResourcePolicy& policy) {
    session_->startMonitor(watchRoots, compareRoots, appDir, thresholdPercent,
                           stableSeconds, pollSeconds, gpuEnabled, policy);
}

void LoopbackBackendClient::setMonitorPolicy(const msf::ResourcePolicy& policy) {
    session_->setMonitorPolicy(policy);
}

void LoopbackBackendClient::requestThumb(const QString& path, const QSize& size, bool isVideo,
                                          quint64 requestId) {
    Q_UNUSED(size);
    // Uniform async contract: answer through thumbReady on the event loop,
    // never inline — the GUI must not depend on delivery timing (the real
    // transport is always asynchronous).
    ThumbResult r;
    const ThumbBytes b = session_->requestThumb(path, isVideo);
    if (b.ok) {
        r.rgba = QByteArray(reinterpret_cast<const char*>(b.rgba.data()), (int)b.rgba.size());
        r.width = b.width;
        r.height = b.height;
        r.ok = true;
    }
    QMetaObject::invokeMethod(
        this, [=, this] { emit thumbReady(requestId, r); }, Qt::QueuedConnection);
}

QVector<BackendFile> LoopbackBackendClient::requestFiles() {
    QVector<BackendFile> out;
    for (const auto& f : session_->requestFiles())
        out.push_back({f.path, f.size, f.fpHex, f.duration});
    return out;
}

BackendStatus LoopbackBackendClient::lastStatus() const {
    const SessionStatus s = session_->lastStatus();
    return {s.analyzed, s.gpuActive, s.gpuAvailable, s.gpuDone};
}

BackendMonitorStatus LoopbackBackendClient::lastMonitorStatus() const {
    const SessionMonitorStatus s = session_->lastMonitorStatus();
    return {s.running, s.loadState, s.cpuPercent, s.memoryPercent,
            s.gpuPercent, s.analyzed, s.pending};
}

std::string LoopbackBackendClient::telemetryJsonForTest() const {
    return session_->telemetryJsonForTest();
}
