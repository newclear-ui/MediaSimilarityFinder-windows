#include "mainwindow.h"
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QThread>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QToolButton>
#include <QShortcut>
#include <QDesktopServices>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QInputDialog>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QGridLayout>
#include <QMetaObject>
#include <QTimer>
#include <algorithm>
#include <filesystem>
#include <cmath>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

ScanWorker::ScanWorker(QString root,QString appDir,int distance,int cpu,int gpu,bool gpuEnabled):root_(std::move(root)),appDir_(std::move(appDir)),distance_(distance),cpu_(cpu),gpu_(gpu),gpuEnabled_(gpuEnabled){}
void ScanWorker::run(){
 try {
  engine_.setResourcePolicy(msf::make_policy(msf::ResourceMode::Custom,cpu_,gpu_));
  auto policy=engine_.resourcePolicy(); policy.gpuEnabled=gpuEnabled_; engine_.setResourcePolicy(policy);
  if(!engine_.openIndexForRoot(root_.toStdString(),appDir_.toStdString())) throw std::runtime_error("Portable index open failed");
  control_.progress=[this](std::size_t done,std::size_t total,const std::string& path){emit progress(total?int(done*100/total):100,QString::fromStdString(path));};
  auto r=engine_.scan(root_.toStdString(),unsigned(distance_),&control_);
  if(control_.cancel.load()){emit finished("Scan cancelled");return;}
  QStringList paths, matches;
  const auto& fs=engine_.files();
  for(const auto& f:fs) paths << QString::fromStdString(f.path);
  for(const auto& m:r.matches) matches << (QString::fromStdString(m.leftPath)+"\t"+QString::fromStdString(m.rightPath)+"\t"+QString::number(m.percent,'f',1));
  emit results(paths,matches);
  emit finished(QString("Scan complete: %1 files, %2 analyzed, %3 candidates, %4 groups — GPU %5, CPU fallback %6").arg(r.scanned).arg(r.analyzed).arg(r.candidates).arg(r.groups).arg(r.gpuImages).arg(r.gpuFallbackImages));
 } catch(const std::exception& e){emit failed(e.what());}
}
void ScanWorker::pause(){control_.pause.store(true);} void ScanWorker::resume(){control_.pause.store(false);} void ScanWorker::cancel(){control_.cancel.store(true);control_.pause.store(false);}

MainWindow::MainWindow(QWidget* p):QMainWindow(p){buildUi(); monitor_=std::make_unique<msf::MediaMonitor>(); monitorTimer_=new QTimer(this); monitorTimer_->setInterval(1000); connect(monitorTimer_,&QTimer::timeout,this,&MainWindow::updateMonitorStatus); monitorTimer_->start();
 tray_=new QSystemTrayIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon),this); auto *tm=new QMenu(this); tm->addAction("Configure monitor folders…",this,&MainWindow::configureMonitor); tm->addAction("Start/Stop monitor",this,&MainWindow::toggleMonitor); tm->addAction("Pause/Resume analysis",this,&MainWindow::toggleMonitorPause); tm->addSeparator(); tm->addAction("Show window",this,&MainWindow::showNormal); tm->addAction("Exit",qApp,&QApplication::quit); tray_->setContextMenu(tm); tray_->setToolTip("Media Similarity Finder — monitor stopped"); tray_->show();}
MainWindow::~MainWindow(){if(worker_){worker_->cancel();thread_->quit();thread_->wait();} if(monitor_) monitor_->stop();}

