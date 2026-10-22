#pragma once
// New Explorer-style UI (mockups in C:\project\uimock):
//  top path/scan bar, left folder tree + summary, middle group list,
//  right detail (grid/list switchable), KO/EN language, mark + context menu,
//  live streaming results during scan.
#include <QMainWindow>
#include <QObject>
#include <QMutex>
#include <QComboBox>
#include <QElapsedTimer>
#include <QIcon>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QPair>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "../src/media_search_engine.h"
#include "../src/database.h"
#include "../src/resource_policy.h"
#include "backend_client.h"

// GUI execution resource strategy (user's real-search resource choice, exactly
// one selected). This is intentionally NOT the CLI benchmark comparison mode:
// the names coincide (AUTO / CPU-only / GPU-max) but the semantics differ.
enum class ExecutionResourceStrategy { Auto, CpuOnly, GpuMax };

class QLineEdit; class QSystemTrayIcon; class QTreeWidget; class QTreeWidgetItem;
class QListWidget; class QListWidgetItem; class QPushButton; class QProgressBar;
class QLabel; class QSpinBox; class QStackedWidget; class QSlider;
class QMouseEvent;
class QToolButton; class QSplitter; class QCheckBox; class QTimer; class QStatusBar;
class QTabWidget; class QFormLayout; class QToolBar; class QMenu; class QAction;
class QStyledItemDelegate; class QCloseEvent; class QDialog;

// 0.9.4.136: preview-size slider with a visible 1.0x reference marker and a
// ratio bubble while dragging. The reference value is the product's standard
// preview size (zoomChanged's default 128); values map to ratios of it.
class ZoomSlider : public QSlider {
    Q_OBJECT
public:
    explicit ZoomSlider(QWidget* parent = nullptr);
    // Draw the reference line/tick at this slider value; <= minimum disables it.
    void setReferenceValue(int v) { reference_ = v; update(); }
    // 0.9.4.137: the slider snaps to 0.05 ratio steps (6.4 px at the 128 px
    // reference), so a 0.9 -> 1.0 move is two visible sizes instead of
    // thirteen and the two-decimal ratio bubble stays meaningful.
    static int snapValueFor(int v, int reference);
signals:
    void previewSizeChanged(int px);
private slots:
    void onValueChanged(int v);
protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
private:
    int handleCenterX() const;
    int reference_ = 0;
    bool dragging_ = false;
    int lastEmitted_ = -1;
};

// ---------------------------------------------------------------- language
enum class UiLang { Ko, En };
QString trStr(UiLang lang, const char* key); // KO/EN string table (see .cpp)
// QuickLook wire protocol (see QL-Win/QuickLook PipeServerManager.cs):
// message "QuickLook.App.PipeMessages.Toggle|<path>|\n" (UTF-8) written to
// \\.\pipe\QuickLook.App.Pipe.<UserSID>. Exported for the pipe test.
QString quickLookToggleMessage(const QString& filePath);
bool quickLookSendMessage(const QString& pipeName, const QByteArray& payload);
// Crash diagnostics (0.9.4.62): Qt message file sink. Installed by main() so
// warnings/criticals survive, and a fatal's last words land in the file
// before Qt aborts (fail-fast preserved, now with a record). Per-message
// open/append/close under a mutex: crash-safe (no buffered loss) and
// thread-safe. Empty path selects %TEMP%/msf_qt.log. Tests pass their own
// path and restore the default handler afterwards.
void installQtMessageLog(const QString& path = QString());
// Letterbox-fit helper: every display icon entering a grid is normalized to
// the exact requested rect, so cells stay uniform (Explorer-like) regardless
// of source aspect/size. Transparent padding, never distorted.
QPixmap squareFittedPixmap(const QPixmap& src, const QSize& size);
// One-time QSettings bootstrap (org/app names + INI location). Call once in
// main() before any default-constructed QSettings is used.
void initAppSettings(const QString& portableDir);

// ScanWorker/MediaMonitor thumbnail session types live in BackendCore now
// (P3a: src/scan_worker.h, src/monitor.h). This header keeps GUI-side
// presentation models only.

