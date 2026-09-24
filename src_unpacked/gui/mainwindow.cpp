#include "mainwindow.h"
#include "video_decoder.h"
#include "../src/image_decoder.h"
#include "../src/path_utils.h"
#include "../src/gpu_backend.h"
#include <QAbstractItemView>
#include <QActionGroup>
#include <QApplication>
#include <QMetaType>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QBuffer>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QProcess>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QCryptographicHash>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <cmath>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <thumbcache.h>
#include <winerror.h>
#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif
#endif

// ------------------------------------------------------------ language table
QString trStr(UiLang lang, const char* key) {
  const bool ko = (lang == UiLang::Ko);
  auto S = [&](const char* k, const QString& e) -> QString { return ko ? QString::fromUtf8(k) : e; };
  if (!std::strcmp(key,"app")) return S("Media Similarity Finder","Media Similarity Finder");
  if (!std::strcmp(key,"selectFolder")) return S("폴더 선택","Select folder");
  if (!std::strcmp(key,"refresh")) return S("새로고침","Refresh");
  if (!std::strcmp(key,"start")) return S("검색/업데이트 시작","Start scan/update");
  if (!std::strcmp(key,"pause")) return S("일시정지","Pause");
  if (!std::strcmp(key,"resume")) return S("계속","Resume");
  if (!std::strcmp(key,"stop")) return S("중지","Stop");
  if (!std::strcmp(key,"settings")) return S("설정","Settings");
  if (!std::strcmp(key,"help")) return S("도움말","Help");
  if (!std::strcmp(key,"language")) return S("언어:","Language:");
  if (!std::strcmp(key,"explorer")) return S("탐색기","Explorer");
  if (!std::strcmp(key,"favorites")) return S("즐겨찾기","Favorites");
  if (!std::strcmp(key,"desktop")) return S("바탕 화면","Desktop");
  if (!std::strcmp(key,"downloads")) return S("다운로드","Downloads");
  if (!std::strcmp(key,"documents")) return S("문서","Documents");
  if (!std::strcmp(key,"pictures")) return S("사진","Pictures");
  if (!std::strcmp(key,"videos")) return S("동영상","Videos");
  if (!std::strcmp(key,"music")) return S("음악","Music");
  if (!std::strcmp(key,"thispc")) return S("내 PC","This PC");
  if (!std::strcmp(key,"network")) return S("네트워크","Network");
  if (!std::strcmp(key,"summary")) return S("검색 요약","Scan summary");
  if (!std::strcmp(key,"total")) return S("총 파일","Total files");
  if (!std::strcmp(key,"scanned")) return S("검색 완료","Scanned");
  if (!std::strcmp(key,"processing")) return S("처리 중","Processing");
  if (!std::strcmp(key,"groups")) return S("유사 그룹","Similar groups");
  if (!std::strcmp(key,"dups")) return S("중복 파일","Duplicates");
  if (!std::strcmp(key,"elapsed")) return S("검색 시간","Elapsed");
  if (!std::strcmp(key,"updated")) return S("마지막 업데이트","Last update");
  if (!std::strcmp(key,"gpu")) return S("GPU 가속","GPU accel.");
  if (!std::strcmp(key,"gpuOn")) return S("GPU 가동","GPU on");
  if (!std::strcmp(key,"gpuOff")) return S("GPU 꺼짐","GPU off");
  if (!std::strcmp(key,"gpuLive")) return S("GPU 처리 %1","GPU processing %1");
  if (!std::strcmp(key,"cpu")) return S("CPU 사용","CPU usage");
  if (!std::strcmp(key,"ram")) return S("RAM 사용","RAM usage");
  if (!std::strcmp(key,"monitor")) return S("모니터","Monitor");
  if (!std::strcmp(key,"general")) return S("일반","General");
  if (!std::strcmp(key,"tabFolder")) return S("폴더","Folders");
  if (!std::strcmp(key,"tabImages")) return S("결과: 이미지","Results: Images");
  if (!std::strcmp(key,"tabVideos")) return S("결과: 비디오","Results: Videos");
  if (!std::strcmp(key,"tabIgnore")) return S("무시 목록","Ignored");
  if (!std::strcmp(key,"viewBtn")) return S("보기","View");
  if (!std::strcmp(key,"duration")) return S("재생시간:","Duration:");
  if (!std::strcmp(key,"ignore")) return S("무시","Ignore");
  if (!std::strcmp(key,"unignore")) return S("무시 해제","Unignore");
  if (!std::strcmp(key,"clearIgnored")) return S("모두 비우기","Clear all");
  if (!std::strcmp(key,"recentFolders")) return S("최근 검색 폴더","Recent folders");
  if (!std::strcmp(key,"scanHere")) return S("이 폴더로 검색","Scan this folder");
  if (!std::strcmp(key,"browse")) return S("찾아보기…","Browse…");
  if (!std::strcmp(key,"ignoredHint")) return S("무시된 파일은 검색 결과에서 제외됩니다 (다음 검색부터 적용).","Ignored files are excluded from results (applies from next scan).");
  if (!std::strcmp(key,"viewXL")) return S("아주 큰 아이콘","Extra large icons");
  if (!std::strcmp(key,"viewL")) return S("큰 아이콘","Large icons");
  if (!std::strcmp(key,"viewM")) return S("보통 아이콘","Medium icons");
  if (!std::strcmp(key,"viewS")) return S("작은 아이콘","Small icons");
  if (!std::strcmp(key,"viewList")) return S("리스트","List");
  if (!std::strcmp(key,"viewDetails")) return S("자세히","Details");
  if (!std::strcmp(key,"viewTiles")) return S("타일","Tiles");
  if (!std::strcmp(key,"kindMenu")) return S("검색 대상","Scan target");
  if (!std::strcmp(key,"kindImages")) return S("이미지","Images");
  if (!std::strcmp(key,"kindVideos")) return S("비디오","Videos");
  if (!std::strcmp(key,"sortSim")) return S("유사도 내림차순","Similarity");
  if (!std::strcmp(key,"sortName")) return S("이름 오름차순","Name");
  if (!std::strcmp(key,"searchGroups")) return S("그룹 검색","Search groups");
  if (!std::strcmp(key,"exportCsv")) return S("그룹 내보내기 (CSV)","Export groups (CSV)");
  if (!std::strcmp(key,"group")) return S("그룹","Group");
  if (!std::strcmp(key,"files")) return S("파일","files");
  if (!std::strcmp(key,"pairs")) return S("쌍","Pairs");
  if (!std::strcmp(key,"remain")) return S("남음","Left");
  if (!std::strcmp(key,"similarity")) return S("유사도","Similarity");
  if (!std::strcmp(key,"reference")) return S("기준 파일","reference");
  if (!std::strcmp(key,"tabDetail")) return S("상세 정보","Details");
  if (!std::strcmp(key,"tabExif")) return S("EXIF 정보","EXIF");
  if (!std::strcmp(key,"tabSim")) return S("유사도 분석","Similarity");
  if (!std::strcmp(key,"tabHash")) return S("해시 정보","Hash");
  if (!std::strcmp(key,"fileName")) return S("파일 이름:","File name:");
  if (!std::strcmp(key,"fullPath")) return S("전체 경로:","Full path:");
  if (!std::strcmp(key,"fileSize")) return S("파일 크기:","File size:");
  if (!std::strcmp(key,"format")) return S("파일 형식:","Format:");
  if (!std::strcmp(key,"modified")) return S("수정 날짜:","Modified:");
  if (!std::strcmp(key,"created")) return S("생성 날짜:","Created:");
  if (!std::strcmp(key,"resolution")) return S("해상도:","Resolution:");
  if (!std::strcmp(key,"marked")) return S("mark됨","marked");
  if (!std::strcmp(key,"bestMatch")) return S("최고 매치:","Best match:");
  if (!std::strcmp(key,"phash")) return S("지각 해시(pHash):","Perceptual hash:");
  if (!std::strcmp(key,"noExif")) return S("EXIF 메타데이터가 없습니다.","No EXIF metadata found.");
  if (!std::strcmp(key,"open")) return S("열기","Open");
  if (!std::strcmp(key,"reveal")) return S("탐색기에서 보기","Reveal in Explorer");
  if (!std::strcmp(key,"copy")) return S("복사","Copy");
  if (!std::strcmp(key,"cut")) return S("잘라내기","Cut");
  if (!std::strcmp(key,"paste")) return S("붙여넣기","Paste");
  if (!std::strcmp(key,"move")) return S("이동...","Move...");
  if (!std::strcmp(key,"del")) return S("삭제","Delete");
  if (!std::strcmp(key,"rename")) return S("이름 바꾸기","Rename");
  if (!std::strcmp(key,"mark")) return S("Mark","Mark");
  if (!std::strcmp(key,"unmark")) return S("Mark 해제","Unmark");
  if (!std::strcmp(key,"markAll")) return S("모두 Mark","Mark all");
  if (!std::strcmp(key,"unmarkAll")) return S("Mark 전체 해제","Unmark all");
  if (!std::strcmp(key,"invertMark")) return S("Mark 반전","Invert marks");
  if (!std::strcmp(key,"analyze")) return S("분석","Analyze");
  if (!std::strcmp(key,"upToDate")) return S("이미 최신 상태입니다 — 변경된 파일이 없습니다.","Already up to date — no changed files.");
  if (!std::strcmp(key,"scanDone")) return S("검색 및 업데이트가 완료되었습니다.","Scan and update completed.");
  if (!std::strcmp(key,"scanCancel")) return S("검색이 중지되었습니다.","Scan cancelled.");
  if (!std::strcmp(key,"scanPartial")) return S("부분 저장됨 (중단 시점까지 유지)","Partially saved (kept up to interruption)");
  if (!std::strcmp(key,"analyzed")) return S("분석됨","analyzed");
  if (!std::strcmp(key,"scanErr")) return S("검색 오류","Scan error");
  if (!std::strcmp(key,"ready")) return S("준비","Ready");
  if (!std::strcmp(key,"listing")) return S("파일 목록 작성 중…","Listing files…");
  if (!std::strcmp(key,"scanning")) return S("검색 중…","Scanning…");
  if (!std::strcmp(key,"paused")) return S("일시정지됨","Paused");
  if (!std::strcmp(key,"quickLoaded")) return S("저장된 매칭 %1건을 불러왔습니다 — 누락된 파일만 검색 중…","Loaded %1 stored matches — scanning only missing files…");
  if (!std::strcmp(key,"chooseTitle")) return S("미디어 폴더 선택","Select media folder");
  if (!std::strcmp(key,"chooseScanFolder")) return S("검색 폴더 선택","Select folder to scan");
  if (!std::strcmp(key,"chooseDest")) return S("이동 대상 폴더","Destination folder");
  if (!std::strcmp(key,"newName")) return S("새 이름:","New name:");
  if (!std::strcmp(key,"monSettings")) return S("실시간 모니터 설정","Real-time Monitor Settings");
  if (!std::strcmp(key,"watchFolders")) return S("감시 폴더 — 새/변경 미디어가 비교를 유발","Watched folders — new/changed media triggers comparison");
  if (!std::strcmp(key,"compareFolders")) return S("비교 폴더 — 중복 탐지의 기존 미디어","Comparison folders — existing media for duplicate detection");
  if (!std::strcmp(key,"addFolder")) return S("폴더 추가…","Add folder…");
  if (!std::strcmp(key,"remove")) return S("삭제","Remove");
  if (!std::strcmp(key,"policy")) return S("분석 정책","Analysis policy");
  if (!std::strcmp(key,"threshold")) return S("유사도 임계값:","Similarity threshold:");
  if (!std::strcmp(key,"stable")) return S("안정화 대기:","Stable-file delay:");
  if (!std::strcmp(key,"poll")) return S("폴백 폴링 간격:","Fallback poll interval:");
  if (!std::strcmp(key,"allowGpu")) return S("모니터 분석에 GPU 허용","Allow GPU acceleration for monitor analysis");
  if (!std::strcmp(key,"monSaved")) return S("모니터 설정 저장됨","Monitor settings saved");
  if (!std::strcmp(key,"monRun")) return S("실시간 모니터 실행 중","Real-time monitor running");
  if (!std::strcmp(key,"monStop")) return S("실시간 모니터 중지됨","Real-time monitor stopped");
  if (!std::strcmp(key,"monNeedCfg")) return S("모니터 미시작: 감시/비교 폴더를 설정하세요","Monitor not started: configure watched and comparison folders");
  if (!std::strcmp(key,"dupTitle")) return S("중복 가능성 감지","Possible duplicate detected");
  if (!std::strcmp(key,"delTitle")) return S("삭제","Delete");
  if (!std::strcmp(key,"delAsk")) return S("선택한 %1개 파일을 삭제할까요? (휴지통으로 이동)","Delete %1 selected file(s)? (moves to Recycle Bin)");
  if (!std::strcmp(key,"delFail")) return S("파일을 삭제할 수 없습니다.","Could not delete the file.");
  if (!std::strcmp(key,"moveFail")) return S("파일을 이동할 수 없습니다.","Could not move the file.");
  if (!std::strcmp(key,"renameFail")) return S("이름을 바꿀 수 없습니다.","Could not rename the file.");
  if (!std::strcmp(key,"csvSaved")) return S("CSV 저장됨: ","CSV saved: ");
  if (!std::strcmp(key,"csvFail")) return S("CSV 저장 실패","CSV save failed");
  if (!std::strcmp(key,"about")) return S("Media Similarity Finder 0.9.2.65\n미디어 중복/유사 검색 (CPU/CUDA)\n언어: 설정에서 한국어/English 전환","Media Similarity Finder 0.9.2.65\nMedia duplicate/similarity search (CPU/CUDA)\nLanguage: switch 한국어/English in Settings");
  return QString::fromUtf8(key);
}

#ifdef _WIN32
static bool recycleFile(const QString& path) {
  SHFILEOPSTRUCTW op{}; std::wstring p = path.toStdWString(); p.push_back(L'\0');
  op.wFunc = FO_DELETE; op.pFrom = p.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
  return SHFileOperationW(&op) == 0;
}
#endif

// ------------------------------------------------------------ ScanWorker
static QString fmtElapsed(qint64 ms);
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

// Fresh thumbnail decodes allowed per 600ms UI tick (see fileThumb below).
// Cache hits are free; the rest show file-type icons until a later tick.
// Keeps pause/cancel/close responsive even when thousands of new groups
// stream in during a scan.
constexpr int kThumbBudgetPerTick = 4;
constexpr int kShellBudgetPerTick = 24;
// One shared provider: constructing QFileIconProvider per call plus a
// per-file SHGetFileInfo costs milliseconds each — times 11k groups per
// 600ms tick it blocked the GUI thread for tens of seconds (blank pane,
// dead pause/cancel). All file-type lookups funnel through placeholderIcon().
static QFileIconProvider* sharedIconProvider() {
  static QFileIconProvider prov;
  return &prov;
}
QIcon MainWindow::placeholderIcon(const QString& path) const {
  const QString key = QFileInfo(path).suffix().toLower();
  auto it = phCache_.find(key);
  if (it != phCache_.cend()) return it.value();
  const QIcon ic = sharedIconProvider()->icon(QFileInfo(path));
  if (phCache_.size() > 64) phCache_.clear(); // suffix space is tiny; guard anyway
  phCache_[key] = ic;
  return ic;
}

