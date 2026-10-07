#pragma once
// New Explorer-style UI (mockups in C:\project\uimock):
//  top path/scan bar, left folder tree + summary, middle group list,
//  right detail (grid/list switchable), KO/EN language, mark + context menu,
//  live streaming results during scan.
#include <QMainWindow>
#include <QObject>
#include <QMutex>
#include <QComboBox>
#include <QIcon>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QMap>
#include <QSet>
#include <atomic>
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
class QToolButton; class QSplitter; class QCheckBox; class QTimer; class QStatusBar;
class QTabWidget; class QFormLayout; class QToolBar; class QMenu; class QAction;
class QStyledItemDelegate; class QCloseEvent; class QDialog;

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
  qulonglong testFullRebuildCount() const { return fullRebuildCount_; }
  static void scanLog(const QString& line); // process-wide scan log file
  static void sortTiedReferencePaths(QStringList&, const QHash<QString,qulonglong>&, const QHash<QString,qulonglong>&);
private slots:
  // scan
  void chooseFolder(); void startScan(); void togglePauseScan(); void cancelScan();
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
    void onDetailedLog(QString);
    void showDetailedLogDialog(const QString& json);
  void resourceChanged(int); void customResourceChanged();
  // groups / files
  void groupSelected(QTreeWidgetItem*,QTreeWidgetItem*); void fileGridSelected(); void fileListSelected();
  void setViewMode(int); void zoomChanged(int); void groupSearchChanged(const QString&);
  void groupViewChanged(int);
  void applyExecutionMode(); void updateKindSelection();
  // P4: builds the two-axis ExecutionPolicy from policy_ + strategy box.
  ExecutionPolicy currentExecPolicy() const;
  void toggleMarkSelected(); void markAll(bool); void invertMarked();
  void setGroupMarked(int gi, bool on);
  void showFileMenu(const QPoint&); void showGroupMenu(const QPoint&);
  void openSelected(); void revealSelected(); void renameSelected(); void deleteSelected();
  void revealPath(const QString& path);
  void previewSelectedQuickLook(); QString quickLookTarget() const; // empty when QuickLook unusable
  void pollQuickLookPipe(); // async launch follow-up (bounded, event-loop driven)
  void copySelected(); void cutSelected(); void pasteFiles(); void moveSelected();
  void refreshFolders(); void folderActivated(QTreeWidgetItem*,int);
  void populateFolderChildren(QTreeWidgetItem*);
  QStringList selectedFiles() const;
  void prunePaths(const QSet<QString>&);
  void refreshAfterFileOperation(const QString&);
  void detailTabChanged(int); void fileActivated(QListWidgetItem*);
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
  void setRunning(bool);
  void applyDetailLogVisibility(); // show/hide logTgl_ from ui/showDetailLog (default ON)
  void rebuildGroups();          // union-find over accumulated matches
  void updateGroupFoot();        // "전체 N · 선택 M" footer label
  void onUiTick();               // 600ms timer body (extracted for testUiTick)
  void refreshStreaming(bool force=false); // throttled rebuild+fill for live scans
  void thumbCatchUpVisible();    // in-place thumbnail fill for visible items only (never rebuilds)
  bool scrollGateActive() const; // slider held or inside the post-scroll cooldown
  void noteUserScroll();         // stamp a user navigation event (wheel/keys/slider)
  void refreshGroupList();       // middle pane from groups_
  void refreshFileViews();       // right grid+list from selected group
  void refreshDetail();          // tabs for current file
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
  qulonglong filePixels(const QString&) const;
  QIcon fileThumb(const QString&, const QSize&, bool bypassBudget = false) const;
  void onThumbReady(quint64 requestId, const ThumbResult& thumb);
  void onFileMetaReady(quint64 requestId, const FileMetaResult& meta);
  void requestFileMeta(const QString& path) const;
  // FileMeta in flight, id-keyed like thumbnails (Type B dedup + stale-drop).
  // Mutable: refreshDetail and fileResolution are const but trigger requests.
  mutable QMap<quint64, QString> fileMetaPending_;
  mutable quint64 fileMetaRequestId_ = 0;
  // In-place fileMeta paint, mirroring the 0.9.4.66 thumbnail pattern: update
  // the matching grid cell / list row text without recreating widgets (which
  // would destroy the user's selection — the P4 selection-lock regression).
  void refreshFileMetaRow(const QString& path);
  QIcon placeholderIcon(const QString& path) const; // per-suffix file-type icon
  void dropThumbCache(const QString& path); // exact + sized variants
  double pathBest(const QString&) const;
  void addMatch(const QString&, const QString&, double, int kind);
  QString findRoot(const QString&); // union-find over pathParent_
  // scan state (P2: the backend lives behind BackendClient; the loopback
  // implementation runs the real ScanWorker/MediaMonitor in-process)
  BackendClient* backend_ = nullptr; QDialog* cancelWait_=nullptr; msf::ResourcePolicy policy_;
  bool backendAvailable_ = true; // supervisor connection state (§21 guard)
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
  bool scanning_=false; qint64 scanStartMs_=0; bool groupsDirty_=false; bool scanPaused_=false;
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
  QHash<QString,int> pathKind_; QHash<QString,double> fileDur_;
  mutable QHash<QString,QIcon> thumbCache_; // memory presentation cache (P3: disk cache lives in Backend)
  mutable QHash<QString,QIcon> phCache_; // placeholder icons by lowercase suffix
  // P3: async thumbnail requests in flight (Type B). Keyed by requestId;
  // the arrival paints only surfaces still wanting that size (stale-drop).
  // Capped: a lost backend must not grow this without bound.
  struct ThumbReq { QString path; QSize size; };
  mutable QMap<quint64, ThumbReq> thumbPendingReq_;
  mutable quint64 thumbRequestId_ = 0;
  // P3: GUI no longer decodes (all decode is Backend-side), so the per-tick
  // budgets and the starvation flag are gone. Miss accounting lives in the
  // pending-request map; arrivals paint via onThumbReady.
  mutable qulonglong thumbStatMem_ = 0, thumbStatBackend_ = 0, thumbStatPlace_ = 0;
  mutable qulonglong thumbPaintedCount_ = 0;
  QSet<QString> ignored_;
  msf::SearchReport lastReport_; bool hasReport_=false;
  // toolbar
  QToolBar* toolBar_=nullptr;
  QLineEdit* folder_=nullptr;   QPushButton *scan_=nullptr,*pause_=nullptr,
    *cancel_=nullptr,*refresh_=nullptr,*monBtn_=nullptr,*logBtn_=nullptr;
  QAction *monSettingsAct_=nullptr, *helpAct_=nullptr; // retexted on language change
  QToolButton* utilBtn_=nullptr;
  QString lastTelemetryJson_;
  QComboBox* preset_=nullptr; QSpinBox* cpu_=nullptr; QCheckBox* gpuEnabled_=nullptr;
  QCheckBox* logTgl_=nullptr; QAction* logTglAct_=nullptr;
  // Execution resource strategy (single-select dropdown): the user's
  // real-search resource choice. logTgl_ enables diagnostic telemetry
  // on the scan.
  QComboBox* strategyBox_=nullptr;
  // left
  QTreeWidget* folders_=nullptr;   QLabel *sumTotal_=nullptr,*sumDone_=nullptr,*sumIndexed_=nullptr,*sumGroups_=nullptr,
    *sumDup_=nullptr,*sumTime_=nullptr,*sumGpu_=nullptr,*sumCpu_=nullptr,*sumRam_=nullptr;
  QLabel *sumValTotal_=nullptr,*sumValDone_=nullptr,*sumValIndexed_=nullptr,*sumValGroups_=nullptr,
    *sumValDup_=nullptr,*sumValTime_=nullptr,*sumValGpu_=nullptr,*sumValCpu_=nullptr,*sumValRam_=nullptr;
  // middle
  QLabel* groupTitle_=nullptr; QComboBox* sortBox_=nullptr; QLineEdit* groupSearch_=nullptr;
  QTabWidget* midTabs_=nullptr;
  QTreeWidget *imgTree_=nullptr, *vidTree_=nullptr;
  QListWidget *imgGrid_=nullptr, *vidGrid_=nullptr;
  QComboBox* viewBox_=nullptr; // view-mode dropdown (same style as the preset combo)
  QStyledItemDelegate* tileDelegate_=nullptr; // Explorer-style Tiles renderer for the group grid
  QStyledItemDelegate* defaultDelegate_=nullptr; // plain delegate restored for icon/list modes
  // (setItemDelegate(nullptr) does NOT restore painting; probed null visuals)
  QPushButton *mediaImgBtn_=nullptr, *mediaVidBtn_=nullptr;   // toggle photo/video scope
  QListWidget* ignoreList_=nullptr; QPushButton *unignoreBtn_=nullptr, *clearIgnoreBtn_=nullptr;
  QStackedWidget* groupsStack_=nullptr; QListWidget* groupsList_=nullptr;
  QSplitter* split_=nullptr;
  QTreeWidget* groupsView_=nullptr; QLabel* groupFoot_=nullptr;
  void connectResView(QTreeWidget* tree, QListWidget* grid);
  void fillPair(QTreeWidget* tree, QListWidget* grid, int wantKind, bool syncSel);
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
  QToolButton *viewGrid_=nullptr,*viewList_=nullptr; QSlider* zoom_=nullptr;
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
};