void MainWindow::buildUi(){
 setWindowTitle("Media Similarity Finder 0.9.2.34 (CUDA/CPU Windows)"); resize(1440,850);
 auto *central=new QWidget(this); auto *root=new QVBoxLayout(central); root->setContentsMargins(8,8,8,8);
 auto *bar=new QHBoxLayout;
 folder_=new QLineEdit; folder_->setPlaceholderText("Search folder…"); browse_=new QPushButton("Browse…");
 preset_=new QComboBox; preset_->addItems({"Maximum","Balanced","Gaming","Custom"});
 cpu_=new QSpinBox; gpu_=new QSpinBox; cpu_->setRange(1,100); gpu_->setRange(1,100); cpu_->setSuffix("% CPU"); gpu_->setSuffix("% GPU");
 cpu_->setValue(policy_.cpuPercent); gpu_->setValue(policy_.gpuPercent);
 gpuEnabled_=new QCheckBox("Enable GPU"); gpuEnabled_->setChecked(true);
 threshold_=new QSlider(Qt::Horizontal); threshold_->setRange(1,20); threshold_->setValue(8); threshold_->setToolTip("Maximum Hamming distance"); threshold_->setFixedWidth(110);
 scan_=new QPushButton("Scan"); pause_=new QPushButton("Pause"); resume_=new QPushButton("Resume"); cancel_=new QPushButton("Cancel");
 details_=new QToolButton; details_->setText("☷"); details_->setCheckable(true); details_->setToolTip("Toggle details view");
 bar->addWidget(folder_,1); bar->addWidget(browse_); bar->addWidget(preset_); bar->addWidget(cpu_); bar->addWidget(gpu_); bar->addWidget(gpuEnabled_); bar->addWidget(new QLabel("Tolerance")); bar->addWidget(threshold_); bar->addWidget(scan_); bar->addWidget(pause_); bar->addWidget(resume_); bar->addWidget(cancel_); bar->addWidget(details_); root->addLayout(bar);
 progress_=new QProgressBar; progress_->setRange(0,100); progress_->setValue(0); status_=new QLabel("Ready"); root->addWidget(progress_); root->addWidget(status_);
 auto *split=new QSplitter(Qt::Horizontal); groups_=new QTreeWidget; groups_->setHeaderLabel("Similar groups"); groups_->setMinimumWidth(250);
 list_=new QListWidget; list_->setSelectionMode(QAbstractItemView::ExtendedSelection); list_->setContextMenuPolicy(Qt::CustomContextMenu); list_->setIconSize(QSize(128,128)); setView(true);
 split->addWidget(groups_); split->addWidget(list_); split->setStretchFactor(1,1); root->addWidget(split,1); setCentralWidget(central);
 connect(browse_,&QPushButton::clicked,this,&MainWindow::chooseFolder); connect(scan_,&QPushButton::clicked,this,&MainWindow::startScan);
 connect(pause_,&QPushButton::clicked,this,&MainWindow::pauseScan); connect(resume_,&QPushButton::clicked,this,&MainWindow::resumeScan); connect(cancel_,&QPushButton::clicked,this,&MainWindow::cancelScan);
 connect(preset_,qOverload<int>(&QComboBox::currentIndexChanged),this,&MainWindow::resourceChanged); connect(cpu_,qOverload<int>(&QSpinBox::valueChanged),this,&MainWindow::customResourceChanged); connect(gpu_,qOverload<int>(&QSpinBox::valueChanged),this,&MainWindow::customResourceChanged);
 connect(groups_,&QTreeWidget::currentItemChanged,this,&MainWindow::groupSelected); connect(list_,&QListWidget::customContextMenuRequested,this,&MainWindow::showContextMenu); connect(list_,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem*){openSelected();}); connect(details_,&QToolButton::toggled,this,[this](bool){setView(!details_->isChecked());});
 new QShortcut(QKeySequence("Ctrl+C"),this,SLOT(copySelected())); new QShortcut(QKeySequence("Ctrl+X"),this,SLOT(cutSelected())); new QShortcut(QKeySequence("Ctrl+V"),this,SLOT(pasteFiles())); new QShortcut(QKeySequence("Delete"),this,SLOT(deleteSelected())); new QShortcut(QKeySequence("F2"),this,SLOT(renameSelected()));
 setRunning(false);
}
void MainWindow::chooseFolder(){auto d=QFileDialog::getExistingDirectory(this,"Select media folder",folder_->text());if(!d.isEmpty())folder_->setText(d);}
void MainWindow::setRunning(bool v){scan_->setEnabled(!v);browse_->setEnabled(!v);pause_->setEnabled(v);resume_->setEnabled(v);cancel_->setEnabled(v);preset_->setEnabled(!v);threshold_->setEnabled(!v); gpuEnabled_->setEnabled(!v);}
void MainWindow::startScan(){
 if(folder_->text().isEmpty()){chooseFolder();if(folder_->text().isEmpty())return;} if(thread_)return;
 QString appDir=QApplication::applicationDirPath();
 thread_=new QThread(this); worker_=new ScanWorker(folder_->text(),appDir,threshold_->value(),cpu_->value(),gpu_->value(),gpuEnabled_->isChecked()); worker_->moveToThread(thread_);
 connect(thread_,&QThread::started,worker_,&ScanWorker::run); connect(worker_,&ScanWorker::progress,this,&MainWindow::scanProgress); connect(worker_,&ScanWorker::results,this,&MainWindow::populateFiles);
 connect(worker_,&ScanWorker::finished,this,&MainWindow::scanFinished); connect(worker_,&ScanWorker::failed,this,&MainWindow::scanFailed); connect(worker_,&ScanWorker::finished,thread_,&QThread::quit); connect(worker_,&ScanWorker::failed,thread_,&QThread::quit);
 connect(thread_,&QThread::finished,worker_,&QObject::deleteLater); connect(thread_,&QThread::finished,thread_,&QObject::deleteLater); connect(thread_,&QThread::finished,this,[this]{worker_=nullptr;thread_=nullptr;setRunning(false);});
 setRunning(true); progress_->setValue(0); status_->setText("Scanning…"); thread_->start();
}
void MainWindow::pauseScan(){if(worker_)QMetaObject::invokeMethod(worker_,"pause",Qt::QueuedConnection);status_->setText("Paused");}
void MainWindow::resumeScan(){if(worker_)QMetaObject::invokeMethod(worker_,"resume",Qt::QueuedConnection);status_->setText("Scanning…");}
void MainWindow::cancelScan(){if(worker_)QMetaObject::invokeMethod(worker_,"cancel",Qt::QueuedConnection);status_->setText("Cancelling…");}
void MainWindow::scanProgress(int p,QString path){progress_->setValue(p);status_->setText(QString("Scanning %1% — %2").arg(p).arg(QFileInfo(path).fileName()));}
void MainWindow::scanFinished(QString msg){status_->setText(msg);}
void MainWindow::scanFailed(QString msg){status_->setText("Error: "+msg);QMessageBox::critical(this,"Scan error",msg);}
void MainWindow::resourceChanged(int i){auto m=static_cast<msf::ResourceMode>(i+1);policy_=msf::make_policy(m,cpu_->value(),gpu_->value()); if(monitor_) monitor_->setPolicy(policy_);cpu_->blockSignals(true);gpu_->blockSignals(true);cpu_->setValue(policy_.cpuPercent);gpu_->setValue(policy_.gpuPercent);cpu_->blockSignals(false);gpu_->blockSignals(false);}
void MainWindow::customResourceChanged(){if(preset_->currentIndex()!=3)preset_->setCurrentIndex(3);policy_=msf::make_policy(msf::ResourceMode::Custom,cpu_->value(),gpu_->value()); if(monitor_) monitor_->setPolicy(policy_);}
void MainWindow::populateFiles(const QStringList& paths,const QStringList& matches){allPaths_=paths;matchRows_=matches;populateGroups();setView(tileView_);}
void MainWindow::populateGroups(){
 groups_->clear();
 struct G{QStringList paths;double best=0;}; QVector<G> gs;
 for(const auto&r:matchRows_){auto p=r.split('\t');if(p.size()!=3)continue;int a=-1,b=-1;for(int i=0;i<gs.size();++i){if(gs[i].paths.contains(p[0]))a=i;if(gs[i].paths.contains(p[1]))b=i;}double sim=p[2].toDouble();
  if(a<0&&b<0){G g;g.paths<<p[0]<<p[1];g.best=sim;gs.push_back(g);} else if(a>=0&&b<0){gs[a].paths<<p[1];gs[a].best=std::max(gs[a].best,sim);} else if(a<0&&b>=0){gs[b].paths<<p[0];gs[b].best=std::max(gs[b].best,sim);} else if(a!=b){gs[a].paths<<gs[b].paths;gs[a].best=std::max(gs[a].best,gs[b].best);gs.removeAt(b);} }
 for(int i=0;i<gs.size();++i){auto *top=new QTreeWidgetItem(groups_,{QString("Group %1  •  %2%%").arg(i+1).arg(gs[i].best,0,'f',1)});top->setData(0,Qt::UserRole,gs[i].paths.join("\n"));for(const auto&p:gs[i].paths)new QTreeWidgetItem(top,{QFileInfo(p).fileName()});}
 if(groups_->topLevelItemCount())groups_->setCurrentItem(groups_->topLevelItem(0));
}
void MainWindow::groupSelected(QTreeWidgetItem*cur,QTreeWidgetItem*){activeGroup_.clear();if(!cur)return;QString data=cur->data(0,Qt::UserRole).toString();if(data.isEmpty()&&cur->parent())data=cur->parent()->data(0,Qt::UserRole).toString();for(const auto&p:data.split('\n',Qt::SkipEmptyParts))activeGroup_.insert(p);setView(tileView_);}
QStringList MainWindow::selectedPaths() const{QStringList r;for(auto*i:list_->selectedItems())r<<i->data(Qt::UserRole).toString();return r;}
void MainWindow::setView(bool tiles){tileView_=tiles;list_->clear();list_->setViewMode(tiles?QListView::IconMode:QListView::ListMode);list_->setResizeMode(QListView::Adjust);list_->setSpacing(tiles?14:2);list_->setIconSize(tiles?QSize(128,128):QSize(32,32));
 QStringList source=allPaths_; if(!activeGroup_.isEmpty()){ source.clear(); for(const auto& p: activeGroup_) source << p; }
 std::sort(source.begin(),source.end(),[](const QString&a,const QString&b){return a.toLower()<b.toLower();}); QFileIconProvider icons;
 for(const auto&p:source){QFileInfo fi(p);auto *it=new QListWidgetItem;it->setText(tileView_?fi.fileName():fi.fileName()+"  —  "+QString::number(fi.size()/1024.0,'f',1)+" KB");it->setData(Qt::UserRole,p);it->setToolTip(p);
  QImageReader rd(p); if(rd.canRead()){rd.setAutoTransform(true);QImage im=rd.read();if(!im.isNull())it->setIcon(QPixmap::fromImage(im.scaled(128,128,Qt::KeepAspectRatio,Qt::SmoothTransformation)));else it->setIcon(icons.icon(fi));}else it->setIcon(icons.icon(fi)); list_->addItem(it); }
}
void MainWindow::viewModeChanged(){}
void MainWindow::showContextMenu(const QPoint&p){if(!list_->itemAt(p))return;QMenu m(this);m.addAction("Open",this,&MainWindow::openSelected);m.addAction("Show in Explorer",this,&MainWindow::revealSelected);m.addSeparator();m.addAction("Rename",this,&MainWindow::renameSelected);m.addAction("Delete",this,&MainWindow::deleteSelected);m.addSeparator();m.addAction("Copy",this,&MainWindow::copySelected);m.addAction("Cut",this,&MainWindow::cutSelected);m.exec(list_->viewport()->mapToGlobal(p));}
void MainWindow::openSelected(){auto ps=selectedPaths();for(const auto&p:ps)QDesktopServices::openUrl(QUrl::fromLocalFile(p));}
void MainWindow::revealSelected(){auto ps=selectedPaths();if(ps.isEmpty())return;QProcess::startDetached("explorer.exe",{QString("/select,%1").arg(QDir::toNativeSeparators(ps.first()))});}
void MainWindow::renameSelected(){auto ps=selectedPaths();if(ps.size()!=1)return;QFileInfo fi(ps.first());bool ok=false;QString n=QInputDialog::getText(this,"Rename","New name:",QLineEdit::Normal,fi.fileName(),&ok);if(ok&&!n.isEmpty()&&n!=fi.fileName()){if(!QFile::rename(fi.filePath(),fi.dir().filePath(n)))QMessageBox::warning(this,"Rename","Could not rename file.");else refreshAfterFileOperation();}}
void MainWindow::deleteSelected(){auto ps=selectedPaths();if(ps.isEmpty())return;if(QMessageBox::question(this,"Delete",QString("Delete %1 selected file(s)?").arg(ps.size()))!=QMessageBox::Yes)return;for(const auto&p:ps)QFile::remove(p);refreshAfterFileOperation();}
void MainWindow::copySelected(){auto ps=selectedPaths();if(ps.isEmpty())return;auto *d=new QMimeData;QList<QUrl> urls;for(const auto&p:ps)urls<<QUrl::fromLocalFile(p);d->setUrls(urls);QApplication::clipboard()->setMimeData(d);status_->setText(QString("Copied %1 file(s)").arg(ps.size()));}
void MainWindow::cutSelected(){copySelected();status_->setText("Cut mode: paste into a folder to move the selected files");}
void MainWindow::pasteFiles(){const auto *d=QApplication::clipboard()->mimeData();if(!d||!d->hasUrls()||folder_->text().isEmpty())return;int n=0;for(const auto&u:d->urls()){QString src=u.toLocalFile();QString dst=QDir(folder_->text()).filePath(QFileInfo(src).fileName());if(QFile::copy(src,dst))++n;}status_->setText(QString("Pasted %1 file(s)").arg(n));}
void MainWindow::refreshAfterFileOperation(){if(!folder_->text().isEmpty())startScan();}


