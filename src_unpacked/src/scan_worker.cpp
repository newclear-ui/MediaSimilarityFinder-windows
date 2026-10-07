// ScanWorker implementation (P3a: 0.9.4.69). Moved verbatim from
// gui/mainwindow.cpp; see scan_worker.h. Two substitutions versus the
// original: the videoStats line goes through msf::backendLogLine (same file,
// same shape), and isVideoExt is a file-local copy (the GUI keeps its own).
#include "scan_worker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <stdexcept>

#include "backend_log.h"
#include "gpu_backend.h"
#include "index_manager.h"
#include "path_utils.h"
#include "scanner.h"

static bool isVideoExt(const QString& path) {
  const QString e = QFileInfo(path).suffix().toLower();
  return e == "mp4" || e == "mkv" || e == "avi" || e == "mov" || e == "webm" || e == "m4v" || e == "wmv";
}

ScanWorker::ScanWorker(QString root, QString appDir, int distance, int cpu, int gpu, bool gpuEnabled,
                     bool scanImages, bool scanVideos)
  : root_(std::move(root)), appDir_(std::move(appDir)), distance_(distance),
    cpu_(cpu), gpu_(gpu), gpuEnabled_(gpuEnabled), scanImages_(scanImages), scanVideos_(scanVideos) {
  qRegisterMetaType<QVector<GuiFile>>();
  gpuAvail_ = msf::GpuBackend().available();
}

