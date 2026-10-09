// ScanWorker (P3a: 0.9.4.69). Moved verbatim from gui/mainwindow.h/.cpp:
// the worker is Backend-owned (process-isolation brief §3.3), so it lives in
// BackendCore (msf_core) instead of the GUI translation unit. Behavior is
// unchanged, including the 0.9.4.62 catch-all and the 0.9.4.65 no-throw
// handler rule. The one MainWindow::scanLog call now goes through
// msf::backendLogLine (same file, same shape).
#pragma once
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <cstddef>
#include <string>

#include "media_search_engine.h"
#include "resource_policy.h"

// A single streamed match (paths resolved in the worker thread).
struct LiveMatch { QString left, right; double percent=0; int kind=1; };
// Per-file data snapshot handed to the GUI thread when a scan finishes.
struct GuiFile { QString path; qulonglong size=0; QString fpHex; double duration=0; };
Q_DECLARE_METATYPE(LiveMatch)
Q_DECLARE_METATYPE(GuiFile)

// ---------------------------------------------------------------- worker
class ScanWorker : public QObject {
  Q_OBJECT
public:
  // Node A: gpu ctor param is deprecated (kept for signature compatibility;
  // the GPU % UI is removed — GPU is ON/OFF only, internal cap unchanged).
  ScanWorker(QString root, QString appDir, int distance, int cpu, int gpu, bool gpuEnabled,
             bool scanImages=true, bool scanVideos=true);
public slots:
  void run(); void pause(); void resume(); void cancel();
  void setIgnored(const QSet<QString>& s);
  void setDetailedLog(bool b) { detailedLogEnabled_ = b; }
  // D3-Minimal test hook: walker-queue capacity override (0 = production
  // default). Lets regression tests force the bounded path with small file
  // sets. Never set by production UI code.
  void setWalkerQueueCapacity(std::size_t n) { walkerCapOverride_ = n; }
  // P4: ResourceMode delivery (was always Custom after the P3 split).
  // Stored before run(); run() builds make_policy(mode_, cpu_, gpu_).
  void setResourceMode(msf::ResourceMode m) { resourceMode_ = m; }
  // 0.9.4.81: analyze-mode delivery (default Sequential/B). Stored before
  // run(); run() copies it into control_.analyzeMode.
  void setAnalyzeMode(msf::AnalyzeMode m) { analyzeMode_ = m; }
  QVector<LiveMatch> takePending(); // thread-safe drain for the GUI
  const msf::MediaSearchEngine& scanEngine() const { return engine_; }
  qulonglong gpuDone() const { return gpuDone_.load(); }
  bool gpuAvailable() const { return gpuAvail_; }
  bool gpuActive() const { return engine_.gpuActive(); }
signals:
  void progress(int,QString);
  void progressCount(qulonglong,qulonglong);
  void walkedCount(qulonglong);
  void telemetryReady(QString);
  void listingProgress(std::size_t);
  // Fingerprint-phase live progress (files hashed, bytes hashed, path).
  // Separate from progress()/progressCount() so the walk/analysis percent
  // math is untouched; the fingerprint has no known total.
  void fingerprintProgress(qulonglong,qulonglong,QString);
  void matchesArrived();            // throttled; call takePending()
  void quickLoaded(int);            // stored matches reloaded from the index
  void revalidated(int,int);        // old-engine pairs re-checked: kept, dropped
  // 0.9.4.85: revalidation progress (pairs checked, total). Emitted while the
  // engine-version gate re-verifies stored pairs, so the UI is not frozen.
  void revalidateProgress(qulonglong,qulonglong);
  void results(QVector<GuiFile> files, QStringList matchRows);
  void targetCount(qulonglong);   // pre-walk file total (fixed denominator)
  void finished(QString);
  void failed(QString);
private:
  QString root_, appDir_; int distance_, cpu_, gpu_; bool gpuEnabled_;
  bool scanImages_, scanVideos_;
  msf::ScanControl control_; msf::MediaSearchEngine engine_;
  QMutex pendingMutex_; QVector<LiveMatch> pending_;
  QVector<LiveMatch> allMatches_;   // worker-thread only; checkpointed incrementally + at the end
  void persistMatchesSnapshot();    // save the full accumulated set (worker thread only)
  int matchesSinceSave_=0; qint64 lastSaveMs_=0; // incremental-checkpoint throttle
  qint64 lastEmitMs_=0;
  // Progress-signal throttle (worker thread only): the engine reports every
  // analyzed file, but the GUI is updated at most every ~150ms so a fast
  // Maximum scan cannot flood the event loop and freeze the UI.
  qint64 lastProgMs_=0; std::size_t lastProgDone_=0, lastProgTotal_=0; std::string lastProgPath_;
  qint64 lastListMs_=0; std::size_t lastListN_=0;
  qint64 lastWalkedMs_=0; std::size_t lastWalkedN_=0;
  qint64 lastFpMs_=0;
  qint64 lastRevalMs_=0; // 0.9.4.85 revalidation-progress throttle
  std::atomic<qulonglong> gpuDone_{0}; // live GPU-accelerated image count
  bool gpuAvail_=false;                // CUDA backend present at construction
  bool detailedLogEnabled_=true;
  std::size_t walkerCapOverride_=0; // see setWalkerQueueCapacity
  msf::ResourceMode resourceMode_=msf::ResourceMode::Custom; // see setResourceMode
  msf::AnalyzeMode analyzeMode_=msf::AnalyzeMode::Sequential; // see setAnalyzeMode
};
