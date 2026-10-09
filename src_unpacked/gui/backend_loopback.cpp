// LoopbackBackendClient implementation (P3a: 0.9.4.69). Thin forwarder over
// BackendSession: converts core/session types to the BackendClient contract
// types. No engine logic lives here.
#include "backend_loopback.h"

#include "backend_sysinfo.h"

LoopbackBackendClient::LoopbackBackendClient(QObject* parent) : BackendClient(parent) {
    qRegisterMetaType<QVector<BackendMatch>>();
    qRegisterMetaType<QVector<BackendFile>>();
    qRegisterMetaType<ThumbResult>();
    qRegisterMetaType<BackendStatus>();
    qRegisterMetaType<BackendMonitorStatus>();
    qRegisterMetaType<BackendMonitorMatch>();
    qRegisterMetaType<BackendMonitorEvent>();
    qRegisterMetaType<FileMetaResult>();
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
                BackendStatus out;
                out.analyzed = st.analyzed;
                out.unchanged = st.unchanged;
                out.gpuActive = st.gpuActive;
                out.gpuAvailable = st.gpuAvailable;
                out.gpuDone = st.gpuDone;
                // Loopback has no Health ticker, so sample the GUI process here
                // (the working process under loopback); otherwise the summary
                // CPU/RAM rows would always read 0.
                msf::sampleOwnProcess(out.backendCpu, out.backendRssMB);
                emit statusSnapshot(out);
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
    connect(session_.get(), &BackendSession::policyApplied, this,
            [this](bool partial, int mode, int cpu, int gpu, int strategy, bool gpuOn) {
                emit backendLogLine(QStringLiteral("policy APPLIED partial=%1 mode=%2 cpu=%3 gpu=%4 strategy=%5 gpuOn=%6")
                                        .arg(partial)
                                        .arg(mode)
                                        .arg(cpu)
                                        .arg(gpu)
                                        .arg(strategy)
                                        .arg(gpuOn));
            });
}

void LoopbackBackendClient::startScan(const BackendScanConfig& cfg) {
    session_->startScan(cfg.root, cfg.appDir, cfg.distance, cfg.cpu, cfg.gpuPercent,
                        cfg.gpuEnabled, cfg.scanImages, cfg.scanVideos,
                        cfg.ignored, cfg.detailedLog,
                        cfg.exec.cpuMode, cfg.exec.strategy, cfg.testMode,
                        static_cast<int>(cfg.analyzeMode));
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

void LoopbackBackendClient::updateResourcePolicy(const ExecutionPolicy& exec) {
    session_->updateResourcePolicy(exec.cpuMode, exec.cpuPercent, exec.gpuPercent,
                                   exec.strategy, exec.gpuEnabled);
}

void LoopbackBackendClient::requestThumb(const QString& path, const QSize& size, bool isVideo,
                                          quint64 requestId) {
    Q_UNUSED(size);
    // Uniform async contract: answer through thumbReady on the event loop,
    // never inline — the GUI must not depend on delivery timing (the real
    // transport is always asynchronous). JPEG decoded with QtGui here;
    // the real transport carries the same bytes base64.
    ThumbResult r;
    const msf::JpegThumb j = session_->requestThumb(path, isVideo);
    if (j.ok && !j.bytes.empty()) {
        // Decode with the shared libjpeg-turbo helper, not QtGui: the Qt JPEG
        // plugin is not deployed (see backend_thumb.h), so QImage::fromData
        // would silently fail and no thumbnail would ever paint.
        const msf::Argb32Image im = msf::decodeJpegArgb32(j.bytes.data(), j.bytes.size());
        if (im.ok) {
            r.rgba = QByteArray(reinterpret_cast<const char*>(im.bytes.data()),
                                (int)im.bytes.size());
            r.width = im.width;
            r.height = im.height;
            r.ok = true;
        }
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

void LoopbackBackendClient::requestFileMeta(const QString& path, quint64 requestId) {
    const msf::FileMeta m = session_->requestFileMeta(path.toStdString());
    FileMetaResult r;
    r.path = path;
    r.width = m.width;
    r.height = m.height;
    r.duration = m.duration;
    r.ok = m.ok;
    QMetaObject::invokeMethod(
        this, [=, this] { emit fileMetaReady(requestId, r); }, Qt::QueuedConnection);
}

BackendStatus LoopbackBackendClient::lastStatus() const {
    const SessionStatus s = session_->lastStatus();
    BackendStatus out;
    out.analyzed = s.analyzed;
    out.unchanged = s.unchanged;
    out.gpuActive = s.gpuActive;
    out.gpuAvailable = s.gpuAvailable;
    out.gpuDone = s.gpuDone;
    msf::sampleOwnProcess(out.backendCpu, out.backendRssMB);
    return out;
}

BackendMonitorStatus LoopbackBackendClient::lastMonitorStatus() const {
    const SessionMonitorStatus s = session_->lastMonitorStatus();
    return {s.running, s.loadState, s.cpuPercent, s.memoryPercent,
            s.gpuPercent, s.analyzed, s.pending};
}

std::string LoopbackBackendClient::telemetryJsonForTest() const {
    return session_->telemetryJsonForTest();
}
