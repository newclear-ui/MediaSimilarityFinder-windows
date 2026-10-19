// BackendSession implementation (P3a: 0.9.4.69). See backend_session.h.
#include "backend_session.h"

#include <sqlite3.h>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QPair>
#include <QUuid>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "backend_log.h"
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
    // 0.9.4.84: the engine counters must move even when no match streams (an
    // incremental rescan of an unchanged folder). Poll them while scanning.
    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(500);
    connect(statusTimer_, &QTimer::timeout, this, &BackendSession::pushStatusSnapshot);
}

BackendSession::~BackendSession() {
    shutdown();
}

void BackendSession::teardownWorker() {
    scanning_ = false;
    if (statusTimer_) statusTimer_->stop();
    stopLoad(); // a pending Load must finish before the session is reused/closed
    if (thread_) {
        thread_->quit();
        thread_->wait();
        delete worker_;
        delete thread_;
        thread_ = nullptr;
        worker_ = nullptr;
    }
}

void BackendSession::stopLoad() {
    if (loadThread_) {
        loadThread_->wait();
        loadThread_->deleteLater();
        loadThread_ = nullptr;
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


// 0.9.4.135: run one SQL statement, collecting the SQLite error text.
static bool execSqlQuiet(sqlite3* db, const char* sql, std::string* err) {
    char* e = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &e) != SQLITE_OK) {
        if (err && e) *err = e;
        sqlite3_free(e);
        return false;
    }
    return true;
}