void ScanWorker::run() {
  try {
    // Test-only fault injection (0.9.4.62): drives the catch-all below
    // deterministically for the crash-handler regression test. Production
    // code never sets this variable, so the branch is dead otherwise.
    if (qEnvironmentVariableIsSet("MSF_TEST_THROW_NONSTD")) throw 42;
    engine_.setResourcePolicy(msf::make_policy(resourceMode_, cpu_, gpu_));
    auto policy = engine_.resourcePolicy(); policy.gpuEnabled = gpuEnabled_; engine_.setResourcePolicy(policy);
    control_.scanImages = scanImages_; control_.scanVideos = scanVideos_;
    control_.telemetryEnabled = detailedLogEnabled_;
    control_.walkerQueueCapacity = walkerCapOverride_;
    control_.buildVersion = QCoreApplication::applicationVersion().toStdString();
    if (!engine_.openIndexForRoot(root_.toStdString(), appDir_.toStdString()))
      throw std::runtime_error("Portable index open failed");
    {
      msf::IndexPaths paths;
      std::string excluded;
      if (msf::IndexManager::resolve(msf::path_from_utf8(appDir_.toStdString()),
                                     msf::path_from_utf8(root_.toStdString()), paths))
        excluded = msf::path_to_utf8(paths.directory.parent_path());
      const qulonglong total = msf::Scanner().count(root_.toStdString(), excluded,
                                                    scanImages_, scanVideos_,
                                                    control_.ignoredPaths, &control_.cancel);
      emit targetCount(total);
    }
    // Engine-version gate: pairs stored by an older verdict generation are
    // re-checked with the current logic (no rescan) before anything displays
    // them. Drops old false positives, keeps the rest, stamps the version.
    // Cancelled here means: stop before touching results.
    {
      int kept = 0, dropped = 0;
      msf::TelemetryConfig bcfg;
      bcfg.root = root_.toStdString();
      bcfg.build = QCoreApplication::applicationVersion().toStdString();
      bcfg.engine = msf::MediaSearchEngine::kEngineVersion;
      bcfg.db = msf::Database::kDatabaseVersion;
      bcfg.distance = (unsigned)distance_;
      bcfg.scanImages = scanImages_; bcfg.scanVideos = scanVideos_;
      bcfg.gpuEnabled = gpuEnabled_; bcfg.detail = detailedLogEnabled_;
      engine_.beginTelemetry(bcfg, detailedLogEnabled_);
      const qint64 revT0 = QDateTime::currentMSecsSinceEpoch();
      if (!engine_.revalidateMatches(&control_, &kept, &dropped)) {
        engine_.abortTelemetry();
        if (detailedLogEnabled_ && engine_.hasTelemetry())
          emit telemetryReady(QString::fromStdString(engine_.telemetryJson()));
        emit finished(QString("CANCELLED|0|0")); return;
      }
      control_.revalidateMs = (double)(QDateTime::currentMSecsSinceEpoch() - revT0);
      if (kept + dropped > 0) emit revalidated(kept, dropped);
    }
    // Quick load: the scan button restores the stored duplicate groups before
    // analyzing anything, so a repeat scan of the same folder shows previous
    // results immediately, then appends only files that changed meanwhile.
    {
      const auto stored = engine_.loadMatches();
      int loaded = 0;
      for (const auto& m : stored) {
        const QString l = QString::fromStdString(m.leftPath), r = QString::fromStdString(m.rightPath);
        const bool video = isVideoExt(l) || isVideoExt(r);
        const LiveMatch lm{l, r, m.percent, video ? 2 : 1};
        allMatches_.push_back(lm);
        if ((video && !scanVideos_) || (!video && !scanImages_)) continue;
        if (control_.ignoredPaths.find(m.leftPath) != control_.ignoredPaths.end() ||
            control_.ignoredPaths.find(m.rightPath) != control_.ignoredPaths.end()) continue;
        { QMutexLocker g(&pendingMutex_); pending_.push_back(lm); }
        ++loaded;
      }
      if (loaded > 0) { emit quickLoaded(loaded); emit matchesArrived(); }
    }
    // Progress signals arrive once per analyzed file; a fast Maximum scan would
    // flood the GUI event loop (setText per file) and freeze the window —
    // no pause/cancel/move possible. Throttle display updates to ~7Hz; the
    // latest values are kept and flushed when the scan returns, so pause,
    // cancel, and close stay responsive no matter the scan speed.
    lastProgMs_ = 0; lastProgDone_ = 0; lastProgTotal_ = 0; lastProgPath_.clear();
    lastListMs_ = 0; lastListN_ = 0; lastWalkedMs_ = 0; lastWalkedN_ = 0;
    lastFpMs_ = 0;
    // Fingerprint-phase live progress. Same 150 ms throttle as the walk
    // callbacks: the fingerprint can hash thousands of files per second.
    // Separate signal so the walk/analysis percent math is untouched.
    control_.fingerprintProgress = [this](std::size_t n, std::uint64_t b, const std::string& path) {
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (now - lastFpMs_ > 150) {
        lastFpMs_ = now;
        emit fingerprintProgress((qulonglong)n, (qulonglong)b, QString::fromStdString(path));
      }
    };
    control_.progress = [this](std::size_t done, std::size_t total, const std::string& path) {
      lastProgDone_ = done; lastProgTotal_ = total; lastProgPath_ = path;
      gpuDone_.store((qulonglong)engine_.gpuImagesProcessed());
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (now - lastProgMs_ > 150) {
        lastProgMs_ = now;
        emit progress(total ? int(done * 100 / total) : 100, QString::fromStdString(path));
        emit progressCount((qulonglong)done, (qulonglong)total);
      }
    };
    control_.listing = [this](std::size_t n) {
      lastListN_ = n;
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (now - lastListMs_ > 150) { lastListMs_ = now; emit listingProgress(n); }
    };
    control_.walked = [this](std::size_t n) {
      lastWalkedN_ = n;
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (now - lastWalkedMs_ > 150 || n == 0) { lastWalkedMs_ = now; emit walkedCount((qulonglong)n); }
    };
    control_.onMatch = [this](const msf::SearchMatch& m) {
      { QMutexLocker g(&pendingMutex_);
        const QString l = QString::fromStdString(m.leftPath), r = QString::fromStdString(m.rightPath);
        pending_.push_back(LiveMatch{l, r, m.percent, isVideoExt(l) ? 2 : 1});
      }
      const QString l = QString::fromStdString(m.leftPath), r = QString::fromStdString(m.rightPath);
      allMatches_.push_back(LiveMatch{l, r, m.percent, isVideoExt(l) ? 2 : 1});
      ++matchesSinceSave_;
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (now - lastEmitMs_ > 200) { lastEmitMs_ = now; emit matchesArrived(); }
      // Incremental checkpoint: persist the FULL accumulated set (loaded + new,
      // including pairs whose files are currently missing from disk) so that a
      // kill, crash, or early close still leaves every match found so far in the
      // index. Time-gated from the very first match (no count gate), so even a
      // scan stopped after a handful of matches keeps them; the 5s cadence
      // bounds the cost on million-match scans. Must stay wholesale (never a
      // partial set): saveMatches deletes rows absent from the saved set.
      if (matchesSinceSave_ > 0 && now - lastSaveMs_ > 5000) {
        lastSaveMs_ = now; matchesSinceSave_ = 0;
        persistMatchesSnapshot();
      }
    };
    // Streaming-only delivery: on million-match scans, retaining every match
    // (two path strings each) costs hundreds of MB. The GUI accumulates
    // groups incrementally from onMatch and needs no retained vector.
    control_.retainMatches = false;
    auto r = engine_.scan(root_.toStdString(), unsigned(distance_), &control_);
    const QString telemetryJson = engine_.hasTelemetry() ? QString::fromStdString(engine_.telemetryJson()) : QString();
    gpuDone_.store((qulonglong)engine_.gpuImagesProcessed());
    // Flush the throttled progress display with the final counts.
    {
      const std::size_t done = lastProgDone_, total = lastProgTotal_;
      emit progress(total ? int(done * 100 / total) : 100, QString::fromStdString(lastProgPath_));
      emit progressCount((qulonglong)done, (qulonglong)total);
      if (lastListN_ > 0) emit listingProgress(lastListN_);
    }
    // Final persist of the accumulated match set (loaded + new, including pairs
    // whose files are currently missing from disk). Wholesale replacement keeps
    // every pair ever found, so unfinished work on those files resumes on the
    // next scan of the same folder; a partial set on cancel keeps the last
    // completed checkpoint, matching the scan-side semantics.
    persistMatchesSnapshot();
    // P4: the accumulated set has been persisted wholesale; retaining it
    // would pin every match's strings until the next scan (hundreds of MB on
    // match storms). Release it — nothing reads allMatches_ after this point
    // (results come from r.matches; takePending() is untouched).
    allMatches_.clear();
    allMatches_.shrink_to_fit();
    // Diagnostic counters (ChatGPT step 1): where a video-heavy scan with few
    // results loses its pairs. Log-only (the finished message below is parsed
    // positionally and must not change shape).
    msf::backendLogLine(QString("videoStats indexedVideos=%1 pairs=%2 temporal=%3 matches=%4")
                .arg(r.indexedVideos).arg(r.videoCandidatePairs)
                .arg(r.videoTemporalChecks).arg(r.videoMatches).toStdString());
    { QMutexLocker g(&pendingMutex_); if (!pending_.isEmpty()) emit matchesArrived(); }
    if (control_.cancel.load()) { if (!telemetryJson.isEmpty()) emit telemetryReady(telemetryJson); emit finished(QString("CANCELLED|%1|%2").arg(r.scanned).arg(r.analyzed)); return; }
    const auto& fs = engine_.files();
    QVector<GuiFile> files; files.reserve((int)fs.size());
    for (const auto& f : fs) {
      GuiFile g;
      g.path = QString::fromStdString(f.path);
      g.size = (qulonglong)f.size;
      g.fpHex = QString("%1").arg((qulonglong)f.fingerprint, 16, 16, QChar('0'));
      g.duration = f.duration;
      files.push_back(g);
    }
    QStringList matches;
    for (const auto& m : r.matches)
      matches << (QString::fromStdString(m.leftPath) + "\t" + QString::fromStdString(m.rightPath)
                  + "\t" + QString::number(m.percent, 'f', 1));
    emit results(files, matches);
    if (!telemetryJson.isEmpty()) emit telemetryReady(telemetryJson);
    // Analysis failures are a settled state, not silently dropped files. Surface
    // them in the completion message so a scan that skipped nothing still says so.
    emit finished(QString("Scan complete: %1 files, %2 analyzed, %3 candidates, %4 groups%5")
                      .arg(r.scanned).arg(r.analyzed).arg(r.candidates).arg(r.groups)
                      .arg(r.failed ? QString(", %1 could not be analyzed").arg(r.failed) : QString())
                  + QString("|%1|%2|%3|%4|%5").arg(r.scanned).arg(r.analyzed).arg(r.unchanged).arg(r.groups).arg(r.candidates));
  } catch (const std::exception& e) {
    // A failed scan must not discard what it already found: checkpoint first
    // so the next scan of the same folder quick-loads the partial results.
    // (0.9.4.65) Neither step may throw out of this handler: run() is a Qt
    // slot, so an escaping exception crosses Qt internals into terminate() ->
    // abort() (the 0xC0000409 signature, proven by the 2026-10-07 dump whose
    // fault stack sits in this handler's persist path). Persist and report
    // are attempted independently; each swallows its own failure.
    try { persistMatchesSnapshot(); } catch (...) {}
    try { emit failed(e.what()); } catch (...) {}
    // P4: same release as the normal path (persist already checkpointed).
    // shrink_to_fit guarded: this handler must never throw (0.9.4.65 rule).
    allMatches_.clear();
    try { allMatches_.shrink_to_fit(); } catch (...) {}
  } catch (...) {
    // Fail-fast converted to a recorded failure (0.9.4.62): a non-standard
    // exception used to terminate the whole process with no record (the
    // 0xC0000409 signature seen in real crashes). Persist partial matches
    // and report failed instead. SEH access violations still crash: MSVC
    // builds without /EHa do not unwind those through catch(...), so real
    // memory corruption keeps failing fast instead of being masked.
    // (0.9.4.65) Same no-throw rule as above: the handler itself throwing
    // re-enters terminate() -> abort() with no record.
    try { persistMatchesSnapshot(); } catch (...) {}
    try { emit failed("unhandled non-standard exception in scan worker"); } catch (...) {}
    allMatches_.clear();
    try { allMatches_.shrink_to_fit(); } catch (...) {}
  }
}
void ScanWorker::persistMatchesSnapshot() {
  // Test-only fault injection (0.9.4.65): makes the failure-handler path
  // throw deterministically, proving the handler itself never lets an
  // exception escape the worker slot (terminate -> abort). Production code
  // never sets this variable, so the branch is dead otherwise.
  if (qEnvironmentVariableIsSet("MSF_TEST_THROW_PERSIST")) throw std::runtime_error("MSF_TEST_THROW_PERSIST");
  std::vector<msf::SearchMatch> all; all.reserve((std::size_t)allMatches_.size());
  for (const auto& m : allMatches_) all.push_back({m.left.toStdString(), m.right.toStdString(), m.percent});
  engine_.saveMatches(all);
}
void ScanWorker::pause() { control_.pause.store(true); }
void ScanWorker::resume() { control_.pause.store(false); }
void ScanWorker::cancel() { control_.cancel.store(true); control_.pause.store(false); }
void ScanWorker::setIgnored(const QSet<QString>& s) {
  control_.ignoredPaths.clear();
  for (const auto& p : s) {
    const QString clean = QDir::cleanPath(p);
    std::error_code ec;
    const auto native = msf::path_from_utf8(clean.toUtf8().toStdString());
    auto absolute = std::filesystem::absolute(native, ec);
    if (ec) absolute = native;
    control_.ignoredPaths.insert(msf::path_to_utf8(absolute.lexically_normal()));
  }
}
QVector<LiveMatch> ScanWorker::takePending() {
  QMutexLocker g(&pendingMutex_);
  QVector<LiveMatch> out = pending_; pending_.clear(); return out;
}
