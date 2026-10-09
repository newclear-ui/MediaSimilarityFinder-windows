// BackendSession implementation (P3a: 0.9.4.69). See backend_session.h.
#include "backend_session.h"

#include <QDateTime>
#include <QDir>
#include <QUuid>
#include <functional>
#include <sstream>

#include "file_meta.h"
#include "gpu_backend.h"
#include "image_decoder.h"
#include "index_manager.h"
#include "path_utils.h"
#include "video_decoder.h"

BackendSession::BackendSession(QObject* parent) : QObject(parent) {
    qRegisterMetaType<QVector<LiveMatch>>();
    qRegisterMetaType<QVector<GuiFile>>();
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

// Test Mode uses the same worker/engine path as a production scan, but points
// it at a fresh per-run scratch application directory. A unique leaf keeps
// every Test Mode run cold (full reprocessing) and keeps the production
// <appDir>/Index tree, video cache, profile, and thumbnails untouched.
static QString testScratchAppDir(const QString& appDir, const QString& root) {
    const std::string canonical =
        msf::IndexManager::canonicalRoot(msf::path_from_utf8(root.toStdString()));
    std::string id;
    if (!canonical.empty()) {
        id = msf::IndexManager::folderId(canonical);
    } else {
        std::ostringstream out;
        out << std::hex << std::hash<std::string>{}(root.toStdString());
        id = "unresolved-" + out.str();
    }
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"));
    const QString nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    return QDir(appDir).filePath(QStringLiteral("TestMode/") + QString::fromStdString(id) +
                                  QLatin1Char('-') + stamp + QLatin1Char('-') + nonce);
}

void BackendSession::startScan(const QString& root, const QString& appDir, int distance,
                              int cpu, int gpuPercent, bool gpuEnabled,
                              bool scanImages, bool scanVideos,
                              const QSet<QString>& ignored, bool detailedLog,
                              int cpuMode, int strategy, bool testMode, int analyzeMode) {
    teardownWorker();
    const QString effectiveAppDir = testMode ? testScratchAppDir(appDir, root) : appDir;
    lastAppDir_ = effectiveAppDir;
    lastRoot_ = root;
    thread_ = new QThread(this);
    // P4: the two-axis policy crosses with identical meaning (no Backend
    // reinterpretation). CPU_ONLY forces the GPU lane off here, backend-side;
    // GPU_MAX keeps the scheduler auto-split (no share boost exists in code).
    const bool gpuEff = gpuEnabled && strategy != 1;
    worker_ = new ScanWorker(root, effectiveAppDir, distance, cpu, gpuPercent,
                             gpuEff, scanImages, scanVideos);
    const auto mode = (cpuMode >= 1 && cpuMode <= 5)
                          ? static_cast<msf::ResourceMode>(cpuMode)
                          : msf::ResourceMode::Custom;
    worker_->setResourceMode(mode);
    worker_->setAnalyzeMode(msf::analyzeModeFromInt(analyzeMode));
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
    thumbs_.close();
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
    thumbs_.prune(); // drop disk thumbs whose files left the index
    emit finished(msg);
    emit stateChanged(QStringLiteral("READY"));
}

void BackendSession::onFailed(QString msg) {
    scanning_ = false;
    drainToGui();
    thumbs_.prune();
    emit failed(msg);
    emit stateChanged(QStringLiteral("READY"));
}

void BackendSession::pushStatusSnapshot() {
    if (!worker_) return;
    // Same thread-safety profile as the former MainWindow tick reads: the
    // engine exposes these counters for concurrent observation.
    const auto& engine = worker_->scanEngine();
    lastStatus_.analyzed = (qulonglong)engine.analyzedCount();
    lastStatus_.unchanged = (qulonglong)engine.unchangedCount();
    lastStatus_.gpuActive = engine.gpuActive();
    lastStatus_.gpuAvailable = worker_->gpuAvailable();
    lastStatus_.gpuDone = worker_->gpuDone();
    emit statusSnapshot(lastStatus_);
}

msf::JpegThumb BackendSession::requestThumb(const QString& path, bool isVideo, int desiredMaxDim) {
    msf::JpegThumb miss;
    if (!worker_) return miss;
    thumbs_.ensureOpen(lastAppDir_.toStdString(), lastRoot_.toStdString());
    const std::string p = path.toStdString();
    auto engineFetch = [&]() -> msf::RawArt {
        msf::RawArt a;
        if (!worker_) return a;
        const auto& engine = worker_->scanEngine();
        if (isVideo) {
            std::vector<unsigned char> px;
            if (engine.getVideoThumb(p, px) && px.size() >= (std::size_t)48 * 48) {
                a.bgra = std::move(px);
                a.width = 48;
                a.height = 48;
                a.gray = true;
                a.ok = true;
            }
            return a;
        }
        std::vector<unsigned char> px;
        int pw = 0, ph = 0;
        if (!engine.getColorThumb(p, pw, ph, px) || pw <= 0 || ph <= 0 ||
            px.size() != (std::size_t)pw * ph * 4)
            return a;
        a.bgra = std::move(px);
        a.width = pw;
        a.height = ph;
        a.ok = true;
        return a;
    };
    msf::StoredThumb s = thumbs_.fetch(p, desiredMaxDim, engineFetch);
    if (!s.ok) return miss;
    msf::JpegThumb out;
    out.bytes = std::move(s.jpeg);
    out.width = s.width;
    out.height = s.height;
    out.ok = true;
    return out;
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

void BackendSession::updateResourcePolicy(int cpuMode, int cpuPercent, int gpuPercent,
                                          int strategy, bool gpuEnabled) {
    // Monitor applies immediately. The engine side applies at the next scan
    // (worker pool sizing is only safe at scan start; the scheduler reads a
    // per-scan hardware snapshot). partial=true tells the GUI exactly that.
    msf::ResourcePolicy policy;
    policy.mode = (cpuMode >= 1 && cpuMode <= 5)
                      ? static_cast<msf::ResourceMode>(cpuMode)
                      : msf::ResourceMode::Custom;
    policy.cpuPercent = cpuPercent;
    policy.gpuPercent = gpuPercent;
    policy.gpuEnabled = gpuEnabled && strategy != 1;
    if (monitor_) monitor_->setPolicy(policy);
    emit policyApplied(scanning_, (int)policy.mode, cpuPercent, gpuPercent,
                       strategy, policy.gpuEnabled);
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