void ScanWorker::run() {
  try {
    engine_.setResourcePolicy(msf::make_policy(msf::ResourceMode::Custom, cpu_, gpu_));
    auto policy = engine_.resourcePolicy(); policy.gpuEnabled = gpuEnabled_; engine_.setResourcePolicy(policy);
    control_.scanImages = scanImages_; control_.scanVideos = scanVideos_;
    if (!engine_.openIndexForRoot(root_.toStdString(), appDir_.toStdString()))
      throw std::runtime_error("Portable index open failed");
    // Quick load: the scan button restores the stored duplicate groups before
    // analyzing anything, so a repeat scan of the same folder shows previous
    // results immediately, then appends only files that changed meanwhile.
    {
      const auto stored = engine_.loadMatches();
      int loaded = 0;
      for (const auto& m : stored) {
        const QString l = QString::fromStdString(m.leftPath), r = QString::fromStdString(m.rightPath);
        const LiveMatch lm{l, r, m.percent, isVideoExt(l) ? 2 : 1};
        { QMutexLocker g(&pendingMutex_); pending_.push_back(lm); }
        allMatches_.push_back(lm);
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
    lastListMs_ = 0; lastListN_ = 0;
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
    // Diagnostic counters (ChatGPT step 1): where a video-heavy scan with few
    // results loses its pairs. Log-only (the finished message below is parsed
    // positionally and must not change shape).
    MainWindow::scanLog(QString("videoStats indexedVideos=%1 pairs=%2 temporal=%3 matches=%4")
                .arg(r.indexedVideos).arg(r.videoCandidatePairs)
                .arg(r.videoTemporalChecks).arg(r.videoMatches));
    { QMutexLocker g(&pendingMutex_); if (!pending_.isEmpty()) emit matchesArrived(); }
    if (control_.cancel.load()) { emit finished(QString("CANCELLED|%1|%2").arg(r.scanned).arg(r.analyzed)); return; }
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
    emit finished(QString("Scan complete: %1 files, %2 analyzed, %3 candidates, %4 groups").arg(r.scanned).arg(r.analyzed).arg(r.candidates).arg(r.groups)
                  + QString("|%1|%2|%3|%4|%5").arg(r.scanned).arg(r.analyzed).arg(r.unchanged).arg(r.groups).arg(r.candidates));
  } catch (const std::exception& e) {
    // A failed scan must not discard what it already found: checkpoint first
    // so the next scan of the same folder quick-loads the partial results.
    persistMatchesSnapshot();
    emit failed(e.what());
  }
}
void ScanWorker::persistMatchesSnapshot() {
  std::vector<msf::SearchMatch> all; all.reserve((std::size_t)allMatches_.size());
  for (const auto& m : allMatches_) all.push_back({m.left.toStdString(), m.right.toStdString(), m.percent});
  engine_.saveMatches(all);
}
void ScanWorker::pause() { control_.pause.store(true); }
void ScanWorker::resume() { control_.pause.store(false); }
void ScanWorker::cancel() { control_.cancel.store(true); control_.pause.store(false); }
void ScanWorker::setIgnored(const QSet<QString>& s) {
  control_.ignoredPaths.clear();
  for (const auto& p : s) control_.ignoredPaths.insert(p.toStdString());
}
QVector<LiveMatch> ScanWorker::takePending() {
  QMutexLocker g(&pendingMutex_);
  QVector<LiveMatch> out = pending_; pending_.clear(); return out;
}

// ------------------------------------------------------------ MainWindow core
// One-time QSettings identity + storage bootstrap. MUST run before any
// default-constructed QSettings is used: without organization/application
// names every settings read/write silently fails (AccessError) and no UI
// state (window size, splitter, headers, view mode, ...) persists across
// runs. INI next to the executable keeps portable builds truly portable
// instead of depending on the Windows registry.
void initAppSettings(const QString& portableDir) {
  QCoreApplication::setOrganizationName("newclear-ui");
  QCoreApplication::setApplicationName("MediaSimilarityFinder");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, portableDir);
}
MainWindow::MainWindow(QWidget* p) : QMainWindow(p) {
  const QStringList ig0 = QSettings().value("ui/ignored").toStringList();
  ignored_ = QSet<QString>(ig0.begin(), ig0.end());
  buildUi();
  applyStaticTexts();
  restoreUiState();
  monitor_ = std::make_unique<msf::MediaMonitor>();
  monitorTimer_ = new QTimer(this); monitorTimer_->setInterval(1000);
  connect(monitorTimer_, &QTimer::timeout, this, &MainWindow::updateMonitorStatus);
  monitorTimer_->start();
  uiTimer_ = new QTimer(this); uiTimer_->setInterval(600);
  connect(uiTimer_, &QTimer::timeout, this, [this] {
    // Fresh decode budget every tick: list refreshes below may request
    // hundreds of uncached thumbs; only the first few decode now, the rest
    // show file-type icons until a later tick. Cache hits are always free.
    thumbBudget_ = kThumbBudgetPerTick;
    shellBudget_ = kShellBudgetPerTick;
    if (!groupsDirty_) { if (scanning_) updateStatusCounts(); }
    else {
      groupsDirty_ = false;
      drainMatches(); // matches streamed since the last tick (also covers pause:
                      // the worker emits nothing while paused, so without this
                      // the final pre-pause matches would sit undrained)
      refreshStreaming();
    }
    if (scanning_) {
      // Recompose with live elapsed so a long single file (e.g. a big video)
      // shows activity instead of a frozen message.
      const qint64 el = QDateTime::currentMSecsSinceEpoch() - scanStartMs_;
      statusMsg_->setText(scanStatusText(lastDoneN_, lastTotalN_, maxPctShown_, lastPath_, el));
      scanHeartbeat();
    }
    updateGpuLabel();
  });
  tray_ = new QSystemTrayIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon), this);
  auto* tm = new QMenu(this);
  tm->addAction(trStr(lang(), "monSettings"), this, &MainWindow::configureMonitor);
  tm->addAction(trStr(lang(), "monitor"), this, &MainWindow::toggleMonitor);
  tray_->setContextMenu(tm);
  tray_->setToolTip(trStr(lang(), "app"));
  tray_->show();
  setRunning(false);
  statusMsg_->setText(trStr(lang(), "ready"));
}
MainWindow::~MainWindow() {
  saveUiState();
  thumbDb_.close(); thumbDbOpen_ = false;
  if (worker_) worker_->cancel();
  if (thread_) { thread_->quit(); thread_->wait(); delete worker_; delete thread_; }
  if (monitor_) monitor_->stop();
}
void MainWindow::saveUiState() {
  QSettings st;
  st.setValue("ui/mainGeom", saveGeometry());
  st.setValue("ui/splitter", split_ ? split_->saveState() : QByteArray());
  st.setValue("ui/imgTreeHeader", imgTree_ ? imgTree_->header()->saveState() : QByteArray());
  st.setValue("ui/vidTreeHeader", vidTree_ ? vidTree_->header()->saveState() : QByteArray());
  st.setValue("ui/listHeader", list_ ? list_->header()->saveState() : QByteArray());
}
void MainWindow::restoreUiState() {
  QSettings st;
  restoreGeometry(st.value("ui/mainGeom").toByteArray());
  if (split_) {
    const auto sp = st.value("ui/splitter").toByteArray();
    if (!sp.isEmpty()) split_->restoreState(sp);
  }
  auto restoreHeader = [&st](QTreeWidget* t, const char* key) {
    if (!t) return;
    const auto h = st.value(key).toByteArray();
    if (!h.isEmpty()) t->header()->restoreState(h);
  };
  restoreHeader(imgTree_, "ui/imgTreeHeader");
  restoreHeader(vidTree_, "ui/vidTreeHeader");
  restoreHeader(list_, "ui/listHeader");
}
UiLang MainWindow::lang() const {
  return QSettings().value("ui/language", "ko").toString() == "en" ? UiLang::En : UiLang::Ko;
}

void MainWindow::buildUi() {
  setWindowTitle(trStr(lang(), "app") + QStringLiteral(" 0.9.2.65"));
  resize(1500, 880);
  auto* central = new QWidget(this); setCentralWidget(central);
  auto* outer = new QVBoxLayout(central); outer->setContentsMargins(6, 6, 6, 6); outer->setSpacing(6);
  buildToolbar();
  split_ = new QSplitter(Qt::Horizontal, central);
  split_->setOpaqueResize(true);
  auto* leftW = new QWidget(split_); auto* midW = new QWidget(split_); auto* rightW = new QWidget(split_);
  buildLeft(leftW); buildMiddle(midW); buildRight(rightW);
  split_->addWidget(leftW); split_->addWidget(midW); split_->addWidget(rightW);
  // NOTE: setCollapsible must come after the widgets exist; calling it on an
  // empty splitter prints "QSplitter::setCollapsible: Index out of range".
  split_->setCollapsible(0, true); split_->setCollapsible(1, true); split_->setCollapsible(2, false);
  split_->setStretchFactor(0, 0); split_->setStretchFactor(1, 0); split_->setStretchFactor(2, 1);
  const auto saved = QSettings().value("ui/splitter").toByteArray();
  if (saved.isEmpty()) split_->setSizes({200, 330, 950});
  // Non-empty saved state is applied by restoreUiState() after all panes exist.
  outer->addWidget(split_, 1);
  statusBar_ = statusBar();
  statusMsg_ = new QLabel(this); statusCount_ = new QLabel(this); statusProg_ = new QProgressBar(this);
  statusProg_->setRange(0, 100); statusProg_->setValue(0); statusProg_->setFixedWidth(220);
  gpuLbl_ = new QLabel(this);
  statusBar_->addWidget(statusMsg_, 1); statusBar_->addWidget(statusCount_); statusBar_->addWidget(statusProg_);
  statusBar_->addPermanentWidget(gpuLbl_);
  new QShortcut(QKeySequence(Qt::Key_Space), this, SLOT(toggleMarkSelected()));
  new QShortcut(QKeySequence::Delete, this, SLOT(deleteSelected()));
}

void MainWindow::buildToolbar() {
  toolBar_ = new QToolBar(this); toolBar_->setMovable(false);
  centralWidget()->layout()->addWidget(toolBar_);
  folder_ = new QLineEdit(toolBar_);
  folder_->setPlaceholderText(QStringLiteral("D:\\MediaLibrary"));
  folder_->setMinimumWidth(240);
  QSettings st; folder_->setText(st.value("ui/lastFolder", "").toString());
  browse_ = new QPushButton(QStringLiteral("…"), toolBar_); browse_->setFixedWidth(30);
  connect(browse_, &QPushButton::clicked, this, &MainWindow::chooseFolder);
  refresh_ = new QPushButton(QStringLiteral("🔄 ") + trStr(lang(), "refresh"), toolBar_);
  refresh_->setToolTip(trStr(lang(), "refresh"));
  connect(refresh_, &QPushButton::clicked, this, &MainWindow::refreshFolders);
  scan_ = new QPushButton(toolBar_); scan_->setObjectName("scan");
  scan_->setDefault(true);
  pause_ = new QPushButton(toolBar_); pause_->setCheckable(true); cancel_ = new QPushButton(toolBar_);
  connect(scan_, &QPushButton::clicked, this, &MainWindow::startScan);
  connect(pause_, &QPushButton::clicked, this, &MainWindow::togglePauseScan);
  connect(cancel_, &QPushButton::clicked, this, &MainWindow::cancelScan);
  preset_ = new QComboBox(toolBar_);
  preset_->addItems({QStringLiteral("Maximum"), QStringLiteral("High"), QStringLiteral("Balanced"),
                     QStringLiteral("Gaming"), QStringLiteral("Custom")});
  preset_->setCurrentIndex(2);
  connect(preset_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resourceChanged);
  cpu_ = new QSpinBox(toolBar_); gpu_ = new QSpinBox(toolBar_);
  cpu_->setRange(1, 100); gpu_->setRange(1, 100);
  cpu_->setSuffix(QStringLiteral("% CPU")); gpu_->setSuffix(QStringLiteral("% GPU"));
  cpu_->setValue(policy_.cpuPercent); gpu_->setValue(policy_.gpuPercent);
  connect(cpu_, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::customResourceChanged);
  connect(gpu_, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::customResourceChanged);
  gpuEnabled_ = new QCheckBox(toolBar_); gpuEnabled_->setChecked(true);
  kindBtn_ = new QToolButton(toolBar_);
  kindBtn_->setText(trStr(lang(), "kindMenu") + QStringLiteral(" ▾"));
  kindBtn_->setPopupMode(QToolButton::InstantPopup);
  kindMenu_ = new QMenu(kindBtn_);
  kindImgAct_ = kindMenu_->addAction(trStr(lang(), "kindImages"));
  kindVidAct_ = kindMenu_->addAction(trStr(lang(), "kindVideos"));
  for (auto* a : {kindImgAct_, kindVidAct_}) {
    a->setCheckable(true); a->setChecked(true);
    connect(a, &QAction::toggled, this, [this] { updateKindBtn(); });
  }
  kindBtn_->setMenu(kindMenu_);
  {
    const int km = QSettings().value("ui/kindMask", 3).toInt();
    kindImgAct_->setChecked(km & 1); kindVidAct_->setChecked(km & 2);
  }
  monBtn_ = new QPushButton(toolBar_); monBtn_->setCheckable(true);
  connect(monBtn_, &QPushButton::clicked, this, &MainWindow::toggleMonitor);
  monPauseBtn_ = new QPushButton(QStringLiteral("⏸"), toolBar_);
  monPauseBtn_->setToolTip(trStr(lang(), "pause"));
  monPauseBtn_->setCheckable(true);
  connect(monPauseBtn_, &QPushButton::clicked, this, &MainWindow::toggleMonitorPause);
  auto* settingsBtn = new QPushButton(QStringLiteral("⚙"), toolBar_);
  settingsBtn->setToolTip(trStr(lang(), "settings"));
  connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::configureMonitor);
  auto* helpBtn = new QPushButton(QStringLiteral("☰"), toolBar_);
  helpBtn->setToolTip(trStr(lang(), "help"));
  connect(helpBtn, &QPushButton::clicked, this, &MainWindow::showHelp);
  toolBar_->addWidget(folder_); toolBar_->addWidget(browse_);
  toolBar_->addWidget(refresh_); toolBar_->addSeparator();
  toolBar_->addWidget(scan_); toolBar_->addWidget(pause_); toolBar_->addWidget(cancel_);
  toolBar_->addSeparator(); toolBar_->addWidget(preset_); toolBar_->addWidget(cpu_); toolBar_->addWidget(gpu_);
  toolBar_->addWidget(gpuEnabled_);
  toolBar_->addWidget(kindBtn_);
  toolBar_->addWidget(settingsBtn); toolBar_->addWidget(helpBtn);
  toolBar_->addSeparator(); toolBar_->addWidget(monBtn_); toolBar_->addWidget(monPauseBtn_);
  auto* spacer = new QWidget(toolBar_); spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolBar_->addWidget(spacer);
}

void MainWindow::buildLeft(QWidget* w) {
  auto* lay = new QVBoxLayout(w); lay->setContentsMargins(0, 0, 0, 0);
  auto* h = new QLabel(this); h->setObjectName("leftTitle"); h->setStyleSheet("font-weight:bold;");
  lay->addWidget(h);
  folders_ = new QTreeWidget(w);
  folders_->setHeaderHidden(true);
  // With a hidden header the single column stretches to the viewport by
  // default, which suppresses the horizontal scrollbar forever. Size to
  // contents so long paths scroll horizontally like Explorer.
  folders_->header()->setStretchLastSection(false);
  folders_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  folders_->setMinimumWidth(190);
  lay->addWidget(folders_, 1);
  connect(folders_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) { populateFolderChildren(it); });
  connect(folders_, &QTreeWidget::itemClicked, this, &MainWindow::folderActivated);
  connect(folders_, &QTreeWidget::itemActivated, this, &MainWindow::folderActivated);
  auto* sumTitle = new QLabel(this); sumTitle->setObjectName("sumTitle"); sumTitle->setStyleSheet("font-weight:bold;");
  lay->addWidget(sumTitle);
  auto* form = new QFormLayout; lay->addLayout(form);
  sumTotal_ = new QLabel("-", w); sumDone_ = new QLabel("-", w); sumGroups_ = new QLabel("-", w);
  sumDup_ = new QLabel("-", w); sumTime_ = new QLabel("-", w); sumGpu_ = new QLabel("-", w);
  sumCpu_ = new QLabel("-", w); sumRam_ = new QLabel("-", w); sumMon_ = new QLabel("-", w);
  sumTotal_->setObjectName("sumTotal"); sumDone_->setObjectName("sumDone"); sumGroups_->setObjectName("sumGroups");
  sumDup_->setObjectName("sumDup"); sumTime_->setObjectName("sumTime"); sumGpu_->setObjectName("sumGpu");
  sumCpu_->setObjectName("sumCpu"); sumRam_->setObjectName("sumRam"); sumMon_->setObjectName("sumMon");
  // value labels are the field widgets; refreshSummary() writes "name: value" into the name labels
  // and keeps raw values here for layout stability.
  sumValTotal_ = new QLabel("-", w); sumValDone_ = new QLabel("-", w); sumValGroups_ = new QLabel("-", w);
  sumValDup_ = new QLabel("-", w); sumValTime_ = new QLabel("-", w); sumValGpu_ = new QLabel("-", w);
  sumValCpu_ = new QLabel("-", w); sumValRam_ = new QLabel("-", w); sumValMon_ = new QLabel("-", w);
  form->addRow(sumTotal_, sumValTotal_); form->addRow(sumDone_, sumValDone_);
  form->addRow(sumGroups_, sumValGroups_); form->addRow(sumDup_, sumValDup_);
  form->addRow(sumTime_, sumValTime_); form->addRow(sumGpu_, sumValGpu_);
  form->addRow(sumCpu_, sumValCpu_); form->addRow(sumRam_, sumValRam_);
  form->addRow(sumMon_, sumValMon_);
  refreshFolders();
}