// A duplicate group built incrementally from streamed matches.
struct DupGroup {
  QStringList paths;      // members, [0] is the reference
  double best=0;          // best similarity inside the group
  int kind=1;             // 1=image, 2=video (MediaKind values)
  QHash<QString,double> pct; // per-path best percent
  QString root;           // union-find root (stable group identity for append)
};

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  // P3: production constructs with a real BackendSupervisor; tests and
  // loopback paths pass nothing (a LoopbackBackendClient is created).
  // MainWindow takes ownership of a passed client.
  explicit MainWindow(QWidget* parent = nullptr, BackendClient* backend = nullptr); ~MainWindow();
  // Test hook: last scan's telemetry JSON (empty when no scan ran or the
  // worker is gone). Lets acceptance tests assert the detailed-log result
  // of a real MainWindow scan without touching private state.
  std::string telemetryJsonForTest() const;
  int testAutoResumeTries() const { return autoResumeTries_; }
  // Scroll-regression test hooks (0.9.4.61): run one UI-timer tick body
  // synchronously, and read the rebuild/catch-up counters. Production code
  // never calls these; they exist so offscreen tests can drive ticks
  // deterministically instead of waiting on wall-clock timer intervals.
  void testUiTick();
  // Painted-thumbnail counter for the scroll regression test (replaces the
  // retired in-place count: arrivals, not synchronous sets, do the painting).
  qulonglong testThumbPaintedCount() const { return thumbPaintedCount_; }
  // Deterministic repaint trigger for the scroll test: drops the memory
  // cache (and outstanding request ids) so the next catch-up ticks
  // re-request and re-paint through the real async path.
  void testDropThumbCache() { thumbCache_.clear(); thumbPendingReq_.clear(); }
  qulonglong testThumbPendingCount() const { return (qulonglong)thumbPendingReq_.size(); }
  // View-mode regression test hook (0.9.4.66): run one thumbnail catch-up
  // pass synchronously, exactly as the UI timer does. Production code never
  // calls this; it exists so the offscreen probe can drive thumbnail
  // requests through the real catch-up path instead of setting item icons
  // directly and bypassing it. Arrivals paint via onThumbReady.
  void testThumbCatchUp() { thumbCatchUpVisible(); }
  // 0.9.4.109: memory-cache probe. Returns the presentation icon stored for
  // (path, size) — empty when not cached. Lets the view-mode round-trip test
  // assert that a warm-cache switch restores exactly the cached pixmap on the
  // item (the regression: catch-up dropped cache hits, so items kept the
  // previous mode's pixmap).
  QIcon testCachedThumb(const QString& path, const QSize& size) const {
    return thumbCache_.value(path + '|' + QString::number(size.width()) + 'x' + QString::number(size.height()));
  }
  // 0.9.4.105 (D): force one middle-pane refill through the real fill path so
  // tests can prove a bulk rebuild issues no per-row thumbnail requests.
  void testRefreshGroupList() { refreshGroupList(); }
  qulonglong testFullRebuildCount() const { return fullRebuildCount_; }
  // 0.9.4.121: backend thumbnail request counter (monotonic). The middle
  // grid heals lost answers through the catch-up pump, but the file grid
  // used to rely on the single request fired at row creation; this counter
  // lets the layout probe prove both panes re-ask a dropped answer after a
  // catch-up tick, independent of backend answer timing.
  qulonglong testThumbRequestCount() const { return thumbRequestCount_; }
  // 0.9.4.105: backend metadata request counter. Pins "select a file -> one
  // metadata request" (the old code re-requested on every duration-less
  // answer, an endless rebuild/request ping-pong that blanked the detail).
  qulonglong testFileMetaRequestCount() const { return fileMetaRequestCount_; }
  // 0.9.4.106: display badge flags per path. Bit 1 = a 100.0% pair with equal
  // content (size + quick hash) was seen; bit 2 = a 100.0% pair that is only
  // perceptually identical (watermark/sticker/re-encode/crop variant). Shown
  // next to the percentage so "100%" no longer reads as "same file" blindly.
  int testContentFlag(const QString& path) const { return contentFlag_.value(path, 0); }
  // 0.9.4.142 probe: scan-start view preservation (selection/preview/thumbnails).
  int testCurrentGroupIndex() const { return currentGroup_; }
  QString testCurrentFile() const { return currentFile_; }
  bool testPreviewHasPixmap() const;
  QStringList testGroupPathsFor(const QString& path) const {
    const int gi = pathGroup_.value(path, -1);
    return (gi >= 0 && gi < groups_.size()) ? groups_[gi].paths : QStringList{};
  }
  // 0.9.4.112 (S4): write the last detailed-log record through the exact
  // production save routine (the dialog's Save button only adds the file
  // picker on top). Returns true when the record was written.
  bool testSaveDetailedLog(const QString& path) {
    return !lastTelemetryJson_.isEmpty() && saveDetailedLogTo(path, lastTelemetryJson_);
  }
  // 0.9.4.113 (B2): last detailed-log telemetry JSON received from the backend
  // over the real process path. Lets tests verify settings -> engine ->
  // telemetry (the same record the dialog displays) without a file log.
  QString testLastTelemetryJson() const { return lastTelemetryJson_; }
  // Mark-state probe (0.9.4.99): all marked paths. Pins reference-exclusion
  // (the group checkbox never auto-marks paths[0]).
  QStringList testMarkedPaths() const { return QStringList(marked_.begin(), marked_.end()); }
  // Action-operand probe (0.9.4.103): copy/cut/move/recycle target set.
  QStringList testActionFiles() const { return actionFiles(); }
  // Detail-pane probe (0.9.4.99): fileName value text of the first detail
  // row ("-" when no file is shown). Pins the user-visible symptom of the
  // check/uncheck-then-select blank-detail defect regardless of cause.
  QString testDetailFileName() const;
  // Detail-form installation probe: the refreshDetail() form object must
  // stay installed on its container (a silently orphaned layout would show
  // nothing while all data paths test green).
  bool testDetailInstalled() const;
  // Detail-form row bound probe: refreshDetail() must not accumulate rows.
  int testDetailRowCount() const;
  // Resource-policy test hook (0.9.4.96): GPU acceleration switch state.
  bool testGpuOn() const { return gpuOn_; }
  static void scanLog(const QString& line); // process-wide scan log file
  static void sortTiedReferencePaths(QStringList&, const QHash<QString,qulonglong>&, const QHash<QString,qulonglong>&);
