#pragma once
#include <QMainWindow>
#include <QThread>
#include <QSet>
#include <atomic>
#include <memory>
#include <QStringList>
#include "../src/media_search_engine.h"
#include "../src/resource_policy.h"
#include "../src/monitor.h"

class QLineEdit; class QSystemTrayIcon; class QTreeWidget; class QTreeWidgetItem; class QListWidget; class QPushButton;
class QProgressBar; class QLabel; class QComboBox; class QSpinBox; class QStackedWidget;
class QSlider; class QToolButton; class QSplitter; class QCheckBox; class QTimer;

class ScanWorker : public QObject {
 Q_OBJECT
public:
 ScanWorker(QString root, QString appDir, int distance, int cpu, int gpu, bool gpuEnabled);
public slots: void run(); void pause(); void resume(); void cancel();
signals:
 void progress(int,QString); void results(QStringList,QStringList); void finished(QString); void failed(QString);
private:
 QString root_, appDir_; int distance_, cpu_, gpu_; bool gpuEnabled_; msf::ScanControl control_; msf::MediaSearchEngine engine_;
};

class MainWindow : public QMainWindow {
 Q_OBJECT
public:
 explicit MainWindow(QWidget* parent=nullptr); ~MainWindow();
private slots:
 void chooseFolder(); void startScan(); void pauseScan(); void resumeScan(); void cancelScan();
 void scanProgress(int,QString); void scanFinished(QString); void scanFailed(QString); void resourceChanged(int);
 void customResourceChanged(); void groupSelected(QTreeWidgetItem*,QTreeWidgetItem*);
 void showContextMenu(const QPoint&); void openSelected(); void revealSelected(); void renameSelected(); void deleteSelected();
 void copySelected(); void cutSelected(); void pasteFiles(); void viewModeChanged();
private:
 void buildUi(); void setRunning(bool); void populateFiles(const QStringList&,const QStringList&); void populateGroups();
 QStringList selectedPaths() const; void setView(bool tiles); void refreshAfterFileOperation();
 void configureMonitor(); void toggleMonitor(); void toggleMonitorPause(); void monitorEvent(const msf::MonitorEvent&); void showMonitorMatch(const msf::MonitorEvent&); void updateMonitorStatus();
 QLineEdit* folder_=nullptr; QTreeWidget* groups_=nullptr; QListWidget* list_=nullptr;
 QPushButton *browse_=nullptr,*scan_=nullptr,*pause_=nullptr,*resume_=nullptr,*cancel_=nullptr;
 QProgressBar* progress_=nullptr; QLabel* status_=nullptr; QComboBox* preset_=nullptr;
  QSpinBox *cpu_=nullptr,*gpu_=nullptr; QSlider *threshold_=nullptr; QToolButton *details_=nullptr;
  QCheckBox* gpuEnabled_=nullptr;
 QThread* thread_=nullptr; ScanWorker* worker_=nullptr; msf::ResourcePolicy policy_;
 QStringList allPaths_; QStringList matchRows_; QSet<QString> activeGroup_; bool tileView_=true;
 std::unique_ptr<msf::MediaMonitor> monitor_; QSystemTrayIcon* tray_=nullptr; QTimer* monitorTimer_=nullptr; bool monitorEnabled_=false; bool monitorPaused_=false;
};
