// LoopbackBackendClient implementation (P2: 0.9.4.68). See backend_loopback.h.
#include "backend_loopback.h"

#include <QCoreApplication>

LoopbackBackendClient::LoopbackBackendClient(QObject* parent) : BackendClient(parent) {
    // Queued cross-thread deliveries below need every custom signal type
    // registered (same rule as the former direct worker connections).
    qRegisterMetaType<QVector<BackendMatch>>();
    qRegisterMetaType<QVector<BackendFile>>();
    qRegisterMetaType<ThumbResult>();
    qRegisterMetaType<BackendStatus>();
    qRegisterMetaType<BackendMonitorStatus>();
    qRegisterMetaType<BackendMonitorMatch>();
    qRegisterMetaType<BackendMonitorEvent>();
    monitor_ = std::make_unique<msf::MediaMonitor>();
}

LoopbackBackendClient::~LoopbackBackendClient() {
    shutdown();
}

void LoopbackBackendClient::teardownWorker() {
    if (thread_) {
        thread_->quit();
        thread_->wait();
        delete worker_;
        delete thread_;
        thread_ = nullptr;
        worker_ = nullptr;
    }
}

void LoopbackBackendClient::startScan(const BackendScanConfig& cfg) {
    teardownWorker();
    thread_ = new QThread(this);
    worker_ = new ScanWorker(cfg.root, cfg.appDir, cfg.distance, cfg.cpu, cfg.gpuPercent,
                             cfg.gpuEnabled, cfg.scanImages, cfg.scanVideos);
    worker_->setIgnored(cfg.ignored);
    worker_->setDetailedLog(cfg.detailedLog);
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::started, worker_, &ScanWorker::run);
    connect(worker_, &ScanWorker::progress, this, &LoopbackBackendClient::onProgress);
    connect(worker_, &ScanWorker::progressCount, this, &LoopbackBackendClient::onProgressCount);
    connect(worker_, &ScanWorker::walkedCount, this, &LoopbackBackendClient::onWalkedCount);
    connect(worker_, &ScanWorker::fingerprintProgress, this, &LoopbackBackendClient::onFingerprintProgress);
    connect(worker_, &ScanWorker::targetCount, this, &LoopbackBackendClient::onTargetCount);
    connect(worker_, &ScanWorker::listingProgress, this, &LoopbackBackendClient::onListingProgress);
    connect(worker_, &ScanWorker::matchesArrived, this, &LoopbackBackendClient::onMatchesArrived);
    connect(worker_, &ScanWorker::quickLoaded, this, &LoopbackBackendClient::onQuickLoaded);
    connect(worker_, &ScanWorker::revalidated, this, &LoopbackBackendClient::onRevalidated);
    connect(worker_, &ScanWorker::results, this, &LoopbackBackendClient::onResults);
    connect(worker_, &ScanWorker::telemetryReady, this, &LoopbackBackendClient::onTelemetryReady);
    connect(worker_, &ScanWorker::finished, this, &LoopbackBackendClient::onFinished);
    connect(worker_, &ScanWorker::failed, this, &LoopbackBackendClient::onFailed);
    connect(worker_, &ScanWorker::finished, thread_, &QThread::quit);
    connect(worker_, &ScanWorker::failed, thread_, &QThread::quit);
    thread_->start();
}

void LoopbackBackendClient::pause() {
    if (!worker_) return;
    // Direct call, NOT queued: pause() only stores an atomic, and a queued
    // slot could never fire while run() occupies the worker thread's event
    // loop. Same rule as the former MainWindow direct calls.
    worker_->pause();
    // Synchronous drain preserves pause-time completeness: everything
    // streamed so far is delivered within the click (takePending is
    // thread-safe by contract).
    onMatchesArrived();
}

void LoopbackBackendClient::resume() {
    if (!worker_) return;
    worker_->resume();
}

void LoopbackBackendClient::cancel() {
    if (!worker_) return;
    // Direct call (see pause()): a queued cancel would only run after run()
    // returns, i.e. never in time to stop the scan.
    worker_->cancel();
}

void LoopbackBackendClient::shutdown() {
    teardownWorker();
    stopMonitor();
}

void LoopbackBackendClient::onMatchesArrived() {
    if (!worker_) return;
    const auto v = worker_->takePending();
    if (v.isEmpty()) return;
    QVector<BackendMatch> batch;
    batch.reserve(v.size());
    for (const auto& m : v) batch.push_back({m.left, m.right, m.percent, m.kind});
    emit matchesBatch(batch);
}

void LoopbackBackendClient::onProgressCount(qulonglong done, qulonglong total) {
    emit progressCount(done, total);
    pushStatusSnapshot();
}

void LoopbackBackendClient::onWalkedCount(qulonglong n) {
    emit walkedCount(n);
    pushStatusSnapshot();
}

void LoopbackBackendClient::onFingerprintProgress(qulonglong files, qulonglong bytes, QString path) {
    emit fingerprintProgress(files, bytes, path);
    pushStatusSnapshot();
}