private slots:
  // scan
  void chooseFolder(); void startScan(); void beginScan(bool resumeLastConfig = false); void togglePauseScan(); void cancelScan();
  void scanProgress(int,QString); void onMatchesBatch(const QVector<BackendMatch>&); void scanFinished(QString); void scanFailed(QString);
  void onStatusSnapshot(BackendStatus);
  void onScanCounts(qulonglong,qulonglong);
  void onFingerprintProgress(qulonglong,qulonglong,QString);
  void onTargetCount(qulonglong);
  void onWalkedCount(qulonglong);
  void onListingProgress(std::size_t);
  void onResults(QVector<BackendFile> files, QStringList matchRows);
  void onQuickLoaded(int);
  void onRevalidated(int,int);
  void onRevalidateProgress(qulonglong,qulonglong);
    void onDetailedLog(QString);
    void showDetailedLogDialog(const QString& json);
  void resourceChanged(int);
  // groups / files
  void groupSelected(QTreeWidgetItem*,QTreeWidgetItem*); void fileGridSelected(); void fileListSelected();
  void toggleTestMode(bool);
  void setViewMode(int); void zoomChanged(int); void groupSearchChanged(const QString&);
  // 0.9.4.138: width-aware three-line text for the preview grid cells.
  QString fileGridItemText(const QString& path, bool ref, double pct) const;
  void groupViewChanged(int);
  void applyExecutionMode(); void updateKindSelection();
  void applyCpuPercentVisibility(); // show/hide cpu_ from ui/showCpuPercent (default OFF)
  void applyTestButtonVisibility(); // show/hide testBtn_ from ui/showTestButton (default ON)
  // P4: builds the two-axis ExecutionPolicy from policy_ + strategy box.
  ExecutionPolicy currentExecPolicy() const;
  void toggleMarkSelected(); void markAll(bool); void invertMarked();
  void setGroupMarked(int gi, bool on);
  // 0.9.4.99: group-box check state under reference-exclusion (paths[0] is
  // never auto-marked). Checked == all non-reference files marked.
  Qt::CheckState groupMarkState(int gi) const;
  void showFileMenu(const QPoint&); void showGroupMenu(const QPoint&);
  void openSelected(); void revealSelected(); void renameSelected(); void deleteSelected();
  void revealPath(const QString& path);
  void previewSelectedQuickLook(); QString quickLookTarget() const; // empty when QuickLook unusable
  void pollQuickLookPipe(); // async launch follow-up (bounded, event-loop driven)
  void copySelected(); void cutSelected(); void pasteFiles(); void moveSelected();
  void refreshFolders(); void folderActivated(QTreeWidgetItem*,int);
  void loadIndex(); // 0.9.4.93: load the selected folder's index (no scan)
  void reverifyIndex(); // 0.9.4.134: re-verify stored duplicate members, then reload the view
  // 0.9.4.134: completion counters (checked/changed/re-analyzed/pairs before-after).
  void onReverifyFinished(qulonglong members, qulonglong checked, qulonglong changed,
                          qulonglong reanalyzed, qulonglong pairsBefore, qulonglong pairsAfter);
  // 0.9.4.135: Test Mode difference report (Yes/No adoption) + outcome.
  void onTestModeDiff(qulonglong changed, qulonglong added, qulonglong removed,
                      qulonglong pairDiff, qulonglong pairsMain, qulonglong pairsScratch);
  void onTestApplied(bool ok, QString detail);
  void populateFolderChildren(QTreeWidgetItem*);
  QStringList selectedFiles() const;
  // 0.9.4.103: copy/cut/move/recycle operand (checked files first).
  QStringList actionFiles() const;
  void prunePaths(const QSet<QString>&);
  void refreshAfterFileOperation(const QString&);
  void detailTabChanged(int); void fileActivated(QListWidgetItem*); void showFileDetails(const QString&);
  // monitor
  void configureMonitor(); void toggleMonitor();
  void monitorEvent(const BackendMonitorEvent&); void showMonitorMatch(const BackendMonitorEvent&); void
  onMonitorSnapshot(const BackendMonitorStatus&);
  void onBackendConnection(bool available, QString message);
  // misc
  void setLanguage(int); void showHelp(); void applyStaticTexts();