// ---------------------------------------------------------------------------
// Explorer-style Tiles renderer for the group grid: a large icon on the left
// with the (multi-line) label on its right, like Windows Explorer's Tiles view.
// The check box is painted at the standard leading position with standard style
// metrics, so the view's built-in hit testing keeps toggling marks.
class TileDelegate : public QStyledItemDelegate {
public:
  static constexpr int kIconSide = 48;
  static constexpr int kTileW = 320;
  static constexpr int kTileH = 76;
  explicit TileDelegate(QObject* parent=nullptr) : QStyledItemDelegate(parent) {}
  QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
    return QSize(kTileW, kTileH);
  }
  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QStyleOptionViewItem o(option);
    initStyleOption(&o, index);
    const QWidget* w = o.widget;
    QStyle* st = w ? w->style() : QApplication::style();
    painter->save();
    st->drawPrimitive(QStyle::PE_PanelItemViewItem, &o, painter, w); // selection/focus bg
    const QPalette::ColorRole textRole =
        (o.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text;
    const bool enabled = (o.state & QStyle::State_Enabled) != 0;
    QRect r = o.rect.adjusted(4, 4, -4, -4);
    if (index.flags() & Qt::ItemIsUserCheckable) {
      const int cw = st->pixelMetric(QStyle::PM_IndicatorWidth, &o, w);
      const int ch = st->pixelMetric(QStyle::PM_IndicatorHeight, &o, w);
      const QRect cr(r.left(), r.top() + (r.height() - ch) / 2, cw, ch);
      QStyleOptionViewItem co(o);
      co.rect = cr;
      co.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange | QStyle::State_HasFocus);
      const auto cs = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
      co.state |= (cs == Qt::Checked) ? QStyle::State_On
                  : (cs == Qt::PartiallyChecked ? QStyle::State_NoChange : QStyle::State_Off);
      st->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &co, painter, w);
      r.setLeft(cr.right() + 4);
    }
    const QRect ir(r.left(), r.top() + (r.height() - kIconSide) / 2, kIconSide, kIconSide);
    if (!o.icon.isNull()) o.icon.paint(painter, ir);
    r.setLeft(ir.right() + 8);
    if (r.isValid() && !o.text.isEmpty()) {
      const QStringList lines = o.text.split(QLatin1Char('\n'));
      QFont bold = o.font; bold.setBold(true);
      painter->setFont(bold);
      const int lh = QFontMetrics(bold).height();
      st->drawItemText(painter, r, Qt::AlignLeft | Qt::AlignTop, o.palette,
                       enabled, lines.value(0), textRole);
      if (lines.size() > 1) {
        painter->setFont(o.font);
        const QRect rr(r.left(), r.top() + lh + 2, r.width(), r.height() - lh - 2);
        st->drawItemText(painter, rr, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                         o.palette, enabled, lines.mid(1).join(QLatin1Char('\n')), textRole);
      }
    }
    painter->restore();
  }
};

void MainWindow::buildMiddle(QWidget* w) {
  auto* lay = new QVBoxLayout(w); lay->setContentsMargins(0, 0, 0, 0);
  groupTitle_ = new QLabel(w); groupTitle_->setStyleSheet("font-weight:bold;");
  lay->addWidget(groupTitle_);
  auto* bar = new QHBoxLayout;
  sortBox_ = new QComboBox(w);
  viewBtn_ = new QToolButton(w);
  viewBtn_->setText(trStr(lang(), "viewBtn") + QStringLiteral(" ▾"));
  viewBtn_->setPopupMode(QToolButton::InstantPopup);
  viewMenu_ = new QMenu(viewBtn_);
  const char* vkeys[7] = {"viewXL", "viewL", "viewM", "viewS", "viewList", "viewDetails", "viewTiles"};
  auto* vgroup = new QActionGroup(viewBtn_); vgroup->setExclusive(true);
  for (int i = 0; i < 7; ++i) {
    QAction* a = viewMenu_->addAction(trStr(lang(), vkeys[i]));
    a->setCheckable(true); a->setData(i); vgroup->addAction(a); viewActs_.push_back(a);
    connect(a, &QAction::triggered, this, [this, i] { groupViewChanged(i); });
  }
  viewBtn_->setMenu(viewMenu_);
  groupSearch_ = new QLineEdit(w); groupSearch_->setClearButtonEnabled(true);
  connect(groupSearch_, &QLineEdit::textChanged, this, &MainWindow::groupSearchChanged);
  connect(sortBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { refreshGroupList(); });
  bar->addWidget(sortBox_); bar->addWidget(viewBtn_); bar->addWidget(groupSearch_, 1);
  lay->addLayout(bar);
  midTabs_ = new QTabWidget(w);
  lay->addWidget(midTabs_, 1);
  // ---- results tabs (image / video share the view-mode logic via active pair)
  auto* imgTab = new QWidget(midTabs_);
  auto* imgLay = new QVBoxLayout(imgTab); imgLay->setContentsMargins(0, 0, 0, 0);
  imgTree_ = new QTreeWidget(imgTab); imgTree_->setColumnCount(5); imgTree_->setRootIsDecorated(false);
  imgGrid_ = new QListWidget(imgTab);
  imgGrid_->setViewMode(QListView::IconMode); imgGrid_->setResizeMode(QListView::Adjust);
  imgGrid_->setMovement(QListView::Static); imgGrid_->setSpacing(8);
  imgLay->addWidget(imgTree_); imgLay->addWidget(imgGrid_);
  midTabs_->addTab(imgTab, QString());
  auto* vidTab = new QWidget(midTabs_);
  auto* vidLay = new QVBoxLayout(vidTab); vidLay->setContentsMargins(0, 0, 0, 0);
  vidTree_ = new QTreeWidget(vidTab); vidTree_->setColumnCount(5); vidTree_->setRootIsDecorated(false);
  vidGrid_ = new QListWidget(vidTab);
  vidGrid_->setViewMode(QListView::IconMode); vidGrid_->setResizeMode(QListView::Adjust);
  vidGrid_->setMovement(QListView::Static); vidGrid_->setSpacing(8);
  vidLay->addWidget(vidTree_); vidLay->addWidget(vidGrid_);
  midTabs_->addTab(vidTab, QString());
  connectResView(imgTree_, imgGrid_);
  connectResView(vidTree_, vidGrid_);
  tileDelegate_ = new TileDelegate(this); // shared Tiles renderer for both grids
  defaultDelegate_ = new QStyledItemDelegate(this); // explicit default: restoring
  // via setItemDelegate(nullptr) leaves items with null visuals (probed).
  // ---- ignore tab
  auto* igTab = new QWidget(midTabs_);
  auto* iglay = new QVBoxLayout(igTab);
  iglay->addWidget(new QLabel(trStr(lang(), "ignoredHint"), igTab));
  ignoreList_ = new QListWidget(igTab);
  iglay->addWidget(ignoreList_, 1);
  auto* igrow = new QHBoxLayout;
  unignoreBtn_ = new QPushButton(igTab);
  clearIgnoreBtn_ = new QPushButton(igTab);
  connect(unignoreBtn_, &QPushButton::clicked, this, &MainWindow::unignoreSelected);
  connect(clearIgnoreBtn_, &QPushButton::clicked, this, &MainWindow::clearIgnored);
  igrow->addWidget(unignoreBtn_); igrow->addWidget(clearIgnoreBtn_); igrow->addStretch(1);
  iglay->addLayout(igrow);
  midTabs_->addTab(igTab, QString());
  connect(midTabs_, &QTabWidget::currentChanged, this, &MainWindow::onMidTabChanged);
  // legacy single-pair pointers track the ACTIVE tab's pair
  groupsStack_ = nullptr;
  groupsList_ = imgGrid_;
  groupsView_ = imgTree_;
  auto* foot = new QHBoxLayout;
  groupFoot_ = new QLabel(w); groupFoot_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  foot->addWidget(groupFoot_, 1);
  lay->addLayout(foot);
  updateIgnoreTab();
  groupViewChanged(QSettings().value("ui/groupView", 0).toInt());
  onMidTabChanged(0);
}

void MainWindow::buildRight(QWidget* w) {
  auto* lay = new QVBoxLayout(w); lay->setContentsMargins(0, 0, 0, 0);
  auto* head = new QHBoxLayout;
  detailTitle_ = new QLabel(w); detailTitle_->setStyleSheet("font-weight:bold;font-size:14px;");
  detailSim_ = new QLabel(w); detailSim_->setStyleSheet("color:#2f855a;font-weight:bold;");
  head->addWidget(detailTitle_); head->addWidget(detailSim_);
  detailCount_ = new QLabel(w);
  viewGrid_ = new QToolButton(w); viewGrid_->setText(QStringLiteral("▦")); viewGrid_->setCheckable(true);
  viewList_ = new QToolButton(w); viewList_->setText(QStringLiteral("☰")); viewList_->setCheckable(true);
  viewGrid_->setChecked(true);
  connect(viewGrid_, &QToolButton::clicked, this, [this] { setViewMode(0); });
  connect(viewList_, &QToolButton::clicked, this, [this] { setViewMode(1); });
  zoom_ = new QSlider(Qt::Horizontal, w); zoom_->setRange(48, 256); zoom_->setValue(128); zoom_->setFixedWidth(130);
  connect(zoom_, &QSlider::valueChanged, this, &MainWindow::zoomChanged);
  head->addStretch(1); head->addWidget(detailCount_);
  head->addWidget(viewGrid_); head->addWidget(viewList_); head->addWidget(zoom_);
  lay->addLayout(head);
  viewStack_ = new QStackedWidget(w);
  grid_ = new QListWidget(w);
  grid_->setViewMode(QListView::IconMode); grid_->setResizeMode(QListView::Adjust);
  grid_->setMovement(QListView::Static); grid_->setSpacing(10); grid_->setIconSize(QSize(128, 128));
  connect(grid_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem*, QListWidgetItem*) { fileGridSelected(); });
  connect(grid_, &QListWidget::itemDoubleClicked, this, &MainWindow::fileActivated);
  connect(grid_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
    if (!it) return;
    if (it->checkState() == Qt::Checked) marked_.insert(it->data(Qt::UserRole).toString());
    else marked_.remove(it->data(Qt::UserRole).toString());
    updateStatusCounts();
  });
  grid_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(grid_, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) { showFileMenu(grid_->mapToGlobal(p)); });
  list_ = new QTreeWidget(w);
  list_->setColumnCount(7); list_->setRootIsDecorated(false);
  connect(list_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem*, QTreeWidgetItem*) { fileListSelected(); });
  connect(list_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
    if (it) { currentFile_ = it->data(1, Qt::UserRole).toString(); openSelected(); }
  });
  connect(list_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
    if (!it || col != 0) return;
    const QString p = it->data(1, Qt::UserRole).toString();
    if (it->checkState(0) == Qt::Checked) marked_.insert(p); else marked_.remove(p);
    updateStatusCounts();
  });
  list_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) { showFileMenu(list_->mapToGlobal(p)); });
  viewStack_->addWidget(grid_); viewStack_->addWidget(list_);
  lay->addWidget(viewStack_, 3);
  fileBar_ = new QToolBar(w); fileBar_->setMovable(false);
  lay->addWidget(fileBar_);
  detailTabs_ = new QTabWidget(w);
  auto* tabD = new QWidget(detailTabs_); auto* tabDlay = new QHBoxLayout(tabD);
  preview_ = new QLabel(tabD); preview_->setFixedSize(220, 190); preview_->setAlignment(Qt::AlignCenter);
  preview_->setStyleSheet("border:1px solid #ccc;border-radius:6px;background:#f5f5f5;");
  detailForm_ = new QFormLayout;
  auto* formW = new QWidget(tabD); formW->setLayout(detailForm_);
  tabDlay->addWidget(preview_); tabDlay->addWidget(formW, 1);
  detailTabs_->addTab(tabD, QString());
  exifLabel_ = new QLabel(detailTabs_); exifLabel_->setWordWrap(true); exifLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  detailTabs_->addTab(exifLabel_, QString());
  auto* tabS = new QWidget(detailTabs_); auto* tabSlay = new QVBoxLayout(tabS);
  simLabel_ = new QLabel(tabS); simBar_ = new QProgressBar(tabS); simBar_->setRange(0, 100);
  tabSlay->addWidget(simLabel_); tabSlay->addWidget(simBar_); tabSlay->addStretch(1);
  detailTabs_->addTab(tabS, QString());
  hashLabel_ = new QLabel(detailTabs_); hashLabel_->setWordWrap(true); hashLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  detailTabs_->addTab(hashLabel_, QString());
  connect(detailTabs_, &QTabWidget::currentChanged, this, &MainWindow::detailTabChanged);
  lay->addWidget(detailTabs_, 2);
}

void MainWindow::applyStaticTexts() {
  const UiLang l = lang();
  setWindowTitle(trStr(l, "app") + QStringLiteral(" 0.9.2.65"));
  scan_->setText(QStringLiteral("▶ ") + trStr(l, "start"));
  refresh_->setText(QStringLiteral("🔄 ") + trStr(l, "refresh"));
  pause_->setText(scanPaused_ ? trStr(l, "resume") : QStringLiteral("❚❚ ") + trStr(l, "pause"));
  pause_->setChecked(scanPaused_);
  cancel_->setText(QStringLiteral("■ ") + trStr(l, "stop"));
  gpuEnabled_->setText(trStr(l, "allowGpu"));
  monBtn_->setText(QStringLiteral("👁 ") + trStr(l, "monitor"));
  monBtn_->setChecked(monitorEnabled_);
  auto* leftTitle = findChild<QLabel*>("leftTitle"); if (leftTitle) leftTitle->setText(trStr(l, "explorer"));
  auto* sumTitle = findChild<QLabel*>("sumTitle"); if (sumTitle) sumTitle->setText(trStr(l, "summary"));
  sumTotal_->setText(trStr(l, "total")); sumDone_->setText(trStr(l, "scanned")); sumGroups_->setText(trStr(l, "groups"));
  sumDup_->setText(trStr(l, "dups")); sumTime_->setText(trStr(l, "elapsed")); sumGpu_->setText(trStr(l, "gpu"));
  sumCpu_->setText(trStr(l, "cpu")); sumRam_->setText(trStr(l, "ram")); sumMon_->setText(trStr(l, "monitor"));
  refreshSummary();
  groupTitle_->setText(trStr(l, "groups") + QString(" (%1)").arg(groups_.size()));
  sortBox_->blockSignals(true);
  const int ssort = sortBox_->currentIndex();
  sortBox_->clear(); sortBox_->addItem(trStr(l, "sortSim")); sortBox_->addItem(trStr(l, "sortName"));
  sortBox_->setCurrentIndex(ssort < 0 ? 0 : ssort);
  sortBox_->blockSignals(false);
  groupSearch_->setPlaceholderText(trStr(l, "searchGroups"));
  viewBtn_->setText(trStr(l, "viewBtn") + QStringLiteral(" ▾"));
  {
    const char* vkeys[7] = {"viewXL", "viewL", "viewM", "viewS", "viewList", "viewDetails", "viewTiles"};
    for (int i = 0; i < viewActs_.size() && i < 7; ++i) viewActs_[i]->setText(trStr(l, vkeys[i]));
  }
  kindBtn_->setText(trStr(l, "kindMenu") + QStringLiteral(" ▾"));
  kindImgAct_->setText(trStr(l, "kindImages"));
  kindVidAct_->setText(trStr(l, "kindVideos"));
  updateKindBtn();
  midTabs_->setTabText(0, trStr(l, "tabImages"));
  midTabs_->setTabText(1, trStr(l, "tabVideos"));
  updateIgnoreTab();
  imgTree_->setHeaderLabels({trStr(l, "group"), trStr(l, "files"), trStr(l, "pairs"), trStr(l, "similarity"), trStr(l, "fileSize")});
  vidTree_->setHeaderLabels({trStr(l, "group"), trStr(l, "files"), trStr(l, "pairs"), trStr(l, "similarity"), trStr(l, "fileSize")});
  unignoreBtn_->setText(trStr(l, "unignore"));
  clearIgnoreBtn_->setText(trStr(l, "clearIgnored"));
  viewGrid_->setToolTip(trStr(l, "viewGrid")); viewList_->setToolTip(trStr(l, "viewList"));
  list_->setHeaderLabels({"", trStr(l, "colFile"), trStr(l, "similarity"), trStr(l, "resolution"),
                          trStr(l, "format"), trStr(l, "fileSize"), trStr(l, "colDate")});
  detailTabs_->setTabText(0, trStr(l, "tabDetail")); detailTabs_->setTabText(1, trStr(l, "tabExif"));
  detailTabs_->setTabText(2, trStr(l, "tabSim")); detailTabs_->setTabText(3, trStr(l, "tabHash"));
  fileBar_->clear();
  fileBar_->addAction(trStr(l, "open"), this, &MainWindow::openSelected);
  fileBar_->addAction(trStr(l, "reveal"), this, &MainWindow::revealSelected);
  fileBar_->addAction(trStr(l, "copy"), this, &MainWindow::copySelected);
  fileBar_->addAction(trStr(l, "cut"), this, &MainWindow::cutSelected);
  fileBar_->addAction(trStr(l, "paste"), this, &MainWindow::pasteFiles);
  fileBar_->addAction(trStr(l, "move"), this, &MainWindow::moveSelected);
  fileBar_->addAction(trStr(l, "del"), this, &MainWindow::deleteSelected);
  refreshGroupList(); refreshFileViews(); refreshDetail(); updateStatusCounts();
}

