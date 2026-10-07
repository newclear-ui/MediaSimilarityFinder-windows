// BackendSession implementation (P3a: 0.9.4.69). See backend_session.h.
#include "backend_session.h"

#include "file_meta.h"
#include "gpu_backend.h"
#include "image_decoder.h"
#include "video_decoder.h"

BackendSession::BackendSession(QObject* parent) : QObject(parent) {
    qRegisterMetaType<QVector<LiveMatch>>();
    qRegisterMetaType<QVector<GuiFile>>();
    qRegisterMetaType<ThumbBytes>();
    qRegisterMetaType<SessionStatus>();
    qRegisterMetaType<SessionMonitorStatus>();
    qRegisterMetaType<msf::MonitorEvent>();
    monitor_ = std::make_unique<msf::MediaMonitor>();
}

BackendSession::~BackendSession() {
    shutdown();
}

void BackendSession::teardownWorker() {
    scanning_ = false;
    if (thread_) {
        thread_->quit();
        thread_->wait();
        delete worker_;
        delete thread_;
        thread_ = nullptr;
        worker_ = nullptr;
    }
}

void BackendSession::startScan(const QString& root, const QString& appDir, int distance,
                              int cpu, int gpuPercent, bool gpuEnabled,
                              bool scanImages, bool scanVideos,
                              const QSet<QString>& ignored, bool detailedLog) {
    teardownWorker();
    thread_ = new QThread(this);
    worker_ = new ScanWorker(root, appDir, distance, cpu, gpuPercent,
                             gpuEnabled, scanImages, scanVideos);
    worker_->setIgnored(ignored);
    worker_->setDetailedLog(detailedLog);
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::started, worker_, &ScanWorker::run);
    connect(worker_, &ScanWorker::progress, this, &BackendSession::progress);
    connect(worker_, &ScanWorker::progressCount, this, &BackendSession::progressCount);
    connect(worker_, &ScanWorker::walkedCount, this, &BackendSession::walkedCount);
    connect(worker_, &ScanWorker::fingerprintProgress, this, &BackendSession::fingerprintProgress);
    connect(worker_, &ScanWorker::targetCount, this, &BackendSession::targetCount);
    connect(worker_, &ScanWorker::listingProgress, this, &BackendSession::listingProgress);
    connect(worker_, &ScanWorker::matchesArrived, this, &BackendSession::onMatchesArrived);
    connect(worker_, &ScanWorker::quickLoaded, this, &BackendSession::quickLoaded);
    connect(worker_, &ScanWorker::revalidated, this, &BackendSession::revalidated);
    connect(worker_, &ScanWorker::results, this, &BackendSession::onResults);
    connect(worker_, &ScanWorker::telemetryReady, this, &BackendSession::telemetryReady);
    connect(worker_, &ScanWorker::finished, this, &BackendSession::onFinished);
    connect(worker_, &ScanWorker::failed, this, &BackendSession::onFailed);
    connect(worker_, &ScanWorker::finished, thread_, &QThread::quit);
    connect(worker_, &ScanWorker::failed, thread_, &QThread::quit);
    thread_->start();
    scanning_ = true;
    emit stateChanged(QStringLiteral("SCANNING"));
}

void BackendSession::pause() {
    if (!worker_) return;
    // Direct call, NOT queued: pause() only stores an atomic, and a queued
    // slot could never fire while run() occupies the worker thread's event
    // loop — which is exactly why pause appeared dead mid-scan.
    worker_->pause();
    // Synchronous drain preserves pause-time completeness: everything
    // streamed so far is delivered within the call (takePending is
    // thread-safe by contract).
    onMatchesArrived();
    emit stateChanged(QStringLiteral("PAUSED"));
}

void BackendSession::resume() {
    if (!worker_) return;
    worker_->resume();
    emit stateChanged(QStringLiteral("SCANNING"));
}

void BackendSession::cancel() {
    if (!worker_) return;
    // Direct call (see pause()): a queued cancel would only run after run()
    // returns, i.e. never in time to stop the scan.
    worker_->cancel();
    emit stateChanged(QStringLiteral("CANCELLING"));
}

void BackendSession::shutdown() {
    emit stateChanged(QStringLiteral("SHUTTING_DOWN"));
    teardownWorker();
    stopMonitor();
}

void BackendSession::drainToGui() {
    if (!worker_) return;
    const auto v = worker_->takePending();
    if (v.isEmpty()) return;
    emit matchesBatch(v);
}

void BackendSession::onMatchesArrived() {
    drainToGui();
    pushStatusSnapshot();
}

void BackendSession::onResults(QVector<GuiFile> files, QStringList matchRows) {
    drainToGui(); // stragglers streamed just before results must not be lost
    emit results(files, matchRows);
}

