#pragma once
// New Explorer-style UI (mockups in C:\project\uimock):
//  top path/scan bar, left folder tree + summary, middle group list,
//  right detail (grid/list switchable), KO/EN language, mark + context menu,
//  live streaming results during scan.
#include <QMainWindow>
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QIcon>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QSet>
#include <atomic>
#include <memory>
#include "../src/media_search_engine.h"
#include "../src/resource_policy.h"
#include "../src/monitor.h"

class QLineEdit; class QSystemTrayIcon; class QTreeWidget; class QTreeWidgetItem;
class QListWidget; class QListWidgetItem; class QPushButton; class QProgressBar;
class QLabel; class QComboBox; class QSpinBox; class QStackedWidget; class QSlider;
class QToolButton; class QSplitter; class QCheckBox; class QTimer; class QStatusBar;
class QTabWidget; class QFormLayout; class QToolBar; class QMenu; class QAction;
class QStyledItemDelegate;

// ---------------------------------------------------------------- language
enum class UiLang { Ko, En };
QString trStr(UiLang lang, const char* key); // KO/EN string table (see .cpp)
// One-time QSettings bootstrap (org/app names + INI location). Call once in
// main() before any default-constructed QSettings is used.
void initAppSettings(const QString& portableDir);

// A single streamed match (paths resolved in the worker thread).
struct LiveMatch { QString left, right; double percent=0; int kind=1; };
// Per-file data snapshot handed to the GUI thread when a scan finishes.
struct GuiFile { QString path; qulonglong size=0; QString fpHex; double duration=0; };
Q_DECLARE_METATYPE(GuiFile)

// ---------------------------------------------------------------- worker
class ScanWorker : public QObject {
  Q_OBJECT
public:
  ScanWorker(QString root, QString appDir, int distance, int cpu, int gpu, bool gpuEnabled,
             bool scanImages=true, bool scanVideos=true);
public slots:
  void run(); void pause(); void resume(); void cancel();
  void setIgnored(const QSet<QString>& s);
  QVector<LiveMatch> takePending(); // thread-safe drain for the GUI
  qulonglong gpuDone() const { return gpuDone_.load(); }
  bool gpuAvailable() const { return gpuAvail_; }
signals:
  void progress(int,QString);
  void progressCount(qulonglong,qulonglong);
  void listingProgress(std::size_t);
  void matchesArrived();            // throttled; call takePending()
  void quickLoaded(int);            // stored matches reloaded from the index
  void results(QVector<GuiFile> files, QStringList matchRows);
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
  std::atomic<qulonglong> gpuDone_{0}; // live GPU-accelerated image count
  bool gpuAvail_=false;                // CUDA backend present at construction
};

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
  explicit MainWindow(QWidget* parent=nullptr); ~MainWindow();
private slots:
  // scan
  void chooseFolder(); void startScan(); void togglePauseScan(); void cancelScan();
  void scanProgress(int,QString); void drainMatches(); void scanFinished(QString); void scanFailed(QString);
  void onScanCounts(qulonglong,qulonglong);
  void onListingProgress(std::size_t);
  void onQuickLoaded(int);
  void onResults(QVector<GuiFile> files, QStringList matchRows);
  void resourceChanged(int); void customResourceChanged();
  // groups / files
  void groupSelected(QTreeWidgetItem*,QTreeWidgetItem*); void fileGridSelected(); void fileListSelected();
  void setViewMode(int); void zoomChanged(int); void groupSearchChanged(const QString&);
  void groupViewChanged(int); void updateKindBtn();
  void toggleMarkSelected(); void markAll(bool); void invertMarked();
  void setGroupMarked(int gi, bool on);
  void showFileMenu(const QPoint&); void showGroupMenu(const QPoint&);
  void openSelected(); void revealSelected(); void renameSelected(); void deleteSelected();
  void copySelected(); void cutSelected(); void pasteFiles(); void moveSelected();
  void refreshFolders(); void folderActivated(QTreeWidgetItem*,int);
  void populateFolderChildren(QTreeWidgetItem*);
  QStringList selectedFiles() const;
  void prunePaths(const QSet<QString>&);
  void refreshAfterFileOperation(const QString&);
  void detailTabChanged(int); void fileActivated(QListWidgetItem*);
  // monitor
  void configureMonitor(); void toggleMonitor(); void toggleMonitorPause();
  void monitorEvent(const msf::MonitorEvent&); void showMonitorMatch(const msf::MonitorEvent&); void updateMonitorStatus();
  // misc
  void setLanguage(int); void showHelp(); void applyStaticTexts();