void MainWindow::setLanguage(int idx) {
  QSettings().setValue("ui/language", idx == 1 ? "en" : "ko");
  applyStaticTexts();
}

// ------------------------------------------------------------ scan control
void MainWindow::chooseFolder() {
  const QString d = QFileDialog::getExistingDirectory(this, trStr(lang(), "chooseTitle"), folder_->text());
  if (!d.isEmpty()) { folder_->setText(d); QSettings().setValue("ui/lastFolder", d); }
}
void MainWindow::setRunning(bool v) {
  scanning_ = v;
  scanPaused_ = false; maxPctShown_ = 0; lastDoneN_ = 0; lastTotalN_ = 0;
  scan_->setEnabled(!v); browse_->setEnabled(!v); refresh_->setEnabled(!v);
  pause_->setEnabled(v); pause_->setChecked(false); cancel_->setEnabled(v);
  pause_->setText(QStringLiteral("❚❚ ") + trStr(lang(), "pause"));
  if (!v) scan_->setFocus(); // return the highlight to Start, as at launch
  statusProg_->setRange(0, 100); statusProg_->setValue(0);
  if (v) uiTimer_->start(); else uiTimer_->stop();
  updateGpuLabel();
}
void MainWindow::startScan() {
  if (scanning_) return;
  if (folder_->text().isEmpty()) { chooseFolder(); if (folder_->text().isEmpty()) return; }
  // Thumbnail disk cache: a second connection to the managed index DB
  // (WAL-safe). Lets rescans reuse decoded thumbs instead of burning the
  // per-tick budget. Silent no-op when the index cannot be opened.
  thumbDb_.close(); thumbDbOpen_ = false;
  {
    msf::IndexPaths paths;
    if (msf::IndexManager::resolve(msf::path_from_utf8(QApplication::applicationDirPath().toStdString()),
                                   msf::path_from_utf8(folder_->text().toStdString()), paths) &&
        thumbDb_.open(msf::path_to_utf8(paths.database)) && thumbDb_.initialize())
      thumbDbOpen_ = true;
  }
  // Remember the most-recently scanned folders (max 2) for the favorites list.
  // Compare normalized so separator/trailing-slash variants of one folder
  // cannot duplicate the entry.
  {
    const QString cur = QDir::cleanPath(folder_->text());
    QStringList recent = QSettings().value("ui/recentFolders").toStringList();
    for (int i = recent.size() - 1; i >= 0; --i)
      if (QDir::cleanPath(recent[i]).compare(cur, Qt::CaseInsensitive) == 0) recent.removeAt(i);
    recent.prepend(cur);
    while (recent.size() > 2) recent.removeLast();
    if (recent != QSettings().value("ui/recentFolders").toStringList()) {
      QSettings().setValue("ui/recentFolders", recent);
      refreshFolders();
    }
  }
  if (thread_) { thread_->quit(); thread_->wait(); delete worker_; delete thread_; thread_ = nullptr; worker_ = nullptr; }
  groups_.clear(); pathGroup_.clear(); pathParent_.clear();
  bestPct_.clear(); resCache_.clear(); pathKind_.clear(); thumbCache_.clear(); thumbFail_.clear();
  allPaths_.clear(); matchRows_.clear(); fileSize_.clear(); fileFp_.clear(); fileDur_.clear();
  currentGroup_ = -1; currentFile_.clear(); hasReport_ = false;
  refreshGroupList(); refreshFileViews(); refreshDetail();
  QString appDir = QApplication::applicationDirPath();
  thread_ = new QThread(this);
  const int dist = 8;
  worker_ = new ScanWorker(folder_->text(), appDir, dist, cpu_->value(), gpu_->value(), gpuEnabled_->isChecked(),
                          kindImgAct_->isChecked(), kindVidAct_->isChecked());
  worker_->setIgnored(ignored_);
  worker_->moveToThread(thread_);
  connect(thread_, &QThread::started, worker_, &ScanWorker::run);
  connect(worker_, &ScanWorker::progress, this, &MainWindow::scanProgress);
  connect(worker_, &ScanWorker::progressCount, this, &MainWindow::onScanCounts);
  connect(worker_, &ScanWorker::listingProgress, this, &MainWindow::onListingProgress);
  connect(worker_, &ScanWorker::matchesArrived, this, &MainWindow::drainMatches);
  connect(worker_, &ScanWorker::quickLoaded, this, &MainWindow::onQuickLoaded);
  connect(worker_, &ScanWorker::results, this, &MainWindow::onResults);
  connect(worker_, &ScanWorker::finished, this, &MainWindow::scanFinished);
  connect(worker_, &ScanWorker::failed, this, &MainWindow::scanFailed);
  connect(worker_, &ScanWorker::finished, thread_, &QThread::quit);
  connect(worker_, &ScanWorker::failed, thread_, &QThread::quit);
  scanStartMs_ = QDateTime::currentMSecsSinceEpoch();
  scanLog(QString("start folder=%1").arg(folder_->text()));
  setRunning(true);
  statusMsg_->setText(trStr(lang(), "scanning"));
  statusProg_->setValue(0);
  thread_->start();
}
void MainWindow::togglePauseScan() {
  if (!scanning_ || !worker_) return; // pause acts only while its own scan runs
  scanPaused_ = !scanPaused_;
  // Direct call, NOT QueuedConnection: pause()/resume() only store atomics,
  // and a queued slot can never fire while run() occupies the worker thread's
  // event loop — which is exactly why pause appeared dead mid-scan.
  if (scanPaused_) worker_->pause(); else worker_->resume();
  pause_->setChecked(scanPaused_);
  pause_->setText(scanPaused_ ? trStr(lang(), "resume") : QStringLiteral("❚❚ ") + trStr(lang(), "pause"));
  statusMsg_->setText(trStr(lang(), scanPaused_ ? "paused" : "scanning"));
  // Freezing the frame immediately: drain everything streamed so far so the
  // paused view is complete within one tick instead of whenever the worker
  // happens to emit next (it emits nothing while paused).
  drainMatches();
  groupsDirty_ = true;
}
void MainWindow::cancelScan() {
  if (!scanning_ || !worker_) return;
  // Direct call (see togglePauseScan): a queued cancel slot would only run
  // after run() returns, i.e. never in time to stop the scan.
  worker_->cancel();
  statusMsg_->setText(trStr(lang(), "scanCancel"));
}
void MainWindow::onScanCounts(qulonglong done, qulonglong total) {
  lastDoneN_ = (std::size_t)done; lastTotalN_ = (std::size_t)total;
}
void MainWindow::scanProgress(int p, QString path) {
  lastPct_ = p; lastPath_ = path;
  statusProg_->setRange(0, 100);
  // Streaming totals grow as the walk continues, so raw percent can dip.
  // The bar never moves backwards; exact counts stay in the message.
  if (p > maxPctShown_) maxPctShown_ = p;
  statusProg_->setValue(maxPctShown_);
  const qint64 el = QDateTime::currentMSecsSinceEpoch() - scanStartMs_;
  statusMsg_->setText(scanStatusText(lastDoneN_, lastTotalN_, maxPctShown_, path, el));
  updateStatusCounts();
}
void MainWindow::onListingProgress(std::size_t n) {
  statusProg_->setRange(0, 0); // indeterminate: walking the directory tree
  statusMsg_->setText(QString("%1 %2").arg(trStr(lang(), "listing")).arg(n));
}
void MainWindow::onQuickLoaded(int n) {
  drainMatches();
  statusMsg_->setText(trStr(lang(), "quickLoaded").arg(n));
}
void MainWindow::scanFinished(QString msg) {
  drainMatches();
  if (thumbDbOpen_) thumbDb_.pruneThumbs(); // drop thumbs of files gone from the index
  scanLog(QString("finish %1").arg(msg));
  rebuildGroups(); refreshGroupList(); refreshFileViews(); refreshDetail();
  if (msg.startsWith(QStringLiteral("CANCELLED"))) {
    // Partial progress is kept by design (checkpoints): report what survived.
    const QStringList st = msg.split('|');
    const QString detail = st.size() > 2
        ? QString(" (%1 %2, %3 %4)").arg(st[1]).arg(trStr(lang(), "scanned")).arg(st[2]).arg(trStr(lang(), "analyzed"))
        : QString();
    statusMsg_->setText(trStr(lang(), "scanPartial") + detail);
  } else {
    const QStringList st = msg.split('|');
    // st[0]=text st[1]=scanned st[2]=analyzed st[3]=unchanged st[4]=groups st[5]=candidates
    lastStats_ = st.mid(1, 5);
    repElapsedMs_ = QDateTime::currentMSecsSinceEpoch() - scanStartMs_;
    const qulonglong analyzed = st.size() > 2 ? st[2].toULongLong() : 0;
    const qulonglong unchanged = st.size() > 3 ? st[3].toULongLong() : 0;
    hasReport_ = true;
    if (analyzed == 0 && unchanged > 0)
      statusMsg_->setText(trStr(lang(), "upToDate") + QString(" (%1 %2)").arg(unchanged).arg(trStr(lang(), "files")));
    else
      statusMsg_->setText(trStr(lang(), "scanDone") + QString(" (%1)").arg(st[0]));
  }
  refreshSummary();
  updateStatusCounts();
  setRunning(false);
  statusProg_->setValue(100);
}
void MainWindow::scanFailed(QString msg) {
  scanLog(QString("failed %1").arg(msg));
  QMessageBox::critical(this, trStr(lang(), "scanErr"), msg);
  statusMsg_->setText(trStr(lang(), "scanErr") + ": " + msg);
  setRunning(false);
}
void MainWindow::onResults(QVector<GuiFile> files, QStringList matchRows) {
  drainMatches();
  allPaths_.clear(); matchRows_ = matchRows;
  fileSize_.clear(); fileFp_.clear(); fileDur_.clear();
  for (const auto& f : files) {
    allPaths_ << f.path;
    fileSize_[f.path] = QString::number(f.size);
    fileFp_[f.path] = f.fpHex;
    fileDur_[f.path] = f.duration;
  }
  // fold final retained matches (same data as streamed, dedup by rebuild)
  for (const auto& row : matchRows_) {
    const QStringList p = row.split('\t');
    if (p.size() != 3) continue;
    addMatch(p[0], p[1], p[2].toDouble(), isVideoExt(p[0]) ? 2 : 1);
  }
  rebuildGroups(); refreshGroupList(); refreshFileViews(); refreshDetail(); updateStatusCounts();
}
void MainWindow::resourceChanged(int i) {
  auto m = static_cast<msf::ResourceMode>(i + 1);
  policy_ = msf::make_policy(m, cpu_->value(), gpu_->value());
  if (monitor_) monitor_->setPolicy(policy_);
  cpu_->blockSignals(true); gpu_->blockSignals(true);
  cpu_->setValue(policy_.cpuPercent); gpu_->setValue(policy_.gpuPercent);
  cpu_->blockSignals(false); gpu_->blockSignals(false);
}
void MainWindow::customResourceChanged() {
  if (preset_->currentIndex() != 4) preset_->setCurrentIndex(4);
  policy_ = msf::make_policy(msf::ResourceMode::Custom, cpu_->value(), gpu_->value());
  if (monitor_) monitor_->setPolicy(policy_);
}