void LoopbackBackendClient::onResults(QVector<GuiFile> files, QStringList matchRows) {
    QVector<BackendFile> out;
    out.reserve(files.size());
    for (const auto& f : files) out.push_back({f.path, f.size, f.fpHex, f.duration});
    emit results(out, matchRows);
}

void LoopbackBackendClient::pushStatusSnapshot() {
    if (!worker_) return;
    // Same thread-safety profile as the former MainWindow tick reads: the
    // engine exposes these counters for concurrent observation.
    const auto& engine = worker_->scanEngine();
    lastStatus_.analyzed = (qulonglong)engine.analyzedCount();
    lastStatus_.gpuActive = engine.gpuActive();
    lastStatus_.gpuAvailable = worker_->gpuAvailable();
    lastStatus_.gpuDone = worker_->gpuDone();
    emit statusSnapshot(lastStatus_);
}

ThumbResult LoopbackBackendClient::requestThumb(const QString& path, const QSize& size,
                                               bool isVideo, quint64 /*requestId*/) {
    ThumbResult r;
    if (!worker_) return r;
    const auto& engine = worker_->scanEngine();
    const std::string p = path.toStdString();
    if (isVideo) {
        std::vector<unsigned char> px;
        if (engine.getVideoThumb(p, px) && px.size() >= (std::size_t)48 * 48) {
            r.rgba = QByteArray(reinterpret_cast<const char*>(px.data()), (int)px.size());
            r.width = 48;
            r.height = 48;
            r.ok = true;
        }
        return r;
    }
    std::vector<unsigned char> px;
    int pw = 0, ph = 0;
    if (!engine.getColorThumb(p, pw, ph, px) || pw <= 0 || ph <= 0 ||
        px.size() != (std::size_t)pw * ph * 4)
        return r;
    r.rgba = QByteArray(reinterpret_cast<const char*>(px.data()), (int)px.size());
    r.width = pw;
    r.height = ph;
    Q_UNUSED(size);
    r.ok = true;
    return r;
}

std::string LoopbackBackendClient::telemetryJsonForTest() const {
    if (!worker_ || !worker_->scanEngine().hasTelemetry()) return {};
    return worker_->scanEngine().telemetryJson();
}

QVector<BackendFile> LoopbackBackendClient::requestFiles() {
    QVector<BackendFile> out;
    if (!worker_) return out;
    for (const auto& f : worker_->scanEngine().files())
        out.push_back({QString::fromStdString(f.path), (qulonglong)f.size,
                       QString("%1").arg((qulonglong)f.fingerprint, 16, 16, QChar('0')),
                       f.duration});
    return out;
}

void LoopbackBackendClient::startMonitor(const QStringList& watchRoots, const QStringList& compareRoots,
                                        const QString& appDir, double thresholdPercent,
                                        int stableSeconds, int pollSeconds, bool gpuEnabled,
                                        const msf::ResourcePolicy& policy) {
    msf::MonitorConfig c;
    for (const auto& x : watchRoots) c.watchRoots.push_back(x.toStdString());
    for (const auto& x : compareRoots) c.compareRoots.push_back(x.toStdString());
    c.applicationDirectory = appDir.toStdString();
    c.thresholdPercent = thresholdPercent;
    c.stableSeconds = stableSeconds;
    c.pollSeconds = pollSeconds;
    c.gpuEnabled = gpuEnabled;
    // The monitor invokes this from its own thread; re-emit as a queued
    // signal so GUI slots always run on the GUI thread (same marshaling the
    // former invokeMethod call provided).
    monitor_->start(c, policy, [this](const msf::MonitorEvent& e) {
        emit monitorEvent(convertEvent(e));
    });
}

void LoopbackBackendClient::stopMonitor() {
    if (monitor_) monitor_->stop();
}

void LoopbackBackendClient::setMonitorPolicy(const msf::ResourcePolicy& policy) {
    if (monitor_) monitor_->setPolicy(policy);
}

void LoopbackBackendClient::refreshMonitor() {
    // Fire-and-forget snapshot hint (Type C): sample here, deliver via the
    // snapshot signal so no caller ever pulls across a future process
    // boundary. Same thread-safety profile as the former GUI tick reads.
    if (!monitor_) return;
    lastMonStatus_ = convertStatus(monitor_->status());
    emit monitorSnapshot(lastMonStatus_);
}

BackendMonitorEvent LoopbackBackendClient::convertEvent(const msf::MonitorEvent& e) {
    BackendMonitorEvent out;
    out.type = static_cast<int>(e.type);
    out.path = QString::fromStdString(e.path);
    out.detail = QString::fromStdString(e.detail);
    for (const auto& m : e.matches)
        out.matches.push_back({QString::fromStdString(m.newPath),
                               QString::fromStdString(m.existingPath), m.percent});
    return out;
}

BackendMonitorStatus LoopbackBackendClient::convertStatus(const msf::MonitorStatus& s) {
    BackendMonitorStatus out;
    out.running = s.running;
    out.loadState = static_cast<int>(s.loadState);
    out.cpuPercent = s.cpuPercent;
    out.memoryPercent = s.memoryPercent;
    out.gpuPercent = s.gpuPercent;
    out.analyzed = (qulonglong)s.analyzed;
    out.pending = (qulonglong)s.pending;
    return out;
}