private:
  UiLang lang() const;
  void buildUi(); void buildToolbar(); void buildLeft(QWidget*); void buildMiddle(QWidget*); void buildRight(QWidget*);
  void setRunning(bool);
  void rebuildGroups();          // union-find over accumulated matches
  void refreshGroupList();       // middle pane from groups_
  void refreshFileViews();       // right grid+list from selected group
  void refreshDetail();          // tabs for current file
  void refreshSummary(const msf::SearchReport* r=nullptr);
  void updateGpuLabel();
  void updateStatusCounts();
  void scanHeartbeat();
  void saveUiState();              // window geometry + splitter + header layouts
  void restoreUiState();           // counterpart applied after buildUi()
  static void scanLog(const QString& line);
  QString fmtSize(qulonglong) const;
  QString fileResolution(const QString&) const; // cached QImageReader::size
  QIcon fileThumb(const QString&, const QSize&, bool bypassBudget = false) const;
  QIcon placeholderIcon(const QString& path) const; // per-suffix file-type icon
  double pathBest(const QString&) const;
  void addMatch(const QString&, const QString&, double, int kind);
  QString findRoot(const QString&); // union-find over pathParent_
  // scan state
  QThread* thread_=nullptr; ScanWorker* worker_=nullptr; msf::ResourcePolicy policy_;
  QVector<LiveMatch> matches_;                 // accumulated this scan
  QVector<DupGroup> groups_;                   // built from matches_
  QHash<QString,int> pathGroup_;               // path -> group index
  QHash<QString,QString> pathParent_;           // union-find parent
  QStringList allPaths_; QStringList matchRows_;
  QHash<QString,QString> fileSize_; QHash<QString,QString> fileFp_;
  mutable QHash<QString,QString> resCache_;
  QString currentFile_; int currentGroup_=-1;
  int lastPct_=0; QString lastPath_; int maxPctShown_=0;
  qulonglong lastDoneN_=0, lastTotalN_=0;
  QStringList cutPaths_;
  bool scanning_=false; qint64 scanStartMs_=0; bool groupsDirty_=false; bool scanPaused_=false;
  QStringList lastStats_; // scanned|analyzed|unchanged|groups|candidates from finished()
  qint64 repElapsedMs_=0;
  QHash<QString,double> bestPct_; QSet<QString> marked_;
  QHash<QString,int> pathKind_; QHash<QString,double> fileDur_;
  mutable QHash<QString,QIcon> thumbCache_;
  mutable QHash<QString,QIcon> phCache_; // placeholder icons by lowercase suffix
  // Per-tick fresh-decode budget (see fileThumb): bounds GUI-thread blockage
  // so pause/cancel/close stay responsive during huge scans. Reset each tick.
  mutable int thumbBudget_ = 0;
  QSet<QString> ignored_;
  std::size_t lastDone_=0, lastTotal_=0;
  msf::SearchReport lastReport_; bool hasReport_=false;
  // toolbar
  QToolBar* toolBar_=nullptr;
  QLineEdit* folder_=nullptr; QPushButton *browse_=nullptr,*scan_=nullptr,*pause_=nullptr,
    *cancel_=nullptr,*refresh_=nullptr,*monBtn_=nullptr,*monPauseBtn_=nullptr;
  QComboBox* preset_=nullptr; QSpinBox *cpu_=nullptr,*gpu_=nullptr; QCheckBox* gpuEnabled_=nullptr;
  // left
  QTreeWidget* folders_=nullptr;   QLabel *sumTotal_=nullptr,*sumDone_=nullptr,*sumGroups_=nullptr,
    *sumDup_=nullptr,*sumTime_=nullptr,*sumGpu_=nullptr,*sumCpu_=nullptr,*sumRam_=nullptr,*sumMon_=nullptr;
  QLabel *sumValTotal_=nullptr,*sumValDone_=nullptr,*sumValGroups_=nullptr,
    *sumValDup_=nullptr,*sumValTime_=nullptr,*sumValGpu_=nullptr,*sumValCpu_=nullptr,*sumValRam_=nullptr,*sumValMon_=nullptr;
  // middle
  QLabel* groupTitle_=nullptr; QComboBox* sortBox_=nullptr; QLineEdit* groupSearch_=nullptr;
  QTabWidget* midTabs_=nullptr;
  QTreeWidget *imgTree_=nullptr, *vidTree_=nullptr;
  QListWidget *imgGrid_=nullptr, *vidGrid_=nullptr;
  QToolButton* viewBtn_=nullptr; QMenu* viewMenu_=nullptr; QVector<QAction*> viewActs_;
  QStyledItemDelegate* tileDelegate_=nullptr; // Explorer-style Tiles renderer for the group grid
  QToolButton* kindBtn_=nullptr; QMenu* kindMenu_=nullptr; QAction *kindImgAct_=nullptr, *kindVidAct_=nullptr;
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
  // monitor
  std::unique_ptr<msf::MediaMonitor> monitor_; QSystemTrayIcon* tray_=nullptr; QTimer* monitorTimer_=nullptr;
  bool monitorEnabled_=false; bool monitorPaused_=false;
  QTimer* uiTimer_=nullptr; // throttled refresh while scanning
};