// ------------------------------------------------------------ folders (left)
void MainWindow::refreshFolders() {
  folders_->clear();
  QFileIconProvider icons;
  auto* fav = new QTreeWidgetItem(folders_, QStringList(trStr(lang(), "favorites")));
  fav->setExpanded(true);
  const QStyle* st = QApplication::style();
  auto favPath = [&](const char* key, const QString& path) {
    auto* it = new QTreeWidgetItem(fav, QStringList(trStr(lang(), key)));
    it->setData(0, Qt::UserRole, path);
    it->setIcon(0, icons.icon(QFileIconProvider::Folder));
    Q_UNUSED(st);
  };
  favPath("desktop", QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
  favPath("downloads", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
  favPath("documents", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
  favPath("pictures", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
  favPath("videos", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
  favPath("music", QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
  // Most-recently scanned folders (max 2): the most useful jump targets for
  // resuming unfinished work on previous results. Skip paths already listed
  // above (e.g. scanning Downloads would otherwise duplicate the entry), and
  // defensively dedupe normalized variants (separator/case) so one folder can
  // never appear twice even if stored raw forms differ.
  {
    const auto norm = [](const QString& p) {
      return QDir::cleanPath(p).toLower();
    };
    QSet<QString> listed;
    for (auto loc : {QStandardPaths::DesktopLocation, QStandardPaths::DownloadLocation,
                     QStandardPaths::DocumentsLocation, QStandardPaths::PicturesLocation,
                     QStandardPaths::MoviesLocation, QStandardPaths::MusicLocation})
      listed.insert(norm(QStandardPaths::writableLocation(loc)));
    QSet<QString> seenMru;
    const QStringList recent = QSettings().value("ui/recentFolders").toStringList();
    for (const auto& rp : recent) {
      if (rp.isEmpty() || !QFileInfo(rp).isDir()) continue;
      const QString n = norm(rp);
      if (listed.contains(n) || seenMru.contains(n)) continue;
      seenMru.insert(n);
      auto* it = new QTreeWidgetItem(fav, QStringList(QDir::toNativeSeparators(rp)));
      it->setData(0, Qt::UserRole, rp);
      it->setIcon(0, icons.icon(QFileIconProvider::Folder));
      it->setToolTip(0, rp);
    }
  }
  auto* pc = new QTreeWidgetItem(folders_, QStringList(trStr(lang(), "thispc")));
  pc->setExpanded(true);
  for (const auto& d : QDir::drives()) {
    const QStorageInfo si(d.absoluteFilePath());
    auto* it = new QTreeWidgetItem(pc, QStringList(d.absoluteFilePath()
        + (si.displayName().isEmpty() ? QString() : " (" + si.displayName() + ")")));
    it->setData(0, Qt::UserRole, d.absoluteFilePath());
    it->setIcon(0, icons.icon(QFileIconProvider::Drive));
    it->addChild(new QTreeWidgetItem(QStringList("…")));
  }
  auto* net = new QTreeWidgetItem(folders_, QStringList(trStr(lang(), "network")));
  net->setIcon(0, icons.icon(QFileIconProvider::Network));
}
void MainWindow::populateFolderChildren(QTreeWidgetItem* it) {
  if (!it) return;
  const QString path = it->data(0, Qt::UserRole).toString();
  if (path.isEmpty()) return;
  it->takeChildren();
  QDir dir(path);
  const auto subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  QFileIconProvider icons;
  for (const auto& s : subs) {
    QDir sub(dir.filePath(s));
    if (!sub.isReadable()) continue;
    auto* c = new QTreeWidgetItem(it, QStringList(s));
    c->setData(0, Qt::UserRole, sub.absolutePath());
    c->setIcon(0, icons.icon(QFileIconProvider::Folder));
    if (!sub.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) c->addChild(new QTreeWidgetItem(QStringList("…")));
    if (it->childCount() > 200) break;
  }
}
void MainWindow::folderActivated(QTreeWidgetItem* it, int) {
  if (!it) return;
  const QString path = it->data(0, Qt::UserRole).toString();
  if (path.isEmpty() || !QFileInfo(path).isDir()) return;
  folder_->setText(QDir::toNativeSeparators(path));
  QSettings().setValue("ui/lastFolder", folder_->text());
}

// ------------------------------------------------------------ groups (union-find + middle)
QString MainWindow::findRoot(const QString& p) {
  QString r = p;
  while (pathParent_.value(r, r) != r) r = pathParent_.value(r);
  QString q = p;
  while (pathParent_.value(q, q) != r) { const QString n = pathParent_.value(q); pathParent_[q] = r; q = n; }
  return r;
}
void MainWindow::addMatch(const QString& l, const QString& r, double pct, int kind) {
  if (l.isEmpty() || r.isEmpty() || l == r) return;
  ++matchSeq_;
  if (!pathParent_.contains(l)) pathParent_[l] = l;
  if (!pathParent_.contains(r)) pathParent_[r] = r;
  const QString rl = findRoot(l), rr = findRoot(r);
  if (rl != rr) pathParent_[rr] = rl;
  bestPct_[l] = std::max(bestPct_.value(l, 0.0), pct);
  bestPct_[r] = std::max(bestPct_.value(r, 0.0), pct);
  if (!pathKind_.contains(l)) pathKind_[l] = kind;
  if (!pathKind_.contains(r)) pathKind_[r] = kind;
}
void MainWindow::drainMatches() {
  if (!worker_) return;
  const auto v = worker_->takePending();
  if (v.isEmpty()) return;
  for (const auto& m : v) addMatch(m.left, m.right, m.percent, m.kind);
  groupsDirty_ = true;
}
void MainWindow::refreshStreaming() {
  rebuildGroups();
  // Full list rebuilds (widget churn for every group) are the most expensive
  // GUI work during a scan. Throttle them adaptively so paint events and
  // pause/cancel clicks always get through: at most one full fill per
  // 3x measured cost (600ms..3s). Status counts stay live every tick.
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 interval = std::clamp(lastFillCostMs_ * 3, (qint64)600, (qint64)3000);
  if (matchSeq_ != lastFillSig_ && now - lastFillMs_ >= interval) {
    lastFillSig_ = matchSeq_; lastFillMs_ = now;
    QElapsedTimer t; t.start();
    refreshGroupList(); refreshFileViews();
    lastFillCostMs_ = t.elapsed();
  }
  updateStatusCounts();
}
void MainWindow::rebuildGroups() {
  QHash<QString, QStringList> buckets;
  for (auto it = pathParent_.cbegin(); it != pathParent_.cend(); ++it) buckets[findRoot(it.key())] << it.key();
  groups_.clear(); pathGroup_.clear();
  for (auto it = buckets.cbegin(); it != buckets.cend(); ++it) {
    if (it.value().size() < 2) continue;
    DupGroup g; g.paths = it.value();
    std::sort(g.paths.begin(), g.paths.end(),
              [this](const QString& a, const QString& b) { return bestPct_.value(a, 0) > bestPct_.value(b, 0); });
    g.best = 0; g.kind = pathKind_.value(g.paths[0], 1);
    for (const auto& p : g.paths) { g.pct[p] = bestPct_.value(p, 0); g.best = std::max(g.best, g.pct[p]); }
    groups_.push_back(g);
  }
  if (sortBox_->currentIndex() == 1)
    std::sort(groups_.begin(), groups_.end(), [](const DupGroup& a, const DupGroup& b) { return a.paths[0] < b.paths[0]; });
  else
    std::sort(groups_.begin(), groups_.end(), [](const DupGroup& a, const DupGroup& b) { return a.best > b.best; });
  for (int i = 0; i < groups_.size(); ++i)
    for (const auto& p : groups_[i].paths) pathGroup_[p] = i;
  if (currentGroup_ >= groups_.size()) { currentGroup_ = -1; currentFile_.clear(); }
}
QString MainWindow::fmtSize(qulonglong n) const {
  if (n < 1024) return QString("%1 B").arg(n);
  if (n < 1024ULL * 1024) return QString("%1 KB").arg(n / 1024.0, 0, 'f', 1);
  if (n < 1024ULL * 1024 * 1024) return QString("%1 MB").arg(n / (1024.0 * 1024), 0, 'f', 2);
  return QString("%1 GB").arg(n / (1024.0 * 1024 * 1024), 0, 'f', 2);
}
double MainWindow::pathBest(const QString& p) const { return bestPct_.value(p, 0.0); }
qulonglong MainWindow::fileSizeCached(const QString& p) {
  auto it = fileSize_.find(p);
  if (it != fileSize_.cend()) return it.value().toULongLong();
  // Quick-loaded groups reference files the current scan has not (re)indexed
  // yet; without this they permanently show "0 B" until a full scan ends.
  const qulonglong s = (qulonglong)QFileInfo(p).size();
  fileSize_[p] = QString::number(s);
  return s;
}
void MainWindow::connectResView(QTreeWidget* tree, QListWidget* grid) {
  connect(tree, &QTreeWidget::currentItemChanged, this, &MainWindow::groupSelected);
  connect(tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
    if (!it || col != 0) return;
    setGroupMarked(it->data(0, Qt::UserRole).toInt(), it->checkState(0) == Qt::Checked);
  });
  tree->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tree, &QWidget::customContextMenuRequested, this, [this, tree](const QPoint& p) { showGroupMenu(tree->mapToGlobal(p)); });
  connect(grid, &QListWidget::currentItemChanged, this, &MainWindow::gridSelected);
  connect(grid, &QListWidget::itemChanged, this, &MainWindow::gridCheckChanged);
  grid->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(grid, &QWidget::customContextMenuRequested, this, [this, grid](const QPoint& p) { showGroupMenu(grid->mapToGlobal(p)); });
}
void MainWindow::gridSelected(QListWidgetItem* cur, QListWidgetItem*) {
  if (!cur) return;
  const int gi = cur->data(Qt::UserRole).toInt();
  if (gi < 0 || gi >= groups_.size()) return;
  currentGroup_ = gi; currentFile_.clear();
  refreshFileViews(); refreshDetail(); updateStatusCounts();
  groupFoot_->setText(QString("%1: %2").arg(groups_.size()).arg(currentGroup_ + 1));
}
void MainWindow::gridCheckChanged(QListWidgetItem* it) {
  if (!it) return;
  setGroupMarked(it->data(Qt::UserRole).toInt(), it->checkState() == Qt::Checked);
}
void MainWindow::updateKindBtn() {
  int m = (kindImgAct_->isChecked() ? 1 : 0) | (kindVidAct_->isChecked() ? 2 : 0);
  if (!m) { // never allow an empty selection
    kindImgAct_->blockSignals(true); kindVidAct_->blockSignals(true);
    kindImgAct_->setChecked(true); kindVidAct_->setChecked(true);
    kindImgAct_->blockSignals(false); kindVidAct_->blockSignals(false);
    m = 3;
  }
  QSettings().setValue("ui/kindMask", m);
  kindBtn_->setToolTip(trStr(lang(), "kindMenu") + ": "
                         + (m == 3 ? trStr(lang(), "kindImages") + "+" + trStr(lang(), "kindVideos")
                                   : (m == 1 ? trStr(lang(), "kindImages") : trStr(lang(), "kindVideos"))));
}
void MainWindow::groupViewChanged(int idx) {
  if (idx < 0) idx = 0;
  if (idx > 6) idx = 5;
  QSettings().setValue("ui/groupView", idx);
  for (auto* a : viewActs_) a->setChecked(a->data().toInt() == idx);
  QTreeWidget* tree = (midTabs_ && midTabs_->currentIndex() == 1) ? vidTree_ : imgTree_;
  QListWidget* grid = (midTabs_ && midTabs_->currentIndex() == 1) ? vidGrid_ : imgGrid_;
  groupsView_ = tree; groupsList_ = grid;
  if (idx <= 4 || idx == 6) {
    // Grid modes: 0-3 large-to-small icons, 4 list, 6 Explorer-style tiles
    // (large icon left, label right). A previously saved 6 (removed preview
    // toggle) now lands on tiles instead of crashing.
    tree->setVisible(false); grid->setVisible(true);
    const bool tiles = (idx == 6);
    grid->setItemDelegate(tiles ? tileDelegate_ : defaultDelegate_);
    if (tiles) {
      grid->setViewMode(QListView::IconMode);
      grid->setIconSize(QSize(TileDelegate::kIconSide, TileDelegate::kIconSide));
      grid->setGridSize(QSize(TileDelegate::kTileW, TileDelegate::kTileH));
    } else {
      grid->setViewMode(idx == 4 ? QListView::ListMode : QListView::IconMode);
      static const int sizes[5] = {256, 128, 64, 32, 32};
      grid->setIconSize(QSize(sizes[idx], sizes[idx]));
      grid->setGridSize(QSize()); // back to automatic per-item layout
    }
    for (int r = 0; r < grid->count(); ++r)
      if (grid->item(r)->data(Qt::UserRole).toInt() == currentGroup_) { grid->setCurrentRow(r); break; }
  } else {
    grid->setItemDelegate(defaultDelegate_);
    grid->setVisible(false); tree->setVisible(true);
    for (int r = 0; r < tree->topLevelItemCount(); ++r)
      if (tree->topLevelItem(r)->data(0, Qt::UserRole).toInt() == currentGroup_) {
        tree->setCurrentItem(tree->topLevelItem(r)); break;
      }
  }
}
void MainWindow::setGroupMarked(int gi, bool on) {
  if (gi < 0 || gi >= groups_.size()) return;
  for (const auto& p : groups_[gi].paths) { if (on) marked_.insert(p); else marked_.remove(p); }
  QTreeWidget* trees[2] = {imgTree_, vidTree_};
  QListWidget* grids[2] = {imgGrid_, vidGrid_};
  for (auto* t : trees) t->blockSignals(true);
  for (auto* g : grids) g->blockSignals(true);
  auto stateOf = [this](int g2) {
    if (g2 < 0 || g2 >= groups_.size()) return Qt::Unchecked;
    int n = 0;
    for (const auto& p : groups_[g2].paths) if (marked_.contains(p)) ++n;
    return n == 0 ? Qt::Unchecked : (n == groups_[g2].paths.size() ? Qt::Checked : Qt::PartiallyChecked);
  };
  for (auto* t : trees)
    for (int r = 0; r < t->topLevelItemCount(); ++r) {
      auto* it = t->topLevelItem(r);
      it->setCheckState(0, stateOf(it->data(0, Qt::UserRole).toInt()));
    }
  for (auto* g : grids)
    for (int r = 0; r < g->count(); ++r) {
      auto* it = g->item(r);
      it->setCheckState(stateOf(it->data(Qt::UserRole).toInt()));
    }
  for (auto* t : trees) t->blockSignals(false);
  for (auto* g : grids) g->blockSignals(false);
  refreshFileViews(); updateStatusCounts();
}
void MainWindow::fillPair(QTreeWidget* tree, QListWidget* grid, int wantKind, bool syncSel) {
  tree->blockSignals(true); grid->blockSignals(true);
  tree->clear(); grid->clear();
  const QString f = groupSearch_->text().trimmed().toLower();
  for (int i = 0; i < groups_.size(); ++i) {
    const auto& g = groups_[i];
    if (g.kind != wantKind) continue;
    if (!f.isEmpty()) {
      bool hit = false;
      for (const auto& p : g.paths) if (p.toLower().contains(f)) { hit = true; break; }
      if (!hit) continue;
    }
    qulonglong bytes = 0;
    for (const auto& p : g.paths) bytes += fileSizeCached(p);
    int marked = 0;
    for (const auto& p : g.paths) if (marked_.contains(p)) ++marked;
    auto* it = new QTreeWidgetItem(tree);
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(0, marked == 0 ? Qt::Unchecked : (marked == g.paths.size() ? Qt::Checked : Qt::PartiallyChecked));
    it->setText(0, QString("%1 %2").arg(trStr(lang(), "group")).arg(i + 1));
    it->setText(1, QString("%1 %2").arg(g.paths.size()).arg(trStr(lang(), "files")));
    const qulonglong pairs = (qulonglong)g.paths.size() * ((qulonglong)g.paths.size() - 1) / 2;
    it->setText(2, QString("%1 %2").arg(pairs).arg(trStr(lang(), "pairs")));
    it->setText(3, QString("%1%").arg(g.best, 0, 'f', 1));
    it->setText(4, fmtSize(bytes));
    it->setData(0, Qt::UserRole, i);
    if (syncSel && i == currentGroup_) tree->setCurrentItem(it);
    const QString rep = g.paths.isEmpty() ? QString() : g.paths[0];
    // Request the view's own icon size so cells stay uniform; fileThumb
    // normalizes every icon to that exact square (see squareFittedPixmap).
    auto* li = new QListWidgetItem(fileThumb(rep, grid->iconSize()),
                                   QString("%1 %2\n%3 %4 · %5 %6 · %7%\n%8")
                                       .arg(trStr(lang(), "group")).arg(i + 1)
                                       .arg(g.paths.size()).arg(trStr(lang(), "files"))
                                       .arg(pairs).arg(trStr(lang(), "pairs")).arg(g.best, 0, 'f', 1)
                                       .arg(fmtSize(bytes)));
    li->setFlags(li->flags() | Qt::ItemIsUserCheckable);
    li->setCheckState(marked == 0 ? Qt::Unchecked : (marked == g.paths.size() ? Qt::Checked : Qt::PartiallyChecked));
    li->setData(Qt::UserRole, i);
    li->setToolTip(rep);
    grid->addItem(li);
    if (syncSel && i == currentGroup_) grid->setCurrentItem(li);
  }
  tree->resizeColumnToContents(0);
  tree->blockSignals(false); grid->blockSignals(false);
}
void MainWindow::refreshGroupList() {
  int ni = 0, nv = 0;
  for (const auto& g : groups_) { if (g.kind == 2) ++nv; else ++ni; }
  groupTitle_->setText(trStr(lang(), "groups") + QString(" (%1)").arg(groups_.size()));
  midTabs_->setTabText(0, QString("%1 (%2)").arg(trStr(lang(), "tabImages")).arg(ni));
  midTabs_->setTabText(1, QString("%1 (%2)").arg(trStr(lang(), "tabVideos")).arg(nv));
  midTabs_->setTabText(2, QString("%1 (%2)").arg(trStr(lang(), "tabIgnore")).arg(ignored_.size()));
  const int tab = midTabs_->currentIndex();
  // Fill only the visible tab: building thousands of widget items (plus their
  // thumbnail decodes) for a hidden tab every 600ms is pure GUI-thread waste.
  // Switching tabs calls refreshGroupList() via activateTab(), so the hidden
  // side is always filled lazily on show.
  if (tab == 0) fillPair(imgTree_, imgGrid_, 1, true);
  else if (tab == 1) fillPair(vidTree_, vidGrid_, 2, true);
  groupFoot_->setText(QString("%1: %2").arg(groups_.size()).arg(currentGroup_ >= 0 ? QString::number(currentGroup_ + 1) : "-"));
  updateIgnoreTab();
}
void MainWindow::activateTab(int idx) {
  const bool res = (idx == 0 || idx == 1);
  sortBox_->setEnabled(res); viewBtn_->setEnabled(res); groupSearch_->setEnabled(res);
  if (idx == 0 || idx == 1) {
    groupsView_ = (idx == 1) ? vidTree_ : imgTree_;
    groupsList_ = (idx == 1) ? vidGrid_ : imgGrid_;
    refreshGroupList();
    groupViewChanged(QSettings().value("ui/groupView", 0).toInt());
  } else if (idx == 2) {
    updateIgnoreTab();
  }
}
void MainWindow::onMidTabChanged(int idx) { activateTab(idx); }
void MainWindow::updateIgnoreTab() {
  ignoreList_->clear();
  QStringList ig = ignored_.values(); ig.sort();
  ignoreList_->addItems(ig);
}
void MainWindow::unignoreSelected() {
  QList<QString> sel;
  for (auto* it : ignoreList_->selectedItems()) sel << it->text();
  if (sel.isEmpty() && ignoreList_->currentItem()) sel << ignoreList_->currentItem()->text();
  if (sel.isEmpty()) return;
  for (const auto& p : sel) ignored_.remove(p);
  QSettings().setValue("ui/ignored", QStringList(ignored_.begin(), ignored_.end()));
  updateIgnoreTab(); refreshGroupList(); updateStatusCounts();
  statusMsg_->setText(trStr(lang(), "ready"));
}
void MainWindow::clearIgnored() {
  if (ignored_.isEmpty()) return;
  ignored_.clear();
  QSettings().setValue("ui/ignored", QStringList());
  updateIgnoreTab(); updateStatusCounts();
}
void MainWindow::applyIgnore(const QStringList& paths, bool on) {
  if (paths.isEmpty()) return;
  for (const auto& p : paths) { if (on) ignored_.insert(p); else ignored_.remove(p); }
  QSettings().setValue("ui/ignored", QStringList(ignored_.begin(), ignored_.end()));
  if (on) prunePaths(QSet<QString>(paths.begin(), paths.end()));
  updateIgnoreTab(); updateStatusCounts();
  if (on) statusMsg_->setText(trStr(lang(), "ignoredHint"));
}
void MainWindow::groupSelected(QTreeWidgetItem* cur, QTreeWidgetItem*) {
  if (!cur) { currentGroup_ = -1; currentFile_.clear(); }
  else {
    const int gi = cur->data(0, Qt::UserRole).toInt();
    if (gi < 0 || gi >= groups_.size()) return;
    currentGroup_ = gi; currentFile_.clear();
  }
  refreshFileViews(); refreshDetail(); updateStatusCounts();
  groupFoot_->setText(QString("%1: %2").arg(groups_.size()).arg(currentGroup_ >= 0 ? QString::number(currentGroup_ + 1) : "-"));
}
void MainWindow::groupSearchChanged(const QString&) { refreshGroupList(); }
// ------------------------------------------------------------ right pane: files + detail
// Header-only dimensions via ffprobe (no decode): fallback for formats Qt
// cannot read the size of. One process spawn, cached by the caller.
static QSize ffprobeSize(const QString& path) {
  const QString q = QStringLiteral("ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 \"") + path + '"';
  const QByteArray cmd = q.toLocal8Bit();
#ifdef _WIN32
  FILE* fp = _popen(cmd.constData(), "r");
#else
  FILE* fp = popen(cmd.constData(), "r");
#endif
  if (!fp) return QSize();
  char buf[128] = {0};
  QString out;
  while (fgets(buf, sizeof(buf), fp)) out += QString::fromLocal8Bit(buf);
#ifdef _WIN32
  _pclose(fp);
#else
  pclose(fp);
#endif
  const QStringList parts = out.trimmed().split(',');
  if (parts.size() != 2) return QSize();
  bool okW = false, okH = false;
  const int w = parts[0].trimmed().toInt(&okW), h = parts[1].trimmed().toInt(&okH);
  if (!okW || !okH || w <= 0 || h <= 0) return QSize();
  return QSize(w, h);
}
QString MainWindow::fileResolution(const QString& path) const {  auto it = resCache_.find(path);
  if (it != resCache_.cend()) return it.value();  QString r = "-";
  if (isVideoExt(path)) {
    msf::VideoDecoder dec;
    if (dec.open(path.toStdString())) {
      msf::VideoInfo vi;
      if (dec.info(vi) && vi.width > 0 && vi.height > 0)
        r = QString("%1x%2").arg(vi.width).arg(vi.height);
      dec.close();
    }
  } else {
    QImageReader rd(path);
    const QSize s = rd.size();
    if (s.isValid()) r = QString("%1x%2").arg(s.width()).arg(s.height());
    else {
      // Header-only probe for formats Qt cannot read the size of (e.g. PNG
      // without the plugin): ffprobe reads dimensions without decoding.
      // Cached like everything else here, so the process spawn is one-time.
      const QSize probe = ffprobeSize(path);
      if (probe.isValid()) r = QString("%1x%2").arg(probe.width()).arg(probe.height());
    }
  }
  resCache_[path] = r;
  return r;
}
#ifdef _WIN32
// Best-effort shell thumbnail from the persistent IThumbnailCache. Returns a
// null image on any failure (no cached entry, thumbnail service missing, ...);
// callers then fall through to their normal decode path.
// CLSID_ThumbnailCache is only declared when INITGUID is defined; spell the
// well-known GUID out to keep this TU header-local.
static const GUID kThumbnailCacheClsid = {0xc8199035, 0xdb49, 0x4e95, {0x91, 0xb7, 0x7e, 0x86, 0x2f, 0x12, 0x04, 0x59}};
static QImage shellThumbnailImage(const QString& path) {
  QImage out;
  const std::wstring wpath = path.toStdWString();
  IShellItem* item = nullptr;
  if (FAILED(SHCreateItemFromParsingName(wpath.c_str(), nullptr, IID_PPV_ARGS(&item)))) return out;
  IThumbnailCache* cache = nullptr;
  if (FAILED(CoCreateInstance(kThumbnailCacheClsid, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&cache)))) { item->Release(); return out; }
  ISharedBitmap* sb = nullptr;
  const HRESULT hr = cache->GetThumbnail(item, 256, WTS_INCACHEONLY | WTS_SCALETOREQUESTEDSIZE,
                                         &sb, nullptr, nullptr);
  if (SUCCEEDED(hr) && sb) {
    HBITMAP hb = nullptr;
    if (SUCCEEDED(sb->GetSharedBitmap(&hb)) && hb) {
      BITMAP bm{};
      if (::GetObject(hb, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = bm.bmWidth;
        bi.bmiHeader.biHeight = -bm.bmHeight; // top-down, matches QImage scanlines
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        const int nPix = bm.bmWidth * bm.bmHeight;
        std::vector<unsigned char> buf((std::size_t)nPix * 4);
        const HDC dc = ::GetDC(nullptr);
        if (dc && ::GetDIBits(dc, hb, 0, (UINT)bm.bmHeight, buf.data(), &bi, DIB_RGB_COLORS) == bm.bmHeight) {
          // Alpha from the DIB is 0; RGB32 requires 0xff in the MSB so the
          // preview does not render as transparent black.
          for (int i = 3; i < nPix * 4; i += 4) buf[i] = 0xff;
          QImage img(buf.data(), bm.bmWidth, bm.bmHeight, bm.bmWidth * 4, QImage::Format_RGB32);
          out = img.copy();
        }
        if (dc) ::ReleaseDC(nullptr, dc);
      }
      // The HBITMAP is owned by the shared bitmap; do not DeleteObject it here.
    }
    sb->Release();
  }
  cache->Release();
  item->Release();
  return out;
}
#else
static QImage shellThumbnailImage(const QString&) { return QImage(); }
#endif
QPixmap squareFittedPixmap(const QPixmap& src, const QSize& size) {
  QPixmap canvas(size);
  canvas.fill(Qt::transparent);
  if (src.isNull() || size.width() <= 0 || size.height() <= 0) return canvas;
  QPainter pr(&canvas);
  const QSize fit = src.size().scaled(size, Qt::KeepAspectRatio);
  pr.drawPixmap(QRect(QPoint((size.width() - fit.width()) / 2, (size.height() - fit.height()) / 2), fit),
                src, QRect(QPoint(0, 0), src.size()));
  return canvas;
}
void MainWindow::dropThumbCache(const QString& path) {
  thumbCache_.remove(path);
  // Sized variants are keyed "path|WxH"; drop those too.
  for (auto it = thumbCache_.begin(); it != thumbCache_.end();) {
    const QString& k = it.key();
    if (k.size() > path.size() && k.startsWith(path) && k[path.size()] == '|') it = thumbCache_.erase(it);
    else ++it;
  }
}
QIcon MainWindow::fileThumb(const QString& path, const QSize& size, bool bypassBudget) const {
  // Cache key carries the requested size: cells follow the delivered pixmap
  // size (probed 90x91..262x283 for one view), so sharing one pixmap across
  // sizes would keep cells uneven.
  const QString key = path + '|' + QString::number(size.width()) + 'x' + QString::number(size.height());
  auto tc = thumbCache_.find(key);
  if (tc != thumbCache_.cend()) return tc.value();
  // Disk cache first: a fast indexed read, no budget spent. Thumbs decoded in
  // any earlier scan reappear instantly on rescan instead of re-burning the
  // per-tick budget (the reason loaded groups showed generic icons).
  const QFileInfo fi(path);
  // Match scanner/database timestamp precision (milliseconds) so a same-size file
   // replaced within the same second cannot accidentally reuse an old thumbnail.
   const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
  const qulonglong fsize = (qulonglong)fi.size();
  if (thumbDbOpen_) {
    std::vector<unsigned char> bytes;
    if (thumbDb_.getThumb(path.toStdString(), mtime, fsize, bytes)) {
      const QImage disk = QImage::fromData(bytes.data(), (int)bytes.size());
      if (!disk.isNull()) {
        QIcon ic = QIcon(squareFittedPixmap(QPixmap::fromImage(disk), size));
        thumbCache_[key] = ic;
        if (thumbCache_.size() > 3000) {
          auto it = thumbCache_.begin();
          for (int n = 0; n < 750 && it != thumbCache_.end(); ++n) it = thumbCache_.erase(it);
        }
        return ic;
      }
    }
  }
  // Skip-list (Similarity-inspired): a path whose heavy decode already failed
  // returns the cheap file-type icon immediately without spending the shared
  // per-tick budget, so corrupt/undecodable files cannot starve live thumbs.
  if (thumbFail_.contains(path)) return placeholderIcon(path);
  // Decode budget: each cache miss (shell COM, image decode, FFmpeg seek) can
  // block the GUI thread for milliseconds-to-seconds. Over budget, return a
  // cheap file-type icon WITHOUT caching it, so the real thumb is retried on
  // a later tick. The explicitly selected file (detail pane) bypasses.
  // Shell thumbnails (cheap COM) get a wider lane than heavy decodes so
  // Explorer-cached thumbs fill ~10x faster.
  if (!bypassBudget) {
    if (thumbBudget_ <= 0)
      return placeholderIcon(path);
    --thumbBudget_;
  }
  QPixmap pm;
  // Shell thumbnail cache is the fast lane: Explorer already stored a rendered
  // thumb for most media (video frames, Office documents, HEIC/WebP). Using it
  // first sidesteps a full engine decode for the preview pane and, unlike WIC,
  // works for container/sidecar files that ship no decodable pixel stream.
  // WTS_INCACHEONLY keeps this read-only: a missing entry falls through to the
  // normal decoders instead of writing the cache back from this thread.
  if (bypassBudget || shellBudget_ > 0) {
    if (!bypassBudget) --shellBudget_;
    const QImage shell = shellThumbnailImage(path);
    if (!shell.isNull()) pm = QPixmap::fromImage(shell);
  }
  if (pm.isNull()) {
    if (isVideoExt(path)) {
      msf::VideoDecoder dec;
      if (dec.open(path.toStdString())) {
        msf::ColorFrame cf;
        const int dim = std::max(size.width(), size.height());
        if (dec.frameAtColor(0.5, dim, dim, cf) && cf.rgb.size() == (size_t)cf.width * cf.height * 3 && cf.width > 0 && cf.height > 0) {
          QImage im(cf.rgb.data(), cf.width, cf.height, cf.width * 3, QImage::Format_RGB888);
          pm = QPixmap::fromImage(im.copy());
        }
        dec.close();
      }
    } else {
      QImageReader rd(path);
      QImage im;
      if (rd.canRead()) {
        rd.setAutoTransform(true);
        im = rd.read();
      }
      if (im.isNull()) {
        // Qt image-format plugins (e.g. qpng) may be absent from a portable
        // deployment while WIC is always present. Decode color through WIC so
        // previews stay color (the gray engine decode is fingerprint-only and
        // must not leak into display).
        msf::ImageDecoder dec;
        msf::ColorImage c;
        if (dec.decodeColorAspect(path.toStdString(), 256, c) && c.width > 0 && c.height > 0 &&
            c.bgra.size() == (size_t)c.width * c.height * 4) {
          im = QImage(c.bgra.data(), c.width, c.height, c.width * 4, QImage::Format_ARGB32).copy();
        }
      }
      if (!im.isNull()) pm = QPixmap::fromImage(im);
    }
  }
  QIcon ic;
  if (!pm.isNull()) {
    // Normalize to the exact requested rect: identical cells regardless of
    // source aspect/size (Explorer-like uniform grid), never distorted.
    ic = QIcon(squareFittedPixmap(pm, size));
    // Persist for future rescans (best effort): 192px JPEG keeps the DB small
    // while staying recognizable up to XL views.
    if (thumbDbOpen_) {
      QImage store = squareFittedPixmap(pm, QSize(192, 192)).toImage();
      QByteArray ba; QBuffer buf(&ba); buf.open(QIODevice::WriteOnly);
      if (buf.isOpen() && store.save(&buf, "JPG", 70) && !ba.isEmpty()) {
        std::vector<unsigned char> v(ba.cbegin(), ba.cend());
        thumbDb_.putThumb(path.toStdString(), mtime, fsize, v);
      }
    }
  } else {
    ic = placeholderIcon(path);
    if (thumbFail_.size() > 2000) {
      auto it = thumbFail_.begin();
      for (int n = 0; n < 500 && it != thumbFail_.end(); ++n) it = thumbFail_.erase(it);
    }
    thumbFail_.insert(path);
  }
  // Bound the cache: group-list refreshes re-request the same representatives,
  // but an unbounded cache over a 100k+ scan would cost gigabytes. Evict a
  // chunk, never all: a full clear on huge scans caused a perpetual re-decode
  // storm (every refresh re-decoded thousands of thumbs, freezing the UI).
  if (thumbCache_.size() > 3000) {
    auto it = thumbCache_.begin();
    for (int n = 0; n < 750 && it != thumbCache_.end(); ++n) it = thumbCache_.erase(it);
  }
  thumbCache_[key] = ic;
  return ic;
}
void MainWindow::setViewMode(int i) {
  viewGrid_->setChecked(i == 0); viewList_->setChecked(i == 1);
  viewStack_->setCurrentIndex(i == 1 ? 1 : 0);
}
void MainWindow::zoomChanged(int v) { grid_->setIconSize(QSize(v, v)); refreshFileViews(); }
static QString pctText(double v, bool ref, UiLang l) {
  if (ref) return QString("100% · ") + trStr(l, "reference");
  return QString("%1%").arg(v, 0, 'f', 1);
}
void MainWindow::refreshFileViews() {
  grid_->blockSignals(true); list_->blockSignals(true);
  grid_->clear(); list_->clear();
  detailTitle_->setText(trStr(lang(), "group"));
  detailSim_->clear(); detailCount_->clear();
  if (currentGroup_ < 0 || currentGroup_ >= groups_.size()) {
    grid_->blockSignals(false); list_->blockSignals(false); return;
  }
  const auto& g = groups_[currentGroup_];
  detailTitle_->setText(QString("%1 %2").arg(trStr(lang(), "group")).arg(currentGroup_ + 1));
  detailSim_->setText(QString("%1: %2%").arg(trStr(lang(), "similarity")).arg(g.best, 0, 'f', 1));
  qulonglong bytes = 0;
  for (const auto& p : g.paths) bytes += fileSize_.value(p, "0").toULongLong();
  detailCount_->setText(QString("%1 %2 (%3)").arg(g.paths.size()).arg(trStr(lang(), "files")).arg(fmtSize(bytes)));
  for (int i = 0; i < g.paths.size(); ++i) {
    const QString& p = g.paths[i];
    QFileInfo fi(p);
    const bool ref = (i == 0);
    const double pct = ref ? 100.0 : g.pct.value(p, 0);
    auto* gi = new QListWidgetItem(fileThumb(p, grid_->iconSize()), QString());
    gi->setData(Qt::UserRole, p);
    gi->setFlags(gi->flags() | Qt::ItemIsUserCheckable);
    gi->setCheckState(marked_.contains(p) ? Qt::Checked : Qt::Unchecked);
    // Long names wrap into extra lines and desynchronize row heights when
    // mixed with short names. Elide to one line; the full path stays in the
    // tooltip (hover shows it).
    const QFontMetrics fm(grid_->font());
    const QString shown = fm.elidedText(fi.fileName(), Qt::ElideMiddle, grid_->iconSize().width() + 64);
    gi->setText(shown + "\n" + fmtSize(fi.size()) + " · " + fileResolution(p) + "\n" + pctText(pct, ref, lang()));
    gi->setToolTip(p);
    grid_->addItem(gi);
    auto* li = new QTreeWidgetItem(list_);
    li->setCheckState(0, marked_.contains(p) ? Qt::Checked : Qt::Unchecked);
    li->setText(1, fi.fileName()); li->setData(1, Qt::UserRole, p);
    li->setText(2, pctText(pct, ref, lang()));
    li->setText(3, fileResolution(p));
    li->setText(4, fi.suffix().toUpper());
    li->setText(5, fmtSize(fi.size()));
    li->setText(6, fi.lastModified().toString("yyyy-MM-dd hh:mm"));
    li->setToolTip(1, p);
  }
  list_->resizeColumnToContents(1);
  if (!currentFile_.isEmpty() && !g.paths.contains(currentFile_)) currentFile_.clear();
  grid_->blockSignals(false); list_->blockSignals(false);
}
void MainWindow::fileGridSelected() {
  auto* it = grid_->currentItem();
  currentFile_ = it ? it->data(Qt::UserRole).toString() : QString();
  refreshDetail();
}
void MainWindow::fileListSelected() {
  auto* it = list_->currentItem();
  currentFile_ = it ? it->data(1, Qt::UserRole).toString() : QString();
  refreshDetail();
}
void MainWindow::fileActivated(QListWidgetItem* it) {
  if (it) { currentFile_ = it->data(Qt::UserRole).toString(); openSelected(); }
}
void MainWindow::detailTabChanged(int) { refreshDetail(); }
void MainWindow::refreshDetail() {
  while (detailForm_->count()) { auto* it = detailForm_->takeAt(0); delete it->widget(); delete it; }
  exifLabel_->clear(); simLabel_->clear(); simBar_->setValue(0); hashLabel_->clear();
  if (currentFile_.isEmpty()) { preview_->setText("—"); return; }
  QFileInfo fi(currentFile_);
  preview_->setPixmap(fileThumb(currentFile_, QSize(220, 190), true).pixmap(220, 190));
  const bool ref = (currentGroup_ >= 0 && !groups_[currentGroup_].paths.isEmpty()
                    && groups_[currentGroup_].paths[0] == currentFile_);
  const double pct = ref ? 100.0 : pathBest(currentFile_);
  detailForm_->addRow(trStr(lang(), "fileName"), new QLabel(fi.fileName(), this));
  auto* pl = new QLabel(currentFile_, this); pl->setTextInteractionFlags(Qt::TextSelectableByMouse); pl->setWordWrap(true);
  detailForm_->addRow(trStr(lang(), "fullPath"), pl);
  detailForm_->addRow(trStr(lang(), "fileSize"),
                      new QLabel(QString("%1 (%2 bytes)").arg(fmtSize(fi.size())).arg(fi.size()), this));
  detailForm_->addRow(trStr(lang(), "format"), new QLabel(fi.suffix().toUpper(), this));
  detailForm_->addRow(trStr(lang(), "modified"),
                      new QLabel(fi.lastModified().toString("yyyy-MM-dd hh:mm:ss"), this));
  detailForm_->addRow(trStr(lang(), "created"),
                      new QLabel(fi.birthTime().isValid() ? fi.birthTime().toString("yyyy-MM-dd hh:mm:ss") : "-", this));
  detailForm_->addRow(trStr(lang(), "resolution"), new QLabel(fileResolution(currentFile_), this));
  const double dur = fileDur_.value(currentFile_, 0.0);
  detailForm_->addRow(trStr(lang(), "duration"),
                      new QLabel(dur > 0 ? QString("%1:%2").arg(int(dur) / 60, 2, 10, QChar('0')).arg(int(dur) % 60, 2, 10, QChar('0')) : "-", this));
  detailForm_->addRow(trStr(lang(), "similarity"),
                      new QLabel(ref ? QString("100% · ") + trStr(lang(), "reference")
                                     : QString("%1%").arg(pct, 0, 'f', 1), this));
  // EXIF tab
  QImageReader rd(currentFile_);
  const auto keys = rd.textKeys();
  if (keys.isEmpty()) exifLabel_->setText(trStr(lang(), "noExif"));
  else {
    QString t;
    for (const auto& k : keys) t += k + ": " + rd.text(k) + "\n";
    exifLabel_->setText(t);
  }
  // similarity tab
  QString bestPath; double best = -1;
  if (currentGroup_ >= 0)
    for (const auto& p : groups_[currentGroup_].paths) {
      if (p == currentFile_) continue;
      const double v = pathBest(p);
      if (v > best) { best = v; bestPath = p; }
    }
  if (bestPath.isEmpty()) { simLabel_->setText("-"); }
  else {
    simLabel_->setText(QString("%1\n%2\n%3%").arg(trStr(lang(), "bestMatch")).arg(bestPath).arg(best, 0, 'f', 1));
    simBar_->setValue(int(std::clamp(best, 0.0, 100.0)));
  }
  // hash tab
  const QString fp = fileFp_.value(currentFile_);
  hashLabel_->setText(QString("%1\n%2").arg(trStr(lang(), "phash")).arg(fp.isEmpty() ? "-" : ("0x" + fp)));
}

// ------------------------------------------------------------ mark + file ops
QStringList MainWindow::selectedFiles() const {
  QStringList out;
  if (viewStack_->currentIndex() == 1) {
    for (auto* it : list_->selectedItems()) out << it->data(1, Qt::UserRole).toString();
    if (out.isEmpty() && list_->currentItem()) out << list_->currentItem()->data(1, Qt::UserRole).toString();
  } else {
    for (auto* it : grid_->selectedItems()) out << it->data(Qt::UserRole).toString();
    if (out.isEmpty() && grid_->currentItem()) out << grid_->currentItem()->data(Qt::UserRole).toString();
  }
  if (out.isEmpty() && !currentFile_.isEmpty()) out << currentFile_;
  out.removeDuplicates();
  return out;
}
void MainWindow::toggleMarkSelected() {
  // Space in the middle (group) pane marks the whole group; elsewhere it marks files.
  QWidget* fw = QApplication::focusWidget();
  auto inMid = [fw](QAbstractItemView* v) -> bool {
    return fw && (fw == v || fw == v->viewport() || v->isAncestorOf(fw));
  };
  QTreeWidget* trees[2] = {imgTree_, vidTree_};
  QListWidget* grids[2] = {imgGrid_, vidGrid_};
  for (auto* t : trees)
    if (inMid(t)) {
      const int gi = t->currentItem() ? t->currentItem()->data(0, Qt::UserRole).toInt() : currentGroup_;
      if (gi < 0 || gi >= groups_.size()) return;
      bool anyUn = false;
      for (const auto& p : groups_[gi].paths) if (!marked_.contains(p)) { anyUn = true; break; }
      setGroupMarked(gi, anyUn);
      return;
    }
  for (auto* g : grids)
    if (inMid(g)) {
      const int gi = g->currentItem() ? g->currentItem()->data(Qt::UserRole).toInt() : currentGroup_;
      if (gi < 0 || gi >= groups_.size()) return;
      bool anyUn = false;
      for (const auto& p : groups_[gi].paths) if (!marked_.contains(p)) { anyUn = true; break; }
      setGroupMarked(gi, anyUn);
      return;
    }
  const auto files = selectedFiles();
  if (files.isEmpty()) return;
  bool anyUnmarked = false;
  for (const auto& f : files) if (!marked_.contains(f)) { anyUnmarked = true; break; }
  for (const auto& f : files) { if (anyUnmarked) marked_.insert(f); else marked_.remove(f); }
  refreshFileViews(); updateStatusCounts();
}
void MainWindow::markAll(bool on) {
  if (currentGroup_ < 0) return;
  for (const auto& p : groups_[currentGroup_].paths) { if (on) marked_.insert(p); else marked_.remove(p); }
  refreshFileViews(); updateStatusCounts();
}
void MainWindow::invertMarked() {
  if (currentGroup_ < 0) return;
  for (const auto& p : groups_[currentGroup_].paths) {
    if (marked_.contains(p)) marked_.remove(p); else marked_.insert(p);
  }
  refreshFileViews(); updateStatusCounts();
}
void MainWindow::showFileMenu(const QPoint& pos) {
  QMenu m(this);
  m.addAction(trStr(lang(), "open"), this, &MainWindow::openSelected);
  m.addAction(trStr(lang(), "reveal"), this, &MainWindow::revealSelected);
  m.addSeparator();
  m.addAction(trStr(lang(), "copy"), this, &MainWindow::copySelected);
  m.addAction(trStr(lang(), "cut"), this, &MainWindow::cutSelected);
  m.addAction(trStr(lang(), "paste"), this, &MainWindow::pasteFiles);
  m.addAction(trStr(lang(), "move"), this, &MainWindow::moveSelected);
  m.addAction(trStr(lang(), "rename"), this, &MainWindow::renameSelected);
  m.addAction(trStr(lang(), "del"), this, &MainWindow::deleteSelected);
  m.addSeparator();
  m.addAction(trStr(lang(), "mark") + " (Space)", this, &MainWindow::toggleMarkSelected);
  m.addAction(trStr(lang(), "markAll"), this, [this] { markAll(true); });
  m.addAction(trStr(lang(), "unmarkAll"), this, [this] { markAll(false); });
  m.addAction(trStr(lang(), "invertMark"), this, &MainWindow::invertMarked);
  m.addSeparator();
  m.addAction(trStr(lang(), "ignore"), this, [this] { applyIgnore(selectedFiles(), true); });
  m.addAction(trStr(lang(), "unignore"), this, [this] { applyIgnore(selectedFiles(), false); });
  m.addSeparator();
  m.addAction(trStr(lang(), "analyze"), this, [this] {
    const auto f = selectedFiles();
    if (!f.isEmpty()) { folder_->setText(QFileInfo(f[0]).absolutePath()); startScan(); }
  });
  m.exec(pos);
}
void MainWindow::showGroupMenu(const QPoint& pos) {
  QMenu m(this);
  m.addAction(trStr(lang(), "markAll"), this, [this] { markAll(true); });
  m.addAction(trStr(lang(), "unmarkAll"), this, [this] { markAll(false); });
  m.addSeparator();
  m.addAction(trStr(lang(), "ignore"), this, [this] {
    if (currentGroup_ >= 0 && currentGroup_ < groups_.size()) applyIgnore(groups_[currentGroup_].paths, true);
  });
  m.addAction(trStr(lang(), "unignore"), this, [this] {
    if (currentGroup_ >= 0 && currentGroup_ < groups_.size()) applyIgnore(groups_[currentGroup_].paths, false);
  });
  m.exec(pos);
}
void MainWindow::openSelected() {
  for (const auto& p : selectedFiles()) QDesktopServices::openUrl(QUrl::fromLocalFile(p));
}
void MainWindow::revealSelected() {
  const auto ps = selectedFiles();
  if (ps.isEmpty()) return;
  QProcess::startDetached("explorer.exe", {QString("/select,%1").arg(QDir::toNativeSeparators(ps.first()))});
}
void MainWindow::renameSelected() {
  const auto ps = selectedFiles();
  if (ps.size() != 1) return;
  QFileInfo fi(ps.first());
  bool ok = false;
  const QString n = QInputDialog::getText(this, trStr(lang(), "rename"), trStr(lang(), "newName:"),
                                          QLineEdit::Normal, fi.fileName(), &ok);
  if (ok && !n.isEmpty() && n != fi.fileName()) {
    if (!QFile::rename(fi.filePath(), fi.dir().filePath(n)))
      QMessageBox::warning(this, trStr(lang(), "rename"), trStr(lang(), "renameFail"));
    else refreshAfterFileOperation(QFileInfo(fi.dir().filePath(n)).absoluteFilePath());
  }
}
void MainWindow::deleteSelected() {
  const auto ps = selectedFiles();
  if (ps.isEmpty()) return;
  if (QMessageBox::question(this, trStr(lang(), "delTitle"),
                            trStr(lang(), "delAsk").arg(ps.size())) != QMessageBox::Yes) return;
  QStringList gone;
  for (const auto& p : ps) {
#ifdef _WIN32
    if (recycleFile(p)) gone << p;
#else
    if (QFile::remove(p)) gone << p;
#endif
  }
  if (gone.size() != ps.size())
    QMessageBox::warning(this, trStr(lang(), "delTitle"), trStr(lang(), "delFail"));
  if (!gone.isEmpty()) prunePaths(QSet<QString>(gone.begin(), gone.end()));
}
void MainWindow::copySelected() {
  const auto ps = selectedFiles();
  if (ps.isEmpty()) return;
  auto* d = new QMimeData; QList<QUrl> urls;
  for (const auto& p : ps) urls << QUrl::fromLocalFile(p);
  d->setUrls(urls);
  QApplication::clipboard()->setMimeData(d);
}
void MainWindow::cutSelected() {
  copySelected();
  cutPaths_ = selectedFiles();
}
void MainWindow::pasteFiles() {
  const QMimeData* d = QApplication::clipboard()->mimeData();
  if (!d || !d->hasUrls() || folder_->text().isEmpty()) return;
  const QDir dst(folder_->text());
  for (const auto& u : d->urls()) {
    const QString src = u.toLocalFile();
    const QString target = dst.filePath(QFileInfo(src).fileName());
    if (cutPaths_.contains(src)) {
    if (QFile::rename(src, target)) cutPaths_.removeOne(src);
    else if (QFile::copy(src, target) && QFile::remove(src)) cutPaths_.removeOne(src);
    } else QFile::copy(src, target);
  }
  refreshAfterFileOperation(QString());
}
void MainWindow::moveSelected() {
  const auto ps = selectedFiles();
  if (ps.isEmpty()) return;
  const QString dst = QFileDialog::getExistingDirectory(this, trStr(lang(), "chooseDest"), folder_->text());
  if (dst.isEmpty()) return;
  QStringList gone;
  for (const auto& p : ps) {
    const QString target = QDir(dst).filePath(QFileInfo(p).fileName());
    if (QFile::rename(p, target)) gone << p;
    else if (QFile::copy(p, target) && QFile::remove(p)) gone << p;
  }
  if (gone.size() != ps.size())
    QMessageBox::warning(this, trStr(lang(), "move"), trStr(lang(), "moveFail"));
  if (!gone.isEmpty()) prunePaths(QSet<QString>(gone.begin(), gone.end()));
}
void MainWindow::prunePaths(const QSet<QString>& gone) {
  for (const auto& p : gone) {
    marked_.remove(p); resCache_.remove(p); fileSize_.remove(p); fileFp_.remove(p);
    fileDur_.remove(p); bestPct_.remove(p); pathKind_.remove(p); dropThumbCache(p);
    thumbFail_.remove(p);
    pathParent_.remove(p);
    if (currentFile_ == p) currentFile_.clear();
  }
  rebuildGroups(); refreshGroupList(); refreshFileViews(); refreshDetail(); updateStatusCounts();
}
void MainWindow::refreshAfterFileOperation(const QString&) {
  // file set changed on disk; simplest correct step is a fresh incremental scan
  if (!folder_->text().isEmpty() && !scanning_) startScan();
}

// ------------------------------------------------------------ summary + status
static QString fmtElapsed(qint64 ms) {
  const qint64 s = ms / 1000;
  return QString("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0')).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}
// Similarity-inspired ETR: linear extrapolation from the observed analyze
// rate. Totals grow while walking, so this is an estimate, never a promise.
QString MainWindow::scanStatusText(qulonglong done, qulonglong total, int pct,
                                   const QString& path, qint64 elapsedMs) const {
  const QString base = QString("%1 / %2 (%3%) — %4 — %5").arg(done).arg(total).arg(pct)
                           .arg(QFileInfo(path).fileName()).arg(fmtElapsed(elapsedMs));
  if (elapsedMs < 1000 || done == 0 || total <= done) return base;
  const qint64 remainMs = elapsedMs * (qint64)(total - done) / (qint64)done;
  return base + QString(" — %1 %2").arg(trStr(lang(), "remain")).arg(fmtElapsed(remainMs));
}
void MainWindow::refreshSummary(const msf::SearchReport*) {
  const UiLang l = lang();
  if (!hasReport_ || lastStats_.size() < 5) {
    sumValTotal_->setText("-"); sumValDone_->setText("-"); sumValGroups_->setText("-");
    sumValDup_->setText("-"); sumValTime_->setText("-"); sumValGpu_->setText(gpuEnabled_->isChecked() ? "GPU" : "CPU");
    sumValCpu_->setText("-"); sumValRam_->setText("-");
    if (!monitor_ || !monitor_->running()) sumValMon_->setText("-");
    return;
  }
  sumValTotal_->setText(lastStats_[0]);
  sumValDone_->setText(lastStats_[1] + " / " + lastStats_[2]);
  sumValGroups_->setText(lastStats_[3]);
  qulonglong dupFiles = 0;
  for (const auto& g : groups_) dupFiles += (qulonglong)g.paths.size();
  sumValDup_->setText(QString::number(dupFiles));
  sumValTime_->setText(fmtElapsed(repElapsedMs_));
  sumValGpu_->setText(gpuEnabled_->isChecked() ? "GPU" : "CPU");
}
void MainWindow::scanHeartbeat() {
  static qint64 lastBeat = 0;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - lastBeat < 10000) return;
  lastBeat = now;
  scanLog(QString("alive elapsed=%1 lastPct=%2 lastPath=%3 groups=%4 marked=%5")
              .arg(fmtElapsed(now - scanStartMs_)).arg(lastPct_).arg(lastPath_)
              .arg(groups_.size()).arg(marked_.size()));
}
void MainWindow::scanLog(const QString& line) {
  const QString p = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                    + QStringLiteral("/msf_scan.log");
  QFile f(p);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
  QTextStream out(&f);
  out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << " " << line << "\n";
}
void MainWindow::updateStatusCounts() {
  qulonglong files = 0;
  for (const auto& g : groups_) files += (qulonglong)g.paths.size();
  statusCount_->setText(QString("%1: %2 · %3: %4 · %5: %6")
                            .arg(trStr(lang(), "groups")).arg(groups_.size())
                            .arg(trStr(lang(), "files")).arg(files)
                            .arg(trStr(lang(), "marked")).arg(marked_.size()));
  if (scanning_) sumValTime_->setText(fmtElapsed(QDateTime::currentMSecsSinceEpoch() - scanStartMs_));
  updateGpuLabel();
}
// Live GPU status for the status bar. During a scan this shows how many images
// the CUDA backend already hashed; at rest it reports whether the backend is on.
void MainWindow::updateGpuLabel() {
  if (!gpuLbl_) return;
  const UiLang l = lang();
  const bool avail = worker_ ? worker_->gpuAvailable() : false;
  const qulonglong n = worker_ ? worker_->gpuDone() : 0;
  QString txt;
  if (scanning_ && n > 0) txt = trStr(l, "gpuLive").arg(n);
  else if (gpuEnabled_ && gpuEnabled_->isChecked() && avail) txt = trStr(l, "gpuOn");
  else txt = trStr(l, "gpuOff");
  gpuLbl_->setText("⚡ " + txt);
  gpuLbl_->setStyleSheet(gpuEnabled_ && gpuEnabled_->isChecked() && avail
                             ? QStringLiteral("color:#2e7d32;") : QStringLiteral("color:#999;"));
}
  void MainWindow::showHelp() {
    QMessageBox::about(this, trStr(lang(), "help"), trStr(lang(), "about"));
  }

// ------------------------------------------------------------ monitor (kept behavior)
void MainWindow::configureMonitor() {
  QSettings st;
  QDialog dlg(this);
  dlg.setWindowTitle(trStr(lang(), "monSettings"));
  dlg.resize(760, 560);
  dlg.restoreGeometry(QSettings().value("ui/settingsGeom").toByteArray());
  auto* root = new QVBoxLayout(&dlg);
  auto* tabs = new QTabWidget(&dlg);
  root->addWidget(tabs, 1);
  auto* generalTab = new QWidget(tabs);
  auto* generalLay = new QVBoxLayout(generalTab);
  auto* langRow = new QHBoxLayout;
  auto* langLabel = new QLabel(trStr(lang(), "language"), generalTab);
  auto* langSel = new QComboBox(generalTab);
  langSel->addItem(QStringLiteral("한국어"), QStringLiteral("ko"));
  langSel->addItem(QStringLiteral("English"), QStringLiteral("en"));
  langSel->setCurrentIndex(lang() == UiLang::Ko ? 0 : 1);
  langRow->addWidget(langLabel); langRow->addWidget(langSel); langRow->addStretch(1);
  generalLay->addLayout(langRow); generalLay->addStretch(1);
  tabs->addTab(generalTab, trStr(lang(), "general"));
  auto* monTab = new QWidget(tabs);
  auto* monLay = new QVBoxLayout(monTab);
  tabs->addTab(monTab, trStr(lang(), "monitor"));

  auto* watchGroup = new QGroupBox(trStr(lang(), "watchFolders"), monTab);
  auto* watchLayout = new QVBoxLayout(watchGroup);
  auto* watchList = new QListWidget(watchGroup);
  watchList->addItems(st.value("monitor/watchRoots").toStringList());
  auto* watchButtons = new QHBoxLayout;
  auto* watchAdd = new QPushButton(trStr(lang(), "addFolder"), watchGroup);
  auto* watchRemove = new QPushButton(trStr(lang(), "remove"), watchGroup);
  watchButtons->addWidget(watchAdd); watchButtons->addWidget(watchRemove); watchButtons->addStretch();
  watchLayout->addWidget(watchList); watchLayout->addLayout(watchButtons);
  monLay->addWidget(watchGroup, 1);

  auto* compareGroup = new QGroupBox(trStr(lang(), "compareFolders"), monTab);
  auto* compareLayout = new QVBoxLayout(compareGroup);
  auto* compareList = new QListWidget(compareGroup);
  compareList->addItems(st.value("monitor/compareRoots").toStringList());
  auto* compareButtons = new QHBoxLayout;
  auto* compareAdd = new QPushButton(trStr(lang(), "addFolder"), compareGroup);
  auto* compareRemove = new QPushButton(trStr(lang(), "remove"), compareGroup);
  compareButtons->addWidget(compareAdd); compareButtons->addWidget(compareRemove); compareButtons->addStretch();
  compareLayout->addWidget(compareList); compareLayout->addLayout(compareButtons);
  monLay->addWidget(compareGroup, 1);

  auto* settingsGroup = new QGroupBox(trStr(lang(), "policy"), monTab);
  auto* settings = new QGridLayout(settingsGroup);
  auto* thresholdLabel = new QLabel(trStr(lang(), "threshold"), settingsGroup);
  auto* threshold = new QSpinBox(settingsGroup); threshold->setRange(50, 100); threshold->setSuffix(" %");
  threshold->setValue(st.value("monitor/thresholdPercent", 90).toInt());
  auto* stableLabel = new QLabel(trStr(lang(), "stable"), settingsGroup);
  auto* stable = new QSpinBox(settingsGroup); stable->setRange(1, 60); stable->setSuffix(" s");
  stable->setValue(st.value("monitor/stableSeconds", 3).toInt());
  auto* pollLabel = new QLabel(trStr(lang(), "poll"), settingsGroup);
  auto* poll = new QSpinBox(settingsGroup); poll->setRange(1, 60); poll->setSuffix(" s");
  poll->setValue(st.value("monitor/pollSeconds", 2).toInt());
  auto* gpu = new QCheckBox(trStr(lang(), "allowGpu"), settingsGroup);
  gpu->setChecked(st.value("monitor/gpuEnabled", gpuEnabled_->isChecked()).toBool());
  settings->addWidget(thresholdLabel, 0, 0); settings->addWidget(threshold, 0, 1);
  settings->addWidget(stableLabel, 1, 0); settings->addWidget(stable, 1, 1);
  settings->addWidget(pollLabel, 2, 0); settings->addWidget(poll, 2, 1);
  settings->addWidget(gpu, 3, 0, 1, 2);
  monLay->addWidget(settingsGroup);

  auto addFolder = [this](QListWidget* list) {
    const QString dir = QFileDialog::getExistingDirectory(this, trStr(lang(), "chooseTitle"));
    if (dir.isEmpty()) return;
    const QString clean = QDir::cleanPath(dir);
    for (int i = 0; i < list->count(); ++i)
      if (QDir::cleanPath(list->item(i)->text()).compare(clean, Qt::CaseInsensitive) == 0) return;
    list->addItem(clean);
  };
  connect(watchAdd, &QPushButton::clicked, this, [&] { addFolder(watchList); });
  connect(compareAdd, &QPushButton::clicked, this, [&] { addFolder(compareList); });
  connect(watchRemove, &QPushButton::clicked, watchList, [watchList] { delete watchList->takeItem(watchList->currentRow()); });
  connect(compareRemove, &QPushButton::clicked, compareList, [compareList] { delete compareList->takeItem(compareList->currentRow()); });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
  root->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  const bool accepted = (dlg.exec() == QDialog::Accepted);
  QSettings().setValue("ui/settingsGeom", dlg.saveGeometry());
  if (!accepted) return;

  QStringList watches, compares;
  for (int i = 0; i < watchList->count(); ++i) watches << watchList->item(i)->text();
  for (int i = 0; i < compareList->count(); ++i) compares << compareList->item(i)->text();
  st.setValue("monitor/watchRoots", watches);
  st.setValue("monitor/compareRoots", compares);
  st.setValue("monitor/thresholdPercent", threshold->value());
  st.setValue("monitor/stableSeconds", stable->value());
  st.setValue("monitor/pollSeconds", poll->value());
  st.setValue("monitor/gpuEnabled", gpu->isChecked());
  st.setValue("ui/language", langSel->currentData().toString());
  setLanguage(langSel->currentIndex());
  statusMsg_->setText(QString("%1 — %2 / %3").arg(trStr(lang(), "monSaved")).arg(watches.size()).arg(compares.size()));
}
void MainWindow::toggleMonitor() {
  if (monitorEnabled_) {
    monitor_->stop(); monitorEnabled_ = false; monitorPaused_ = false;
    monBtn_->setChecked(false); monPauseBtn_->setChecked(false);
    tray_->setToolTip(trStr(lang(), "app"));
    statusMsg_->setText(trStr(lang(), "monStop"));
    sumValMon_->setText("-");
    return;
  }
  QSettings st;
  auto ws = st.value("monitor/watchRoots").toStringList();
  auto cs = st.value("monitor/compareRoots").toStringList();
  if (ws.isEmpty() || cs.isEmpty()) {
    configureMonitor();
    ws = st.value("monitor/watchRoots").toStringList();
    cs = st.value("monitor/compareRoots").toStringList();
  }
  if (ws.isEmpty() || cs.isEmpty()) { statusMsg_->setText(trStr(lang(), "monNeedCfg")); return; }
  msf::MonitorConfig c;
  for (const auto& x : ws) c.watchRoots.push_back(x.toStdString());
  for (const auto& x : cs) c.compareRoots.push_back(x.toStdString());
  c.applicationDirectory = QApplication::applicationDirPath().toStdString();
  c.thresholdPercent = st.value("monitor/thresholdPercent", 90).toDouble();
  c.stableSeconds = st.value("monitor/stableSeconds", 3).toInt();
  c.pollSeconds = st.value("monitor/pollSeconds", 2).toInt();
  c.gpuEnabled = st.value("monitor/gpuEnabled", gpuEnabled_->isChecked()).toBool();
  monitor_->start(c, policy_, [this](const msf::MonitorEvent& e) {
    QMetaObject::invokeMethod(this, [this, e] { monitorEvent(e); }, Qt::QueuedConnection);
  });
  monitorEnabled_ = true; monitorPaused_ = false;
  monBtn_->setChecked(true); monPauseBtn_->setChecked(false);
  tray_->setToolTip(trStr(lang(), "monRun"));
  statusMsg_->setText(trStr(lang(), "monRun"));
}
void MainWindow::toggleMonitorPause() {
  if (!monitorEnabled_ || !monitor_) return;
  monitorPaused_ = !monitorPaused_;
  monitor_->setPaused(monitorPaused_);
  monPauseBtn_->setChecked(monitorPaused_);
  tray_->setToolTip(monitorPaused_ ? trStr(lang(), "paused") : trStr(lang(), "monRun"));
}
void MainWindow::monitorEvent(const msf::MonitorEvent& e) {
  if (e.type == msf::MonitorEvent::Type::Match) { showMonitorMatch(e); return; }
  if (e.type == msf::MonitorEvent::Type::Deferred) {
    statusMsg_->setText(QString("Monitor delayed: %1").arg(QString::fromStdString(e.path))); return;
  }
  if (e.type == msf::MonitorEvent::Type::Error) {
    statusMsg_->setText(QString("Monitor error: %1").arg(QString::fromStdString(e.path))); return;
  }
  if (e.type == msf::MonitorEvent::Type::Started || e.type == msf::MonitorEvent::Type::Stopped)
    statusMsg_->setText(QString::fromStdString(e.detail));
}
void MainWindow::updateMonitorStatus() {
  if (!monitor_ || !monitor_->running()) return;
  const auto s = monitor_->status();
  auto state = [](msf::LoadState x) {
    switch (x) {
    case msf::LoadState::Idle: return "Idle"; case msf::LoadState::Light: return "Light";
    case msf::LoadState::Busy: return "Busy"; case msf::LoadState::Heavy: return "Heavy";
    default: return "Critical";
    }
  };
  const QString gpu = s.gpuPercent < 0 ? "n/a" : QString::number(s.gpuPercent, 'f', 0) + "%";
  sumValMon_->setText(s.paused ? trStr(lang(), "paused") : trStr(lang(), "monRun"));
  sumValCpu_->setText(QString("%1%").arg(s.cpuPercent, 0, 'f', 0));
  sumValRam_->setText(QString("%1%").arg(s.memoryPercent, 0, 'f', 0));
  tray_->setToolTip(QString("%1 | %2 | GPU %3 | Q %4").arg(state(s.loadState)).arg(s.analyzed).arg(gpu).arg(s.pending));
}
void MainWindow::showMonitorMatch(const msf::MonitorEvent& e) {
  if (e.matches.empty()) return;
  QSettings st; const msf::MonitorMatch* selected = nullptr;
  for (const auto& candidate : e.matches) {
    QString raw = QString::fromStdString(candidate.newPath) + "|" + QString::fromStdString(candidate.existingPath);
    QString key = QString::fromLatin1(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (!st.value("monitor/skipped/" + key, false).toBool()) { selected = &candidate; break; }
  }
  if (!selected) return; const auto& m = *selected;
  QMessageBox box(this);
  box.setWindowTitle(trStr(lang(), "dupTitle"));
  box.setText(QString("New file:\n%1\n\nExisting file:\n%2\n\nSimilarity: %3%")
                  .arg(QString::fromStdString(m.newPath)).arg(QString::fromStdString(m.existingPath)).arg(m.percent, 0, 'f', 1));
  auto* openNew = box.addButton(trStr(lang(), "open"), QMessageBox::ActionRole);
  auto* openOld = box.addButton(trStr(lang(), "reveal"), QMessageBox::ActionRole);
  auto* delNew = box.addButton(trStr(lang(), "del"), QMessageBox::DestructiveRole);
  auto* skip = box.addButton("Skip", QMessageBox::RejectRole);
  box.exec();
  if (box.clickedButton() == openNew) QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(m.newPath)));
  else if (box.clickedButton() == openOld)
    QProcess::startDetached("explorer.exe", {QString("/select,%1").arg(QDir::toNativeSeparators(QString::fromStdString(m.existingPath)))});
#ifdef _WIN32
  else if (box.clickedButton() == delNew) {
    if (!recycleFile(QString::fromStdString(m.newPath)))
      QMessageBox::warning(this, trStr(lang(), "del"), trStr(lang(), "delFail"));
  }
#else
  else if (box.clickedButton() == delNew)
    QMessageBox::information(this, trStr(lang(), "del"), trStr(lang(), "delFail"));
#endif
  else if (box.clickedButton() == skip) {
    QString raw = QString::fromStdString(m.newPath) + "|" + QString::fromStdString(m.existingPath);
    QString key = QString::fromLatin1(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256).toHex());
    st.setValue("monitor/skipped/" + key, true);
  }
}