void MainWindow::configureMonitor(){
    QSettings st;
    QDialog dlg(this);
    dlg.setWindowTitle("Real-time Monitor Settings");
    dlg.resize(760, 520);
    auto *root = new QVBoxLayout(&dlg);

    auto *watchGroup = new QGroupBox("Watched folders — new/changed media triggers comparison", &dlg);
    auto *watchLayout = new QVBoxLayout(watchGroup);
    auto *watchList = new QListWidget(watchGroup);
    watchList->addItems(st.value("monitor/watchRoots").toStringList());
    auto *watchButtons = new QHBoxLayout;
    auto *watchAdd = new QPushButton("Add folder…", watchGroup);
    auto *watchRemove = new QPushButton("Remove", watchGroup);
    watchButtons->addWidget(watchAdd); watchButtons->addWidget(watchRemove); watchButtons->addStretch();
    watchLayout->addWidget(watchList); watchLayout->addLayout(watchButtons);
    root->addWidget(watchGroup, 1);

    auto *compareGroup = new QGroupBox("Comparison folders — existing media used for duplicate detection", &dlg);
    auto *compareLayout = new QVBoxLayout(compareGroup);
    auto *compareList = new QListWidget(compareGroup);
    compareList->addItems(st.value("monitor/compareRoots").toStringList());
    auto *compareButtons = new QHBoxLayout;
    auto *compareAdd = new QPushButton("Add folder…", compareGroup);
    auto *compareRemove = new QPushButton("Remove", compareGroup);
    compareButtons->addWidget(compareAdd); compareButtons->addWidget(compareRemove); compareButtons->addStretch();
    compareLayout->addWidget(compareList); compareLayout->addLayout(compareButtons);
    root->addWidget(compareGroup, 1);

    auto *settingsGroup = new QGroupBox("Analysis policy", &dlg);
    auto *settings = new QGridLayout(settingsGroup);
    auto *thresholdLabel = new QLabel("Similarity threshold:", settingsGroup);
    auto *threshold = new QSpinBox(settingsGroup); threshold->setRange(50,100); threshold->setSuffix(" %"); threshold->setValue(st.value("monitor/thresholdPercent",90).toInt());
    auto *stableLabel = new QLabel("Stable file delay:", settingsGroup);
    auto *stable = new QSpinBox(settingsGroup); stable->setRange(1,60); stable->setSuffix(" s"); stable->setValue(st.value("monitor/stableSeconds",3).toInt());
    auto *pollLabel = new QLabel("Fallback poll interval:", settingsGroup);
    auto *poll = new QSpinBox(settingsGroup); poll->setRange(1,60); poll->setSuffix(" s"); poll->setValue(st.value("monitor/pollSeconds",2).toInt());
    auto *gpu = new QCheckBox("Allow GPU acceleration for monitor analysis", settingsGroup); gpu->setChecked(st.value("monitor/gpuEnabled",gpuEnabled_->isChecked()).toBool());
    settings->addWidget(thresholdLabel,0,0); settings->addWidget(threshold,0,1); settings->addWidget(stableLabel,1,0); settings->addWidget(stable,1,1); settings->addWidget(pollLabel,2,0); settings->addWidget(poll,2,1); settings->addWidget(gpu,3,0,1,2);
    root->addWidget(settingsGroup);

    auto addFolder = [this](QListWidget *list){
        const QString dir=QFileDialog::getExistingDirectory(this,"Select monitor folder");
        if(dir.isEmpty()) return;
        const QString clean=QDir::cleanPath(dir);
        for(int i=0;i<list->count();++i) if(QDir::cleanPath(list->item(i)->text()).compare(clean,Qt::CaseInsensitive)==0) return;
        list->addItem(clean);
    };
    connect(watchAdd,&QPushButton::clicked,this,[&]{addFolder(watchList);});
    connect(compareAdd,&QPushButton::clicked,this,[&]{addFolder(compareList);});
    connect(watchRemove,&QPushButton::clicked,watchList,[watchList]{delete watchList->takeItem(watchList->currentRow());});
    connect(compareRemove,&QPushButton::clicked,compareList,[compareList]{delete compareList->takeItem(compareList->currentRow());});

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel, &dlg);
    root->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
    if(dlg.exec()!=QDialog::Accepted) return;

    QStringList watches, compares;
    for(int i=0;i<watchList->count();++i) watches << watchList->item(i)->text();
    for(int i=0;i<compareList->count();++i) compares << compareList->item(i)->text();
    st.setValue("monitor/watchRoots",watches);
    st.setValue("monitor/compareRoots",compares);
    st.setValue("monitor/thresholdPercent",threshold->value());
    st.setValue("monitor/stableSeconds",stable->value());
    st.setValue("monitor/pollSeconds",poll->value());
    st.setValue("monitor/gpuEnabled",gpu->isChecked());
    status_->setText(QString("Monitor settings saved — %1 watched / %2 comparison folders").arg(watches.size()).arg(compares.size()));
}
void MainWindow::toggleMonitor(){
    if(monitorEnabled_){monitor_->stop();monitorEnabled_=false;monitorPaused_=false;tray_->setToolTip("Media Similarity Finder — monitor stopped");status_->setText("Real-time monitor stopped");return;}
    QSettings st; auto ws=st.value("monitor/watchRoots").toStringList(); auto cs=st.value("monitor/compareRoots").toStringList();
    if(ws.isEmpty()||cs.isEmpty()){configureMonitor();ws=st.value("monitor/watchRoots").toStringList();cs=st.value("monitor/compareRoots").toStringList();}
    if(ws.isEmpty()||cs.isEmpty()){status_->setText("Monitor not started: configure watched and comparison folders");return;}
    msf::MonitorConfig c; for(const auto& x:ws)c.watchRoots.push_back(x.toStdString()); for(const auto& x:cs)c.compareRoots.push_back(x.toStdString()); c.applicationDirectory=QApplication::applicationDirPath().toStdString(); c.thresholdPercent=st.value("monitor/thresholdPercent",90).toDouble(); c.stableSeconds=st.value("monitor/stableSeconds",3).toInt(); c.pollSeconds=st.value("monitor/pollSeconds",2).toInt(); c.gpuEnabled=st.value("monitor/gpuEnabled",gpuEnabled_->isChecked()).toBool();
    monitor_->start(c,policy_,[this](const msf::MonitorEvent&e){QMetaObject::invokeMethod(this,[this,e]{monitorEvent(e);},Qt::QueuedConnection);}); monitorEnabled_=true; monitorPaused_=false;tray_->setToolTip("Media Similarity Finder — monitor running");status_->setText("Real-time monitor running");
}
void MainWindow::toggleMonitorPause(){
    if(!monitorEnabled_ || !monitor_) return;
    monitorPaused_=!monitorPaused_;
    monitor_->setPaused(monitorPaused_);
    tray_->setToolTip(monitorPaused_ ? "Media Similarity Finder — monitor paused" : "Media Similarity Finder — monitor running");
    status_->setText(monitorPaused_ ? "Real-time monitor analysis paused" : "Real-time monitor analysis resumed");
}
void MainWindow::monitorEvent(const msf::MonitorEvent&e){
    if(e.type==msf::MonitorEvent::Type::Match){showMonitorMatch(e);return;}
    if(e.type==msf::MonitorEvent::Type::Deferred){status_->setText(QString("Monitor delayed: %1").arg(QString::fromStdString(e.path)));return;}
    if(e.type==msf::MonitorEvent::Type::Error){status_->setText(QString("Monitor error: %1").arg(QString::fromStdString(e.path)));return;}
    if(e.type==msf::MonitorEvent::Type::Started||e.type==msf::MonitorEvent::Type::Stopped)status_->setText(QString::fromStdString(e.detail));
}
void MainWindow::updateMonitorStatus(){
    if(!monitor_ || !monitor_->running()) return;
    const auto s=monitor_->status();
    auto state=[](msf::LoadState x){switch(x){case msf::LoadState::Idle:return "Idle";case msf::LoadState::Light:return "Light";case msf::LoadState::Busy:return "Busy";case msf::LoadState::Heavy:return "Heavy";default:return "Critical";}};
    QString gpu=s.gpuPercent<0?"n/a":QString::number(s.gpuPercent,'f',0)+"%";
    status_->setText(QString("Monitor: %1 | Load %2 | CPU %3% | GPU %4 | RAM %5% | Queue %6 | Analyzed %7 | Matches %8 | Deferred %9")
        .arg(s.running?(s.paused?"PAUSED":"RUNNING"):"STOPPED").arg(state(s.loadState)).arg(s.cpuPercent,0,'f',0).arg(gpu).arg(s.memoryPercent,0,'f',0).arg(s.pending).arg(s.analyzed).arg(s.matches).arg(s.deferred));
}