private:
  UiLang lang() const;
  void closeEvent(QCloseEvent*) override;
  void showEvent(QShowEvent*) override; // first show boots the backend (ensureRunning, idempotent)

  // --- GUI execution resource strategy --------------------------------------
  // The single-select strategy checkboxes. Read through executionStrategy();
  // never handed to any benchmark runner.
  ExecutionResourceStrategy executionStrategy() const;
  // Sizes the strategy dropdown to its widest item text (no slack).
  void fitStrategyBoxWidth();
  // Keeps the CPU spinbox prefix/suffix ("CPU ", "%") visible but
  // non-selectable: clamps selection and cursor into the digit range.
  void clampCpuDigitSelection();
  bool eventFilter(QObject* watched, QEvent* ev) override;
  // Single gate for the scan/execution enable state. A scan in progress
  // disables starting another run, and Pause stays a scan-only control
  // (the detailed-log path has no Pause).
  void updateExecutionUiState();
  void buildUi(); void buildToolbar(); void buildLeft(QWidget*); void buildMiddle(QWidget*); void buildRight(QWidget*);
  void paintTestButton(); // checked-state highlight for the Test Mode toggle
  void setRunning(bool);
  void updateSortEnabled(); // group-sort combo enabled only when idle on a group tab
  void applyDetailLogVisibility(); // show/hide logTgl_ from ui/showDetailLog (default ON)
  void rebuildGroups();          // union-find over accumulated matches
  void updateGroupFoot();        // "전체 N · 선택 M" footer label
  void onUiTick();               // 600ms timer body (extracted for testUiTick)
  void refreshStreaming(bool force=false); // throttled rebuild+fill for live scans
  void thumbCatchUpVisible();    // in-place thumbnail fill for visible items only (never rebuilds)
  void flushThumbLayout();       // one coalesced Batched layout for arrived thumbnails
  bool scrollGateActive() const; // slider held or inside the post-scroll cooldown
  void noteUserScroll();         // stamp a user navigation event (wheel/keys/slider)
  void refreshGroupList(bool append=false); // middle pane from groups_ (append=extend only)
  void refreshFileViews();       // right grid+list from selected group
  void refreshDetail();          // tabs for current file
  // 0.9.4.112 (S4): the detailed-log save path as a unit — the dialog's Save
  // button calls this after the file picker; the acceptance test drives it
  // directly. Returns false (and shows the failure status) when not written.
  bool saveDetailedLogTo(const QString& path, const QString& json);
  void refreshSummary(const msf::SearchReport* r=nullptr);
  void updateGpuLabel();
  QString gpuStateText() const;
  void updateStatusCounts();
  void updateSysLabels(); // GPU state row (P4: CPU/RAM rows come from the status snapshot)
  void scanHeartbeat();
  void saveUiState();              // window geometry + splitter + header layouts
  void restoreUiState();           // counterpart applied after buildUi()
  QString fmtSize(qulonglong) const;
  QString scanStatusText(qulonglong done, qulonglong total, int pct, const QString& path, qint64 elapsedMs) const;
  qint64 elapsedActiveMs() const; // scan clock minus paused intervals
  QString fileResolution(const QString&) const; // cached backend dimensions (P4: no GUI decode)
  // 0.9.4.117: cached-only pixel count for the tie ordering of a full group
  // rebuild. Never issues a backend request (the requesting variant made the
  // rebuild issue one FILE_META request per path - minutes of GUI stall).
  qulonglong filePixels(const QString&) const;
  QIcon fileThumb(const QString&, const QSize&, bool bypassBudget = false) const;
  void onThumbReady(quint64 requestId, const ThumbResult& thumb);
  void onFileMetaReady(quint64 requestId, const FileMetaResult& meta);
  void requestFileMeta(const QString& path) const;
  // FileMeta in flight, id-keyed like thumbnails (Type B dedup + stale-drop).
  // Mutable: refreshDetail and fileResolution are const but trigger requests.
  mutable QMap<quint64, QString> fileMetaPending_;
  mutable quint64 fileMetaRequestId_ = 0;
  // 0.9.4.105: one metadata request per path per scan/load generation. Without
  // this, a duration-less answer (any image) let the next refreshDetail ask
  // again, whose answer rebuilt the detail again: an endless request/rebuild
  // ping-pong whose widget churn blanked the detail text.
  mutable QSet<QString> fileMetaAsked_;
  mutable qulonglong fileMetaRequestCount_ = 0;
  // In-place fileMeta paint, mirroring the 0.9.4.66 thumbnail pattern: update
  // the matching grid cell / list row text without recreating widgets (which
  // would destroy the user's selection — the P4 selection-lock regression).
  void refreshFileMetaRow(const QString& path);
  QIcon placeholderIcon(const QString& path) const; // per-suffix file-type icon
  void dropThumbCache(const QSet<QString>& paths); // 0.9.4.104 (B): single sweep
  double pathBest(const QString&) const;
  void addMatch(const QString&, const QString&, double, int kind, bool contentIdentical = false);
  QString findRoot(const QString&); // union-find over pathParent_
  // 0.9.4.117: deferred bulk fill for large result sets. A synchronous fill of
  // the user's loaded index (9,055 groups) froze the GUI event loop for
  // minutes (measured by group_fill_perf_probe). Rows are added in chunks on
  // successive event-loop turns; a generation counter cancels stale jobs when
  // a newer fill starts (append/rebuild/tab switch).
  struct GroupFillJob {
    QVector<int> index;                      // groups_ indices to add, in order
    QVector<QPair<QString, QString>> shown;  // signature snapshot on completion
    int wantKind = 1;
    bool syncSel = false;
    bool append = false;                     // extend an existing pane (no clear)
    std::size_t pos = 0;
    QTreeWidget* tree = nullptr;
    QListWidget* grid = nullptr;
    int treePos = 0, gridPos = 0;
    QString treeAnchorPath, gridAnchorPath;
    int treeAnchorContent = 0, gridAnchorContent = 0;
    QElapsedTimer t0;
  };
  void continueGroupFill(const std::shared_ptr<GroupFillJob>& job, int gen);
  // Shared by the synchronous and deferred full-fill paths.
  void captureFillAnchors(QTreeWidget* tree, QListWidget* grid, int treePos, int gridPos,
                          QString& treeAnchorPath, int& treeAnchorContent,
                          QString& gridAnchorPath, int& gridAnchorContent);
  void restoreFillAnchors(QTreeWidget* tree, QListWidget* grid, int treePos, int gridPos,
                          const QString& treeAnchorPath, int treeAnchorContent,
                          const QString& gridAnchorPath, int gridAnchorContent);
  std::shared_ptr<GroupFillJob> fillJob_;
  int fillGen_ = 0;
  // scan state (P2: the backend lives behind BackendClient; the loopback
  // implementation runs the real ScanWorker/MediaMonitor in-process)
  BackendClient* backend_ = nullptr; QDialog* cancelWait_=nullptr; msf::ResourcePolicy policy_;
  bool backendAvailable_ = true; // supervisor connection state (§21 guard)
  bool backendRestarting_ = false;
  BackendScanConfig lastScanCfg_;
  bool haveLastScanCfg_ = false;
  BackendStatus backendStatus_; // pushed snapshot cache (Type C): ticks read this, never the engine
  BackendMonitorStatus backendMonStatus_; // pushed monitor snapshot cache (Type C)
  QVector<DupGroup> groups_;                   // built incrementally from streamed matches
  QHash<QString,int> pathGroup_;               // path -> group index
  QHash<QString,QString> pathParent_;           // union-find parent
  QStringList allPaths_; QStringList matchRows_;
  QHash<QString,QString> fileSize_; QHash<QString,QString> fileFp_;
  qulonglong fileSizeCached(const QString&); // index value, else live stat
  // P3: thumbnail persistence lives in the Backend (G1.3). The GUI keeps
  // the memory presentation cache above plus file metadata maps.
  mutable QHash<QString,QString> resCache_;
  QString currentFile_; int currentGroup_=-1;
  int lastPct_=0; QString lastPath_; int maxPctShown_=0;
  qulonglong lastDoneN_=0, lastTotalN_=0;
  // Monotonic read-progress across phases within one scan. Fingerprint-hashed
  // and walked counts are different sequences sharing no denominator; taking
  // the max keeps the panel from visibly resetting to 0 at the phase handoff.
  // Reset at scan start alongside the other counters.
  qulonglong lastReadN_=0;
  qulonglong targetTotal_=0; bool targetKnown_=false;
  qulonglong lastListN_=0; // directory-walk listing count (heartbeat-visible)
  QString qlPendingPath_; int qlPollLeft_ = 0; // pending preview while its server starts
  QStringList cutPaths_;
  // 0.9.4.105 (C): recycle/move run on a worker thread with a progress dialog
  // (the shell call / copy loop no longer blocks the GUI). Busy flag refuses
  // re-entrant file operations while one is in flight.
  bool fileOpBusy_ = false;
  bool scanning_=false; qint64 scanStartMs_=0; bool groupsDirty_=false; bool scanPaused_=false;
  // Backend-crash auto-resume. A backend crash mid-scan aborts the scan; when
  // the supervisor brings a fresh backend back to READY, the GUI re-issues the
  // scan once (bounded) so the user does not have to press Start again. The
  // engine checkpoints progress, so the re-walk skips already-indexed files.
  bool scanCrashed_=false; int autoResumeTries_=0;
  qint64 pauseStartMs_=0, pausedAccumMs_=0; // ETR excludes paused time (see elapsedActiveMs)
  // Live-refresh streaming state: full list rebuilds cost up to ~1s at 11k
  // groups, so they are throttled adaptively (see refreshStreaming) instead of
  // every 600ms tick — otherwise timer timeouts backlog behind each slow tick
  // and paint events (plus pause/cancel clicks) starve forever.
  qulonglong matchSeq_=0;      // bumped per accepted match in addMatch
  qulonglong lastFillSig_=0;   // matchSeq_ at the last full list fill
  qint64 lastFillMs_=0;        // when the last full fill ran
  qint64 lastFillCostMs_=0;    // measured cost of the last full fill
  // Scroll-regression guards (0.9.4.61): full list refills never run while the
  // user is dragging a scrollbar or inside the short post-navigation cooldown,
  // so thumbnail catch-up cannot destroy the scrolled position. Input itself
  // is never blocked or eaten — only the rebuild is deferred to a later tick.
  qint64 lastUserScrollMs_=0;
  bool sliderHeld_=false;
  // Test-only instrumentation (never exposed to telemetry/benchmark schemas):
  // full refills, asserted by the regression test. Thumbnail arrivals are
  // counted separately below (testThumbPaintedCount).
  mutable qulonglong fullRebuildCount_=0;
  QStringList lastStats_; // scanned|analyzed|unchanged|groups|candidates from finished()
  qint64 repElapsedMs_=0;
  QHash<QString,double> bestPct_; QSet<QString> marked_;
  // 0.9.4.106: per-path display badge. bit1 = a 100.0% pair with equal content
  // (size + quick hash) was seen; bit2 = a 100.0% pair that is only
  // perceptually identical (watermark/sticker/re-encode/crop derivative).
  // Cleared at scan/load start and on prunePaths; display-only, never a verdict.
  QHash<QString,int> contentFlag_;
  QHash<QString,int> pathKind_; QHash<QString,double> fileDur_;
  mutable QHash<QString,QIcon> thumbCache_; // memory presentation cache (P3: disk cache lives in Backend)
  mutable QHash<QString,QIcon> phCache_; // placeholder icons by lowercase suffix
  // P3: async thumbnail requests in flight (Type B). Keyed by requestId;
  // the arrival paints only surfaces still wanting that size (stale-drop).
  // Capped: a lost backend must not grow this without bound.
  struct ThumbReq { QString path; QSize size; };
  mutable QMap<quint64, ThumbReq> thumbPendingReq_;
  mutable quint64 thumbRequestId_ = 0;
  // 0.9.4.105 (D): true while a bulk fill populates the middle list. fileThumb
  // then serves cache hits/placeholders only — a full rebuild used to request
  // art for every row (thousands of requests), which overflowed the pending
  // map so in-flight answers arrived as "unknown" and were dropped, leaving
  // permanent placeholder blanks. The visible-only catch-up pump runs right
  // after the fill and on every idle tick, so visible rows still fill.
  bool thumbBulkFill_ = false;
  // P3: GUI no longer decodes (all decode is Backend-side), so the per-tick
  // budgets and the starvation flag are gone. Miss accounting lives in the
  // pending-request map; arrivals paint via onThumbReady.
  mutable qulonglong thumbStatMem_ = 0, thumbStatBackend_ = 0, thumbStatPlace_ = 0;
  mutable qulonglong thumbPaintedCount_ = 0;
  mutable qulonglong thumbRequestCount_ = 0; // 0.9.4.121: see testThumbRequestCount
  // Coalesced icon-layout work (0.9.4.75): thumbnail arrivals mark the visible
  // grids dirty instead of forcing a Batched layout per arrival. The first
  // arrival in a burst schedules one zero-delay layout pass.
  bool thumbLayoutPending_ = false;
  QSet<QString> ignored_;
  msf::SearchReport lastReport_; bool hasReport_=false;
  // toolbar
  QToolBar* toolBar_=nullptr;
  QLineEdit* folder_=nullptr;   QPushButton *scan_=nullptr,*pause_=nullptr,
    *cancel_=nullptr,*monBtn_=nullptr,*logBtn_=nullptr,*testBtn_=nullptr;
  // 0.9.4.149: merged Load/Re-verify slot (QToolButton + dropdown menu).
  // The menu shows Load on top and Re-verify below; the main button part
  // executes the currently selected action.
  QToolButton* loadSplitBtn_=nullptr; QMenu* loadSplitMenu_=nullptr;
  QAction *loadSplitLoadAct_=nullptr, *loadSplitReverifyAct_=nullptr;
  enum class LoadSplitMode { Load, Reverify };
  LoadSplitMode loadSplitMode_ = LoadSplitMode::Load;
  void setLoadSplitMode(LoadSplitMode m);
  void executeLoadSplit();
  void retextLoadSplit(UiLang l);
  QAction *monSettingsAct_=nullptr, *helpAct_=nullptr; // retexted on language change
  QToolButton* utilBtn_=nullptr;
  QString lastTelemetryJson_;
  QComboBox* preset_=nullptr; QSpinBox* cpu_=nullptr;
  // 0.9.4.96: GPU acceleration moved to Settings > General (ui/gpuEnabled).
  // The toolbar checkbox is gone; strategy forcing (CPU-only off) applies to
  // this stored switch with the same overwrite semantics as before.
  bool gpuOn_ = true;
  QCheckBox* logTgl_=nullptr; QAction* logTglAct_=nullptr; QAction* cpuAct_=nullptr; QAction* testBtnAct_=nullptr;
  // Execution resource strategy (single-select dropdown): the user's
  // real-search resource choice. logTgl_ enables diagnostic telemetry
  // on the scan.
  QComboBox* strategyBox_=nullptr;
  // left
  QTreeWidget* folders_=nullptr;   QLabel *sumTotal_=nullptr,*sumDone_=nullptr,*sumIndexed_=nullptr,*sumGroups_=nullptr,
    *sumDup_=nullptr,*sumVerify_=nullptr,*sumTime_=nullptr,*sumGpu_=nullptr,*sumCpu_=nullptr,*sumRam_=nullptr;
  QLabel *sumValTotal_=nullptr,*sumValDone_=nullptr,*sumValIndexed_=nullptr,*sumValGroups_=nullptr,
    *sumValDup_=nullptr,*sumValVerify_=nullptr,*sumValTime_=nullptr,*sumValGpu_=nullptr,*sumValCpu_=nullptr,*sumValRam_=nullptr;
  // 0.9.4.95: summary-panel pair-verify row (total/confirmed duplicate pairs,
  // result-oriented). Parsed from the telemetry record in onDetailedLog so the
  // panel never depends on the dialog. Zero means "no record yet".
  qulonglong lastVerifyTotal_=0, lastVerifyDone_=0;
  // middle
  // Group sort: 0=none (default, top), 1=similarity, 2=name. "None" keeps a
  // stable append order (existing groups keep their position, new groups are
  // appended) so the middle pane extends without recreating rows during a scan;
  // a running scan locks the combo to None and restores the user's choice when
  // the scan ends.
  // 0.9.4.139: group sort types. Size = number of files in the group (the
  // user asked for "bigger groups first" as a first-class order).
  static constexpr int kSortNone = 0, kSortSize = 1, kSortSim = 2, kSortName = 3;
  QLabel* groupTitle_=nullptr; QComboBox* sortBox_=nullptr; QComboBox* sortDirBox_=nullptr;
  QLineEdit* groupSearch_=nullptr;
  int preScanSort_ = kSortNone; // sort restored when the running scan ends
  // 0.9.4.93: "불러오기" (Load) shows the stored index in None order and locks
  // the sort combo until the next scan (load-only view is never re-sorted).
  // 0.9.4.93: load-only ("불러오기") in-flight state. While true the Load button
  // is disabled (one press per load), a non-modal "loading" notice is up, and
  // onResults must not show the finished-scan report popup.
  bool loading_ = false;
  QDialog* loadWait_ = nullptr;
  // 0.9.4.134: Re-verify notice (separate from Load's notice so the wording is
  // accurate while the duplicate members are being checked).
  QDialog* reverifyWait_ = nullptr;
  // 0.9.4.132: load-only summary state. Load() populates the left summary from
  // the loaded index (files/groups/pairs + load duration) instead of leaving
  // the "-" placeholders; loadedSummary_ switches the verify row to the
  // stored-pair reading ("- / N") and clears on the next scan answer.
  qint64 loadStartMs_ = 0;
  bool loadedSummary_ = false;
  // 0.9.4.142: a scan started while a result set is already on screen freezes
  // that view until the answer arrives (field: Load -> Search blanked the
  // group thumbnails and the right preview while the backend was busy).
  bool holdViewDuringScan_ = false;
  // 0.9.4.143: after a scan that re-analyzed files, silently reload the stored
  // set so the view reflects the corrected pairs (stale pairs dropped).
  bool reloadAfterScan_ = false;
  bool startedWithView_ = false; // 0.9.4.143: scan began with a result set on screen
  bool silentReload_ = false;
  QString reloadSelRep_, reloadSelFile_;
  int reloadSort_ = 0;
  int reloadVerifyTotal_ = 0, reloadVerifyDone_ = 0;
  // 0.9.4.134: "재검증" in-flight flag (same lifecycle as loading_; the view is
  // replaced by the refreshed stored set when results arrive).
  bool reverify_ = false;
  // 0.9.4.94: completion-path dedup. onResults (scan branch) already performs
  // the full middle/right rebuild; scanFinished skips repeating it for the
  // same scan so the "writing report" phase cannot stall the GUI thread twice.
  // Set only by the scan branch of onResults; cleared at scan/load start.
  bool resultsApplied_ = false;
  // 0.9.4.94: right-pane scroll identity. refreshFileViews() recreates every
  // row; same-content refills (streaming, finish, zoom) restore the file
  // position, while a newly selected group starts at the top. The signature
  // embeds the selected group plus the file-set shape.
  QString lastFileSig_;
  // None-mode stable group order (union-find roots) and the per-kind rows
  // currently materialized in the middle pane: (root, signature) pairs used to
  // decide whether a refresh can append or must fall back to a full rebuild.
  QStringList noneOrder_;
  QVector<QPair<QString,QString>> imgShown_, vidShown_;
  QTabWidget* midTabs_=nullptr;
  QTreeWidget *imgTree_=nullptr, *vidTree_=nullptr;
  QListWidget *imgGrid_=nullptr, *vidGrid_=nullptr;
  QComboBox* viewBox_=nullptr; // view-mode dropdown (same style as the preset combo)
  QStyledItemDelegate* tileDelegate_=nullptr; // Explorer-style Tiles renderer for the group grid
  QStyledItemDelegate* defaultDelegate_=nullptr; // plain delegate restored for icon/list modes
  // (setItemDelegate(nullptr) does NOT restore painting; probed null visuals)
  QComboBox* kindBox_=nullptr; // photo/video/all scope dropdown (0.9.4.99)
  QListWidget* ignoreList_=nullptr; QPushButton *unignoreBtn_=nullptr, *clearIgnoreBtn_=nullptr;
  QStackedWidget* groupsStack_=nullptr; QListWidget* groupsList_=nullptr;
  QSplitter* split_=nullptr;
  QTreeWidget* groupsView_=nullptr; QLabel* groupFoot_=nullptr;
  void connectResView(QTreeWidget* tree, QListWidget* grid);
  void fillPair(QTreeWidget* tree, QListWidget* grid, int wantKind, bool syncSel, bool append);
  void addGroupItems(QTreeWidget* tree, QListWidget* grid, int i, int wantKind, bool syncSel);
  QString groupSig(const DupGroup& g) const; // (size,best) signature for append-change detection
  void activateTab(int idx);
  void onMidTabChanged(int idx);
  void updateIgnoreTab();
  void unignoreSelected();
  void clearIgnored();
  void applyIgnore(const QStringList& paths, bool on);
  void gridSelected(QListWidgetItem*, QListWidgetItem*);
  void gridCheckChanged(QListWidgetItem*);
  // right
  QLabel* detailTitle_=nullptr; QLabel* detailSim_=nullptr; QLabel* detailCount_=nullptr;
  QToolButton *viewGrid_=nullptr,*viewList_=nullptr; ZoomSlider* zoom_=nullptr;
  QStackedWidget* viewStack_=nullptr; QListWidget* grid_=nullptr; QTreeWidget* list_=nullptr;
  QTabWidget* detailTabs_=nullptr; QLabel* preview_=nullptr; QFormLayout* detailForm_=nullptr;
  QLabel *exifLabel_=nullptr,*simLabel_=nullptr,*hashLabel_=nullptr; QProgressBar* simBar_=nullptr;
  QToolBar* fileBar_=nullptr;
  // status
  QStatusBar* statusBar_=nullptr; QLabel* statusMsg_=nullptr; QLabel* statusCount_=nullptr;
  QLabel* gpuLbl_=nullptr;
  QProgressBar* statusProg_=nullptr;
  // monitor (P2: owned by the backend; GUI keeps presentation + snapshot cache)
  QSystemTrayIcon* tray_=nullptr; QTimer* monitorTimer_=nullptr;
  bool monitorEnabled_=false;
  QTimer* uiTimer_=nullptr; // throttled refresh while scanning
  QTimer* cpuTimer_=nullptr; // 0.9.4.88: samples the GUI process's own CPU (1 s)
  double guiCpu_=0.0;        // last GUI-process CPU% (0-100 of all cores)
};











