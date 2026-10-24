// ScanWorker implementation (P3a: 0.9.4.69). Moved verbatim from
// gui/mainwindow.cpp; see scan_worker.h. Two substitutions versus the
// original: the videoStats line goes through msf::backendLogLine (same file,
// same shape), and isVideoExt is a file-local copy (the GUI keeps its own).
#include "scan_worker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backend_log.h"
#include "gpu_backend.h"
#include "index_janitor.h"
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
    // Index janitor (0.9.4.79): sweep stale per-root indexes and old TestMode
    // scratch before resolving this scan's index. Bounded directory walk,
    // never throws, never fails the scan; the outcome is one backend-log line.
    try {
      const auto nowMs = (std::int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
      const auto swept = msf::sweepStaleIndexes(
          msf::path_from_utf8(appDir_.toStdString()), nowMs);
      if (swept.indexRemoved > 0 || swept.scratchRemoved > 0)
        msf::backendLogLine(QString("janitor removed index=%1 scratch=%2 kept=%3")
                                .arg(swept.indexRemoved).arg(swept.scratchRemoved)
                                .arg(swept.kept).toStdString());
    } catch (...) {}
    engine_.setResourcePolicy(msf::make_policy(resourceMode_, cpu_, gpu_));
    auto policy = engine_.resourcePolicy(); policy.gpuEnabled = gpuEnabled_; engine_.setResourcePolicy(policy);
    control_.scanImages = scanImages_; control_.scanVideos = scanVideos_;
    control_.analyzeMode = analyzeMode_;
    control_.fastRescan = fastRescan_;
    control_.telemetryEnabled = detailedLogEnabled_;
    control_.walkerQueueCapacity = walkerCapOverride_;
    control_.buildVersion = QCoreApplication::applicationVersion().toStdString();
    if (!engine_.openIndexForRoot(root_.toStdString(), appDir_.toStdString()))
      throw std::runtime_error("Portable index open failed");
    // 0.9.4.85: the count prepass runs FIRST so the summary shows "총 파일"
    // before the engine-version revalidation (which can decode images for
    // minutes). 0.9.4.83 had moved it after the gate, so during a long
    // revalidation the total stayed "-" and the panel looked frozen.
    {
      lastListMs_ = 0;
      lastListN_ = 0;
      msf::IndexPaths paths;
      std::string excluded;
      if (msf::IndexManager::resolve(msf::path_from_utf8(appDir_.toStdString()),
                                     msf::path_from_utf8(root_.toStdString()), paths))
        excluded = msf::path_to_utf8(paths.directory.parent_path());
      const qint64 countT0 = QDateTime::currentMSecsSinceEpoch();
      msf::backendLogLine("scanPhase=count begin");
      const qulonglong total = msf::Scanner().count(root_.toStdString(), excluded,
                                                     scanImages_, scanVideos_,
                                                     control_.ignoredPaths, &control_.cancel,
                                                     [this](std::size_t n) {
                                                       lastListN_ = n;
                                                       const qint64 now = QDateTime::currentMSecsSinceEpoch();
                                                       if (now - lastListMs_ > 150) {
                                                         lastListMs_ = now;
                                                         emit listingProgress(n);
                                                       }
                                                     });
      emit listingProgress(lastListN_); // flush the final count-pass heartbeat
      msf::backendLogLine(QString("scanPhase=count done entries=%1 target=%2 elapsedMs=%3")
                              .arg(lastListN_).arg(total)
                              .arg(QDateTime::currentMSecsSinceEpoch() - countT0).toStdString());
      emit targetCount(total);
    }
    // Engine-version gate: pairs stored by an older verdict generation are
    // re-checked with the current logic (no rescan) before anything displays
    // them. Drops old false positives, keeps the rest, stamps the version.
    // Cancelled here means: stop before touching results. This pass decodes
    // images for the grey-zone pairs and can run for minutes after an engine
    // bump, so it reports its own progress (0.9.4.85) instead of leaving the UI
    // at 0 with only disk I/O visible.
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
      lastRevalMs_ = 0;
      control_.revalidateProgress = [this](std::size_t done, std::size_t total) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastRevalMs_ > 150 || done >= total) {
          lastRevalMs_ = now;
          emit revalidateProgress((qulonglong)done, (qulonglong)total);
        }
      };
      const qint64 revT0 = QDateTime::currentMSecsSinceEpoch();
      if (!engine_.revalidateMatches(&control_, &kept, &dropped)) {
        engine_.abortTelemetry();
        if (detailedLogEnabled_ && engine_.hasTelemetry())
          emit telemetryReady(QString::fromStdString(engine_.telemetryJson()));
        emit finished(QString("CANCELLED|0|0")); return;
      }
      control_.revalidateProgress = nullptr;
      control_.revalidateMs = (double)(QDateTime::currentMSecsSinceEpoch() - revT0);
      if (kept + dropped > 0) emit revalidated(kept, dropped);
    }
    // Quick load: the scan button restores the stored duplicate groups before
    // analyzing anything, so a repeat scan of the same folder shows previous
    // results immediately, then appends only files that changed meanwhile.
    {
      // 0.9.4.116: repair stale rows before showing them (deleted files used
      // to linger as phantom group members; an offline root is a no-op).
      engine_.cleanupMissingOnDisk(root_.toStdString());
      const auto stored = engine_.loadMatches();
      // 0.9.4.143: remember the stored prefix and clear this scan's change set.
      storedPairs_.clear();
      changedFiles_.clear();
      int loaded = 0;
      for (const auto& m : stored) {
        const QString l = QString::fromStdString(m.leftPath), r = QString::fromStdString(m.rightPath);
        const bool video = isVideoExt(l) || isVideoExt(r);
        const LiveMatch lm{l, r, m.percent, video ? 2 : 1, m.contentIdentical};
        allMatches_.push_back(lm);
        storedPairs_.push_back(lm);
        if ((video && !scanVideos_) || (!video && !scanImages_)) continue;
        if (control_.ignoredPaths.find(m.leftPath) != control_.ignoredPaths.end() ||
            control_.ignoredPaths.find(m.rightPath) != control_.ignoredPaths.end()) continue;
        { QMutexLocker g(&pendingMutex_); pending_.push_back(lm); }
        ++loaded;
      }
      // 0.9.4.143: the stored prefix ends after this loop; everything appended
      // later is a pair emitted by this scan (live or final pass).
      storedCount_ = allMatches_.size();
      if (loaded > 0) { emit quickLoaded(loaded); emit matchesArrived(); }
    }
    // Progress signals arrive once per analyzed file; a fast Maximum scan would
    // flood the GUI event loop (setText per file) and freeze the window —
    // no pause/cancel/move possible. Throttle display updates to ~7Hz; the
    // latest values are kept and flushed when the scan returns, so pause,
    // cancel, and close stay responsive no matter the scan speed.
    lastProgMs_ = 0; lastProgDone_ = 0; lastProgTotal_ = 0; lastProgPath_.clear();
    lastListMs_ = 0; lastListN_ = 0; lastWalkedMs_ = 0; lastWalkedN_ = 0;
    emit listingProgress(0); // new walk phase; do not retain count-pass values
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
    control_.readProgress = [this](std::size_t n) {
      lastWalkedN_ = n;
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (now - lastWalkedMs_ > 150 || n == 0) { lastWalkedMs_ = now; emit walkedCount((qulonglong)n); }
    };
    control_.onMatch = [this](const msf::SearchMatch& m) {
      { QMutexLocker g(&pendingMutex_);
        const QString l = QString::fromStdString(m.leftPath), r = QString::fromStdString(m.rightPath);
        pending_.push_back(LiveMatch{l, r, m.percent, isVideoExt(l) ? 2 : 1, m.contentIdentical});
      }
      const QString l = QString::fromStdString(m.leftPath), r = QString::fromStdString(m.rightPath);
      allMatches_.push_back(LiveMatch{l, r, m.percent, isVideoExt(l) ? 2 : 1, m.contentIdentical});
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
    // 0.9.4.81 B-slice: at each analyze slice boundary persist the accumulated
    // match set (loaded + newly streamed). Runs on the worker thread inside
    // analyze(), so it may call persistMatchesSnapshot() directly. The engine
    // advances its resume frontier only after this returns.
    control_.analyzeCheckpoint = [this]() { persistMatchesSnapshot(); };
    // 0.9.4.143: collect re-analyzed paths so their stale stored pairs are
    // pruned from every checkpoint and from the final saved set.
    control_.onChangedFile = [this](const std::string& p) {
      changedFiles_.insert(QString::fromStdString(p));
    };
    msf::backendLogLine("scanPhase=engine begin");
    auto r = engine_.scan(root_.toStdString(), unsigned(distance_), &control_);
    // Test-only fault injection (0.9.4.153): simulates an engine-internal
    // failure report (the DB-error path that rolls file rows back and returns
    // completed=false) without throwing, to pin the worker's failed-report
    // contract. Production never sets this variable, so the branch is dead
    // otherwise.
    if (qEnvironmentVariableIsSet("MSF_TEST_ENGINE_FAIL")) r.completed = false;
    msf::backendLogLine(QString("scanPhase=engine done scanned=%1 analyzed=%2 unchanged=%3 failed=%4")
                            .arg(r.scanned).arg(r.analyzed).arg(r.unchanged).arg(r.failed).toStdString());
    // 0.9.4.131: the detailed log must agree with the list the user sees.
    // allMatches_ is exactly the displayed stream -- quick-loaded stored pairs
    // (already scope/ignore filtered) plus everything this scan emitted -- so
    // the record's match counters are recomputed from it before the JSON is
    // taken. Without this an all-unchanged rescan logs "pairs 0, groups 0"
    // while the list shows the stored results. Distinct pair identity uses a
    // 128-bit path-pair key (two seeded FNV-1a values); the counts are
    // display/telemetry only and the verdicts never depend on them.
    {
      struct PairKey {
        std::uint64_t a, b;
        bool operator==(const PairKey& o) const { return a == o.a && b == o.b; }
      };
      struct PairKeyHash {
        std::size_t operator()(const PairKey& k) const {
          return (std::size_t)(k.a ^ (k.b * 0x9E3779B97F4A7C15ULL));
        }
      };
      const auto fnv = [](const QString& s, std::uint64_t seed) {
        std::uint64_t h = seed;
        const QByteArray b = s.toUtf8();
        for (char ch : b) { h ^= (unsigned char)ch; h *= 1099511628211ULL; }
        return h;
      };
      std::unordered_set<PairKey, PairKeyHash> pairKeys;
      std::unordered_map<std::string, std::string> parent;   // union-find
      std::unordered_map<std::string, char> kind;            // 'i' image / 'v' video
      const auto findRoot = [&](const std::string& p) {
        std::string rt = p;
        std::string c = p; // path compression keeps chains short
        while (true) {
          const auto it = parent.find(rt);
          if (it == parent.end() || it->second == rt) break;
          parent[c] = it->second;
          c = rt; rt = it->second;
        }
        return rt;
      };
      std::size_t imgPairs = 0, vidPairs = 0;
      // 0.9.4.143: count the same set that will be persisted, so a stale pair
      // of a re-analyzed file is not reported in the telemetry either.
      const QVector<LiveMatch> displayedMatches = currentMatchSet();
      for (const auto& m : displayedMatches) {
        QString l = m.left, r = m.right;
        if (r < l) std::swap(l, r);
        const bool vid = m.kind == 2;
        if (pairKeys.insert(PairKey{fnv(l, 1469598103934665603ULL), fnv(r, 1099511628211ULL)}).second) {
          if (vid) ++vidPairs; else ++imgPairs;
        }
        const std::string ls = l.toStdString(), rs = r.toStdString();
        parent.emplace(ls, ls); parent.emplace(rs, rs);
        kind.emplace(ls, vid ? 'v' : 'i'); kind.emplace(rs, vid ? 'v' : 'i');
        const std::string a = findRoot(ls), b = findRoot(rs);
        if (a != b) parent[a] = b;
      }
      std::unordered_set<std::string> roots, imgRoots, vidRoots;
      std::size_t imgDup = 0, vidDup = 0;
      for (const auto& kv : parent) {
        const std::string rt = findRoot(kv.first);
        roots.insert(rt);
        const bool vid = kind[kv.first] == 'v';
        if (vid) { vidRoots.insert(rt); ++vidDup; } else { imgRoots.insert(rt); ++imgDup; }
      }
      engine_.applyDisplayedResultStats(pairKeys.size(), roots.size(), imgPairs, imgRoots.size(), imgDup,
                                        vidPairs, vidRoots.size(), vidDup);
    }
    const QString telemetryJson = engine_.hasTelemetry() ? QString::fromStdString(engine_.telemetryJson()) : QString();
    gpuDone_.store((qulonglong)engine_.gpuImagesProcessed());
    // Flush the throttled progress display with the final counts.
    {
      const std::size_t done = lastProgDone_, total = lastProgTotal_;
      emit progress(total ? int(done * 100 / total) : 100, QString::fromStdString(lastProgPath_));
      emit progressCount((qulonglong)done, (qulonglong)total);
      if (lastListN_ > 0) emit listingProgress(lastListN_);
      emit walkedCount((qulonglong)lastWalkedN_); // flush producer-side read count
    }
    // Final persist of the accumulated match set (loaded + new, including pairs
    // whose files are currently missing from disk). Wholesale replacement keeps
    // every pair ever found, so unfinished work on those files resumes on the
    // next scan of the same folder; a partial set on cancel keeps the last
    // completed checkpoint, matching the scan-side semantics.
    // 0.9.4.151: a failed save must not report success. The DB keeps the old
    // set (saveMatches rolls back), so report failed instead of results and a
    // "Scan complete" the database does not hold.
    // 0.9.4.152 (P1): carry the re-analyzed-file count (|msfChanged=N) so the
    // GUI reloads the last committed set instead of showing streamed matches
    // that were never persisted; drop the completeness baseline so the next
    // A/AB scan full-verifies (file rows are already committed with new
    // fingerprints while pairs keep the old rows).
    if (!persistMatchesSnapshot()) {
      const int failedChanged = changedFiles_.size();
      try { engine_.invalidateMatchBaseline(); } catch (...) {}
      allMatches_.clear();
      try { allMatches_.shrink_to_fit(); } catch (...) {}
      try { emit failed(QString("match index save failed: stored pairs unchanged|msfChanged=%1").arg(failedChanged)); } catch (...) {}
      try { msf::backendLogLine("scanWorker final match persist failed"); } catch (...) {}
      return;
    }
    // P4: the accumulated set has been persisted wholesale; retaining it
    // would pin every match's strings until the next scan (hundreds of MB on
    // match storms). Release it — nothing reads allMatches_ after this point
    // (results come from r.matches; takePending() is untouched).
    allMatches_.clear();
    allMatches_.shrink_to_fit();
    // Diagnostic counters (ChatGPT step 1): where a video-heavy scan with few
    // results loses its pairs. Log-only. The finished message is parsed
    // positionally (existing fields keep their indices; only APPEND fields).
    // 0.9.4.151: trailing changed-file count (|N on CANCELLED, 7th field on
    // completion) so the GUI can re-apply the stored index after a scan that
    // re-analyzed files but analyzed nothing new (analysis-failure-only) or
    // was cancelled after re-analysis approval.
    msf::backendLogLine(QString("videoStats indexedVideos=%1 pairs=%2 temporal=%3 matches=%4")
                .arg(r.indexedVideos).arg(r.videoCandidatePairs)
                .arg(r.videoTemporalChecks).arg(r.videoMatches).toStdString());
    { QMutexLocker g(&pendingMutex_); if (!pending_.isEmpty()) emit matchesArrived(); }
    const int changedCount = changedFiles_.size();
    if (control_.cancel.load()) {
      // 0.9.4.153 P1-B: a cancelled scan already pruned stale pairs from the
      // stored set (checkpoints + the final persist above committed), while
      // the engine committed re-analyzed file rows. The pre-scan baseline
      // would wrongly vouch for the pruned set, so invalidate it whenever
      // files were re-analyzed: the next A/AB scan full-verifies.
      if (changedCount > 0) {
        try { engine_.invalidateMatchBaseline(); } catch (...) {}
        try { msf::backendLogLine("scanWorker cancel invalidated baseline"); } catch (...) {}
      }
      if (!telemetryJson.isEmpty()) emit telemetryReady(telemetryJson); emit finished(QString("CANCELLED|%1|%2|%3").arg(r.scanned).arg(r.analyzed).arg(changedCount)); return;
    }
    // 0.9.4.153 P1-B: the engine can report failure without throwing (its
    // internal DB error path rolls file rows back). Falling through to
    // results + "Scan complete" would present a real DB failure as success,
    // so fail loudly instead. Cancel is checked first: a user Stop is never
    // a failure, even when the engine also reports incomplete.
    if (!r.completed) {
      if (changedCount > 0) {
        try { engine_.invalidateMatchBaseline(); } catch (...) {}
        try { msf::backendLogLine("scanWorker engine-failure invalidated baseline"); } catch (...) {}
      }
      allMatches_.clear();
      try { allMatches_.shrink_to_fit(); } catch (...) {}
      try { emit failed(QString("scan engine reported failure|msfChanged=%1").arg(changedCount)); } catch (...) {}
      try { msf::backendLogLine("scanWorker engine-reported failure"); } catch (...) {}
      return;
    }
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
                  + "\t" + QString::number(m.percent, 'f', 1)
                  + "\t" + (m.contentIdentical ? "1" : "0")); // 0.9.4.106: identity badge
    emit results(files, matches);
    if (!telemetryJson.isEmpty()) emit telemetryReady(telemetryJson);
    // Analysis failures are a settled state, not silently dropped files. Surface
    // them in the completion message so a scan that skipped nothing still says so.
    emit finished(QString("Scan complete: %1 files, %2 analyzed, %3 candidates, %4 groups%5")
                      .arg(r.scanned).arg(r.analyzed).arg(r.candidates).arg(r.groups)
                      .arg(r.failed ? QString(", %1 could not be analyzed").arg(r.failed) : QString())
                  + QString("|%1|%2|%3|%4|%5|%6").arg(r.scanned).arg(r.analyzed).arg(r.unchanged).arg(r.groups).arg(r.candidates).arg(changedFiles_.size()));
  } catch (const std::exception& e) {
    try { msf::backendLogLine(QString("scanWorker caught exception: %1").arg(QString::fromUtf8(e.what())).toStdString()); } catch (...) {}
    // engine_.scan() may unwind before finishScan() finalizes telemetry. Stop
    // its sampler here; otherwise TelemetryRecorder would be destroyed with a
    // joinable std::thread and call std::terminate after this handler returns.
    try { engine_.abortTelemetry(); } catch (...) {}
    // A failed scan must not discard what it already found: checkpoint first
    // so the next scan of the same folder quick-loads the partial results.
    // (0.9.4.65) Neither step may throw out of this handler: run() is a Qt
    // slot, so an escaping exception crosses Qt internals into terminate() ->
    // abort() (the 0xC0000409 signature, proven by the 2026-10-07 dump whose
    // fault stack sits in this handler's persist path). Persist and report
    // are attempted independently; each swallows its own failure.
    // 0.9.4.153 P1-B: when files were re-analyzed before the unwind, the
    // persisted set is pruned while the engine may have committed new file
    // rows (post-commit failure). Invalidate the baseline so the next A/AB
    // scan cannot trust the pruned set as complete. Pre-commit failures roll
    // file rows back (change detection refires), where this is harmless.
    if (!changedFiles_.isEmpty()) {
      try { engine_.invalidateMatchBaseline(); } catch (...) {}
      try { msf::backendLogLine("scanWorker exception invalidated baseline"); } catch (...) {}
    }
    try { persistMatchesSnapshot(); } catch (...) {}
    // 0.9.4.151: carry the re-analyzed-file count so the GUI can re-apply the
    // stored index when a failed scan already pruned stale pairs (same suffix
    // contract as the non-standard handler below; absent suffix = 0).
    try { emit failed(QString::fromUtf8(e.what()) + QString("|msfChanged=%1").arg(changedFiles_.size())); } catch (...) {}
    try { msf::backendLogLine("scanWorker std::exception failure reported"); } catch (...) {}
    // P4: same release as the normal path (persist already checkpointed).
    // shrink_to_fit guarded: this handler must never throw (0.9.4.65 rule).
    allMatches_.clear();
    try { allMatches_.shrink_to_fit(); } catch (...) {}
  } catch (...) {
    try { msf::backendLogLine("scanWorker caught non-standard exception"); } catch (...) {}
    try { engine_.abortTelemetry(); } catch (...) {}
    // Fail-fast converted to a recorded failure (0.9.4.62): a non-standard
    // exception used to terminate the whole process with no record (the
    // 0xC0000409 signature seen in real crashes). Persist partial matches
    // and report failed instead. SEH access violations still crash: MSVC
    // builds without /EHa do not unwind those through catch(...), so real
    // memory corruption keeps failing fast instead of being masked.
    // (0.9.4.65) Same no-throw rule as above: the handler itself throwing
    // re-enters terminate() -> abort() with no record.
    // 0.9.4.153 P1-B: same baseline rule as the std::exception path above.
    if (!changedFiles_.isEmpty()) {
      try { engine_.invalidateMatchBaseline(); } catch (...) {}
      try { msf::backendLogLine("scanWorker nonstd invalidated baseline"); } catch (...) {}
    }
    try { persistMatchesSnapshot(); } catch (...) {}
    try { emit failed(QStringLiteral("unhandled non-standard exception in scan worker") + QString("|msfChanged=%1").arg(changedFiles_.size())); } catch (...) {}
    try { msf::backendLogLine("scanWorker non-standard failure reported"); } catch (...) {}
    allMatches_.clear();
    try { allMatches_.shrink_to_fit(); } catch (...) {}
  }
}
QVector<LiveMatch> ScanWorker::currentMatchSet() const {
  // 0.9.4.143: the stored prefix minus pairs touching a path re-analyzed by
  // this scan, plus everything this scan emitted. Emitted pairs win on a
  // duplicate key (a re-verified pair carries its current percentage).
  QVector<LiveMatch> out;
  const int prefix = std::clamp(storedCount_, 0, (int)allMatches_.size());
  out.reserve(storedPairs_.size() + (allMatches_.size() - prefix));
  QSet<QString> seen;
  const auto keyOf = [](const QString& l, const QString& r) {
    QString a = l, b = r;
    if (b < a) std::swap(a, b);
    return a + QLatin1Char('\n') + b;
  };
  for (const auto& m : storedPairs_) {
    if (changedFiles_.contains(m.left) || changedFiles_.contains(m.right)) continue;
    const QString k = keyOf(m.left, m.right);
    if (seen.contains(k)) continue;
    seen.insert(k);
    out.push_back(m);
  }
  for (int i = prefix; i < allMatches_.size(); ++i) {
    const auto& m = allMatches_[i];
    const QString k = keyOf(m.left, m.right);
    if (seen.contains(k)) continue;
    seen.insert(k);
    out.push_back(m);
  }
  return out;
}
bool ScanWorker::persistMatchesSnapshot() {
  // 0.9.4.151: test-only fault injection (like MSF_TEST_THROW_PERSIST below):
  // makes the save report failure deterministically so the failure path is
  // provable without a real disk fault.
  if (qEnvironmentVariableIsSet("MSF_TEST_FAIL_PERSIST")) {
    msf::backendLogLine("matchPersist injected failure (MSF_TEST_FAIL_PERSIST)");
    return false;
  }
  // Test-only fault injection (0.9.4.65): makes the failure-handler path
  // throw deterministically, proving the handler itself never lets an
  // exception escape the worker slot (terminate -> abort). Production code
  // never sets this variable, so the branch is dead otherwise.
  if (qEnvironmentVariableIsSet("MSF_TEST_THROW_PERSIST")) throw std::runtime_error("MSF_TEST_THROW_PERSIST");
  int dropped = 0;
  for (const auto& m : storedPairs_)
    if (changedFiles_.contains(m.left) || changedFiles_.contains(m.right)) ++dropped;
  const QVector<LiveMatch> cur = currentMatchSet();
  if (!changedFiles_.isEmpty()) {
    msf::backendLogLine(QString("matchPersist stored=%1 changed=%2 droppedStored=%3 kept=%4")
                            .arg(storedPairs_.size()).arg(changedFiles_.size()).arg(dropped).arg(cur.size())
                            .toStdString());
  }
  std::vector<msf::SearchMatch> all; all.reserve((std::size_t)cur.size());
  for (const auto& m : cur) all.push_back({m.left.toStdString(), m.right.toStdString(), m.percent});
  // 0.9.4.151: the return used to be ignored at every call site, so a failed
  // save still reported "Scan complete" with success telemetry while the DB
  // kept the old set. Report the failure; the final persist's caller turns it
  // into failed() instead of results()+finished().
  if (!engine_.saveMatches(all)) {
    msf::backendLogLine("matchPersist saveMatches failed");
    return false;
  }
  return true;
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