#ifdef _WIN32
static bool recycleFile(const QString&path){SHFILEOPSTRUCTW op{};std::wstring p=path.toStdWString();p.push_back(L'\0');op.wFunc=FO_DELETE;op.pFrom=p.c_str();op.fFlags=FOF_ALLOWUNDO|FOF_NOCONFIRMATION|FOF_SILENT;return SHFileOperationW(&op)==0;}
#endif
void MainWindow::showMonitorMatch(const msf::MonitorEvent&e){
    if(e.matches.empty())return;
    QSettings st; const msf::MonitorMatch* selected=nullptr;
    for(const auto& candidate:e.matches){QString raw=QString::fromStdString(candidate.newPath)+"|"+QString::fromStdString(candidate.existingPath);QString key=QString::fromLatin1(QCryptographicHash::hash(raw.toUtf8(),QCryptographicHash::Sha256).toHex());if(!st.value("monitor/skipped/"+key,false).toBool()){selected=&candidate;break;}}
    if(!selected)return; const auto&m=*selected;
    QMessageBox box(this);box.setWindowTitle("Possible duplicate detected");box.setText(QString("New file:\n%1\n\nExisting file:\n%2\n\nSimilarity: %3%%").arg(QString::fromStdString(m.newPath)).arg(QString::fromStdString(m.existingPath)).arg(m.percent,0,'f',1));
    auto *openNew=box.addButton("Open new",QMessageBox::ActionRole);auto *openOld=box.addButton("Show existing",QMessageBox::ActionRole);auto *delNew=box.addButton("Recycle new",QMessageBox::DestructiveRole);auto *skip=box.addButton("Skip",QMessageBox::RejectRole);box.exec();
    if(box.clickedButton()==openNew)QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(m.newPath)));
    else if(box.clickedButton()==openOld)QProcess::startDetached("explorer.exe",{QString("/select,%1").arg(QDir::toNativeSeparators(QString::fromStdString(m.existingPath)))});
#ifdef _WIN32
    else if(box.clickedButton()==delNew){if(!recycleFile(QString::fromStdString(m.newPath)))QMessageBox::warning(this,"Recycle","Could not move the new file to Recycle Bin.");}
#else
    else if(box.clickedButton()==delNew)QMessageBox::information(this,"Recycle","Recycle Bin action is available in the Windows build.");
#endif
    else if(box.clickedButton()==skip){QString raw=QString::fromStdString(m.newPath)+"|"+QString::fromStdString(m.existingPath);QString key=QString::fromLatin1(QCryptographicHash::hash(raw.toUtf8(),QCryptographicHash::Sha256).toHex());st.setValue("monitor/skipped/"+key,true);}
}