void BackendSession::onFinished(QString msg) {
    scanning_ = false;
    drainToGui();
    emit finished(msg);
    emit stateChanged(QStringLiteral("READY"));
}

void BackendSession::onFailed(QString msg) {
    scanning_ = false;
    drainToGui();
    emit failed(msg);
    emit stateChanged(QStringLiteral("READY"));
}

void BackendSession::pushStatusSnapshot() {
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

ThumbBytes BackendSession::requestThumb(const QString& path, bool isVideo) {
    ThumbBytes r;
    if (!worker_) return r;
    const auto& engine = worker_->scanEngine();
    const std::string p = path.toStdString();
    if (isVideo) {
        std::vector<unsigned char> px;
        if (engine.getVideoThumb(p, px) && px.size() >= (std::size_t)48 * 48) {
            r.rgba = std::move(px);
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
    r.rgba = std::move(px);
    r.width = pw;
    r.height = ph;
    r.ok = true;
    return r;
}

QVector<GuiFile> BackendSession::requestFiles() {
    QVector<GuiFile> out;
    if (!worker_) return out;
    for (const auto& f : worker_->scanEngine().files()) {
        GuiFile g;
        g.path = QString::fromStdString(f.path);
        g.size = (qulonglong)f.size;
        g.fpHex = QString("%1").arg((qulonglong)f.fingerprint, 16, 16, QChar('0'));
        g.duration = f.duration;
        out.push_back(g);
    }
    return out;
}

std::string BackendSession::telemetryJsonForTest() const {
    if (!worker_ || !worker_->scanEngine().hasTelemetry()) return {};
    return worker_->scanEngine().telemetryJson();
}

msf::FileMeta BackendSession::requestFileMeta(const std::string& path) {
    msf::FileMeta m;
    if (worker_) {
        for (const auto& f : worker_->scanEngine().files()) {
            if (f.path != path) continue;
            m.duration = f.duration;
            m.ok = true;
            break;
        }
    }
    const std::string ext = [&] {
        const size_t dot = path.find_last_of('.');
        std::string e = (dot == std::string::npos) ? std::string() : path.substr(dot + 1);
        for (auto& ch : e) ch = (char)tolower((unsigned char)ch);
        return e;
    }();
    const bool isVid = ext == "mp4" || ext == "mkv" || ext == "avi" ||
                       ext == "mov" || ext == "webm" || ext == "m4v" || ext == "wmv";
    if (isVid) {
        msf::VideoDecoder dec;
        if (dec.open(path)) {
            msf::VideoInfo vi;
            if (dec.info(vi) && vi.width > 0 && vi.height > 0) {
                m.width = vi.width;
                m.height = vi.height;
            }
            if (vi.duration > 0) m.duration = vi.duration;
            if (m.width > 0) m.ok = true;
            dec.close();
        }
        return m;
    }
    msf::ImageDecoder dec;
    int w = 0, h = 0;
    if (dec.dimensionsFast(path, w, h) && w > 0 && h > 0) {
        m.width = w;
        m.height = h;
        m.ok = true;
        return m;
    }
    if (msf::ffprobeSize(path, w, h)) {
        m.width = w;
        m.height = h;
        m.ok = true;
    }
    return m;
}

void BackendSession::startMonitor(const QStringList& watchRoots, const QStringList& compareRoots,
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
    // signal so consumers always run on the session owner's thread.
    monitor_->start(c, policy, [this](const msf::MonitorEvent& e) {
        emit monitorEvent(e);
    });
}

void BackendSession::stopMonitor() {
    if (monitor_) monitor_->stop();
}

bool BackendSession::monitorRunning() const {
    return monitor_ && monitor_->running();
}

void BackendSession::setMonitorPolicy(const msf::ResourcePolicy& policy) {
    if (monitor_) monitor_->setPolicy(policy);
}

void BackendSession::refreshMonitor() {
    // Fire-and-forget snapshot hint (Type C): sample here, deliver via the
    // snapshot signal so no caller ever pulls across a future process
    // boundary. Same thread-safety profile as the former GUI tick reads.
    if (!monitor_) return;
    lastMonStatus_ = convertStatus(monitor_->status());
    emit monitorSnapshot(lastMonStatus_);
}

SessionMonitorStatus BackendSession::convertStatus(const msf::MonitorStatus& s) {
    SessionMonitorStatus out;
    out.running = s.running;
    out.loadState = static_cast<int>(s.loadState);
    out.cpuPercent = s.cpuPercent;
    out.memoryPercent = s.memoryPercent;
    out.gpuPercent = s.gpuPercent;
    out.analyzed = (qulonglong)s.analyzed;
    out.pending = (qulonglong)s.pending;
    return out;
}