// 0.9.4.135: Test Mode result adoption. Copies files, matches, thumbs,
// content hashes, traces and meta from the scratch scan database into the main
// index database inside ONE transaction (ATTACH + DELETE/INSERT .. SELECT).
// The main index is either fully replaced by the completed test-scan state or
// left untouched (SQLite rolls back on any failure). No file swapping, so the
// WAL sidecars of either database are handled by SQLite itself.
static bool adoptIndexDatabase(const msf::IndexPaths& main, const msf::IndexPaths& scratch,
                               QString* detail) {
    sqlite3* db = nullptr;
    const std::string mainPath = msf::path_to_utf8(main.database);
    if (sqlite3_open(mainPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        *detail = QStringLiteral("main index open failed");
        return false;
    }
    sqlite3_busy_timeout(db, 5000);
    std::string err;
    std::string srcPath = msf::path_to_utf8(scratch.database);
    std::string attach = "ATTACH DATABASE '";
    for (char ch : srcPath) {
        if (ch == '\'') attach += "''''";
        else attach += ch;
    }
    attach += "' AS src;";
    bool ok = execSqlQuiet(db, attach.c_str(), &err);
    if (ok) ok = execSqlQuiet(db, "BEGIN IMMEDIATE;", &err);
    // Wholesale adoption: every table the completed test scan produced replaces
    // the main index content (pairs and thumbs included, so the adopted state
    // is self-consistent). file_trace is diagnostics but still belongs to the
    // rows being replaced.
    for (const char* sql : {
             "DELETE FROM files; INSERT INTO files SELECT * FROM src.files;",
             "DELETE FROM matches; INSERT INTO matches SELECT * FROM src.matches;",
             "DELETE FROM thumbs; INSERT INTO thumbs SELECT * FROM src.thumbs;",
             "DELETE FROM content_hash; INSERT INTO content_hash SELECT * FROM src.content_hash;",
             "DELETE FROM file_trace; INSERT INTO file_trace SELECT * FROM src.file_trace;",
             "DELETE FROM meta; INSERT INTO meta SELECT * FROM src.meta;"}) {
        if (!ok) break;
        ok = execSqlQuiet(db, sql, &err);
    }
    if (!ok) {
        execSqlQuiet(db, "ROLLBACK;", nullptr);
        *detail = QString::fromStdString(err.empty() ? std::string("adopt failed") : err);
    } else if (!execSqlQuiet(db, "COMMIT;", &err)) {
        execSqlQuiet(db, "ROLLBACK;", nullptr);
        *detail = QString::fromStdString(err);
        ok = false;
    }
    execSqlQuiet(db, "DETACH DATABASE src;", nullptr);
    sqlite3_close(db);
    return ok;
}
void BackendSession::startScan(const QString& root, const QString& appDir, int distance,
                              int cpu, int gpuPercent, bool gpuEnabled,
                              bool scanImages, bool scanVideos,
                              const QSet<QString>& ignored, bool detailedLog,
                              int cpuMode, int strategy, bool testMode, int analyzeMode,
                              bool fastRescan) {
    teardownWorker();
    const QString effectiveAppDir = testMode ? testScratchAppDir(appDir, root) : appDir;
    lastAppDir_ = effectiveAppDir;
    lastRoot_ = root;
    // 0.9.4.135: Test Mode diff/adopt context for this scan (empty scratch
    // dir = production scan, no diff is reported).
    testModePending_ = false;
    testScratchDir_ = testMode ? effectiveAppDir : QString();
    testMainAppDir_ = appDir;
    testScanImages_ = scanImages;
    testScanVideos_ = scanVideos;
    testIgnored_ = ignored;
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
    worker_->setFastRescan(fastRescan);
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
    connect(worker_, &ScanWorker::revalidateProgress, this, &BackendSession::revalidateProgress);
    connect(worker_, &ScanWorker::results, this, &BackendSession::onResults);
    connect(worker_, &ScanWorker::telemetryReady, this, &BackendSession::telemetryReady);
    connect(worker_, &ScanWorker::finished, this, &BackendSession::onFinished);
    connect(worker_, &ScanWorker::failed, this, &BackendSession::onFailed);
    connect(worker_, &ScanWorker::finished, thread_, &QThread::quit);
    connect(worker_, &ScanWorker::failed, thread_, &QThread::quit);
    thread_->start();
    scanning_ = true;
    if (statusTimer_) statusTimer_->start();
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
    if (statusTimer_) statusTimer_->stop();
    drainToGui();
    thumbs_.prune(); // drop disk thumbs whose files left the index
    emit finished(msg);
    // 0.9.4.135: a completed Test Mode scan compares itself with the main index
    // and offers adoption (the main index is untouched until the user says Yes).
    if (!testScratchDir_.isEmpty() && !msg.startsWith(QStringLiteral("CANCELLED")))
        publishTestDiff();
    emit stateChanged(QStringLiteral("READY"));
}

void BackendSession::onFailed(QString msg) {
    scanning_ = false;
    if (statusTimer_) statusTimer_->stop();
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
    // 0.9.4.93: no worker required. A load-only view ("불러오기") has no scan
    // worker, and the store serves from the persistent disk thumbnails (or a
    // WIC/FFmpeg decode) on its own. engineFetch below stays optional.
    if (lastRoot_.isEmpty() || lastAppDir_.isEmpty()) return miss;
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

void BackendSession::loadIndex(const QString& root, const QString& appDir) {
    // Load-only (0.9.4.93): no walk, no analysis. Open the root's index, read
    // the stored files and pairs, and report them through the same
    // results()/quickLoaded() signals a scan uses, so the GUI reuses its
    // existing population path (onResults + onQuickLoaded).
    // 0.9.4.115 hotfix: run the work on a worker thread. It includes the badge
    // confirmation (0.9.4.113) that reads candidate files; executing that on
    // the IPC event thread silenced HEALTH past the supervisor's health window
    // and the backend was killed mid-load (exit code 0xf291 = 62097) on real
    // datasets. The signals cross threads queued, exactly like scan answers.
    stopLoad();
    loadThread_ = QThread::create([this, root, appDir] { runLoadIndex(root, appDir); });
    connect(loadThread_, &QThread::finished, this, [this] {
        if (loadThread_) { loadThread_->deleteLater(); loadThread_ = nullptr; }
    });
    loadThread_->start();
}

void BackendSession::runLoadIndex(const QString& root, const QString& appDir) {
    // 0.9.4.115: test-only seam proving the event thread stays responsive
    // (MSF_TEST_SLOW_LOAD_MS, set before the backend spawns).
    if (qEnvironmentVariableIsSet("MSF_TEST_SLOW_LOAD_MS"))
        QThread::msleep(qEnvironmentVariableIntValue("MSF_TEST_SLOW_LOAD_MS"));
    msf::MediaSearchEngine engine;
    if (!engine.openIndexForRoot(root.toStdString(), appDir.toStdString())) {
        emit failed(QStringLiteral("load index failed: %1").arg(root));
        return;
    }
    lastRoot_ = root;
    lastAppDir_ = appDir;
    // 0.9.4.116: drop stale rows first so the Load list never shows deleted
    // files as phantom members (auto-repair; an offline root is a no-op).
    engine.cleanupMissingOnDisk(root.toStdString());
    const int loaded = publishIndexContent(engine);
    engine.close();
    emit quickLoaded(loaded);
}

// 0.9.4.134: shared finish for Load and Re-verify. The engine's loadMatches
// already carries the content-identity flag (quick-hash candidate gate +
// full-content confirmation); results() owns the file list and pair rows.
int BackendSession::publishIndexContent(msf::MediaSearchEngine& engine) {
    const auto states = engine.storedFiles();
    const auto matches = engine.loadMatches();
    QVector<GuiFile> files;
    files.reserve((int)states.size());
    for (const auto& f : states) {
        GuiFile g;
        g.path = QString::fromStdString(f.path);
        g.size = (qulonglong)f.size;
        g.fpHex = QString("%1").arg((qulonglong)f.fingerprint, 16, 16, QChar('0'));
        g.duration = f.duration;
        files.push_back(g);
    }
    QStringList rows;
    rows.reserve((int)matches.size());
    for (const auto& m : matches) {
        const QString lp = QString::fromStdString(m.leftPath);
        const QString rp = QString::fromStdString(m.rightPath);
        rows << (lp + "\t" + rp + "\t" + QString::number(m.percent, 'f', 1)
                 + "\t" + (m.contentIdentical ? "1" : "0"));
    }
    emit results(files, rows);
    return (int)matches.size();
}

void BackendSession::reverifyIndex(const QString& root, const QString& appDir) {
    stopLoad();
    loadThread_ = QThread::create([this, root, appDir] { runReverifyIndex(root, appDir); });
    connect(loadThread_, &QThread::finished, this, [this] {
        if (loadThread_) { loadThread_->deleteLater(); loadThread_ = nullptr; }
    });
    loadThread_->start();
}

void BackendSession::runReverifyIndex(const QString& root, const QString& appDir) {
    msf::MediaSearchEngine engine;
    if (!engine.openIndexForRoot(root.toStdString(), appDir.toStdString())) {
        emit failed(QStringLiteral("reverify index failed: %1").arg(root));
        return;
    }
    lastRoot_ = root;
    lastAppDir_ = appDir;
    engine.cleanupMissingOnDisk(root.toStdString());
    msf::MediaSearchEngine::ReverifyStats st;
    msf::ScanControl ctl;
    if (!engine.reverifyStoredDuplicates(&ctl, &st)) {
        engine.close();
        emit failed(QStringLiteral("reverify cancelled or failed"));
        return;
    }
    // 0.9.4.134: results first, counters after, so the completion status text
    // (rendered by the GUI from reverifyFinished) is the final message.
    publishIndexContent(engine);
    engine.close();
    emit reverifyFinished((qulonglong)st.members, (qulonglong)st.checked,
                          (qulonglong)st.changed, (qulonglong)st.reanalyzed,
                          (qulonglong)st.pairsBefore, (qulonglong)st.pairsAfter);
}

bool BackendSession::forgetPaths(const QStringList& paths) {
    // 0.9.4.105 (F): remove rows for paths that left the filesystem. The scan
    // worker owns the index while scanning, so refuse then (a concurrent
    // writer would fight its transaction; the next scan reconciles anyway).
    if (paths.isEmpty() || scanning_) return false;
    if (lastRoot_.isEmpty() || lastAppDir_.isEmpty()) return false;
    msf::IndexPaths ip;
    if (!msf::IndexManager::resolve(msf::path_from_utf8(lastAppDir_.toStdString()),
                                    msf::path_from_utf8(lastRoot_.toStdString()), ip))
        return false;
    msf::Database db;
    if (!db.open(msf::path_to_utf8(ip.database)) || !db.initialize()) {
        db.close();
        return false;
    }
    std::vector<std::string> v;
    v.reserve((std::size_t)paths.size());
    for (const auto& p : paths) v.push_back(p.toStdString());
    const bool ok = db.forget(v);
    db.close();
    return ok;
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




// 0.9.4.135: compare the completed Test Mode scan's index with the main index.
// Counts only -- the UI asks Yes/No and adoption replaces the whole state.
void BackendSession::publishTestDiff() {
    msf::IndexPaths mainPaths, scratchPaths;
    const auto rootPath = msf::path_from_utf8(lastRoot_.toStdString());
    if (!msf::IndexManager::resolve(msf::path_from_utf8(testMainAppDir_.toStdString()), rootPath, mainPaths) ||
        !msf::IndexManager::resolve(msf::path_from_utf8(testScratchDir_.toStdString()), rootPath, scratchPaths)) {
        return;
    }
    msf::Database mainDb, scratchDb;
    if (!mainDb.open(msf::path_to_utf8(mainPaths.database)) || !mainDb.initialize()) return;
    if (!scratchDb.open(msf::path_to_utf8(scratchPaths.database)) || !scratchDb.initialize()) return;
    const auto mainRows = mainDb.all();
    const auto scratchRows = scratchDb.all();
    const auto mainPairs = mainDb.loadMatches();
    const auto scratchPairs = scratchDb.loadMatches();
    mainDb.close();
    scratchDb.close();
    std::unordered_map<std::string, const msf::FileState*> mainByPath;
    mainByPath.reserve(mainRows.size() * 2 + 1);
    for (const auto& x : mainRows) mainByPath.emplace(x.path, &x);
    std::unordered_set<std::string> scratchSeen;
    scratchSeen.reserve(scratchRows.size() * 2 + 1);
    qulonglong changed = 0, added = 0, removed = 0;
    for (const auto& x : scratchRows) {
        scratchSeen.insert(x.path);
        const auto it = mainByPath.find(x.path);
        if (it == mainByPath.end()) { ++added; continue; }
        const auto& m = *it->second;
        if (m.fingerprint != x.fingerprint || m.mirrorFingerprint != x.mirrorFingerprint ||
            m.crop4x3 != x.crop4x3 || m.crop1x1 != x.crop1x1 || m.crop9x16 != x.crop9x16 ||
            m.mirrorCrop4x3 != x.mirrorCrop4x3 || m.mirrorCrop1x1 != x.mirrorCrop1x1 ||
            m.mirrorCrop9x16 != x.mirrorCrop9x16 || m.size != x.size)
            ++changed;
    }
    for (const auto& m : mainRows) {
        if (scratchSeen.find(m.path) != scratchSeen.end()) continue;
        // The scratch scan only produced rows inside its scope and outside the
        // ignore list; anything else in the main index is not "removed".
        const bool vid = m.kind == (int)msf::MediaKind::Video;
        if (vid ? !testScanVideos_ : !testScanImages_) continue;
        if (testIgnored_.contains(QString::fromStdString(m.path))) continue;
        ++removed;
    }
    const auto pairKeyOf = [](const msf::StoredMatch& m) {
        return m.left <= m.right ? m.left + '\x1f' + m.right : m.right + '\x1f' + m.left;
    };
    std::unordered_set<std::string> mainPairKeys, scratchPairKeys;
    mainPairKeys.reserve(mainPairs.size() * 2 + 1);
    scratchPairKeys.reserve(scratchPairs.size() * 2 + 1);
    for (const auto& p : mainPairs) mainPairKeys.insert(pairKeyOf(p));
    qulonglong pairDiff = 0;
    for (const auto& p : scratchPairs) {
        const std::string k = pairKeyOf(p);
        scratchPairKeys.insert(k);
        if (mainPairKeys.find(k) == mainPairKeys.end()) ++pairDiff;
    }
    for (const auto& k : mainPairKeys)
        if (scratchPairKeys.find(k) == scratchPairKeys.end()) ++pairDiff;
    const bool differs = changed + added + removed + pairDiff > 0;
    testModePending_ = differs;
    emit testModeDiff(changed, added, removed, pairDiff,
                      (qulonglong)mainPairs.size(), (qulonglong)scratchPairs.size());
}

// 0.9.4.135: user said Yes -- adopt the completed Test Mode result.
void BackendSession::applyTestResult() {
    if (scanning_ || loadThread_ || applyThread_ || !testModePending_) {
        // 0.9.4.135: the refusal reason rides the backend log ([sv]) so a rare
        // timing refusal in automation is diagnosable without a debugger.
        const QString why = QStringLiteral("busy scanning=%1 load=%2 apply=%3 pending=%4")
                                .arg(scanning_ ? 1 : 0).arg(loadThread_ ? 1 : 0)
                                .arg(applyThread_ ? 1 : 0).arg(testModePending_ ? 1 : 0);
        msf::backendLogLine(("testApply refused " + why).toStdString());
        emit testApplied(false, why);
        return;
    }
    if (monitorRunning()) {
        emit testApplied(false, QStringLiteral("monitor running"));
        return;
    }
    // Release the finished test worker's scratch database handles first.
    teardownWorker();
    applyThread_ = QThread::create([this] { runApplyTestResult(); });
    connect(applyThread_, &QThread::finished, this, [this] {
        if (applyThread_) { applyThread_->deleteLater(); applyThread_ = nullptr; }
    });
    applyThread_->start();
}

void BackendSession::runApplyTestResult() {
    if (testScratchDir_.isEmpty() || testMainAppDir_.isEmpty()) {
        emit testApplied(false, QStringLiteral("no test context"));
        return;
    }
    msf::IndexPaths mainPaths, scratchPaths;
    const auto rootPath = msf::path_from_utf8(lastRoot_.toStdString());
    if (!msf::IndexManager::resolve(msf::path_from_utf8(testMainAppDir_.toStdString()), rootPath, mainPaths) ||
        !msf::IndexManager::resolve(msf::path_from_utf8(testScratchDir_.toStdString()), rootPath, scratchPaths)) {
        emit testApplied(false, QStringLiteral("index resolve failed"));
        return;
    }
    // Create/initialize the main index tables when this root has never been
    // scanned in production, so the adopt transaction always has targets.
    {
        msf::Database mainDb;
        if (!mainDb.open(msf::path_to_utf8(mainPaths.database)) || !mainDb.initialize()) {
            emit testApplied(false, QStringLiteral("main index open failed"));
            return;
        }
        mainDb.close();
    }
    QString detail;
    const bool ok = adoptIndexDatabase(mainPaths, scratchPaths, &detail);
    msf::backendLogLine(QString("testApply ok=%1 detail=%2").arg(ok ? 1 : 0).arg(detail).toStdString());
    if (ok) {
        // Future thumbnails/forget calls belong to the production index again.
        lastAppDir_ = testMainAppDir_;
        testModePending_ = false;
        thumbs_.close();
        thumbs_.ensureOpen(lastAppDir_.toStdString(), lastRoot_.toStdString());
        thumbs_.prune();
    }
    emit testApplied(ok, detail);
}


