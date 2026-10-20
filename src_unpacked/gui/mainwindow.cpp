#include "mainwindow.h"
#include "backend_loopback.h"
#include "backend_sysinfo.h" // 0.9.4.88: GUI's own process CPU for the combined display
#include "video_decoder.h"
#include "msf_build_version.h"
#include "../src/image_decoder.h"
#include "../src/proc_capture.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QFileInfo>
#include <QDirIterator>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QWheelEvent>
#include <algorithm>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMetaObject>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QTextBrowser>
#include <QProcess>
#include <QScrollBar>
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
#include <QMouseEvent>
#include <QFontMetrics>
#include <QStyleOptionSlider>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTextEdit>
#include <QTextOption>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QCloseEvent>
#include <vector>
#ifdef _WIN32
#include <sddl.h> // ConvertSidToStringSidW for the QuickLook pipe owner check
#endif
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
#include <shobjidl.h>
#include <shlguid.h>
#include <exdisp.h>
#include <thumbcache.h>
#include <winerror.h>
#include <psapi.h> // process CPU%/working set for the live summary
#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "uuid.lib")
#endif
#endif

// ------------------------------------------------------------ language table
QString trStr(UiLang lang, const char* key) {
  const bool ko = (lang == UiLang::Ko);
  auto S = [&](const char* k, const QString& e) -> QString { return ko ? QString::fromUtf8(k) : e; };
  if (!std::strcmp(key,"app")) return S("Media Similarity Finder","Media Similarity Finder");
  if (!std::strcmp(key,"selectFolder")) return S("폴더 선택","Select folder");
  if (!std::strcmp(key,"load")) return S("불러오기","Load");
  if (!std::strcmp(key,"loadRunning")) return S("인덱스에서 불러오는 중…","Loading from index…");
  if (!std::strcmp(key,"loadWait")) return S("인덱스 목록 로딩중…","Loading index list…");
  if (!std::strcmp(key,"loadWaitTitle")) return S("인덱스 불러오기","Loading index");
  // 0.9.4.134: forced duplicate re-verification ("재검증").
  if (!std::strcmp(key,"reverify")) return S("재검증","Re-verify");
  if (!std::strcmp(key,"reverifyTip")) return S("저장된 중복 파일들의 해시를 다시 확인해 내용이 바뀐 파일만 재분석합니다.","Re-reads the stored duplicate files' hashes and re-analyzes only the files whose content changed.");
  if (!std::strcmp(key,"reverifyRunning")) return S("중복 파일 재검증 중…","Re-verifying duplicate files…");
  if (!std::strcmp(key,"reverifyWait")) return S("저장된 중복 파일을 확인하고 필요하면 다시 분석합니다.","Checking stored duplicate files and re-analyzing changed ones.");
  if (!std::strcmp(key,"reverifyWaitTitle")) return S("재검증","Re-verification");
  if (!std::strcmp(key,"reverifyDone")) return S("재검증 완료 — 확인 %1 · 변경 %2 · 재분석 %3 · 매치 %4 → %5","Re-verification complete — checked %1, changed %2, re-analyzed %3, pairs %4 -> %5");
  // 0.9.4.135: Test Mode difference report (Yes/No adoption of the test result).
  if (!std::strcmp(key,"testDiffTitle")) return S("테스트 결과 반영","Apply test result");
  if (!std::strcmp(key,"testDiffBody")) return S("테스트 스캔이 본 인덱스와 다릅니다.\n\n내용 변경: %1\n신규: %2\n삭제(스캔 범위): %3\n매치: %4 → %5\n\n테스트 결과를 본 인덱스에 반영할까요?","The test scan differs from the main index.\n\nContent changed: %1\nNew: %2\nRemoved (in scan scope): %3\nPairs: %4 -> %5\n\nApply the test result to the main index?");
  if (!std::strcmp(key,"testNoDiff")) return S("테스트 스캔 결과가 본 인덱스와 일치합니다.","The test scan matches the main index.");
  if (!std::strcmp(key,"testNotApplied")) return S("테스트 결과를 반영하지 않았습니다.","Test result not applied.");
  if (!std::strcmp(key,"testApplying")) return S("테스트 결과를 반영하는 중…","Applying the test result…");
  if (!std::strcmp(key,"testApplied")) return S("테스트 결과를 본 인덱스에 반영했습니다.","Applied the test result to the main index.");
  if (!std::strcmp(key,"testApplyFailed")) return S("테스트 결과 반영 실패: %1","Applying the test result failed: %1");
  if (!std::strcmp(key,"start")) return S("검색/업데이트 시작","Start scan/update");
  if (!std::strcmp(key,"pause")) return S("일시정지","Pause");
  if (!std::strcmp(key,"resume")) return S("계속","Resume");
  if (!std::strcmp(key,"stop")) return S("중지","Stop");
  if (!std::strcmp(key,"settings")) return S("설정","Settings");
  if (!std::strcmp(key,"showDetailLog")) return S("상세 로그 표시","Show Detailed Logs");
  if (!std::strcmp(key,"showCpuPercent")) return S("CPU 사용량 % 표기","Show CPU usage %");
  if (!std::strcmp(key,"showTestButton")) return S("테스트 버튼 표시","Show Test button");
  // 0.9.4.134: fast-rescan integrity mode. Default OFF (strict) keeps the
  // current 3-way (size,mtime,quick-hash) change detection.
  if (!std::strcmp(key,"fastRescan")) return S("빠른 재스캔 (크기·수정시각만 비교)","Fast rescan (compare size/mtime only)");
  if (!std::strcmp(key,"fastRescanTip")) return S("켜면 변경되지 않은 파일의 64KiB 해시 읽기를 생략해 재스캔이 빨라집니다. 크기와 수정시각이 같은 내용 변경은 감지하지 못하므로, 중요하면 중복 파일 재검증을 주기적으로 실행하세요.","When on, unchanged files skip the 64 KiB hash read, making rescans much faster. A content change that preserves both size and mtime is then not detected; run the duplicate re-verification periodically if that matters.");
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
  if (!std::strcmp(key,"groupsTotal")) return S("전체","Total");
  if (!std::strcmp(key,"groupSel")) return S("선택","Selected");
  if (!std::strcmp(key,"dups")) return S("중복 파일","Duplicates");
  if (!std::strcmp(key,"elapsed")) return S("검색 시간","Elapsed");
  if (!std::strcmp(key,"updated")) return S("마지막 업데이트","Last update");
  if (!std::strcmp(key,"gpu")) return S("GPU 가속","GPU accel.");
  if (!std::strcmp(key,"gpuOn")) return S("GPU 가동","GPU on");
  if (!std::strcmp(key,"gpuOff")) return S("GPU 꺼짐","GPU off");
  if (!std::strcmp(key,"gpuLive")) return S("GPU 처리 %1","GPU processing %1");
  if (!std::strcmp(key,"gpuAccel")) return S("가속 중","Accelerating");
  if (!std::strcmp(key,"gpuStop")) return S("정지","Stopped");
  if (!std::strcmp(key,"gpuWait")) return S("대기","Idle");
  if (!std::strcmp(key,"cpu")) return S("CPU 사용","CPU usage");
  if (!std::strcmp(key,"ram")) return S("RAM 사용","RAM usage");
  if (!std::strcmp(key,"monitor")) return S("모니터","Monitor");
  if (!std::strcmp(key,"monitorGpu")) return S("모니터링 GPU가속","Monitoring GPU acceleration");
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
  // 0.9.4.143: Favorites recent-folder count setting.
  if (!std::strcmp(key,"recentLimitLabel")) return S("최근 폴더 표시 개수","Recent folder count");
  if (!std::strcmp(key,"recentLimitTip")) return S("왼쪽 즐겨찾기에 표시할 최근 검색 폴더 수입니다 (1~20, 기본 3).","How many recent scan folders to show in Favorites (1-20, default 3).");
  if (!std::strcmp(key,"scanHere")) return S("이 폴더로 검색","Scan this folder");
  if (!std::strcmp(key,"browse")) return S("찾아보기…","Browse…");
  if (!std::strcmp(key,"ignoredHint")) return S("무시된 파일은 검색 결과에서 제외됩니다 (다음 검색부터 적용).","Ignored files are excluded from results (applies from next scan).");
  if (!std::strcmp(key,"viewXL")) return S("아주 큰 아이콘","Extra large icons");
  if (!std::strcmp(key,"viewL")) return S("큰 아이콘","Large icons");
  if (!std::strcmp(key,"viewM")) return S("보통 아이콘","Medium icons");
  if (!std::strcmp(key,"viewS")) return S("작은 아이콘","Small icons");
  // viewGrid pairs with viewList: the two QToolButton toggles above the file
  // view (the check-mark glyph and the rows glyph). viewList was already
  // defined, viewGrid was not, so the toggle's tooltip showed the raw key.
  if (!std::strcmp(key,"viewGrid")) return S("격자","Grid");
  if (!std::strcmp(key,"viewList")) return S("리스트","List");
  if (!std::strcmp(key,"viewDetails")) return S("자세히","Details");
  if (!std::strcmp(key,"viewTiles")) return S("타일","Tiles");
  if (!std::strcmp(key,"kindMenu")) return S("검색 대상","Scan target");
  if (!std::strcmp(key,"kindImages")) return S("사진","Photos");
  if (!std::strcmp(key,"kindVideos")) return S("영상","Videos");
  if (!std::strcmp(key,"kindAll")) return S("모두","All");
  // 0.9.4.139: the sort is a type + a direction (the old labels baked the
  // descending/ascending order into the type text).
  if (!std::strcmp(key,"sortSim")) return S("유사도","Similarity");
  if (!std::strcmp(key,"sortName")) return S("이름","Name");
  if (!std::strcmp(key,"sortSize")) return S("그룹 크기","Group size");
  if (!std::strcmp(key,"sortNone")) return S("없음","None");
  if (!std::strcmp(key,"sortDesc")) return S("내림차순","Descending");
  if (!std::strcmp(key,"sortAsc")) return S("오름차순","Ascending");
  if (!std::strcmp(key,"searchGroups")) return S("그룹 검색","Search groups");
  if (!std::strcmp(key,"exportCsv")) return S("그룹 내보내기 (CSV)","Export groups (CSV)");
  if (!std::strcmp(key,"group")) return S("그룹","Group");
  if (!std::strcmp(key,"files")) return S("파일","files");
  if (!std::strcmp(key,"pairs")) return S("쌍","Pairs");
  if (!std::strcmp(key,"remain")) return S("남음","Left");
  if (!std::strcmp(key,"similarity")) return S("유사도","Similarity");
  if (!std::strcmp(key,"reference")) return S("기준 파일","reference");
  // 0.9.4.106: what a displayed 100.0% actually means (size + quick hash equal
  // vs only perceptually identical derivative). Display-only badges.
  if (!std::strcmp(key,"identicalContent")) return S("동일 내용","identical content");
  if (!std::strcmp(key,"perceptualMatch")) return S("지각 동일","perceptual match");
  if (!std::strcmp(key,"tabDetail")) return S("상세 정보","Details");
  if (!std::strcmp(key,"tabExif")) return S("EXIF 정보","EXIF");
  if (!std::strcmp(key,"tabSim")) return S("유사도 분석","Similarity");
  if (!std::strcmp(key,"tabHash")) return S("해시 정보","Hash");
  // Column headers for the file list view. The labels above (fileName, modified,
  // ...) end in a colon because they are detail-form rows; a tree column header
  // must not, so these are separate keys rather than a reuse of those. They were
  // missing, and the fallback at the end of trStr() returned the key itself, so
  // the header rendered the literal text "colFile" and "colDate". colDate holds
  // QFileInfo::lastModified(), matching the "수정 날짜" terminology.
  if (!std::strcmp(key,"colFile")) return S("파일 이름","File name");
  if (!std::strcmp(key,"colDate")) return S("수정 날짜","Modified");
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
  if (!std::strcmp(key,"quickLook")) return S("QuickLook으로 미리보기","Preview with QuickLook");
  if (!std::strcmp(key,"quickLookFail")) return S("QuickLook을 시작할 수 없습니다","Could not start QuickLook");
  if (!std::strcmp(key,"quickLookStarting")) return S("QuickLook 시작 중…","Starting QuickLook…");
  if (!std::strcmp(key,"revalidated")) return S("구버전 인덱스 재검증: %1 유지·%2 제외, 최신 엔진에 맞춤","Legacy index revalidated: %1 kept, %2 dropped for the current engine");
  if (!std::strcmp(key,"revalidating")) return S("저장된 쌍 재검증 중 %1/%2 (엔진 버전 갱신)","Revalidating stored pairs %1/%2 (engine version update)");
  if (!std::strcmp(key,"reveal")) return S("탐색기에서 보기","Reveal in Explorer");
  if (!std::strcmp(key,"revealNoWindow")) return S("열려 있는 해당 폴더 탐색기 창이 없습니다","No open Explorer window for this folder");
  if (!std::strcmp(key,"revealFocusFail")) return S("탐색기 창은 열렸지만 파일을 선택하지 못했습니다","Explorer window opened, but the file could not be selected");
  if (!std::strcmp(key,"revealFail")) return S("탐색기에서 파일을 찾지 못했습니다","Could not find the file in Explorer");
  if (!std::strcmp(key,"copy")) return S("복사","Copy");
  if (!std::strcmp(key,"cut")) return S("잘라내기","Cut");
  if (!std::strcmp(key,"paste")) return S("붙여넣기","Paste");
  if (!std::strcmp(key,"move")) return S("이동...","Move...");
  if (!std::strcmp(key,"del")) return S("휴지통으로","Move to Trash");
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
  // 0.9.4.132: Load-only completion (no scan follows). The quick-load wording
  // above belongs to the scan path; reusing it after a load claimed a scan
  // that never started.
  if (!std::strcmp(key,"loadDone")) return S("인덱스 불러오기 완료 — 파일 %1 · 매치 %2","Index load complete — %1 files · %2 matches");
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
  if (!std::strcmp(key,"scanGpuTip")) return S("검색 분석에 GPU 사용 (이미지 배치·영상 pHash/SSIM)","Use GPU for scan analysis (image batches, video pHash/SSIM)");
  if (!std::strcmp(key,"monSaved")) return S("모니터 설정 저장됨","Monitor settings saved");
  if (!std::strcmp(key,"monRun")) return S("실시간 모니터 실행 중","Real-time monitor running");
  if (!std::strcmp(key,"monStop")) return S("실시간 모니터 중지됨","Real-time monitor stopped");
  if (!std::strcmp(key,"monNeedCfg")) return S("모니터 미시작: 감시/비교 폴더를 설정하세요","Monitor not started: configure watched and comparison folders");
  if (!std::strcmp(key,"backendDown")) return S("백엔드 사용 불가: 재시작 중입니다","Backend unavailable: restarting");
  if (!std::strcmp(key,"dupTitle")) return S("중복 가능성 감지","Possible duplicate detected");
  if (!std::strcmp(key,"delTitle")) return S("삭제","Delete");
  if (!std::strcmp(key,"delAsk")) return S("선택한 %1개 파일을 삭제할까요? (휴지통으로 이동)","Delete %1 selected file(s)? (moves to Recycle Bin)");
  if (!std::strcmp(key,"delFail")) return S("파일을 삭제할 수 없습니다.","Could not delete the file.");
  if (!std::strcmp(key,"moveFail")) return S("파일을 이동할 수 없습니다.","Could not move the file.");
  if (!std::strcmp(key,"renameFail")) return S("이름을 바꿀 수 없습니다.","Could not rename the file.");
  if (!std::strcmp(key,"csvSaved")) return S("CSV 저장됨: ","CSV saved: ");
  if (!std::strcmp(key,"csvFail")) return S("CSV 저장 실패","CSV save failed");
  if (!std::strcmp(key,"detailLogTitle")) return S("상세 로그","Detailed Logs");
  if (!std::strcmp(key,"detailLogSave")) return S("JSON 저장…","Save JSON…");
  if (!std::strcmp(key,"detailLogClose")) return S("닫기","Close");
  if (!std::strcmp(key,"detailLogSaved")) return S("상세 로그 저장됨: ","Detailed log saved: ");
  if (!std::strcmp(key,"detailLogSaveFail")) return S("상세 로그 저장 실패","Detailed log save failed");
  if (!std::strcmp(key,"sumVerify")) return S("쌍검증 (전체/확정)","Pair verify (total/confirmed)");
  if (!std::strcmp(key,"detailLogParseFail")) return S("상세 로그 열기 실패 (기록 손상)","Could not open the detailed log (record damaged)");
  if (!std::strcmp(key,"detailLogDone")) return S("완료","Completed");
  if (!std::strcmp(key,"detailLogStopped")) return S("중단","Stopped");
  if (!std::strcmp(key,"detailLogState")) return S("상태","Status");
  if (!std::strcmp(key,"detailLogWall")) return S("소요","Elapsed");
  if (!std::strcmp(key,"detailLogFiles")) return S("파일","Files");
  if (!std::strcmp(key,"detailLogImages")) return S("이미지","Images");
  if (!std::strcmp(key,"detailLogVideos")) return S("비디오","Videos");
  if (!std::strcmp(key,"detailLogCpuUse")) return S("CPU 사용률","CPU usage");
  // 0.9.4.129: the summary CPU row now names both series instead of showing
  // only the process-tree number. A field report read "CPU 2%" while the
  // machine was visibly loaded (the process metric is small by design on
  // multithreaded scans); the system-wide mean makes the difference explicit.
  if (!std::strcmp(key,"detailLogCpuProc")) return S("프로세스","process");
  if (!std::strcmp(key,"detailLogCpuSys")) return S("시스템","system");
  // 0.9.4.136: preview-size gauge (1.0x marker + drag ratio bubble).
  if (!std::strcmp(key,"zoomTip")) return S("미리보기 크기 (1.0 = 기본)","Preview size (1.0 = default)");
  if (!std::strcmp(key,"detailLogMemMax")) return S("메모리 최대","Peak memory");
  if (!std::strcmp(key,"detailLogGpuDuty")) return S("GPU 듀티","GPU duty");
  if (!std::strcmp(key,"detailLogIo")) return S("디스크 I/O","Disk I/O");
  if (!std::strcmp(key,"detailLogMatches")) return S("매치","Matches");
  if (!std::strcmp(key,"detailLogSlow")) return S("느린 파일","Slowest files");
  if (!std::strcmp(key,"detailLogFailed")) return S("실패한 파일","Failed files");
  if (!std::strcmp(key,"detailLogSummary")) return S("검색 요약","Search Summary");
  if (!std::strcmp(key,"detailLogDuration")) return S("검색 시간","Search Duration");
  if (!std::strcmp(key,"detailLogDupGroups")) return S("중복 그룹","duplicate groups");
  if (!std::strcmp(key,"detailLogDupFiles")) return S("중복 파일","duplicate files");
  if (!std::strcmp(key,"detailLogDupPairs")) return S("중복 쌍","duplicate pairs");
  if (!std::strcmp(key,"detailLogThroughput")) return S("처리량(분석 단계)","throughput (analysis stage)");
  // 0.9.4.119: the file line now shows two explicitly-based rates. The old
  // single "(0.0 files/s)" was analyzed/wall, which reads as nonsense next to
  // scanned=177,781; both bases are named instead (JSON values unchanged).
  if (!std::strcmp(key,"detailLogRateOverall")) return S("전체","overall");
  if (!std::strcmp(key,"detailLogRateNew")) return S("신규","new");
  if (!std::strcmp(key,"detailLogTotal")) return S("합계","Total");
  if (!std::strcmp(key,"detailLogTimeSplit")) return S("시간 배분","Time split");
  if (!std::strcmp(key,"detailLogMatching")) return S("매칭","Matching");
  if (!std::strcmp(key,"readDone")) return S("읽기 완료","Read complete");
  if (!std::strcmp(key,"indexDone")) return S("인덱스 완료","Index complete");
  if (!std::strcmp(key,"detailLogToggle")) return S("상세 로그","Detailed Logs");
  if (!std::strcmp(key,"fpProgress")) return S("지문 생성 중","Fingerprinting");
  if (!std::strcmp(key,"searchLog")) return S("검색 로그","Search Log");
  if (!std::strcmp(key,"detailLogOff")) return S("상세 기록 꺼짐 (결과만 표시)","Detail recording off (results only)");
  // Execution-strategy keys. Kept apart from the detailLog* keys above:
  // those render the diagnostic telemetry dialog, these label the GUI
  // execution-resource strategy (AUTO / CPU-only / GPU-max).
  // Progress wording deliberately says "completed"/"last": S2 reports a case only
  // when it finishes, so there is no in-flight file that could honestly be named.
  if (!std::strcmp(key,"strategyModeAuto")) return S("AUTO","AUTO");
  if (!std::strcmp(key,"strategyModeCpu")) return S("CPU 단독","CPU only");
  if (!std::strcmp(key,"strategyModeGpu")) return S("GPU 최대 활용","GPU MAX");
  if (!std::strcmp(key,"detailLogSaved")) return S("결과 저장: %1","Saved: %1");
  if (!std::strcmp(key,"detailLogSaveFail")) return S("결과 저장 실패 (기존 결과 유지)","Could not save the result (previous result kept)");
  if (!std::strcmp(key,"strategyModeTip")) return S("실행 자원 전략 선택 (1개만)","Execution strategy (select one)");
  if (!std::strcmp(key,"testMode")) return S("테스트","Test");
  if (!std::strcmp(key,"testModeTip")) return S("인덱스 저장 없이 같은 설정으로 전체 처리를 다시 실행 (기존 인덱스 유지)","Re-run full processing with the same settings without saving the index (existing index kept)");
  if (!std::strcmp(key,"testModeOn")) return S("테스트 모드 켜짐: 다음 검색은 임시 인덱스로 전체 처리하며 기존 인덱스를 저장하지 않습니다","Test Mode on: the next scan fully reprocesses into a scratch index and does not save the existing index");
  if (!std::strcmp(key,"testModeOff")) return S("테스트 모드 꺼짐: 다음 검색은 기존 인덱스를 정상 업데이트합니다","Test Mode off: the next scan normally updates the existing index");
  // Analyze-mode selector (0.9.4.81): an option inside Settings. The friendly
  // label is shown; the stable internal key (B/A/AB) is logged, never shown.
  if (!std::strcmp(key,"analyzeModeLabel")) return S("매칭 검증 방식","Matching verification");
  if (!std::strcmp(key,"analyzeModeStable")) return S("안정형 (스캔 후 검증)","Stable (verify after scan)");
  if (!std::strcmp(key,"analyzeModeFast")) return S("고속형 (스캔 중 검증)","Fast (verify during scan)");
  if (!std::strcmp(key,"analyzeModeAuto")) return S("자동 (혼합 검증)","Auto (hybrid)");
  if (!std::strcmp(key,"analyzeModeTip")) return S("중복 후보 검증을 언제/어떻게 수행할지 선택합니다. 결과(판정)는 동일하며 기본은 안정형입니다.","Choose when/how duplicate candidates are verified. Results are identical; default is Stable.");
  // 0.9.4.108: detailed-log summary header rows (mode + scanned folder).
  if (!std::strcmp(key,"summaryMode")) return S("검색 방법","Search method");
  if (!std::strcmp(key,"summaryFolder")) return S("검색 폴더","Search folder");
  if (!std::strcmp(key,"detailLogTip")) return S("실제 검색에 상세 로그 연결 (별도 벤치마크 실행 아님)","Attach detailed logging to the actual search (not a separate benchmark run)");
  if (!std::strcmp(key,"repWaitTitle")) return S("검색 리포트 작성 중","Writing search report");
  if (!std::strcmp(key,"repWait")) return S("검색 리포트를 작성 중입니다. 잠시만 기다려 주세요…","Writing the search report. Please wait a moment…");
  if (!std::strcmp(key,"repWaitClose")) return S("검색 리포트를 작성 중입니다. 종료하시겠습니까?","The search report is being written. Exit anyway?");
  if (!std::strcmp(key,"stopWait")) return S("정지 처리 중입니다. 진행 중인 분석이 끝나는 대로 정리됩니다…","Stopping. Wrapping up the in-flight analysis…");
  if (!std::strcmp(key,"about")) {
    // Version comes from the generated build header, so the About text can no
    // longer drift away from the actual build.
    const std::string ver(MSF_BUILD_VERSION);
    return S(("Media Similarity Finder " + ver + "\n미디어 중복/유사 검색 (CPU/GPU)\n언어: 설정에서 한국어/English 전환").c_str(),
              QStringLiteral("Media Similarity Finder ") + QString::fromLatin1(MSF_BUILD_VERSION)
                  + QStringLiteral("\nMedia duplicate/similarity search (CPU/GPU)\nLanguage: switch 한국어/English in Settings"));
  }
  return QString::fromUtf8(key);
}
// High-contrast selection for result/file views: the native theme highlight
// is too faint to tell which file is selected. One shared blue for active
// selection, a lighter blue when the view is inactive.
static void applySelectionStyle(QWidget* w) {
  w->setStyleSheet(
      "QListWidget::item:selected { background:#2b6cb0; color:#ffffff; }"
      "QListWidget::item:selected:!active { background:#7aa7d9; color:#ffffff; }"
      "QTreeWidget::item:selected { background:#2b6cb0; color:#ffffff; }"
      "QTreeWidget::item:selected:!active { background:#7aa7d9; color:#ffffff; }");
}
// Small non-modal notice shown while the finished-scan report views rebuild
// (or while a stop request winds down in-flight analyses). It is closed as
// soon as the real results are on screen, so users wait instead of closing
// the program mid-rebuild.
static QDialog* showReportWaitPopup(QWidget* parent, UiLang lang, const char* textKey = "repWait",
                                    const char* titleKey = "repWaitTitle") {
  auto* dlg = new QDialog(parent);
  dlg->setWindowTitle(trStr(lang, titleKey));
  dlg->setModal(false);
  auto* lay = new QVBoxLayout(dlg);
  lay->setContentsMargins(24, 18, 24, 18);
  auto* lab = new QLabel(trStr(lang, textKey), dlg);
  // Two-line wrapped text sized to content: single-line fixed boxes clipped
  // CJK glyphs (the stop-wait notice lost its tail at 360px).
  lab->setAlignment(Qt::AlignCenter);
  lab->setWordWrap(true);
  lay->addWidget(lab);
  dlg->setMinimumWidth(460);
  dlg->adjustSize();
  dlg->setFixedSize(dlg->size());
  dlg->show();
  QApplication::processEvents();
  return dlg;
}

#ifdef _WIN32
static bool recycleFile(const QString& path) {
  SHFILEOPSTRUCTW op{}; std::wstring p = path.toStdWString(); p.push_back(L'\0');
  op.wFunc = FO_DELETE; op.pFrom = p.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
  return SHFileOperationW(&op) == 0;
}
// 0.9.4.104 (A): batch recycle — one SHFileOperationW call for the whole set.
// Per-file shell calls cost one COM/shell round-trip each, so N checked files
// meant N round-trips on the GUI thread. A single double-null-terminated
// multi-string call does them all at once. SHFileOperationW reports only
// overall success, so existence is verified per file afterwards: survivors
// are retried individually (locked/long paths), keeping the previous
// partial-progress semantics (gone = actually recycled).
static QStringList recycleFiles(const QStringList& paths) {
  QStringList gone;
  if (paths.isEmpty()) return gone;
  std::wstring multi;
  for (const auto& path : paths) {
    const std::wstring p = path.toStdWString();
    multi.append(p.c_str(), p.size() + 1);
  }
  multi.push_back(L'\0');
  SHFILEOPSTRUCTW op{};
  op.wFunc = FO_DELETE; op.pFrom = multi.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
  if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted) {
    for (const auto& path : paths) {
      if (!QFile::exists(path)) gone << path;
      else if (recycleFile(path) && !QFile::exists(path)) gone << path;
    }
    return gone;
  }
  for (const auto& path : paths) if (recycleFile(path)) gone << path;
  return gone;
}
#endif

// ------------------------------------------------------------ ScanWorker
static QString fmtElapsed(qint64 ms);
static QString pctText(double v, bool ref, UiLang l, int identity = 0);
static bool isVideoExt(const QString& path) {
  const QString e = QFileInfo(path).suffix().toLower();
  return e == "mp4" || e == "mkv" || e == "avi" || e == "mov" || e == "webm" || e == "m4v" || e == "wmv";
}

// Post-navigation full-rebuild cooldown (0.9.4.61): after a wheel/keyboard
// navigation event or a scrollbar release, full list refills wait this long
// so they never land mid-gesture. Short on purpose (inside one 600ms timer
// interval): input is never blocked, the rebuild is only deferred.
constexpr qint64 kScrollGateCooldownMs = 500;
// 0.9.4.105 (C): shared progress dialog for background file operations.
// total<=0 renders an indefinite busy bar (the single-call shell recycle);
// total>0 a determinate bar plus a cancel button (the per-file move loop).
// minimumDuration suppresses the dialog for quick ops; the operation's finish
// handler always closes it, and closing it must not auto-quit anything.
static QProgressDialog* newFileOpDialog(QWidget* parent, const QString& title, int total,
                                        bool cancellable, const QString& cancelText) {
  auto* dlg = new QProgressDialog(title, cancellable ? cancelText : QString(),
                                  0, total <= 0 ? 0 : total, parent);
  dlg->setWindowTitle(title);
  dlg->setWindowModality(Qt::WindowModal);
  dlg->setMinimumDuration(250);
  dlg->setAutoClose(false);
  dlg->setAutoReset(false);
  dlg->setValue(0);
  dlg->show();
  return dlg;
}
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


// ------------------------------------------------------------ MainWindow core
// One-time QSettings identity + storage bootstrap. MUST run before any
// default-constructed QSettings is used: without organization/application
// names every settings read/write silently fails (AccessError) and no UI
// state (window size, splitter, headers, view mode, ...) persists across
// runs. INI next to the executable keeps portable builds truly portable
// instead of depending on the Windows registry.
//
// 0.9.4.24: the organization name is the product name instead of a personal
// handle, because it is the on-disk folder name and therefore part of what a
// user sees in the portable distribution. The application name was already
// product-based and is unchanged.
static const char* const kSettingsOrganization = "MediaSimilarityFinder-ui";
// Pre-0.9.4.24 organization. Kept only to migrate its INI once; it must never
// become the active identity again.
static const char* const kLegacySettingsOrganization = "newclear-ui";
static const char* const kSettingsApplication = "MediaSimilarityFinder";

namespace {
// Keys whose presence proves the migrated copy is readable, not merely
// present. A file that copied but is empty or truncated would fail here
// instead of silently resetting the user's UI to defaults.
const char* const kMigrationProbeKeys[] = {"ui/mainGeom", "ui/splitter"};
constexpr int kMigrationProbeCount = 2;

bool migrationCopyLegacySettings(const QString& portableDir) {
  const QString legacyIni =
      QDir(portableDir).filePath(QString::fromLatin1(kLegacySettingsOrganization) + QLatin1Char('/') +
                                 QLatin1String(kSettingsApplication) + QLatin1String(".ini"));
  if (!QFileInfo::exists(legacyIni)) return false;   // Case A: nothing to do.

  const QString targetDir =
      QDir(portableDir).filePath(QString::fromLatin1(kSettingsOrganization));
  const QString targetIni = QDir(targetDir).filePath(QLatin1String(kSettingsApplication) + QLatin1String(".ini"));

  // Case C: a new-location file already exists. Never overwrite it: the new
  // location wins and the legacy file is preserved untouched, so no data can
  // be lost. This also makes the whole operation idempotent.
  if (QFileInfo::exists(targetIni)) return false;

  // Case D: if the new directory cannot be created, do not touch the legacy
  // file. The app still starts and simply writes to whatever path Qt uses.
  if (!QDir().mkpath(targetDir)) return false;
  if (!QFile::copy(legacyIni, targetIni)) return false;   // legacy preserved

  // Verify before removing anything (Section 5 ordering). A copy that cannot
  // be opened, or that lost its keys, is treated as a failed migration.
  {
    QSettings probe;
    probe.sync();
    if (probe.status() != QSettings::NoError) { QFile::remove(targetIni); return false; }
    for (int i = 0; i < kMigrationProbeCount; ++i)
      if (!probe.contains(QString::fromLatin1(kMigrationProbeKeys[i]))) {
        QFile::remove(targetIni);
        return false;
      }
  }

  // Only now is the legacy file removable. The directory is removed only when
  // it is left empty, so an unexpected file inside it keeps the whole thing.
  QFile::remove(legacyIni);
  QDir legacyDir(portableDir);
  legacyDir.cd(QString::fromLatin1(kLegacySettingsOrganization));
  if (legacyDir.isEmpty()) legacyDir.rmdir(".");
  return true;
}
}  // namespace

void initAppSettings(const QString& portableDir) {
  QCoreApplication::setOrganizationName(QString::fromLatin1(kSettingsOrganization));
  QCoreApplication::setApplicationName(QString::fromLatin1(kSettingsApplication));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, portableDir);
  // Runs after the identity is set, because the copy is validated by opening
  // the new file through QSettings, and before any caller reads a value.
  migrationCopyLegacySettings(portableDir);
}
static QMutex g_qtMsgMutex;
static QString g_qtMsgPath;
static void qtMessageLogHandler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
  const char* tag = "INFO";
  switch (type) {
    case QtDebugMsg: tag = "DEBUG"; break;
    case QtInfoMsg: tag = "INFO"; break;
    case QtWarningMsg: tag = "WARNING"; break;
    case QtCriticalMsg: tag = "CRITICAL"; break;
    case QtFatalMsg: tag = "FATAL"; break;
  }
  // No Qt calls here that could log (no recursion): raw file write only.
  const QByteArray line = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8()
      + " [" + tag + "] " + msg.toUtf8() + "\n";
  QMutexLocker g(&g_qtMsgMutex);
  QFile f(g_qtMsgPath);
  if (f.open(QIODevice::WriteOnly | QIODevice::Append)) { f.write(line); f.close(); }
  // Returning from a fatal handler lets Qt abort as usual: fail-fast kept,
  // now with the last words recorded above.
}
void installQtMessageLog(const QString& path) {
  const QString p = path.isEmpty()
      ? QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/msf_qt.log")
      : path;
  QMutexLocker g(&g_qtMsgMutex);
  g_qtMsgPath = p;
  qInstallMessageHandler(qtMessageLogHandler);
}
// 0.9.4.136: preview-size slider. Requirements from the field request:
//  - a visible marker at the 1.0x standard preview size (reference_ <= minimum
//    means "no marker"),
//  - the current ratio (0.5, 1.1, 1.5, ...) shown while the handle is dragged,
//  - the whole control grays out via Qt's disabled style in list view
//    (setViewMode disables it; this class only dims its own marker there).
ZoomSlider::ZoomSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {
    // Room above the groove for the ratio bubble; the row otherwise sizes the
    // slider from the default style height.
    setMinimumHeight(30);
    // 0.9.4.137: 0.05 ratio steps (6 px at the 128 px reference) for the
    // keyboard/wheel; drag and clicks are snapped in onValueChanged.
    setSingleStep(6);
    setPageStep(26);
    connect(this, &QSlider::valueChanged, this, &ZoomSlider::onValueChanged);
}

// Snap a raw slider value (pixels) to the nearest 0.05 x reference stop.
// 0.9.4.137: without this, 0.9 -> 1.0 was thirteen 1-px size changes; the
// user asked for coarser, predictable stops and a two-decimal readout.
int ZoomSlider::snapValueFor(int v, int reference) {
    if (reference <= 0) return v;
    constexpr double kStepRatio = 0.05;
    const double ratio = (double)v / (double)reference;
    const long long k = std::llround(ratio / kStepRatio);
    return (int)std::llround((double)k * kStepRatio * (double)reference);
}

void ZoomSlider::onValueChanged(int v) {
    const int snapped = snapValueFor(v, reference_);
    if (snapped != v && snapped >= minimum() && snapped <= maximum()) {
        QSignalBlocker blocker(this);
        setValue(snapped);
    }
    update();
    if (snapped != lastEmitted_ && snapped >= minimum() && snapped <= maximum()) {
        lastEmitted_ = snapped;
        // Consumers see only snapped sizes (the raw valueChanged stream is
        // intentionally not used by MainWindow any more).
        emit previewSizeChanged(snapped);
    }
}

int ZoomSlider::handleCenterX() const {
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove =
        style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const int hw = style()->pixelMetric(QStyle::PM_SliderLength, &opt, this);
    const int span = std::max(0, groove.width() - hw);
    return groove.left() + hw / 2 +
           QStyle::sliderPositionFromValue(minimum(), maximum(), value(), span, opt.upsideDown);
}

void ZoomSlider::paintEvent(QPaintEvent* ev) {
    QSlider::paintEvent(ev);
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove =
        style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const int hw = style()->pixelMetric(QStyle::PM_SliderLength, &opt, this);
    const int span = std::max(0, groove.width() - hw);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // 1.0 reference: light gray vertical line + small tick above the groove.
    if (reference_ > minimum() && reference_ < maximum()) {
        const int x = groove.left() + hw / 2 +
                      QStyle::sliderPositionFromValue(minimum(), maximum(), reference_, span,
                                                      opt.upsideDown);
        const int alpha = isEnabled() ? 150 : 70;
        p.setPen(QPen(QColor(120, 120, 120, alpha), 1));
        p.drawLine(x, groove.top(), x, groove.bottom());
        const int tickTop = std::max(0, groove.top() - 6);
        QPolygon tri;
        tri << QPoint(x - 3, tickTop) << QPoint(x + 3, tickTop) << QPoint(x, tickTop + 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(120, 120, 120, alpha));
        p.drawPolygon(tri);
    }
    // Ratio bubble above the handle while dragging (e.g. 0.5, 1.1, 1.5).
    if (dragging_ && isEnabled() && reference_ > 0) {
        // 0.9.4.137: two decimals (0.50 / 0.95 / 1.00); with 0.05 steps the
        // second decimal carries real information.
        const double ratio = (double)value() / (double)reference_;
        const QString text =
            QString::number(std::round(ratio * 100.0) / 100.0, 'f', 2);
        QFont f = font();
        if (f.pointSizeF() > 8.5) f.setPointSizeF(f.pointSizeF() - 1.0);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int bw = fm.horizontalAdvance(text) + 12;
        const int bh = fm.height() + 4;
        int x = handleCenterX() - bw / 2;
        x = std::clamp(x, 0, std::max(0, width() - bw));
        const QRect bubble(x, 0, bw, bh);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(45, 45, 45, 215));
        p.drawRoundedRect(bubble, 4, 4);
        p.setPen(Qt::white);
        p.drawText(bubble, Qt::AlignCenter, text);
    }
}

void ZoomSlider::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton) { dragging_ = true; update(); }
    QSlider::mousePressEvent(ev);
}

void ZoomSlider::mouseReleaseEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton) { dragging_ = false; update(); }
    QSlider::mouseReleaseEvent(ev);
}

MainWindow::MainWindow(QWidget* p, BackendClient* backend) : QMainWindow(p) {
  const QStringList ig0 = QSettings().value("ui/ignored").toStringList();
  ignored_ = QSet<QString>(ig0.begin(), ig0.end());
  buildUi();
  applyStaticTexts();
  restoreUiState();
  // P2: the backend lives behind BackendClient; the loopback implementation
  // runs the real ScanWorker/MediaMonitor in-process (P3 spawns it).
  // A passed client (production supervisor) is adopted; tests pass nothing
  // and get the loopback default, so existing GUI tests are unchanged.
  // Signal wiring is once-only: the backend object persists across scans,
  // so per-scan connects would duplicate every delivery.
  backend_ = backend ? backend : new LoopbackBackendClient(this);
  if (backend_ != backend) { /* owns the default */ } else backend_->setParent(this);
  connect(backend_, &BackendClient::progress, this, &MainWindow::scanProgress);
  connect(backend_, &BackendClient::progressCount, this, &MainWindow::onScanCounts);
  connect(backend_, &BackendClient::walkedCount, this, &MainWindow::onWalkedCount);
  connect(backend_, &BackendClient::fingerprintProgress, this, &MainWindow::onFingerprintProgress);
  connect(backend_, &BackendClient::targetCount, this, &MainWindow::onTargetCount);
  connect(backend_, &BackendClient::listingProgress, this, &MainWindow::onListingProgress);
  connect(backend_, &BackendClient::matchesBatch, this, &MainWindow::onMatchesBatch);
  connect(backend_, &BackendClient::quickLoaded, this, &MainWindow::onQuickLoaded);
  connect(backend_, &BackendClient::reverifyFinished, this, &MainWindow::onReverifyFinished);
  connect(backend_, &BackendClient::testModeDiff, this, &MainWindow::onTestModeDiff);
  connect(backend_, &BackendClient::testApplied, this, &MainWindow::onTestApplied);
  connect(backend_, &BackendClient::revalidated, this, &MainWindow::onRevalidated);
  connect(backend_, &BackendClient::revalidateProgress, this, &MainWindow::onRevalidateProgress);
  connect(backend_, &BackendClient::results, this, &MainWindow::onResults);
  connect(backend_, &BackendClient::telemetryReady, this, &MainWindow::onDetailedLog);
  connect(backend_, &BackendClient::finished, this, &MainWindow::scanFinished);
  connect(backend_, &BackendClient::failed, this, &MainWindow::scanFailed);
  connect(backend_, &BackendClient::monitorEvent, this, &MainWindow::monitorEvent);
  connect(backend_, &BackendClient::monitorSnapshot, this, &MainWindow::onMonitorSnapshot);
  connect(backend_, &BackendClient::statusSnapshot, this, &MainWindow::onStatusSnapshot);
  connect(backend_, &BackendClient::thumbReady, this, &MainWindow::onThumbReady);
  connect(backend_, &BackendClient::fileMetaReady, this, &MainWindow::onFileMetaReady);
  connect(backend_, &BackendClient::backendConnection, this, &MainWindow::onBackendConnection);
  connect(backend_, &BackendClient::backendLogLine, this,
          [this](const QString& line) { scanLog(line); });
  monitorTimer_ = new QTimer(this); monitorTimer_->setInterval(1000);
  connect(monitorTimer_, &QTimer::timeout, this, [this] { backend_->refreshMonitor(); });
  monitorTimer_->start();
  uiTimer_ = new QTimer(this); uiTimer_->setInterval(600);
  connect(uiTimer_, &QTimer::timeout, this, &MainWindow::onUiTick);
  // 0.9.4.88: sample the GUI process's own CPU every second so the summary can
  // show GUI + Backend + Backend children combined. Under the in-process
  // loopback the GUI IS the working process (backendCpu already covers it), so
  // guiCpu_ stays 0 to avoid double counting; the GUI also never samples there,
  // which keeps the loopback's own sampler baseline undisturbed.
  cpuTimer_ = new QTimer(this); cpuTimer_->setInterval(1000);
  connect(cpuTimer_, &QTimer::timeout, this, [this] {
    if (backend_ && backend_->separateProcess()) {
      double c = 0.0; unsigned long long r = 0;
      msf::sampleOwnProcess(c, r); guiCpu_ = c;
    } else {
      guiCpu_ = 0.0;
    }
  });
  cpuTimer_->start();
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
void MainWindow::showEvent(QShowEvent* ev) {
  QMainWindow::showEvent(ev);
  if (backend_) backend_->ensureRunning();
}
void MainWindow::closeEvent(QCloseEvent* ev) {
  // While a scan (and its report rebuild) is in flight, closing would drop
  // the run silently. Nudge the user to wait instead; explicit confirm exits.
  if (scanning_ && backend_) {
    const auto r = QMessageBox::question(this, trStr(lang(), "repWaitTitle"),
        trStr(lang(), "repWaitClose"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes) { ev->ignore(); return; }
  }
  ev->accept();
}
MainWindow::~MainWindow() {
  saveUiState();
  if (backend_) backend_->shutdown();
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
    // Saved states predate the middle-first policy and may squeeze the group
    // pane (e.g. a wide right pane from the old stretch factors). Never delete
    // the user's state: only enforce a middle minimum by borrowing from the
    // right pane first (down to its detail minimum), then the left.
    QList<int> sz = split_->sizes();
    if (sz.size() == 3) {
      const int total = sz[0] + sz[1] + sz[2];
      const int leftMin = 140, midMin = 320, rightMin = 360;
      if (sz[1] < midMin && total >= leftMin + midMin + rightMin) {
        int need = midMin - sz[1];
        const int fromRight = qMin(need, sz[2] - rightMin);
        sz[2] -= fromRight; need -= fromRight;
        const int fromLeft = qMin(need, sz[0] - leftMin);
        sz[0] -= fromLeft; need -= fromLeft;
        sz[1] = total - sz[0] - sz[2];
        split_->setSizes(sz);
      }
    }
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
    setWindowTitle(trStr(lang(), "app") + QStringLiteral(" ") + QString::fromLatin1(MSF_BUILD_VERSION));
  resize(1500, 880);
  auto* central = new QWidget(this); setCentralWidget(central);
  auto* outer = new QVBoxLayout(central); outer->setContentsMargins(6, 6, 6, 6); outer->setSpacing(6);
  buildToolbar();
  split_ = new QSplitter(Qt::Horizontal, central);
  split_->setOpaqueResize(true);
  auto* leftW = new QWidget(split_); auto* midW = new QWidget(split_); auto* rightW = new QWidget(split_);
  leftW->setObjectName("leftPane"); midW->setObjectName("middlePane"); rightW->setObjectName("rightPane"); // automation hooks
  split_->setObjectName("mainSplit"); // automation hook, see folder_
  buildLeft(leftW); buildMiddle(midW); buildRight(rightW);
  split_->addWidget(leftW); split_->addWidget(midW); split_->addWidget(rightW);
  // NOTE: setCollapsible must come after the widgets exist; calling it on an
  // empty splitter prints "QSplitter::setCollapsible: Index out of range".
  split_->setCollapsible(0, true); split_->setCollapsible(1, true); split_->setCollapsible(2, false);
  // Middle-first growth (0.9.4.63): extra window width goes to the similar-group
  // pane, not the detail pane. The old (0,0,1) factors ballooned the right pane
  // on every resize; with (0,1,0) only user drags change left/right widths.
  split_->setStretchFactor(0, 0); split_->setStretchFactor(1, 1); split_->setStretchFactor(2, 0);
  const auto saved = QSettings().value("ui/splitter").toByteArray();
  // Default (no saved state): right pane fits ~4 preview images (4x128 + gaps),
  // the rest belongs to the middle pane. Saved states are never deleted here.
  if (saved.isEmpty()) split_->setSizes({200, 720, 560});
  // Non-empty saved state is applied by restoreUiState() after all panes exist.
  outer->addWidget(split_, 1);
  statusBar_ = statusBar();
  statusMsg_ = new QLabel(this); statusMsg_->setObjectName("statusMsg"); statusCount_ = new QLabel(this); statusProg_ = new QProgressBar(this);
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
  // Automation hook (scan workflow regression test): stable lookup name.
  // No behavior change; the name is only used by findChild().
  folder_->setObjectName("folder");
  folder_->setPlaceholderText(QStringLiteral("D:\\MediaLibrary"));
  folder_->setMinimumWidth(200);
  QSettings st; folder_->setText(st.value("ui/lastFolder", "").toString());
  refresh_ = new QPushButton(QStringLiteral("📂 ") + trStr(lang(), "load"), toolBar_);
  refresh_->setObjectName("loadBtn"); // automation hook (backend e2e load test)
  refresh_->setToolTip(trStr(lang(), "load"));
  connect(refresh_, &QPushButton::clicked, this, &MainWindow::loadIndex);
  // 0.9.4.134: forced duplicate re-verification (quick-hash re-read of the
  // stored pair members; heals content changes that preserved size+mtime).
  reverifyBtn_ = new QPushButton(trStr(lang(), "reverify"), toolBar_);
  reverifyBtn_->setObjectName("reverifyBtn"); // automation hook
  reverifyBtn_->setToolTip(trStr(lang(), "reverifyTip"));
  connect(reverifyBtn_, &QPushButton::clicked, this, &MainWindow::reverifyIndex);
  scan_ = new QPushButton(toolBar_); scan_->setObjectName("scan");
  scan_->setDefault(true);
  pause_ = new QPushButton(toolBar_); pause_->setCheckable(true); cancel_ = new QPushButton(toolBar_);
  // Automation hooks, same purpose as "scan": these stable lookup names let
  // tests assert scan lifecycle control states (pause is scan-only: the GUI
  // detailed-log path has no pause of its own).
  pause_->setObjectName("pause"); cancel_->setObjectName("cancel");
  connect(scan_, &QPushButton::clicked, this, &MainWindow::startScan);
  connect(pause_, &QPushButton::clicked, this, &MainWindow::togglePauseScan);
  connect(cancel_, &QPushButton::clicked, this, &MainWindow::cancelScan);
  preset_ = new QComboBox(toolBar_);
  preset_->setObjectName("presetBox"); // automation hook
  // Text-only labels (no percentages): the CPU percent is shown in the
  // read-only CPU box next to it (visible only when ui/showCpuPercent is on).
  // Mapping stays index-based in resourceChanged(). 0.9.4.96: Gaming is now
  // labeled Minimum (same 25/25 values); Custom is gone (presets only).
  preset_->addItems({QStringLiteral("Maximum"), QStringLiteral("High"), QStringLiteral("Balanced"),
                     QStringLiteral("Minimum")});
  preset_->setCurrentIndex(2);
  // Narrow to the content instead of stretching with the widest label so the
  // resource+CPU area stays compact.
  preset_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  preset_->setMinimumContentsLength(9);
  preset_->setMaximumWidth(102);
  connect(preset_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resourceChanged);
  cpu_ = new QSpinBox(toolBar_);
  // The widget, stored policy, and engine input all use the same 10-90 user
  // CPU range. QSpinBox enforces the range, while the policy helper below also
  // normalizes values arriving through make_policy.
  cpu_->setRange(msf::kUserCpuPercentMin, msf::kUserCpuPercentMax);
  cpu_->setObjectName("cpuSpin"); // automation hook
  // Prefix/suffix stay inside the box for looks, but only the digits are
  // selectable/editable (see clampCpuDigitSelection + the SelectAll filter).
  cpu_->setPrefix(QStringLiteral("CPU "));
  cpu_->setSuffix(QStringLiteral("%"));
  // 0.9.4.96: display-only (no custom editing; presets only). No spin buttons
  // and a read-only field, so valueChanged can only come from programmatic
  // preset syncs (always blockSignals-guarded). Visibility is governed by
  // ui/showCpuPercent (default OFF) through applyCpuPercentVisibility().
  // "CPU 90%" (the widest value) must fit: text width + spin buttons + frame.
  {
    const int cpuTextW = cpu_->fontMetrics().horizontalAdvance(QStringLiteral("CPU 90%"));
    cpu_->setMinimumWidth(cpuTextW + 30);
    cpu_->setMaximumWidth(cpuTextW + 54);
  }
  cpu_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  QLineEdit* cpuEdit = cpu_->findChild<QLineEdit*>();
  cpuEdit->setReadOnly(true);
  connect(cpuEdit, &QLineEdit::selectionChanged, this, [this] { clampCpuDigitSelection(); });
  connect(cpuEdit, &QLineEdit::cursorPositionChanged, this, [this] { clampCpuDigitSelection(); });
  cpuEdit->installEventFilter(this);
  // Node A: no manual GPU utilization control. GPU is ON/OFF only
  // (Settings > General switch = Adaptive/AUTO when ON); the deprecated
  // internal gpuPercent cap stays untouched until the Node B scheduler
  // replaces it.
  cpu_->setValue(policy_.cpuPercent);
  // 0.9.4.96: GPU acceleration moved to Settings > General (ui/gpuEnabled).
  gpuOn_ = QSettings().value("ui/gpuEnabled", true).toBool();
  logTgl_ = new QCheckBox(toolBar_); logTgl_->setChecked(true);
  logTgl_->setObjectName("logTgl"); // automation hook, see folder_
  // Execution resource strategy: one dropdown choice of how this program uses
  // CPU/GPU resources (a dropdown can only hold one selection, so the
  // single-select rule is structural). Drives whether the GPU
  // checkbox is enabled: CPU-only forces it off; AUTO and GPU-max leave the
  // program's adaptive GPU path available. The fine-grained CPU budget stays
  // with preset_/cpu_ below.
  strategyBox_ = new QComboBox(toolBar_);
  strategyBox_->setObjectName("strategyBox");  // automation hook
  strategyBox_->addItem(trStr(lang(), "strategyModeAuto"));
  strategyBox_->addItem(trStr(lang(), "strategyModeCpu"));
  strategyBox_->addItem(trStr(lang(), "strategyModeGpu"));
  strategyBox_->setCurrentIndex(0);
  strategyBox_->setToolTip(trStr(lang(), "strategyModeTip"));
  strategyBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  fitStrategyBoxWidth();
  // On change, reflect it in the stored GPU switch and refresh
  // the policy so the scan honours the selection.
  connect(strategyBox_, qOverload<int>(&QComboBox::currentIndexChanged),
          this, [this](int){ applyExecutionMode(); });
  // The scan button is gated on a folder being present, so editing the folder has
  // to re-evaluate that gate; otherwise clearing it would leave the button armed.
  connect(folder_, &QLineEdit::textChanged, this, [this](const QString&) { updateExecutionUiState(); });
  // 0.9.4.99: photo/video scope is a single-select dropdown like the
  // resource preset menu (photos / videos / all). Exactly one scope is
  // always active, so the old both-off refusal is gone by construction.
  kindBox_ = new QComboBox(toolBar_);
  kindBox_->setObjectName("kindBox"); // automation hook
  kindBox_->addItem(trStr(lang(), "kindImages")); // index 0 == photos (mask 1)
  kindBox_->addItem(trStr(lang(), "kindVideos")); // index 1 == videos (mask 2)
  kindBox_->addItem(trStr(lang(), "kindAll"));    // index 2 == all (mask 3)
  {
    const int km = QSettings().value("ui/kindMask", 3).toInt();
    kindBox_->setCurrentIndex(km == 1 ? 0 : (km == 2 ? 1 : 2));
  }
  kindBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  connect(kindBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { updateKindSelection(); });
  monBtn_ = new QPushButton(toolBar_); monBtn_->setCheckable(true);
  connect(monBtn_, &QPushButton::clicked, this, &MainWindow::toggleMonitor);
  logBtn_ = new QPushButton(toolBar_); logBtn_->setEnabled(false);
  connect(logBtn_, &QPushButton::clicked, this, [this] { if (!lastTelemetryJson_.isEmpty()) showDetailedLogDialog(lastTelemetryJson_); });
  // Test Mode is a persistent per-run toggle, not a separate benchmark
  // workflow. When checked, the next scan uses the same settings but runs
  // full processing inside an isolated backend scratch index.
  testBtn_ = new QPushButton(toolBar_); testBtn_->setCheckable(true);
  testBtn_->setObjectName("testBtn"); // automation hook, see folder_
  testBtn_->setChecked(QSettings().value("ui/testMode", false).toBool());
  connect(testBtn_, &QPushButton::toggled, this, &MainWindow::toggleTestMode);
  paintTestButton();
  // Merged settings/help menu, docked at the far right (after the spacer):
  // monitor detail settings + help in one place.
  utilBtn_ = new QToolButton(toolBar_);
  utilBtn_->setText(QStringLiteral("☰"));
  QFont uf = utilBtn_->font(); uf.setPointSize(uf.pointSize() + 4); uf.setBold(true);
  utilBtn_->setFont(uf);
  utilBtn_->setMinimumSize(58, 32);
  utilBtn_->setToolTip(trStr(lang(), "settings") + "/" + trStr(lang(), "help"));
  // Same combo-style side arrow as the preset/view/kind controls.
  utilBtn_->setPopupMode(QToolButton::MenuButtonPopup);
  connect(utilBtn_, &QToolButton::clicked, this, [this] { utilBtn_->showMenu(); });
  auto* utilMenu_ = new QMenu(utilBtn_);
  monSettingsAct_ = utilMenu_->addAction(trStr(lang(), "settings"), this, &MainWindow::configureMonitor);
  monSettingsAct_->setObjectName("settingsAct"); // automation hook, see folder_
  helpAct_ = utilMenu_->addAction(trStr(lang(), "help"), this, &MainWindow::showHelp);
  utilBtn_->setMenu(utilMenu_);
  toolBar_->addWidget(folder_);
  toolBar_->addWidget(refresh_); toolBar_->addSeparator();
  toolBar_->addWidget(scan_); logTglAct_ = toolBar_->addWidget(logTgl_); toolBar_->addWidget(pause_); toolBar_->addWidget(cancel_);
  toolBar_->addSeparator();
  // Execution resource strategy + preset CPU budget as one compact area.
  // The CPU box is a read-only display shown only when ui/showCpuPercent is
  // on; the GPU switch lives in Settings > General.
  toolBar_->addWidget(strategyBox_);
  toolBar_->addWidget(preset_); cpuAct_ = toolBar_->addWidget(cpu_);
  toolBar_->addSeparator();
  toolBar_->addWidget(kindBox_);
  toolBar_->addSeparator(); toolBar_->addWidget(monBtn_); toolBar_->addWidget(logBtn_);
  // All toolbar separators share one clearly visible style: a 1px rule in a
  // mid grey, lighter than before but darker than the faint native etch, with
  // margins matching the height of the existing native separators.
  toolBar_->setStyleSheet(QStringLiteral("QToolBar::separator { background-color: #8c8c8c; width: 2px; margin-top: 6px; margin-bottom: 6px; }"));
  auto* spacer = new QWidget(toolBar_); spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolBar_->addWidget(spacer);
  testBtnAct_ = toolBar_->addWidget(testBtn_); // immediately left of Settings, at the far right
  toolBar_->addWidget(utilBtn_); // far-right menu
  applyDetailLogVisibility(); // ui/showDetailLog (default ON): visibility only, telemetry untouched
  applyCpuPercentVisibility(); // ui/showCpuPercent (default OFF): display only, policy untouched
  applyTestButtonVisibility(); // ui/showTestButton (default ON): visibility only, Test Mode state untouched
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
  sumTotal_ = new QLabel("-", w); sumDone_ = new QLabel("-", w); sumIndexed_ = new QLabel("-", w); sumGroups_ = new QLabel("-", w);
  sumDup_ = new QLabel("-", w); sumVerify_ = new QLabel("-", w); sumTime_ = new QLabel("-", w); sumGpu_ = new QLabel("-", w);
  sumCpu_ = new QLabel("-", w); sumRam_ = new QLabel("-", w);
  sumDup_->setObjectName("sumDup"); sumVerify_->setObjectName("sumVerify"); sumTime_->setObjectName("sumTime"); sumGpu_->setObjectName("sumGpu");
  sumCpu_->setObjectName("sumCpu"); sumRam_->setObjectName("sumRam");
  // value labels are the field widgets; refreshSummary() writes "name: value" into the name labels
  // and keeps raw values here for layout stability.
  sumValTotal_ = new QLabel("-", w); sumValDone_ = new QLabel("-", w); sumValIndexed_ = new QLabel("-", w); sumValGroups_ = new QLabel("-", w);
  sumValDup_ = new QLabel("-", w); sumValVerify_ = new QLabel("-", w); sumValTime_ = new QLabel("-", w); sumValGpu_ = new QLabel("-", w);
  sumValCpu_ = new QLabel("-", w); sumValRam_ = new QLabel("-", w);
  // 0.9.4.132: automation hooks for the summary values (the load regression
  // asserts the panel is populated, not "-").
  sumValTotal_->setObjectName("sumValTotal"); sumValDone_->setObjectName("sumValDone");
  sumValIndexed_->setObjectName("sumValIndexed"); sumValGroups_->setObjectName("sumValGroups");
  sumValTime_->setObjectName("sumValTime");
  sumValDup_->setObjectName("sumValDup"); sumValVerify_->setObjectName("sumValVerify");
  form->addRow(sumTotal_, sumValTotal_); form->addRow(sumDone_, sumValDone_); form->addRow(sumIndexed_, sumValIndexed_);
  form->addRow(sumGroups_, sumValGroups_); form->addRow(sumDup_, sumValDup_); form->addRow(sumVerify_, sumValVerify_);
  form->addRow(sumTime_, sumValTime_); form->addRow(sumGpu_, sumValGpu_);
  form->addRow(sumCpu_, sumValCpu_); form->addRow(sumRam_, sumValRam_);
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
  groupTitle_->setObjectName("groupTitle"); // automation hook, see folder_
  lay->addWidget(groupTitle_);
  auto* bar = new QHBoxLayout;
  // 0.9.4.139: group sort split into type + direction (the old single list had
  // no ascending/descending choice). Items are (re)filled in setLanguage in
  // the order None / Group size / Similarity / Name.
  sortBox_ = new QComboBox(w);
  sortBox_->setObjectName("sortBox"); // automation hook
  sortDirBox_ = new QComboBox(w);
  sortDirBox_->setObjectName("sortDirBox"); // automation hook
  // View-mode dropdown in the same QComboBox style as the preset combo: one
  // arrow only (the old QToolButton drew its own arrow next to the "?" text).
  viewBox_ = new QComboBox(w);
  viewBox_->setObjectName("viewBox"); // automation hook, see folder_
  viewBox_->setMinimumHeight(30);
  {
    const char* vkeys[7] = {"viewXL", "viewL", "viewM", "viewS", "viewList", "viewDetails", "viewTiles"};
    for (int i = 0; i < 7; ++i) viewBox_->addItem(trStr(lang(), vkeys[i]));
  }
  connect(viewBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) { groupViewChanged(i); });
  groupSearch_ = new QLineEdit(w); groupSearch_->setClearButtonEnabled(true);
  connect(groupSearch_, &QLineEdit::textChanged, this, &MainWindow::groupSearchChanged);
  connect(sortBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
    // Default direction per type: names ascending, group size/similarity
    // descending (bigger groups and closer matches first). The user can flip.
    if (sortDirBox_) {
      sortDirBox_->blockSignals(true);
      sortDirBox_->setCurrentIndex(idx == kSortName ? 1 : 0);
      sortDirBox_->blockSignals(false);
    }
    updateSortEnabled();
    if (!scanning_) { rebuildGroups(); refreshGroupList(); }
  });
  connect(sortDirBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    if (sortBox_->currentIndex() == kSortNone) return;
    if (!scanning_) { rebuildGroups(); refreshGroupList(); }
  });
  groupSearch_->setMaximumWidth(280); // balanced against the view button
  bar->addWidget(sortBox_); bar->addWidget(sortDirBox_); bar->addWidget(viewBox_); bar->addWidget(groupSearch_);
  lay->addLayout(bar);
  midTabs_ = new QTabWidget(w);
  lay->addWidget(midTabs_, 1);
  // ---- results tabs (image / video share the view-mode logic via active pair)
  auto* imgTab = new QWidget(midTabs_);
  auto* imgLay = new QVBoxLayout(imgTab); imgLay->setContentsMargins(0, 0, 0, 0);
  imgTree_ = new QTreeWidget(imgTab); imgTree_->setColumnCount(5); imgTree_->setRootIsDecorated(false);
  imgTree_->setObjectName("imgTree"); // automation hook, see folder_
  imgGrid_ = new QListWidget(imgTab);
  imgGrid_->setObjectName("imgGrid"); // automation hook, see folder_
  imgGrid_->setViewMode(QListView::IconMode); imgGrid_->setResizeMode(QListView::Adjust);
  imgGrid_->setMovement(QListView::Static); imgGrid_->setSpacing(8);
  // 0.9.4.94: SinglePass, never Batched. A Batched doItemsLayout() restarts
  // batching from row 0: the scrollbar maximum collapses to the first batch
  // and the restored/clamped deep value is left behind when later batches
  // regrow the maximum (proven in-test: 14461 stuck vs 17491 maximum). Every
  // explicit layout call here (append, rebuild, thumb flush) must observe the
  // final geometry, so the layout is synchronous. Item creation dominates
  // refill cost; the layout pass over uniform-size cells is cheap.
  imgGrid_->setUniformItemSizes(true); imgGrid_->setLayoutMode(QListView::SinglePass);
  imgLay->addWidget(imgTree_); imgLay->addWidget(imgGrid_);
  midTabs_->addTab(imgTab, QString());
  auto* vidTab = new QWidget(midTabs_);
  auto* vidLay = new QVBoxLayout(vidTab); vidLay->setContentsMargins(0, 0, 0, 0);
  vidTree_ = new QTreeWidget(vidTab); vidTree_->setColumnCount(5); vidTree_->setRootIsDecorated(false);
  vidGrid_ = new QListWidget(vidTab);
  vidGrid_->setViewMode(QListView::IconMode); vidGrid_->setResizeMode(QListView::Adjust);
  vidGrid_->setMovement(QListView::Static); vidGrid_->setSpacing(8);
  vidGrid_->setUniformItemSizes(true); vidGrid_->setLayoutMode(QListView::SinglePass);
  vidLay->addWidget(vidTree_); vidLay->addWidget(vidGrid_);
  midTabs_->addTab(vidTab, QString());
  applySelectionStyle(imgTree_); applySelectionStyle(imgGrid_);
  applySelectionStyle(vidTree_); applySelectionStyle(vidGrid_);
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
  // No fixed maximum: the splitter between the group pane and this pane stays
  // movable. Width stability comes from the labels below (word-wrap inside
  // allotted width, never stretching the pane), not from clamping.
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
  // 0.9.4.136: preview-size gauge with a 1.0x reference marker at the standard
  // preview size (128) and a ratio bubble during drag; setViewMode() disables
  // it (gray) in list view.
  zoom_ = new ZoomSlider(w); zoom_->setObjectName("zoomSlider"); // automation hook
  zoom_->setRange(48, 256); zoom_->setValue(128); zoom_->setFixedWidth(130);
  zoom_->setReferenceValue(128);
  zoom_->setToolTip(trStr(lang(), "zoomTip"));
  // 0.9.4.137: snapped sizes only (ZoomSlider emits the 0.05-step value).
  connect(zoom_, &ZoomSlider::previewSizeChanged, this, &MainWindow::zoomChanged);
  head->addStretch(1); head->addWidget(detailCount_);
  head->addWidget(viewGrid_); head->addWidget(viewList_); head->addWidget(zoom_);
  lay->addLayout(head);
  viewStack_ = new QStackedWidget(w);
  grid_ = new QListWidget(w);
  grid_->setObjectName("fileGrid"); // automation hook, see folder_
  grid_->setViewMode(QListView::IconMode); grid_->setResizeMode(QListView::Adjust);
  // Tight uniform cells: no side margins for text to spill into the neighbor
  // photo, and one layout pass for all rows (long/short names align).
  grid_->setMovement(QListView::Static); grid_->setSpacing(2); grid_->setUniformItemSizes(true);
  grid_->setIconSize(QSize(128, 128));
  applySelectionStyle(grid_);
  connect(grid_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem*, QListWidgetItem*) { fileGridSelected(); });
  connect(grid_, &QListWidget::itemDoubleClicked, this, &MainWindow::fileActivated);
  connect(grid_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
    if (!it) return;
    if (it->checkState() == Qt::Checked) marked_.insert(it->data(Qt::UserRole).toString());
    else marked_.remove(it->data(Qt::UserRole).toString());
    updateStatusCounts();
  });
  grid_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(grid_, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) {
    if (QListWidgetItem* it = grid_->itemAt(p)) {
      if (!it->isSelected()) { grid_->clearSelection(); it->setSelected(true); }
      grid_->setCurrentItem(it);
    }
    showFileMenu(grid_->mapToGlobal(p));
  });
  list_ = new QTreeWidget(w);
  list_->setObjectName("fileList"); // automation hook
  list_->setColumnCount(7); list_->setRootIsDecorated(false);
  applySelectionStyle(list_);
  connect(list_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem*, QTreeWidgetItem*) { fileListSelected(); });
  connect(list_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
    // 0.9.4.101: same detail sync as fileActivated (see above).
    if (it) { showFileDetails(it->data(1, Qt::UserRole).toString()); openSelected(); }
  });
  connect(list_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
    if (!it || col != 0) return;
    const QString p = it->data(1, Qt::UserRole).toString();
    if (it->checkState(0) == Qt::Checked) marked_.insert(p); else marked_.remove(p);
    updateStatusCounts();
  });
  list_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) {
    if (QTreeWidgetItem* it = list_->itemAt(p)) {
      if (!it->isSelected()) { list_->clearSelection(); it->setSelected(true); }
      list_->setCurrentItem(it);
    }
    showFileMenu(list_->mapToGlobal(p));
  });
  viewStack_->addWidget(grid_); viewStack_->addWidget(list_);
  lay->addWidget(viewStack_, 3);
  fileBar_ = new QToolBar(w); fileBar_->setMovable(false);
  fileBar_->setObjectName("fileBar"); // automation hook
  // 1px separator like the main toolbar's gray rules (which are 2px).
  fileBar_->setStyleSheet(QStringLiteral("QToolBar::separator { background-color: #8c8c8c; width: 1px; margin-top: 6px; margin-bottom: 6px; }"));
  lay->addWidget(fileBar_);
  detailTabs_ = new QTabWidget(w);
  auto* tabD = new QWidget(detailTabs_); auto* tabDlay = new QHBoxLayout(tabD);
  preview_ = new QLabel(tabD); preview_->setObjectName("preview"); // automation hook (0.9.4.142 probe)
  preview_->setFixedSize(220, 190); preview_->setAlignment(Qt::AlignCenter);
  preview_->setStyleSheet("border:1px solid #ccc;border-radius:6px;background:#f5f5f5;");
  detailForm_ = new QFormLayout;
  auto* formW = new QWidget(tabD); formW->setLayout(detailForm_);
  tabDlay->addWidget(preview_); tabDlay->addWidget(formW, 1);
  detailTabs_->addTab(tabD, QString());
  exifLabel_ = new QLabel(detailTabs_); exifLabel_->setWordWrap(true); exifLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  detailTabs_->addTab(exifLabel_, QString());
  auto* tabS = new QWidget(detailTabs_); auto* tabSlay = new QVBoxLayout(tabS);
  simLabel_ = new QLabel(tabS);
  // 0.9.4.121: this label carries the full best-match path. Without wrapping,
  // its single-line minimum forced the tab widget (hence the whole right
  // pane, hence the locked splitter) to the path width on every file
  // selection - the "detail pane widens by itself" report. Wrap inside the
  // allotted width like the EXIF/hash tabs; never stretch the pane.
  simLabel_->setWordWrap(true); simLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  // Ignored horizontal: a path is a single wrappable "word" (no spaces), so
  // wrapping alone cannot shrink the hint width - a 180-char segment would
  // still force the tab (hence the pane, hence the splitter) to its full
  // pixel width. Ignored keeps the layout stable; zero-width spaces (added
  // where the text is set) give the eye the matching visual folds.
  simLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  simBar_ = new QProgressBar(tabS); simBar_->setRange(0, 100);
  tabSlay->addWidget(simLabel_); tabSlay->addWidget(simBar_); tabSlay->addStretch(1);
  detailTabs_->addTab(tabS, QString());
  hashLabel_ = new QLabel(detailTabs_); hashLabel_->setWordWrap(true); hashLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  detailTabs_->addTab(hashLabel_, QString());
  connect(detailTabs_, &QTabWidget::currentChanged, this, &MainWindow::detailTabChanged);
  lay->addWidget(detailTabs_, 2);
}

void MainWindow::applyStaticTexts() {
  const UiLang l = lang();
  setWindowTitle(trStr(l, "app") + QStringLiteral(" ") + QString::fromLatin1(MSF_BUILD_VERSION));
  scan_->setText(QStringLiteral("▶ ") + trStr(l, "start"));
  refresh_->setText(QStringLiteral("📂 ") + trStr(l, "load"));
  pause_->setText(scanPaused_ ? trStr(l, "resume") : QStringLiteral("❚❚ ") + trStr(l, "pause"));
  pause_->setChecked(scanPaused_);
  cancel_->setText(QStringLiteral("■ ") + trStr(l, "stop"));
  logTgl_->setText(trStr(l, "detailLogToggle"));
  logTgl_->setToolTip(trStr(l, "detailLogTip"));
  // Detailed-log toggle plus the execution-strategy dropdown. logTgl_
  // enables diagnostic telemetry on the real search; the dropdown below is
  // the single-select execution resource strategy, so its items describe
  // a resource choice, not a benchmark-mode execution.
  {
    const int cur = strategyBox_->currentIndex();
    strategyBox_->blockSignals(true);
    strategyBox_->setItemText(0, trStr(l, "strategyModeAuto"));
    strategyBox_->setItemText(1, trStr(l, "strategyModeCpu"));
    strategyBox_->setItemText(2, trStr(l, "strategyModeGpu"));
    strategyBox_->setCurrentIndex(cur);
    strategyBox_->blockSignals(false);
    strategyBox_->setToolTip(trStr(l, "strategyModeTip"));
    fitStrategyBoxWidth();
  }
  monBtn_->setText(QStringLiteral("👁 ") + trStr(l, "monitor"));
  monBtn_->setChecked(monitorEnabled_);
  if (testBtn_) {
    testBtn_->setText(trStr(l, "testMode"));
    testBtn_->setToolTip(trStr(l, "testModeTip"));
    paintTestButton();
  }
  if (monSettingsAct_) monSettingsAct_->setText(trStr(l, "settings"));
  if (helpAct_) helpAct_->setText(trStr(l, "help"));
  if (utilBtn_) utilBtn_->setToolTip(trStr(l, "settings") + "/" + trStr(l, "help"));
  logBtn_->setText(trStr(l, "searchLog"));
  auto* leftTitle = findChild<QLabel*>("leftTitle"); if (leftTitle) leftTitle->setText(trStr(l, "explorer"));
  // The explorer tree is built once at startup, so retext its language-bound
  // headers here: three roots plus the six fixed Favorites children (keys in
  // build order; MRU paths and drive entries are language-neutral).
  if (folders_ && folders_->topLevelItemCount() >= 3) {
    static const char* favKeys[6] = {"desktop", "downloads", "documents", "pictures", "videos", "music"};
    QTreeWidgetItem* fav = folders_->topLevelItem(0);
    fav->setText(0, trStr(l, "favorites"));
    for (int i = 0; i < 6 && i < fav->childCount(); ++i)
      fav->child(i)->setText(0, trStr(l, favKeys[i]));
    folders_->topLevelItem(1)->setText(0, trStr(l, "thispc"));
    folders_->topLevelItem(2)->setText(0, trStr(l, "network"));
  }
  auto* sumTitle = findChild<QLabel*>("sumTitle"); if (sumTitle) sumTitle->setText(trStr(l, "summary"));
  sumTotal_->setText(trStr(l, "total")); sumDone_->setText(trStr(l, "readDone")); sumIndexed_->setText(trStr(l, "indexDone")); sumGroups_->setText(trStr(l, "groups"));
  sumDup_->setText(trStr(l, "dups")); sumVerify_->setText(trStr(l, "sumVerify")); sumTime_->setText(trStr(l, "elapsed")); sumGpu_->setText(trStr(l, "gpu"));
  sumCpu_->setText(trStr(l, "cpu")); sumRam_->setText(trStr(l, "ram"));
  refreshSummary();
  groupTitle_->setText(trStr(l, "groups") + QString(" (%1)").arg(groups_.size()));
  sortBox_->blockSignals(true);
  const int ssort = sortBox_->currentIndex();
  // 0.9.4.139: menu order None / Group size / Similarity / Name (the user
  // asked for the new group-size order right below None).
  sortBox_->clear();
  sortBox_->addItem(trStr(l, "sortNone"));
  sortBox_->addItem(trStr(l, "sortSize"));
  sortBox_->addItem(trStr(l, "sortSim"));
  sortBox_->addItem(trStr(l, "sortName"));
  sortBox_->setCurrentIndex(ssort < 0 ? 0 : ssort);
  sortBox_->blockSignals(false);
  if (sortDirBox_) {
    sortDirBox_->blockSignals(true);
    const int sdir = sortDirBox_->currentIndex();
    sortDirBox_->clear();
    sortDirBox_->addItem(trStr(l, "sortDesc"));
    sortDirBox_->addItem(trStr(l, "sortAsc"));
    sortDirBox_->setCurrentIndex(sdir < 0 ? 0 : sdir);
    sortDirBox_->blockSignals(false);
    updateSortEnabled();
  }
  groupSearch_->setPlaceholderText(trStr(l, "searchGroups"));
  {
    const char* vkeys[7] = {"viewXL", "viewL", "viewM", "viewS", "viewList", "viewDetails", "viewTiles"};
    const int vcur = viewBox_ ? viewBox_->currentIndex() : 0;
    viewBox_->blockSignals(true);
    viewBox_->clear();
    for (int i = 0; i < 7; ++i) viewBox_->addItem(trStr(l, vkeys[i]));
    viewBox_->setCurrentIndex(vcur < 0 ? 0 : vcur);
    viewBox_->blockSignals(false);
  }
  if (kindBox_) {
    const int kcur = kindBox_->currentIndex();
    kindBox_->blockSignals(true);
    kindBox_->setItemText(0, trStr(l, "kindImages"));
    kindBox_->setItemText(1, trStr(l, "kindVideos"));
    kindBox_->setItemText(2, trStr(l, "kindAll"));
    kindBox_->setCurrentIndex(kcur < 0 ? 2 : kcur);
    kindBox_->blockSignals(false);
  }
  updateKindSelection();
  midTabs_->setTabText(0, trStr(l, "tabImages"));
  midTabs_->setTabText(1, trStr(l, "tabVideos"));
  updateIgnoreTab();
  imgTree_->setHeaderLabels({trStr(l, "group"), trStr(l, "files"), trStr(l, "pairs"), trStr(l, "similarity"), trStr(l, "fileSize")});
  vidTree_->setHeaderLabels({trStr(l, "group"), trStr(l, "files"), trStr(l, "pairs"), trStr(l, "similarity"), trStr(l, "fileSize")});
  unignoreBtn_->setText(trStr(l, "unignore"));
  clearIgnoreBtn_->setText(trStr(l, "clearIgnored"));
  viewGrid_->setToolTip(trStr(l, "viewGrid")); viewList_->setToolTip(trStr(l, "viewList"));
  if (zoom_) zoom_->setToolTip(trStr(l, "zoomTip"));
  list_->setHeaderLabels({"", trStr(l, "colFile"), trStr(l, "similarity"), trStr(l, "resolution"),
                          trStr(l, "format"), trStr(l, "fileSize"), trStr(l, "colDate")});
  detailTabs_->setTabText(0, trStr(l, "tabDetail")); detailTabs_->setTabText(1, trStr(l, "tabExif"));
  detailTabs_->setTabText(2, trStr(l, "tabSim")); detailTabs_->setTabText(3, trStr(l, "tabHash"));
  fileBar_->clear();
  fileBar_->addAction(trStr(l, "open"), this, &MainWindow::openSelected);
  fileBar_->addAction(trStr(l, "reveal"), this, &MainWindow::revealSelected);
  // 0.9.4.103: separator between reveal and copy (open/reveal act on one
  // file; copy/cut/paste/move/recycle act on checked files first).
  fileBar_->addSeparator();
  fileBar_->addAction(trStr(l, "copy"), this, &MainWindow::copySelected);
  fileBar_->addAction(trStr(l, "cut"), this, &MainWindow::cutSelected);
  fileBar_->addAction(trStr(l, "paste"), this, &MainWindow::pasteFiles);
  fileBar_->addAction(trStr(l, "move"), this, &MainWindow::moveSelected);
  fileBar_->addAction(trStr(l, "del"), this, &MainWindow::deleteSelected);
  refreshGroupList(); refreshFileViews(); refreshDetail(); updateStatusCounts();
}

// ---------------------------------------------------------------------------
// GUI execution resource strategy
// ---------------------------------------------------------------------------
//
// The three checkboxes are the user's real-search resource choice
// (single-select by construction: a dropdown holds exactly one choice).
// This is intentionally separate from the CLI benchmark comparison modes:
// the same words (AUTO / CPU-only / GPU-max) mean different things in the
// two contexts, so no GpuBackendKind vector is built here.
ExecutionResourceStrategy MainWindow::executionStrategy() const {
  if (strategyBox_) {
    if (strategyBox_->currentIndex() == 1) return ExecutionResourceStrategy::CpuOnly;
    if (strategyBox_->currentIndex() == 2) return ExecutionResourceStrategy::GpuMax;
  }
  return ExecutionResourceStrategy::Auto;
}

void MainWindow::fitStrategyBoxWidth() {
  if (!strategyBox_) return;
  // Widest item text + drop arrow + frame, nothing more: the box hugs the
  // text instead of reserving layout slack. The popup list still sizes itself
  // to the full item text independently.
  const QFontMetrics fm(strategyBox_->font());
  int textW = 0;
  for (int i = 0; i < strategyBox_->count(); ++i)
    textW = std::max(textW, fm.horizontalAdvance(strategyBox_->itemText(i)));
  int arrow = style()->pixelMetric(QStyle::PM_MenuButtonIndicator, nullptr, strategyBox_);
  if (arrow <= 0) arrow = 24;
  int frame = style()->pixelMetric(QStyle::PM_ComboBoxFrameWidth, nullptr, strategyBox_);
  if (frame <= 0) frame = 3;
  // +30 covers item-delegate side padding and focus frame that fontMetrics
  // alone does not include, minus half a Korean glyph: full-glyph cut
  // clipped half of the last char on "GPU 최대 활용".
  const int koGlyph = fm.horizontalAdvance(QStringLiteral("한"));
  strategyBox_->setFixedWidth(textW + arrow + 2 * frame + 30 - koGlyph / 2);
}

// Enable gate for the scan/execution controls: a scan in progress disables
// starting another run. Pause remains a scan-only control.
// There is no process-wide gate here.
void MainWindow::updateExecutionUiState() {
  if (!strategyBox_) return;

  scan_->setEnabled(!scanning_);
  // Test Mode selects the index destination for the next scan start. Lock it
  // while a scan owns the backend so the toggle cannot be misread as
  // affecting the run already in progress.
  if (testBtn_) testBtn_->setEnabled(!scanning_);
  // Pause is a scan-only control. Keeping the rule
  // in one place also makes the idle state consistent, because setRunning() only
  // runs for scans and would otherwise leave the button enabled with no scan.
  pause_->setEnabled(scanning_);
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
  if (v) {
    scanPaused_ = false;
    maxPctShown_ = 0;
    lastPct_ = 0;
    lastPath_.clear();
    lastDoneN_ = 0;
    lastTotalN_ = 0;
    lastReadN_ = 0;
    lastListN_ = 0;
    targetTotal_ = 0;
    targetKnown_ = false;
    // 0.9.4.94: a new scan invalidates the previous completion rebuild mark.
    resultsApplied_ = false;
    // 0.9.4.95: pair-verify counts belong to the finished scan, not the new one.
    lastVerifyTotal_ = 0; lastVerifyDone_ = 0;
    if (sumValVerify_) sumValVerify_->setText(QStringLiteral("-"));
    // 0.9.4.139: a scan still locks the sort to None so streamed matches only
    // append (a sorted list would re-place every row); the user's choice is
    // restored at scan end. Load no longer locks the sort (see loadIndex).
    preScanSort_ = sortBox_->currentIndex();
    if (sortBox_->currentIndex() != kSortNone) {
      sortBox_->blockSignals(true);
      sortBox_->setCurrentIndex(kSortNone);
      sortBox_->blockSignals(false);
    }
  } else {
    scanPaused_ = false;
    // Scan over (indexing + post-processing done): let the user sort by
    // similarity or name again by restoring their pre-scan choice.
    if (preScanSort_ >= 0 && sortBox_->currentIndex() != preScanSort_) {
      sortBox_->blockSignals(true);
      sortBox_->setCurrentIndex(preScanSort_);
      sortBox_->blockSignals(false);
    }
  }
  scanning_ = v;
  // P3 backend guard (§21): the Start button stays disabled while the backend
  // is unavailable, so a dead backend can never be mistaken for idle.
  scan_->setEnabled(!v && backendAvailable_); refresh_->setEnabled(!v); reverifyBtn_->setEnabled(!v);
  pause_->setEnabled(v); pause_->setChecked(false); cancel_->setEnabled(v);
  pause_->setText(QStringLiteral("❚❚ ") + trStr(lang(), "pause"));
  if (!v) scan_->setFocus(); // return the highlight to Start, as at launch
  statusProg_->setRange(0, 100); statusProg_->setValue(0);
  // Keep the UI/thumbnail scheduler alive after scan completion. The timer
  // stops being useful only when the process exits; refreshStreaming() itself
  // has the pending-thumbnail condition and remains budgeted per tick.
  uiTimer_->start();
  updateGpuLabel();
  sumValGpu_->setText(gpuStateText());
  // A scan starting or ending changes the execution enable state, so the gate
  // is reapplied here rather than being duplicated at each call site.
  updateExecutionUiState();
  updateSortEnabled();
}
void MainWindow::startScan() {
  // User-initiated start: reset the crash auto-resume budget.
  autoResumeTries_ = 0;
  scanCrashed_ = false;
  backendRestarting_ = false;
  beginScan(false);
}
// 0.9.4.143: user-configurable recent-folder count shown in Favorites.
static int recentFolderLimit() {
  int n = QSettings().value("ui/recentFolderLimit", 3).toInt();
  return std::clamp(n, 1, 20);
}
void MainWindow::beginScan(bool resumeLastConfig) {
  if (scanning_) return;
  if (!backendAvailable_) {
    statusMsg_->setText(trStr(lang(), "backendDown"));
    return;
  }
  if (resumeLastConfig && haveLastScanCfg_)
    folder_->setText(lastScanCfg_.root);
  if (folder_->text().isEmpty()) { chooseFolder(); if (folder_->text().isEmpty()) return; }
  // P3: thumbnail persistence lives in the Backend (G1.3). The GUI keeps
  // its memory presentation cache only; nothing opens the index DB here.
  // Remember the most-recently scanned folders (max 2) for the favorites list.
  // Compare normalized so separator/trailing-slash variants of one folder
  // cannot duplicate the entry.
  {
    const QString cur = QDir::cleanPath(folder_->text());
    QStringList recent = QSettings().value("ui/recentFolders").toStringList();
    for (int i = recent.size() - 1; i >= 0; --i)
      if (QDir::cleanPath(recent[i]).compare(cur, Qt::CaseInsensitive) == 0) recent.removeAt(i);
    recent.prepend(cur);
    while (recent.size() > recentFolderLimit()) recent.removeLast();
    if (recent != QSettings().value("ui/recentFolders").toStringList()) {
      QSettings().setValue("ui/recentFolders", recent);
      refreshFolders();
    }
  }
  // P2: previous worker teardown lives inside BackendClient::startScan.
  if (cancelWait_) { cancelWait_->close(); cancelWait_->deleteLater(); cancelWait_ = nullptr; }
  // 0.9.4.141: keep the loaded list, selection and preview untouched while the
  // update runs. 0.9.4.140 already kept the presentation thumbnail cache, but
  // this block still reset currentGroup_/currentFile_ and refreshed both panes,
  // so the right-side preview showed "-" for the whole scan.
  // The match accumulators still start fresh (the quick-load stream re-emits
  // every stored pair immediately, and changed pairs must not keep an older,
  // higher percentage), while the file-size/path maps stay until onResults
  // replaces them so the visible panes keep their data.
  thumbStatMem_ = thumbStatBackend_ = thumbStatPlace_ = 0;
  bestPct_.clear(); pathKind_.clear(); contentFlag_.clear();
  scanLog(QString("thumbCache kept=%1").arg(thumbCache_.size()));
  hasReport_ = false;
  // 0.9.4.142: with a result set already on screen, freeze the view (list,
  // selection, previews) for the whole scan and apply the answer in one pass.
  // Otherwise the quick-load/streamed batches rebuilt the panes mid-scan and
  // their thumbnail requests queued behind the busy backend -> placeholders.
  holdViewDuringScan_ = !groups_.isEmpty();
  startedWithView_ = holdViewDuringScan_;
  scanLog(QString("viewHold=%1 groups=%2").arg(holdViewDuringScan_ ? 1 : 0).arg(groups_.size()));
  QString appDir = QApplication::applicationDirPath();
  // P2: the scan lifecycle lives behind BackendClient. The loopback
  // implementation tears down any previous worker internally, so the
  // thread quit/wait/delete sequence formerly here is gone.
  // Signal wiring lives in the constructor (persistent backend object:
  // reconnecting per scan would duplicate every delivery).
  BackendScanConfig cfg;
  if (resumeLastConfig && haveLastScanCfg_) {
    cfg = lastScanCfg_;
  } else {
    cfg.root = folder_->text();
    cfg.appDir = appDir;
    cfg.distance = 8;
    cfg.cpu = cpu_->value();
    cfg.gpuPercent = policy_.gpuPercent;
    cfg.gpuEnabled = gpuOn_;
    cfg.scanImages = !kindBox_ || kindBox_->currentIndex() != 1;
    cfg.scanVideos = !kindBox_ || kindBox_->currentIndex() != 0;
    cfg.ignored = ignored_;
    cfg.detailedLog = logTgl_->isChecked();
    cfg.testMode = testBtn_ && testBtn_->isChecked();
    cfg.analyzeMode = msf::analyzeModeFromInt(QSettings().value("ui/analyzeMode", 0).toInt());
    cfg.fastRescan = QSettings().value("ui/fastRescan", false).toBool();
    cfg.exec = currentExecPolicy();
    lastScanCfg_ = cfg;
    haveLastScanCfg_ = true;
  }
  scanStartMs_ = QDateTime::currentMSecsSinceEpoch();
  pauseStartMs_ = 0; pausedAccumMs_ = 0;
  scanLog(QString("start folder=%1").arg(folder_->text()));
  setRunning(true);
  statusMsg_->setText(trStr(lang(), "scanning"));
  statusProg_->setValue(0);
  backend_->startScan(cfg);
}
void MainWindow::togglePauseScan() {
  if (!scanning_ || !backend_) return; // pause acts only while its own scan runs
  scanPaused_ = !scanPaused_;
  if (scanPaused_) pauseStartMs_ = QDateTime::currentMSecsSinceEpoch();
  else if (pauseStartMs_ > 0) { pausedAccumMs_ += QDateTime::currentMSecsSinceEpoch() - pauseStartMs_; pauseStartMs_ = 0; }
  // Direct call, NOT QueuedConnection: pause()/resume() only store atomics,
  // and a queued slot can never fire while run() occupies the worker thread's
  // event loop — which is exactly why pause appeared dead mid-scan.
  // P2: the loopback drains streamed matches synchronously inside pause(),
  // so the paused view is complete within the click (no separate drain).
  if (scanPaused_) backend_->pause(); else backend_->resume();
  pause_->setChecked(scanPaused_);
  pause_->setText(scanPaused_ ? trStr(lang(), "resume") : QStringLiteral("❚❚ ") + trStr(lang(), "pause"));
  statusMsg_->setText(trStr(lang(), scanPaused_ ? "paused" : "scanning"));
  // Freezing the frame immediately: the loopback already delivered
  // everything streamed so far synchronously inside pause(), so no separate
  // drain is needed (the worker emits nothing paused).
  refreshStreaming(true);
}
void MainWindow::cancelScan() {
  if (!scanning_ || !backend_) return;
  // Direct call (see togglePauseScan): a queued cancel slot would only run
  // after run() returns, i.e. never in time to stop the scan.
  backend_->cancel();
  // Immediate feedback: the worker still needs a moment to wind down
  // in-flight analyses, so the wait notice appears on the click itself —
  // not seconds later when scanFinished arrives. scanFinished/onResults
  // take over (and close) this popup.
  if (!cancelWait_) cancelWait_ = showReportWaitPopup(this, lang(), "stopWait");
  statusMsg_->setText(trStr(lang(), "scanCancel"));
}
void MainWindow::onScanCounts(qulonglong done, qulonglong total) {
  lastDoneN_ = done;
  if (!targetKnown_ || total > lastTotalN_) lastTotalN_ = total;
}
void MainWindow::onTargetCount(qulonglong n) {
  targetTotal_ = (qulonglong)n;
  targetKnown_ = true;
  updateStatusCounts();
}
void MainWindow::onWalkedCount(qulonglong n) {
  if (!targetKnown_ || n > lastTotalN_) lastTotalN_ = n;
  updateStatusCounts();
}
void MainWindow::scanProgress(int p, QString path) {
  lastPct_ = p; lastPath_ = path;
  statusProg_->setRange(0, 100);
  // Headline ratio uses the fixed pre-walk denominator when known: scanned /
  // target. The engine's streaming total grows mid-walk, which used to pin the
  // display at a spurious 100%. Falls back to the engine percent otherwise.
  int show = p;
  if (targetKnown_) {
    show = targetTotal_ == 0 ? 100
           : lastTotalN_ >= targetTotal_ ? 100
           : int((double)lastTotalN_ * 100.0 / (double)targetTotal_);
  }
  // Streaming totals grow as the walk continues, so raw percent can dip.
  // The bar never moves backwards; exact counts stay in the message.
  // The message headline mirrors the fixed denominator when known.
  if (show > maxPctShown_) maxPctShown_ = show;
  statusProg_->setValue(maxPctShown_);
  const qint64 el = elapsedActiveMs();
  const qulonglong msgTotal = targetKnown_ ? targetTotal_ : lastTotalN_;
  const qulonglong msgDone = targetKnown_ ? lastTotalN_ : lastDoneN_;
  statusMsg_->setText(scanStatusText(msgDone, msgTotal, maxPctShown_, path, el));
  updateStatusCounts();
}
void MainWindow::onListingProgress(std::size_t n) {
  lastListN_ = (qulonglong)n; // heartbeat-visible: walk/count phases emit no scanProgress
  statusProg_->setRange(0, 0); // indeterminate: walking the directory tree
  statusMsg_->setText(QString("%1 %2").arg(trStr(lang(), "listing")).arg(n));
}
void MainWindow::onFingerprintProgress(qulonglong n, qulonglong bytes, QString path) {
  // Fingerprint phase has no known total, so the bar stays indeterminate like
  // the listing phase. Shows hashed files and bytes so a minutes-long hash
  // reads as progress, not a freeze. Does not touch the walk/analysis percent.
  statusProg_->setRange(0, 0);
  const double gb = (double)bytes / (1024.0 * 1024.0 * 1024.0);
  QString shortPath = path;
  if (shortPath.size() > 72) shortPath = QStringLiteral("...") + shortPath.right(69);
  statusMsg_->setText(QString("%1: %2 files, %3 GB — %4").arg(trStr(lang(), "fpProgress")).arg(n).arg(gb, 0, 'f', 1).arg(shortPath));
  // The left panel's read-complete row must move during fingerprint too:
  // nothing else updates it before the walk starts. Monotonic across phases
  // so the walk handoff never visibly resets to 0.
  if ((qulonglong)n > lastReadN_) lastReadN_ = (qulonglong)n;
  sumValDone_->setText(QString::number(lastReadN_));
}
void MainWindow::onQuickLoaded(int n) {
  statusMsg_->setText(trStr(lang(), "quickLoaded").arg(n));
}
void MainWindow::onRevalidateProgress(qulonglong done, qulonglong total) {
  // 0.9.4.85: the engine-version gate re-verifies every stored pair (decoding
  // images for grey-zone ones) and can run for minutes. Show its progress so a
  // scan is not mistaken for a freeze.
  statusProg_->setRange(0, 100);
  statusProg_->setValue(total ? int(done * 100 / total) : 0);
  statusMsg_->setText(trStr(lang(), "revalidating").arg(done).arg(total));
}
void MainWindow::onRevalidated(int kept, int dropped) {
  statusMsg_->setText(trStr(lang(), "revalidated").arg(kept).arg(dropped));
}
void MainWindow::onDetailedLog(QString json) {
  lastTelemetryJson_ = json;
  // 0.9.4.95: summary-panel pair-verify row (total/confirmed). Parsed here so
  // the panel never depends on the dialog being opened or saved.
  {
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &perr);
    if (perr.error == QJsonParseError::NoError && doc.isObject()) {
      const QJsonObject mat = doc.object()["matches"].toObject();
      lastVerifyTotal_ = (qulonglong)mat["candidates"].toDouble();
      lastVerifyDone_ = (qulonglong)mat["pairs"].toDouble();
    }
  }
  logBtn_->setEnabled(true);
  logBtn_->setStyleSheet(QStringLiteral("background:#ebf4ff; border:1px solid #2b6cb0; color:#2b6cb0; font-weight:bold;"));
  showDetailedLogDialog(json);
}
void MainWindow::showDetailedLogDialog(const QString& json) {
  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &perr);
  if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
    // 0.9.4.94: never fail silently here. Without this message a damaged
    // telemetry record looks exactly like "log generation never completes":
    // no dialog, no error, and the log button re-opens the same dead record.
    statusMsg_->setText(trStr(lang(), "detailLogParseFail"));
    scanLog(QString("detailLog parse failed at offset %1: %2").arg(perr.offset).arg(perr.errorString()));
    return;
  }
  const QJsonObject root = doc.object();
  const QJsonObject meta = root["meta"].toObject(), cfg = root["config"].toObject();
  const QJsonObject sum = root["summary"].toObject(), imgs = root["images"].toObject();
  const QJsonObject vids = root["videos"].toObject(), res = root["resources"].toObject();
  const QJsonObject mat = root["matches"].toObject();
  const auto f1 = [](double v) { return QString::number(v, 'f', 1); };
  const auto f2 = [](double v) { return QString::number(v, 'f', 2); };
  // Duration as "12m 34.5s" for the user-facing summary. Wall time only;
  // stage splits follow below.
  const auto fmtDur = [&](double ms) {
    const long long totalS = (long long)(ms / 1000.0);
    const long long m = totalS / 60, s = totalS % 60;
    const double frac = (ms / 1000.0) - (double)(m * 60 + s);
    if (m > 0) return QString("%1m %2s").arg(m).arg(QString::number((double)s + frac, 'f', 1));
    return QString("%1s").arg(QString::number(ms / 1000.0, 'f', 1));
  };
  QStringList lines;
  lines << QString("%1: %2").arg(trStr(lang(), "detailLogState"),
      meta["completed"].toBool() ? trStr(lang(), "detailLogDone") : trStr(lang(), "detailLogStopped"));
  if (!cfg["detail"].toBool(true)) lines << trStr(lang(), "detailLogOff");
  // User-facing summary first: per-kind scanned/analyzed/throughput plus the
  // duplicate groups/files/pairs split. Groups and files are different things
  // (clusters vs members) and are shown separately by design.
  {
    const qulonglong iScan = (qulonglong)imgs["scanned"].toDouble();
    const qulonglong iAn = (qulonglong)imgs["count"].toDouble();
    const qulonglong vScan = (qulonglong)vids["scanned"].toDouble();
    const qulonglong vAn = (qulonglong)vids["count"].toDouble();
    const double iMs = sum["imageStageMs"].toDouble();
    const double vMs = sum["videoStageMs"].toDouble();
    const double iRate = (iMs > 0 && iAn > 0) ? 1000.0 * (double)iAn / iMs : 0.0;
    const double vRate = (vMs > 0 && vAn > 0) ? 1000.0 * (double)vAn / vMs : 0.0;
    const qulonglong iGr = (qulonglong)mat["imageGroups"].toDouble();
    const qulonglong iDf = (qulonglong)mat["imageDuplicateFiles"].toDouble();
    const qulonglong iPr = (qulonglong)mat["imagePairs"].toDouble();
    const qulonglong vGr = (qulonglong)mat["videoGroups"].toDouble();
    const qulonglong vDf = (qulonglong)mat["videoDuplicateFiles"].toDouble();
    const qulonglong vPr = (qulonglong)mat["videoPairs"].toDouble();
    lines << QString("== %1 ==").arg(trStr(lang(), "detailLogSummary"));
    // 0.9.4.108: the summary states which matching-verification mode produced
    // this record and which folder was scanned, so the user never has to
    // remember or annotate the mode by hand. (A field run was once reported as
    // Auto while the record's analyze.mode said Fast -- exactly the ambiguity
    // this display removes.) Mode values come from the record, not from the
    // current settings, so an old log always shows its own mode.
    {
      const QString m = root["analyze"].toObject()["mode"].toString();
      const QString modeStr = m == QLatin1String("B") ? trStr(lang(), "analyzeModeStable")
                            : m == QLatin1String("A") ? trStr(lang(), "analyzeModeFast")
                            : m == QLatin1String("AB") ? trStr(lang(), "analyzeModeAuto")
                            : QStringLiteral("-");
      const QString rootPath = meta["root"].toString();
      lines << QString("%1: %2").arg(trStr(lang(), "summaryMode"), modeStr);
      lines << QString("%1: %2").arg(trStr(lang(), "summaryFolder"),
                                    rootPath.isEmpty() ? QStringLiteral("-") : rootPath);
    }
    lines << QString("%1: %2").arg(trStr(lang(), "detailLogDuration"), fmtDur(sum["wallMs"].toDouble()));
    lines << QString("%1  scanned: %2  analyzed: %3  %4: %5 files/sec").arg(trStr(lang(), "detailLogImages"))
        .arg(iScan).arg(iAn).arg(trStr(lang(), "detailLogThroughput")).arg(f1(iRate));
    lines << QString("  %1: %2  %3: %4  %5: %6").arg(trStr(lang(), "detailLogDupGroups")).arg(iGr)
        .arg(trStr(lang(), "detailLogDupFiles")).arg(iDf)
        .arg(trStr(lang(), "detailLogDupPairs")).arg(iPr);
    lines << QString("%1  scanned: %2  analyzed: %3  %4: %5 files/sec").arg(trStr(lang(), "detailLogVideos"))
        .arg(vScan).arg(vAn).arg(trStr(lang(), "detailLogThroughput")).arg(f1(vRate));
    lines << QString("  %1: %2  %3: %4  %5: %6").arg(trStr(lang(), "detailLogDupGroups")).arg(vGr)
        .arg(trStr(lang(), "detailLogDupFiles")).arg(vDf)
        .arg(trStr(lang(), "detailLogDupPairs")).arg(vPr);
    lines << QString("%1  scanned: %2  analyzed: %3  %4: %5  %6: %7")
        .arg(trStr(lang(), "detailLogTotal"))
        .arg((qulonglong)sum["scanned"].toDouble()).arg((qulonglong)sum["analyzed"].toDouble())
        .arg(trStr(lang(), "detailLogDupGroups")).arg(iGr + vGr)
        .arg(trStr(lang(), "detailLogDupFiles")).arg(iDf + vDf);
    lines << QString("%1: Total %2 · Image %3 · Video %4 · %5 %6").arg(trStr(lang(), "detailLogTimeSplit"))
        .arg(fmtDur(sum["wallMs"].toDouble())).arg(fmtDur(iMs)).arg(fmtDur(vMs))
        .arg(trStr(lang(), "detailLogMatching")).arg(fmtDur(sum["analyzeMs"].toDouble()));
  }
  lines << QString("%1: %2 s (walk %3 s, reval %4 s, img %5 s, vid %6 s, analyze %7 s)").arg(trStr(lang(), "detailLogWall"))
      .arg(f1(sum["wallMs"].toDouble() / 1000.0)).arg(f1(sum["walkMs"].toDouble() / 1000.0))
      .arg(f1(sum["revalidateMs"].toDouble() / 1000.0)).arg(f1(sum["imageStageMs"].toDouble() / 1000.0))
      .arg(f1(sum["videoStageMs"].toDouble() / 1000.0)).arg(f1(sum["analyzeMs"].toDouble() / 1000.0));
  // 0.9.4.119: two labelled rates. "전체/overall" = scanned/wall (how fast the
  // scan actually ran); "신규/new" = analyzed/wall (the telemetry filesPerSec,
  // meaning "newly analyzed per second"). The old single unlabelled value was
  // the latter, and a field report rightly called "0.0 files/s" next to
  // scanned=177,781 confusing. JSON semantics are untouched.
  const double wallMsTotal = sum["wallMs"].toDouble();
  const double rateOverall = wallMsTotal > 0
      ? 1000.0 * sum["scanned"].toDouble() / wallMsTotal : 0.0;
  lines << QString("%1: scan %2, analyzed %3, unchanged %4 (%5 %6 files/s · %7 %8 files/s)")
      .arg(trStr(lang(), "detailLogFiles"))
      .arg((qulonglong)sum["scanned"].toDouble()).arg((qulonglong)sum["analyzed"].toDouble())
      .arg((qulonglong)sum["unchanged"].toDouble())
      .arg(trStr(lang(), "detailLogRateOverall")).arg(f1(rateOverall))
      .arg(trStr(lang(), "detailLogRateNew")).arg(f1(sum["filesPerSec"].toDouble()));
  const qulonglong imgN = (qulonglong)imgs["count"].toDouble();
  const qulonglong imgGpu = (qulonglong)imgs["gpuHashed"].toDouble();
  lines << QString("%1: %2 (GPU %3, CPU %4), decode avg %5 ms, hash avg %6 ms").arg(trStr(lang(), "detailLogImages"))
      .arg(imgN).arg(imgGpu).arg(imgN - imgGpu)
      .arg(f2(imgs["meanDecodeMs"].toDouble())).arg(f2(imgs["meanHashMs"].toDouble()));
  lines << QString("%1: %2 (%3 min, build avg %4 ms, %5 s/play-min, %6 s/GB)").arg(trStr(lang(), "detailLogVideos"))
      .arg((qulonglong)vids["count"].toDouble()).arg(f1(vids["playSec"].toDouble() / 60.0))
      .arg(f1(vids["meanBuildMs"].toDouble())).arg(f1(vids["secPerPlayMin"].toDouble())).arg(f1(vids["secPerGB"].toDouble()));
  // 0.9.4.129: process-tree mean/max/std (this app + children) and the
  // system-wide mean are both shown. The process metric is normalized by CPU
  // count, so a fully loaded 16-thread scan can read ~6% while the machine is
  // at 100%; the sys mean is what the task manager shows.
  lines << QString("%1: %2 avg %3%, max %4%, std %5% · %6 avg %7% · %8: %9 MB · %10: %11% (longest idle %12 s)")
      .arg(trStr(lang(), "detailLogCpuUse"))
      .arg(trStr(lang(), "detailLogCpuProc"))
      .arg(f1(res["cpuProcMean"].toDouble())).arg(f1(res["cpuProcMax"].toDouble())).arg(f2(res["cpuProcStd"].toDouble()))
      .arg(trStr(lang(), "detailLogCpuSys")).arg(f1(res["cpuSysMean"].toDouble()))
      .arg(trStr(lang(), "detailLogMemMax")).arg((qulonglong)res["memMBMax"].toDouble())
      .arg(trStr(lang(), "detailLogGpuDuty")).arg(f1(res["gpuDutyPct"].toDouble()))
      .arg(f1(res["gpuLongestIdleMs"].toDouble() / 1000.0));
  {
    const QString vol = res["diskVolume"].toString();
    const double rMax = res["ioReadBpsMax"].toDouble() / 1e6, rMean = res["ioReadBpsMean"].toDouble() / 1e6;
    const double wMax = res["ioWriteBpsMax"].toDouble() / 1e6, wMean = res["ioWriteBpsMean"].toDouble() / 1e6;
    const double pR = res["procIoReadBytes"].toDouble() / 1e9, pW = res["procIoWriteBytes"].toDouble() / 1e9;
    lines << QString("%1%2: read max %3 MB/s, mean %4 · write max %5, mean %6 · process total read %7 GB, write %8 GB%9")
        .arg(trStr(lang(), "detailLogIo")).arg(vol.isEmpty() ? QString() : QString(" (%1)").arg(vol))
        .arg(f1(rMax)).arg(f1(rMean)).arg(f1(wMax)).arg(f1(wMean)).arg(f1(pR)).arg(f1(pW))
        .arg(res["diskAvailable"].toBool(true) ? QString() : QString(" (n/a)"));
  }
  lines << QString("%1: candidates %2, pairs %3, groups %4, reduction %5% (GPU imgs %6, fallback %7)").arg(trStr(lang(), "detailLogMatches"))
      .arg((qulonglong)mat["candidates"].toDouble()).arg((qulonglong)mat["pairs"].toDouble())
      .arg((qulonglong)mat["groups"].toDouble()).arg(f1(mat["reductionPct"].toDouble()))
      .arg((qulonglong)mat["gpuImages"].toDouble()).arg((qulonglong)mat["gpuFallback"].toDouble());
  lines << QString("%1 (img):").arg(trStr(lang(), "detailLogSlow"));
  int shown = 0;
  for (const auto& v : imgs["slowest"].toArray()) {
    if (shown++ >= 5) break;
    const QJsonObject o = v.toObject();
    lines << QString("  %1 ms  %2").arg(f1(o["ms"].toDouble())).arg(o["path"].toString());
  }
  lines << QString("%1 (vid):").arg(trStr(lang(), "detailLogSlow"));
  shown = 0;
  for (const auto& v : vids["slowest"].toArray()) {
    if (shown++ >= 5) break;
    const QJsonObject o = v.toObject();
    lines << QString("  %1 ms  %2").arg(f1(o["ms"].toDouble())).arg(o["path"].toString());
  }
  // 0.9.4.144: files whose analysis failed, with the reason.
  lines << QString("%1 (img):").arg(trStr(lang(), "detailLogFailed"));
  shown = 0;
  for (const auto& v : imgs["failedFiles"].toArray()) {
    if (shown++ >= 10) break;
    const QJsonObject o = v.toObject();
    lines << QString("  %1 — %2").arg(o["reason"].toString(), o["path"].toString());
  }
  lines << QString("%1 (vid):").arg(trStr(lang(), "detailLogFailed"));
  shown = 0;
  for (const auto& v : vids["failedFiles"].toArray()) {
    if (shown++ >= 10) break;
    const QJsonObject o = v.toObject();
    lines << QString("  %1 — %2").arg(o["reason"].toString(), o["path"].toString());
  }
  lines << QString("build %1 · engine %2 · db %3 · d=%4 %5GPU")
      .arg(meta["build"].toString()).arg(meta["engine"].toString()).arg(meta["db"].toString())
      .arg(cfg["distance"].toInt()).arg(cfg["gpuEnabled"].toBool() ? QString() : "no-");
  QDialog dlg(this);
  dlg.setWindowTitle(trStr(lang(), "detailLogTitle"));
  auto* lay = new QVBoxLayout(&dlg);
  auto* view = new QTextBrowser(&dlg);
  view->setText(lines.join('\n'));
  lay->addWidget(view);
  auto* btns = new QHBoxLayout();
  auto* save = new QPushButton(trStr(lang(), "detailLogSave"), &dlg);
  auto* close = new QPushButton(trStr(lang(), "detailLogClose"), &dlg);
  btns->addStretch();
  btns->addWidget(save);
  btns->addWidget(close);
  lay->addLayout(btns);
  connect(save, &QPushButton::clicked, this, [this, json, meta]() {
    QString name = QString("detailed-log-%1.json").arg(meta["startedAt"].toString().remove(':'));
    const QString path = QFileDialog::getSaveFileName(this, trStr(lang(), "detailLogSave"),
        QCoreApplication::applicationDirPath() + "/" + name, "JSON (*.json)");
    if (path.isEmpty()) return;
    saveDetailedLogTo(path, json);
  });
  connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
  dlg.resize(620, 480);
  dlg.exec();
}
// 0.9.4.112 (S4): save unit behind the dialog's Save button. The old
// `write() >= 0` accepted a zero/partial write as success; the exact byte
// count is required so "Detailed log saved" never lies.
bool MainWindow::saveDetailedLogTo(const QString& path, const QString& json) {
  const QByteArray bytes = json.toUtf8();
  QFile f(path);
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size()) {
    statusMsg_->setText(trStr(lang(), "detailLogSaved") + path);
    return true;
  }
  statusMsg_->setText(trStr(lang(), "detailLogSaveFail"));
  return false;
}
void MainWindow::scanFinished(QString msg) {
  // Entry/exit trace for hang diagnosis. If a future "stuck popup" report
  // shows "enter" without the matching "exit" in msf_scan.log, the stall is
  // inside this function; if neither appears, the worker never returned.
  scanLog(QString("scanFinished enter: %1").arg(msg.left(80)));
  holdViewDuringScan_ = false; // 0.9.4.142: the scan is over; stop freezing
  // A stop request already shows this popup from cancelScan(): keep it for
  // the rebuild instead of flashing a second one.
  QDialog* wait = cancelWait_;
  cancelWait_ = nullptr;
  if (!wait) wait = showReportWaitPopup(this, lang());
  // The popup must never outlive this function: every exit below closes it
  // and returns the GUI to idle, even on an unexpected exception. Otherwise
  // a "writing report" popup stays open forever with no way back.
  // 0.9.4.140: only a scan that re-analyzed files can hold stale art; the
  // beginScan comment explains why the cache is kept during the walk.
  bool contentChanged = false;
  bool reloadIntent = false; // 0.9.4.143: changed scan -> silent stored-set reload
  try {
  scanLog(QString("finish %1").arg(msg));
  scanLog(QString("thumbStat mem=%1 backend=%2 place=%3 pending=%4")
      .arg(thumbStatMem_).arg(thumbStatBackend_)
      .arg(thumbStatPlace_).arg((qulonglong)thumbPendingReq_.size()));
  // 0.9.4.94: onResults already performed this exact rebuild for the same
  // scan (results arrive before finished). Repeating it here doubled the
  // worst-case "writing report" stall on the GUI thread with zero new data.
  // Cancelled scans never pass through onResults, so they always rebuild.
  const bool skipRebuild = resultsApplied_ && !msg.startsWith(QStringLiteral("CANCELLED"));
  resultsApplied_ = false;
  if (skipRebuild) scanLog(QString("scanFinished rebuild skipped (already applied by onResults)"));
  // 0.9.4.118: append-first here too - the streamed list normally already
  // holds, so this path must not churn 9k rows (field-measured ~19 s).
  else { rebuildGroups(); refreshGroupList(/*append=*/true); refreshFileViews(); refreshDetail(); }
  if (msg.startsWith(QStringLiteral("CANCELLED"))) {
    // Partial progress is kept by design (checkpoints): report what survived.
    const QStringList st = msg.split('|');
    contentChanged = st.size() > 2 && st[2].toULongLong() > 0;
    const QString detail = st.size() > 2
        ? QString(" (%1 %2, %3 %4)").arg(st[1]).arg(trStr(lang(), "scanned")).arg(st[2]).arg(trStr(lang(), "analyzed"))
        : QString();
    statusMsg_->setText(trStr(lang(), "scanPartial") + detail);
  } else {
    const QStringList st = msg.split('|');
    // st[0]=text st[1]=scanned st[2]=analyzed st[3]=unchanged st[4]=groups st[5]=candidates
    lastStats_ = st.mid(1, 5);
    loadedSummary_ = false; // 0.9.4.132: a scan answer supersedes the load reading
    repElapsedMs_ = elapsedActiveMs();
    const qulonglong analyzed = st.size() > 2 ? st[2].toULongLong() : 0;
    const qulonglong unchanged = st.size() > 3 ? st[3].toULongLong() : 0;
    const qulonglong completed = st.size() > 1 ? st[1].toULongLong() : 0;
    contentChanged = analyzed > 0;
    reloadIntent = analyzed > 0 && startedWithView_ && !(testBtn_ && testBtn_->isChecked());
    if (targetKnown_ && completed != targetTotal_)
      scanLog(QString("targetMismatch fixed=%1 completed=%2").arg(targetTotal_).arg(completed));
    lastTotalN_ = completed;
    hasReport_ = true;
    if (analyzed == 0 && unchanged > 0)
      statusMsg_->setText(trStr(lang(), "upToDate") + QString(" (%1 %2)").arg(unchanged).arg(trStr(lang(), "files")));
    else
      statusMsg_->setText(trStr(lang(), "scanDone") + QString(" (%1)").arg(st[0]));
  }
  refreshSummary();
  updateStatusCounts();
  // 0.9.4.140: analyzed > 0 means at least one file was re-read and may have
  // new content, so the kept presentation art can be stale. Drop it now (the
  // rows keep painting their old icons until arrivals replace them - no blank)
  // and re-ask only the visible rows/preview; the backend is idle by now.
  if (contentChanged) {
    thumbCache_.clear();
    // Resolution/badge metadata can also belong to the pre-change content.
    resCache_.clear();
    fileMetaAsked_.clear();
    scanLog(QString("thumbCache invalidated analyzed>0 kept=0"));
    QTimer::singleShot(0, this, [this] {
      thumbCatchUpVisible();
      refreshFileViews();
      refreshDetail();
    });
  }
  } catch (const std::exception& e) {
    scanLog(QString("scanFinished exception (state preserved): %1").arg(e.what()));
  } catch (...) {
    scanLog(QString("scanFinished unknown exception (state preserved)"));
  }
  wait->close();
  wait->deleteLater();
  setRunning(false);
  statusProg_->setValue(100);
  // 0.9.4.143: a scan that re-analyzed files may have dropped stored pairs.
  // Reload the stored set once (silently) so the view reflects the corrected
  // index; the scan summary/status and the selection/sort are preserved.
  if (reloadIntent) {
    reloadAfterScan_ = true;
    silentReload_ = true;
    reloadSort_ = sortBox_->currentIndex();
    if (currentGroup_ >= 0 && currentGroup_ < groups_.size() && !groups_[currentGroup_].paths.isEmpty())
      reloadSelRep_ = groups_[currentGroup_].paths.front();
    reloadSelFile_ = currentFile_;
    reloadVerifyTotal_ = lastVerifyTotal_;
    reloadVerifyDone_ = lastVerifyDone_;
    scanLog(QString("reloadAfterScan scheduled sel=%1 file=%2 sort=%3")
                .arg(reloadSelRep_).arg(reloadSelFile_).arg(reloadSort_));
    QTimer::singleShot(0, this, [this] {
      if (!scanning_ && !loading_ && !reverify_) loadIndex();
    });
  }
  scanLog(QString("scanFinished exit"));
}
void MainWindow::scanFailed(QString msg) {
  scanLog(QString("failed %1").arg(msg));
  holdViewDuringScan_ = false; // 0.9.4.142: the scan ended abnormally
  // A failed load-only or re-verify request is not a scan failure: clear that
  // state and report it without the scan-failure machinery.
  if (loading_ || reverify_) {
    loading_ = false;
    reverify_ = false;
    if (loadWait_) { loadWait_->close(); loadWait_->deleteLater(); loadWait_ = nullptr; }
    if (reverifyWait_) { reverifyWait_->close(); reverifyWait_->deleteLater(); reverifyWait_ = nullptr; }
    scan_->setEnabled(backendAvailable_);
    refresh_->setEnabled(true);
    reverifyBtn_->setEnabled(true);
    QMessageBox::critical(this, trStr(lang(), "scanErr"), msg);
    statusMsg_->setText(trStr(lang(), "scanErr") + ": " + msg);
    return;
  }
  const bool backendInterrupted =
      msg.contains(QStringLiteral("backend process exited unexpectedly")) ||
      msg.contains(QStringLiteral("backend lost — scan aborted"));
  scanCrashed_ = backendInterrupted && haveLastScanCfg_;
  const bool willAutoResume = scanCrashed_ && backendRestarting_ && autoResumeTries_ < 1;
  // Keep matches streamed before the worker failure visible in the current UI.
  // 0.9.4.118: append-first (no row churn when the streamed list already holds).
  rebuildGroups();
  refreshGroupList(/*append=*/true); refreshFileViews(); refreshDetail();
  if (willAutoResume) {
    statusMsg_->setText(lang() == UiLang::Ko
                            ? QStringLiteral("백엔드가 종료되었습니다. 재시작 후 검색을 자동으로 이어갑니다.")
                            : QStringLiteral("Backend exited. The scan will resume after restart."));
  } else {
    QMessageBox::critical(this, trStr(lang(), "scanErr"), msg);
    statusMsg_->setText(trStr(lang(), "scanErr") + ": " + msg);
  }
  setRunning(false);
}
std::string MainWindow::telemetryJsonForTest() const {
  if (!backend_) return {};
  return backend_->telemetryJsonForTest();
}
void MainWindow::onResults(QVector<BackendFile> files, QStringList matchRows) {
  // Load-only ("불러오기") and Re-verify ("재검증") answers come through this
  // same path but must NOT show the finished-scan report popup; each closes
  // its own notice and re-enables the buttons. A scan answer keeps the
  // existing behavior.
  QDialog* wait = nullptr;
  const bool wasReverify = reverify_;
  const bool wasLoad = loading_ || wasReverify;
  if (wasLoad) {
    if (loading_) {
      loading_ = false;
      wait = loadWait_;
      loadWait_ = nullptr;
    }
    if (wasReverify) {
      reverify_ = false;
      wait = reverifyWait_;
      reverifyWait_ = nullptr;
    }
    scan_->setEnabled(backendAvailable_);
    refresh_->setEnabled(true);
    reverifyBtn_->setEnabled(true);
  } else {
    wait = cancelWait_;
    cancelWait_ = nullptr;
    if (!wait) wait = showReportWaitPopup(this, lang());
  }
  allPaths_.clear(); matchRows_ = matchRows;
  fileSize_.clear(); fileFp_.clear(); fileDur_.clear();
  // 0.9.4.117: stage timing for the field-diagnosed result-apply stall (a
  // 9,055-group apply froze the GUI for minutes; see group_fill_probe).
  QElapsedTimer applyT;
  applyT.start();
  for (const auto& f : files) {
    allPaths_ << f.path;
    fileSize_[f.path] = QString::number(f.size);
    fileFp_[f.path] = f.fpHex;
    fileDur_[f.path] = f.duration;
  }
  const qint64 tMaps = applyT.elapsed();
  // 0.9.4.141: match accumulators (bestPct_/pathKind_/contentFlag_) are NOT
  // reset here. Hybrid's live pairs are streamed only through onMatch and
  // never appear in these final rows, so resetting them would drop their
  // percentages and identity badges. They start fresh at beginScan instead;
  // the metadata caches stay until a scan actually re-analyzed files (see
  // scanFinished), so an unchanged rescan leaves every visible value intact.
  // fold final retained matches (same data as streamed, dedup by rebuild)
  for (const auto& row : matchRows_) {
    const QStringList p = row.split('\t');
    if (p.size() < 3) continue;
    // 0.9.4.106: the 4th field carries the content-identity badge flag;
    // producers older than this build simply omit it (no badge).
    const bool identical = p.size() > 3 && p[3] == QLatin1String("1");
    addMatch(p[0], p[1], p[2].toDouble(), isVideoExt(p[0]) ? 2 : 1, identical);
  }
  const qint64 tMatch = applyT.elapsed();
  rebuildGroups();
  const qint64 tGroups = applyT.elapsed();
  // 0.9.4.118: append-first. During a scan the streamed list usually already
  // matches the rebuilt groups (prefix held), so the final apply no longer
  // re-creates every row (field: repeated 9k-row rebuilds at scan start/end).
  refreshGroupList(/*append=*/true);
  const qint64 tFill = applyT.elapsed();
  refreshFileViews(); refreshDetail(); updateStatusCounts();
  const qint64 tTail = applyT.elapsed();
  scanLog(QString("resultsApply files=%1 rows=%2 groups=%3 maps=%4ms matches=%5ms rebuild=%6ms fill=%7ms tail=%8ms")
              .arg(files.size()).arg(matchRows_.size()).arg(groups_.size())
              .arg(tMaps).arg(tMatch - tMaps).arg(tGroups - tMatch)
              .arg(tFill - tGroups).arg(tTail - tFill));
  // 0.9.4.94: mark this scan's rebuild done so scanFinished (which always
  // follows for a completed scan) does not repeat it. Load answers take the
  // wasLoad branch above and never reach scanFinished; leave the mark clear.
  if (!wasLoad) resultsApplied_ = true;
  if (wasLoad) {
    if (reloadAfterScan_) {
      // 0.9.4.143: the post-scan refresh. The refreshed stored set is the
      // authority (stale pairs are gone), but the scan's summary/status stays
      // on screen and the pre-scan sort/selection are restored by identity.
      sortBox_->blockSignals(true);
      sortBox_->setCurrentIndex(reloadSort_ >= 0 && reloadSort_ < sortBox_->count() ? reloadSort_ : 0);
      sortBox_->blockSignals(false);
      rebuildGroups();
      refreshGroupList();
      const int gi = pathGroup_.value(reloadSelRep_, -1);
      currentGroup_ = gi;
      if (gi >= 0 && !reloadSelFile_.isEmpty() && groups_[gi].paths.contains(reloadSelFile_))
        currentFile_ = reloadSelFile_;
      else
        currentFile_.clear();
      lastVerifyTotal_ = reloadVerifyTotal_;
      lastVerifyDone_ = reloadVerifyDone_;
      hasReport_ = true;
      refreshFileViews(); refreshDetail(); updateStatusCounts(); refreshSummary();
      scanLog(QString("reloadAfterScan applied files=%1 groups=%2 sel=%3")
                  .arg(files.size()).arg(groups_.size()).arg(currentGroup_));
      reloadAfterScan_ = false;
      silentReload_ = false;
      reloadSelRep_.clear();
      reloadSelFile_.clear();
      loadedSummary_ = false;
    } else {
      // 0.9.4.132: populate the left summary from the loaded index. Without this
      // the panel stayed all "-" after Load (field report): the load is the only
      // producer of this result set for the session, so file/group/duplicate
      // rows describe it, the verify row shows the stored pair total ("- / N",
      // nothing was verified now), and the time row shows the load duration.
      lastStats_ = QStringList{QString::number(files.size()), QString::number(files.size()),
                               QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0")};
      repElapsedMs_ = loadStartMs_ > 0 ? QDateTime::currentMSecsSinceEpoch() - loadStartMs_ : 0;
      loadedSummary_ = true;
      lastVerifyDone_ = matchRows_.size();
      hasReport_ = true;
      statusProg_->setRange(0, 100);
      statusProg_->setValue(100);
      statusMsg_->setText(trStr(lang(), "loadDone").arg(files.size()).arg(matchRows_.size()));
      refreshSummary();
    }
  } else {
    loadedSummary_ = false;
  }
  // 0.9.4.142: the answer is applied; let the next view-changing path run again.
  holdViewDuringScan_ = false;
  if (wait) { wait->close(); wait->deleteLater(); }
}
void MainWindow::resourceChanged(int i) {
  auto m = static_cast<msf::ResourceMode>(i + 1);
  // Keep an out-of-range display value from reaching the policy layer. The
  // existing blockSignals pattern prevents this corrective write from
  // re-entering the slot.
  const int cpu = msf::normalize_user_cpu_percent(cpu_->value());
  if (cpu != cpu_->value()) {
    cpu_->blockSignals(true);
    cpu_->setValue(cpu);
    cpu_->blockSignals(false);
  }
  policy_ = msf::make_policy(m, cpu_->value(), policy_.gpuPercent);
  if (backend_) backend_->updateResourcePolicy(currentExecPolicy());
  cpu_->blockSignals(true);
  cpu_->setValue(policy_.cpuPercent);
  cpu_->blockSignals(false);
}
// P4: the two-axis policy in one struct. strategy comes from the execution
// dropdown (AUTO/CPU_ONLY/GPU_MAX); mode+budgets come from policy_.
ExecutionPolicy MainWindow::currentExecPolicy() const {
  ExecutionPolicy e;
  e.cpuMode = static_cast<int>(policy_.mode);
  e.cpuPercent = policy_.cpuPercent;
  e.gpuPercent = policy_.gpuPercent;
  e.strategy = executionStrategy() == ExecutionResourceStrategy::CpuOnly
                   ? 1
                   : (executionStrategy() == ExecutionResourceStrategy::GpuMax ? 2 : 0);
  e.gpuEnabled = gpuOn_;
  return e;
}
void MainWindow::clampCpuDigitSelection() {
  if (!cpu_ || !cpu_->findChild<QLineEdit*>()) return;
  QLineEdit* e = cpu_->findChild<QLineEdit*>();
  const int pre = cpu_->prefix().size(), suf = cpu_->suffix().size();
  const int n = e->text().size();
  const int lo = std::min(pre, n), hi = std::max(lo, n - suf);
  const int ss = e->selectionStart();
  e->blockSignals(true);
  if (ss < 0) {
    e->setCursorPosition(std::clamp(e->cursorPosition(), lo, hi));
  } else {
    int s = std::clamp(ss, lo, hi);
    int se = std::clamp(ss + (int)e->selectedText().size(), lo, hi);
    if (s > se) std::swap(s, se);
    e->setCursorPosition(se);
    if (se > s) e->setSelection(s, se - s);
  }
  e->blockSignals(false);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* ev) {
  // Ctrl+A inside the CPU box selects the digits only, never prefix/suffix.
  if (cpu_ && watched == cpu_->findChild<QLineEdit*>() && ev->type() == QEvent::KeyPress) {    const auto* k = static_cast<QKeyEvent*>(ev);
    if (k->matches(QKeySequence::SelectAll)) {
      QLineEdit* e = cpu_->findChild<QLineEdit*>();
      if (!e) return true;
      const int pre = cpu_->prefix().size();
      e->setSelection(pre, std::max(0, (int)(e->text().size() - pre - cpu_->suffix().size())));
      return true;
    }
  }
  // Scroll-regression gate feed (0.9.4.61, extended 0.9.4.75): wheel and keyboard navigation in
  // the middle group views stamps user activity so the next full refill waits
  // out the cooldown. Scrollbar-focused wheel/keys count as navigation too.
  // The event is never consumed — scrolling behaves exactly
  // as before; only the rebuild is deferred.
  const bool isMidViewport =
      (imgTree_ && (watched == imgTree_ || watched == imgTree_->viewport() || watched == imgTree_->verticalScrollBar())) ||
      (vidTree_ && (watched == vidTree_ || watched == vidTree_->viewport() || watched == vidTree_->verticalScrollBar())) ||
      (imgGrid_ && (watched == imgGrid_ || watched == imgGrid_->viewport() || watched == imgGrid_->verticalScrollBar())) ||
      (vidGrid_ && (watched == vidGrid_ || watched == vidGrid_->viewport() || watched == vidGrid_->verticalScrollBar()));
  if (isMidViewport) {
    if (ev->type() == QEvent::Wheel) { noteUserScroll(); }
    else if (ev->type() == QEvent::KeyPress) {
      switch (static_cast<QKeyEvent*>(ev)->key()) {
        case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_PageUp: case Qt::Key_PageDown:
        case Qt::Key_Home: case Qt::Key_End: case Qt::Key_Left: case Qt::Key_Right:
          noteUserScroll();
          break;
        default:
          break;
      }
    }
  }
  return QMainWindow::eventFilter(watched, ev);
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
void MainWindow::loadIndex() {
  // 0.9.4.93: load-only. Replace the current view with the selected folder's
  // stored index content (files + pairs) without walking or analyzing. The
  // Backend answers through the normal results()/quickLoaded() path, so the
  // list and previews populate exactly like a scan finish.
  if (scanning_ || loading_ || reverify_ || !backend_) return;
  const QString root = folder_->text();
  if (root.isEmpty()) { chooseFolder(); if (folder_->text().isEmpty()) return; }
  // The view is replaced: drop the previous accumulation so old groups cannot
  // linger next to the loaded ones.
  bestPct_.clear(); resCache_.clear(); pathKind_.clear(); thumbCache_.clear();
  fileMetaAsked_.clear(); // 0.9.4.105: new load generation re-asks metadata
  contentFlag_.clear();   // 0.9.4.106: badge flags belong to one result set
  allPaths_.clear(); matchRows_.clear(); fileSize_.clear(); fileFp_.clear(); fileDur_.clear();
  pathParent_.clear(); groups_.clear(); pathGroup_.clear();
  noneOrder_.clear(); imgShown_.clear(); vidShown_.clear();
  marked_.clear();
  currentGroup_ = -1; currentFile_.clear(); hasReport_ = false;
  matchSeq_ = 0; groupsDirty_ = false; lastFillSig_ = 0;
  resultsApplied_ = false; lastFileSig_.clear();
  lastVerifyTotal_ = 0; lastVerifyDone_ = 0;
  // 0.9.4.132: the loaded set becomes the summary's subject. targetKnown_ is
  // cleared so refreshSummary shows the loaded file count, not a stale scan
  // target from earlier in the session.
  targetKnown_ = false;
  loadStartMs_ = QDateTime::currentMSecsSinceEpoch();
  loadedSummary_ = false;
  if (sumValVerify_) sumValVerify_->setText(QStringLiteral("-"));
  // 0.9.4.139: Load starts from the stored order but the sort is no longer
  // locked -- the user may re-sort the loaded list immediately.
  sortBox_->blockSignals(true); sortBox_->setCurrentIndex(kSortNone); sortBox_->blockSignals(false);
  updateSortEnabled();
  refreshGroupList(); refreshFileViews(); refreshDetail();
  statusMsg_->setText(trStr(lang(), "loadRunning"));
  // One press per load: disable the button and show a NON-modal notice, so the
  // window stays movable while the index is read. onResults clears both.
  loading_ = true;
  refresh_->setEnabled(false);
  reverifyBtn_->setEnabled(false);
  scan_->setEnabled(false);
  // The load notice is a SEPARATE popup from the finished-scan report popup:
  // its own title ("인덱스 불러오기") and body ("인덱스 목록 로딩중…"), so a load
  // never shows the "검색 리포트 작성중" wording.
  // 0.9.4.143: the post-scan refresh is silent (no second notice popup).
  if (!silentReload_) loadWait_ = showReportWaitPopup(this, lang(), "loadWait", "loadWaitTitle");
  backend_->loadIndex(root, QApplication::applicationDirPath());
}
void MainWindow::reverifyIndex() {
  // 0.9.4.134: forced duplicate re-verification, then the refreshed view.
  // The backend re-reads the 64 KiB quick hash of every stored-duplicate
  // member (no walk), re-analyzes only the members that changed -- including
  // edits that preserved size+mtime, the fast-rescan blind spot -- updates the
  // index in one transaction, and answers through the normal results() path.
  if (scanning_ || loading_ || reverify_ || !backend_) return;
  const QString root = folder_->text();
  if (root.isEmpty()) { chooseFolder(); if (folder_->text().isEmpty()) return; }
  // Same view replacement as Load: the refreshed stored set becomes the list.
  bestPct_.clear(); resCache_.clear(); pathKind_.clear(); thumbCache_.clear();
  fileMetaAsked_.clear();
  contentFlag_.clear();
  allPaths_.clear(); matchRows_.clear(); fileSize_.clear(); fileFp_.clear(); fileDur_.clear();
  pathParent_.clear(); groups_.clear(); pathGroup_.clear();
  noneOrder_.clear(); imgShown_.clear(); vidShown_.clear();
  marked_.clear();
  currentGroup_ = -1; currentFile_.clear(); hasReport_ = false;
  matchSeq_ = 0; groupsDirty_ = false; lastFillSig_ = 0;
  resultsApplied_ = false; lastFileSig_.clear();
  lastVerifyTotal_ = 0; lastVerifyDone_ = 0;
  targetKnown_ = false;
  loadStartMs_ = QDateTime::currentMSecsSinceEpoch();
  loadedSummary_ = false;
  sortBox_->blockSignals(true); sortBox_->setCurrentIndex(kSortNone); sortBox_->blockSignals(false);
  updateSortEnabled();
  refreshGroupList(); refreshFileViews(); refreshDetail();
  statusMsg_->setText(trStr(lang(), "reverifyRunning"));
  reverify_ = true;
  refresh_->setEnabled(false);
  reverifyBtn_->setEnabled(false);
  scan_->setEnabled(false);
  reverifyWait_ = showReportWaitPopup(this, lang(), "reverifyWait", "reverifyWaitTitle");
  backend_->reverifyIndex(root, QApplication::applicationDirPath());
}
void MainWindow::onReverifyFinished(qulonglong members, qulonglong checked, qulonglong changed,
                                    qulonglong reanalyzed, qulonglong pairsBefore, qulonglong pairsAfter) {
  // 0.9.4.134: completion text (results already repopulated the view and the
  // load-style summary). The last pair count is the displayed set's, so the
  // verify row stays consistent with the list.
  Q_UNUSED(members);
  loadedSummary_ = true;
  lastVerifyDone_ = pairsAfter;
  if (hasReport_) refreshSummary();
  statusMsg_->setText(trStr(lang(), "reverifyDone")
                          .arg(checked).arg(changed).arg(reanalyzed)
                          .arg(pairsBefore).arg(pairsAfter));
  scanLog(QString("reverify members=%1 checked=%2 changed=%3 reanalyzed=%4 pairs=%5->%6")
              .arg(members).arg(checked).arg(changed).arg(reanalyzed)
              .arg(pairsBefore).arg(pairsAfter));
}
void MainWindow::onTestModeDiff(qulonglong changed, qulonglong added, qulonglong removed,
                                qulonglong pairDiff, qulonglong pairsMain, qulonglong pairsScratch) {
  // 0.9.4.135: the completed Test Mode scan compared itself with the main
  // index. Identical -> a status line; differences -> the user decides whether
  // to adopt (Yes) or keep the main index untouched (No). Non-modal so the
  // window stays responsive and the e2e can click the buttons.
  scanLog(QString("testDiff changed=%1 added=%2 removed=%3 pairDiff=%4 pairs=%5->%6")
              .arg(changed).arg(added).arg(removed).arg(pairDiff)
              .arg(pairsMain).arg(pairsScratch));
  if (changed + added + removed + pairDiff == 0) {
    statusMsg_->setText(trStr(lang(), "testNoDiff"));
    return;
  }
  if (auto* old = findChild<QMessageBox*>("testDiffBox")) old->close();
  auto* box = new QMessageBox(QMessageBox::Question, trStr(lang(), "testDiffTitle"),
                              trStr(lang(), "testDiffBody")
                                  .arg(changed).arg(added).arg(removed)
                                  .arg(pairsMain).arg(pairsScratch),
                              QMessageBox::Yes | QMessageBox::No, this);
  box->setObjectName("testDiffBox"); // automation hook (backend e2e)
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setWindowModality(Qt::NonModal);
  connect(box, &QMessageBox::finished, this, [this](int result) {
    if (result == QMessageBox::Yes) {
      statusMsg_->setText(trStr(lang(), "testApplying"));
      if (backend_) backend_->applyTestResult();
    } else {
      statusMsg_->setText(trStr(lang(), "testNotApplied"));
    }
  });
  // show(), not open(): QDialog::open() forces the window-modal state, which
  // the e2e modal-closer timer would close before the buttons can be clicked.
  box->show();
}
void MainWindow::onTestApplied(bool ok, QString detail) {
  statusMsg_->setText(ok ? trStr(lang(), "testApplied")
                         : trStr(lang(), "testApplyFailed").arg(detail));
  scanLog(QString("testApplied ok=%1 %2").arg(ok ? 1 : 0).arg(detail));
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
void MainWindow::addMatch(const QString& l, const QString& r, double pct, int kind, bool contentIdentical) {
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
  // 0.9.4.106: badge flags for pairs that display as 100.0%. "Identical" wins
  // over "perceptual" (any byte-identical 100% pair makes the file honestly
  // identical); the flags are display-only and never touch the verdict.
  if (pct >= 99.9) {
    const int bit = contentIdentical ? 1 : 2;
    contentFlag_[l] |= bit;
    contentFlag_[r] |= bit;
  }
}
void MainWindow::onMatchesBatch(const QVector<BackendMatch>& batch) {
  // P2: push-based delivery replaces the takePending() pull. The loopback
  // drains the worker synchronously (including inside pause()), so by the
  // time a batch arrives everything streamed so far is in it.
  if (!backend_) return;
  if (batch.isEmpty()) return;
  for (const auto& m : batch) addMatch(m.left, m.right, m.percent, m.kind, m.contentIdentical);
  groupsDirty_ = true;
}
void MainWindow::testUiTick() { onUiTick(); }
bool MainWindow::testPreviewHasPixmap() const {
  return preview_ && !preview_->pixmap().isNull();
}
QString MainWindow::testDetailFileName() const {
  if (!detailForm_) return {};
  auto* w = detailForm_->itemAt(0, QFormLayout::FieldRole);
  if (!w || !w->widget()) return {};
  auto* lab = qobject_cast<QLabel*>(w->widget());
  return lab ? lab->text() : QString();
}
// Detail-form installation probe (see header).
bool MainWindow::testDetailInstalled() const {
  if (!detailForm_) return false;
  auto* box = qobject_cast<QWidget*>(detailForm_->parent());
  return box && box->layout() == detailForm_;
}
int MainWindow::testDetailRowCount() const {
  return detailForm_ ? detailForm_->rowCount() : -1;
}
void MainWindow::onUiTick() {
  // P3: no decode happens on the GUI thread anymore (all decode is
  // Backend-side), so there is no per-tick decode budget to refresh.
  // Thumbnail misses ask the backend asynchronously; arrivals paint via
  // onThumbReady. Cache hits are always free.
  if (groupsDirty_) {
    // P2: matches arrive pushed via matchesBatch (the loopback drains the
    // worker synchronously, including inside pause()), so there is nothing
    // left to pull here. groupsDirty_ still clears inside rebuildGroups
    // only, so a scroll-gated tick retries later instead of dropping data.
  }
  refreshStreaming(); // internally splits full rebuild vs thumbnail catch-up
  if (scanning_) updateStatusCounts();
  if (scanning_) {
    // Recompose with live elapsed so a long single file (e.g. a big video)
    // shows activity instead of a frozen message.
    const qint64 el = elapsedActiveMs();
    const qulonglong uiTotal = targetKnown_ ? targetTotal_ : lastTotalN_;
    statusMsg_->setText(scanStatusText(lastTotalN_, uiTotal, maxPctShown_, lastPath_, el));
    scanHeartbeat();
  }
  updateGpuLabel();
  updateSysLabels();
}
void MainWindow::refreshStreaming(bool force) {
  // A thumbnail burst may arrive between ticks. Flush at most one coalesced
  // layout pass here; it never recreates items or restores scroll.
  flushThumbLayout();
  // 0.9.4.142: the loaded view stays frozen for the whole scan (see
  // beginScan). Data still accumulates; onResults applies it in one pass.
  if (scanning_ && holdViewDuringScan_) {
    thumbCatchUpVisible();
    updateStatusCounts();
    return;
  }
  // Full list rebuilds (widget churn for every group) are the most expensive
  // GUI work during a scan. rebuildGroups() itself walks every accumulated
  // path, so it stays inside the gate too — running it every tick on 10k+
  // groups starved paints and froze the pane (blank-list recurrence).
  // Throttle adaptively so paint events and pause/cancel clicks always get
  // through: at most one full refresh per 3x measured cost (600ms..3s).
  // Status counts stay live every tick. force=true (pause click) refreshes
  // immediately regardless of the gate.
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 interval = std::clamp(lastFillCostMs_ * 3, (qint64)600, (qint64)3000);
  // Data change (streamed matches or any matchSeq_ advance) is the only
  // reason for a refill. During a scan the middle pane is filled in append
  // mode: existing rows/thumbnails are kept and only new groups are added at
  // the bottom (the fill falls back to a full rebuild itself if a group
  // merged). Thumbnail starvation alone fills visible icons in place below and
  // must never recreate the widget list: that was the scroll regression (a
  // starved catch-up destroying the scrolled position on every tick). A
  // scroll-gated tick defers the refill without dropping it —
  // groupsDirty_/lastFillSig_ stay set until it runs.
  const bool dataChanged = groupsDirty_ || (matchSeq_ != lastFillSig_);
  if (force || (dataChanged && !scrollGateActive() && now - lastFillMs_ >= interval)) {
    QElapsedTimer t; t.start();
    rebuildGroups(); // also clears groupsDirty_: all known data now reflected
    // Append-only fill is valid only while the scan runs with sort=None; any
    // other mode (or a post-scan tick) uses a full rebuild.
    refreshGroupList(scanning_ && sortBox_->currentIndex() == kSortNone); refreshFileViews();
    lastFillCostMs_ = t.elapsed();
  } else if (!dataChanged && !sliderHeld_) {
    // P3: thumbnail requests are cheap hash lookups now (misses ask the
    // backend asynchronously; arrivals paint via onThumbReady), so catch-up
    // runs every idle tick. It still never rebuilds or touches scroll: that
    // was the scroll regression, and the gate above is untouched.
    thumbCatchUpVisible();
  }
  // Otherwise this tick does nothing: a scroll-gated tick keeps its pending
  // flags (groupsDirty_/lastFillSig_) for a later tick instead of rebuilding.
  updateStatusCounts();
}
void MainWindow::noteUserScroll() {
  lastUserScrollMs_ = QDateTime::currentMSecsSinceEpoch();
}
bool MainWindow::scrollGateActive() const {
  if (sliderHeld_) return true;
  return QDateTime::currentMSecsSinceEpoch() - lastUserScrollMs_ < kScrollGateCooldownMs;
}
void MainWindow::flushThumbLayout() {
  if (!thumbLayoutPending_) return;
  // Never run a layout pass mid-gesture: even a synchronous pass can fight a
  // drag in flight. Keep the request pending and retry after the scroll
  // cooldown instead; icons are already painted by onThumbReady.
  if (scrollGateActive()) {
    QTimer::singleShot(kScrollGateCooldownMs, this, [this] { flushThumbLayout(); });
    return;
  }
  thumbLayoutPending_ = false;
  QListWidget* grids[2] = {imgGrid_, vidGrid_};
  for (auto* grid : grids) {
    if (grid && grid->isVisible()) grid->doItemsLayout();
  }
}
void MainWindow::thumbCatchUpVisible() {
  // Tree rows carry no thumbnails, so only the visible grid participates.
  // Bounded by construction: the loop covers at most the viewport's items
  // (index math only for the rest).
  // P3: misses are only *requested* here; arrivals paint through onThumbReady
  // (which forces the 0.9.4.66 layout pass).
  // 0.9.4.109: cache HITS are applied here. The P3 migration made this pump
  // request-only, but a cache hit produces no backend request and therefore
  // no arrival — after a view-mode switch (or zoom) the item kept its
  // old-size pixmap while the cells were recomputed, which is exactly the
  // 0.9.4.66 overlap ("art paints outside its cell"), re-opened for every
  // warm-cache switch. Applying the hit is a pure memory-cache write: no
  // decode, no request, no rebuild; the coalesced layout pass below is the
  // same one arrivals use, so the scroll-gate contract is unchanged.
  QListWidget* grid = groupsList_;
  if (!grid || !grid->isVisible()) return;
  const QRect vis = grid->viewport()->rect();
  const QSize iconSize = grid->iconSize();
  bool changed = false;
  // 0.9.4.117: iterate only the visible row window. The old loop called
  // visualItemRect() for every row (9,000+ per catch-up at the user's group
  // count), which was one of the bulk-apply stalls. Rows are in visual order,
  // so the window is [first visible .. last visible] plus a small margin; the
  // slow full-range fallback covers the rare case itemAt() cannot resolve.
  int first = -1, last = -1;
  if (auto* top = grid->itemAt(vis.topLeft())) first = grid->row(top);
  if (auto* bottom = grid->itemAt(vis.bottomRight())) last = grid->row(bottom);
  if (first < 0 || last < 0) {
    first = 0;
    last = grid->count() - 1;
  } else {
    first = std::max(0, first - 4);
    last = std::min(grid->count() - 1, last + 4);
  }
  for (int row = first; row <= last; ++row) {
    QListWidgetItem* item = grid->item(row);
    if (!item || !grid->visualItemRect(item).intersects(vis)) continue;
    const int gi = item->data(Qt::UserRole).toInt();
    if (gi < 0 || gi >= groups_.size() || groups_[gi].paths.isEmpty()) continue;
    const QString& rep = groups_[gi].paths.front();
    const QString key = rep + '|' + QString::number(iconSize.width()) + 'x' + QString::number(iconSize.height());
    auto tc = thumbCache_.constFind(key);
    if (tc != thumbCache_.cend()) {
      if (item->icon().cacheKey() != tc.value().cacheKey()) {
        item->setIcon(tc.value());
        changed = true;
      }
      ++thumbStatMem_;
    } else {
      fileThumb(rep, iconSize); // miss: async request pump (no placeholder paint)
    }
  }
  if (changed && !thumbLayoutPending_) {
    thumbLayoutPending_ = true;
    QTimer::singleShot(0, this, [this] { flushThumbLayout(); });
  }
  // 0.9.4.121: the file grid used to rely on the single request fired at row
  // creation. A lost answer (busy backend at scan start, a restart-cleared
  // pending map, a cap refusal) then left a permanent placeholder while the
  // middle healed through the pump above - exactly the field report where
  // only the representative thumbnail survived (it is re-asked through the
  // middle rows too; other members are not). Re-ask visible file rows and
  // the selected preview with the same dedup discipline (fileThumb skips
  // in-flight duplicates), bounded to the visible window exactly like above.
  if (grid_ && grid_->isVisible() && currentGroup_ >= 0) {
    const QRect fvis = grid_->viewport()->rect();
    const QSize fsize = grid_->iconSize();
    if (fsize.isValid() && fsize.width() > 0 && fsize.height() > 0) {
      int ffirst = -1, flast = -1;
      if (auto* top = grid_->itemAt(fvis.topLeft())) ffirst = grid_->row(top);
      if (auto* bottom = grid_->itemAt(fvis.bottomRight())) flast = grid_->row(bottom);
      if (ffirst < 0 || flast < 0) {
        ffirst = 0;
        flast = grid_->count() - 1;
      } else {
        ffirst = std::max(0, ffirst - 4);
        flast = std::min(grid_->count() - 1, flast + 4);
      }
      bool fileChanged = false;
      for (int row = ffirst; row <= flast; ++row) {
        QListWidgetItem* item = grid_->item(row);
        if (!item || !grid_->visualItemRect(item).intersects(fvis)) continue;
        const QString p = item->data(Qt::UserRole).toString();
        if (p.isEmpty()) continue;
        const QString key = p + '|' + QString::number(fsize.width()) + 'x' + QString::number(fsize.height());
        auto tc = thumbCache_.constFind(key);
        if (tc != thumbCache_.cend()) {
          if (item->icon().cacheKey() != tc.value().cacheKey()) {
            item->setIcon(tc.value());
            fileChanged = true;
          }
          ++thumbStatMem_;
        } else {
          fileThumb(p, fsize); // miss: async request pump (no placeholder paint)
        }
      }
      // Mirror onThumbReady's direct pass for the file grid (it does not use
      // the coalesced middle-grid layout).
      if (fileChanged) grid_->doItemsLayout();
    }
  }
  // The selected preview has the same single-shot exposure (requested once in
  // refreshDetail). Re-ask it here; the fixed 220x190 size matches the preview
  // label, the arrival painter, and the detail builder.
  if (!currentFile_.isEmpty()) fileThumb(currentFile_, QSize(220, 190));
}
void MainWindow::updateGroupFoot() {
  // "전체 619 · 선택 116": total groups vs the 1-based selected group.
  // The old "619: 116" left the second number unexplained.
  const QString sel = currentGroup_ >= 0 ? QString::number(currentGroup_ + 1) : "-";
  groupFoot_->setText(QString("%1 %2 · %3 %4").arg(trStr(lang(), "groupsTotal")).arg(groups_.size())
                                                 .arg(trStr(lang(), "groupSel")).arg(sel));
}
void MainWindow::rebuildGroups() {
  QString selectedPath;
  if (currentGroup_ >= 0 && currentGroup_ < groups_.size() && !groups_[currentGroup_].paths.isEmpty())
    selectedPath = groups_[currentGroup_].paths.front();
  QHash<QString, QStringList> buckets;
  for (auto it = pathParent_.cbegin(); it != pathParent_.cend(); ++it) buckets[findRoot(it.key())] << it.key();
  groups_.clear(); pathGroup_.clear();
  for (auto it = buckets.cbegin(); it != buckets.cend(); ++it) {
    if (it.value().size() < 2) continue;
    DupGroup g; g.paths = it.value();
    std::sort(g.paths.begin(), g.paths.end(),
              [this](const QString& a, const QString& b) { return bestPct_.value(a, 0) > bestPct_.value(b, 0); });
    for (int i = 0; i < g.paths.size();) {
      int j = i + 1;
      while (j < g.paths.size() && bestPct_.value(g.paths[j], 0) == bestPct_.value(g.paths[i], 0)) ++j;
      if (j - i > 1) {
        QHash<QString,qulonglong> pix, siz;
        for (int k = i; k < j; ++k) { pix[g.paths[k]] = filePixels(g.paths[k]); siz[g.paths[k]] = fileSizeCached(g.paths[k]); }
        QStringList tied = g.paths.mid(i, j - i);
        sortTiedReferencePaths(tied, pix, siz);
        for (int k = i; k < j; ++k) g.paths[k] = tied[k - i];
      }
      i = j;
    }
    g.best = 0; g.kind = pathKind_.value(g.paths[0], 1);
    for (const auto& p : g.paths) { g.pct[p] = bestPct_.value(p, 0); g.best = std::max(g.best, g.pct[p]); }
    g.root = it.key();
    groups_.push_back(g);
  }
  const int sortMode = sortBox_->currentIndex();
  // 0.9.4.139: direction combo index 0 = descending, 1 = ascending. The path
  // stays the ascending tie-break so equal metrics keep a stable order.
  const bool sortDesc = !sortDirBox_ || sortDirBox_->currentIndex() == 0;
  const auto groupPath = [](const DupGroup& g) {
    return g.paths.isEmpty() ? QString() : g.paths.front();
  };
  if (sortMode == kSortSize) {
    // Group size = number of files in the duplicate group (largest first by
    // default), the order added in this build.
    std::sort(groups_.begin(), groups_.end(), [&](const DupGroup& a, const DupGroup& b) {
      if (a.paths.size() != b.paths.size())
        return sortDesc ? a.paths.size() > b.paths.size() : a.paths.size() < b.paths.size();
      return groupPath(a) < groupPath(b);
    });
  } else if (sortMode == kSortName) {
    std::sort(groups_.begin(), groups_.end(), [&](const DupGroup& a, const DupGroup& b) {
      return sortDesc ? groupPath(b) < groupPath(a) : groupPath(a) < groupPath(b);
    });
  } else if (sortMode == kSortSim) {
    std::sort(groups_.begin(), groups_.end(), [&](const DupGroup& a, const DupGroup& b) {
      if (a.best != b.best) return sortDesc ? a.best > b.best : a.best < b.best;
      return groupPath(a) < groupPath(b);
    });
  } else {
    // kSortNone: stable append order. Groups already listed keep their position;
    // new groups (or ones whose union-find root changed by a merge) are appended
    // at the bottom in root order. This is what lets the middle pane extend
    // without recreating existing rows during a scan.
    QSet<QString> present;
    for (const auto& g : groups_) present.insert(g.root);
    QStringList order;
    for (const QString& r : noneOrder_) if (present.contains(r)) order << r;
    QSet<QString> kept;
    for (const QString& r : order) kept.insert(r);
    QStringList fresh;
    for (const auto& g : groups_) if (!kept.contains(g.root)) fresh << g.root;
    std::sort(fresh.begin(), fresh.end());
    order += fresh;
    QHash<QString,int> idx;
    for (int i = 0; i < groups_.size(); ++i) idx[groups_[i].root] = i;
    QVector<DupGroup> reordered;
    reordered.reserve(order.size());
    for (const QString& r : order) reordered.push_back(groups_[idx.value(r)]);
    groups_ = std::move(reordered);
  }
  // Record the resulting order as the None-mode baseline (also after a sorted
  // fill) so switching to None later preserves the visible order.
  noneOrder_.clear();
  for (const auto& g : groups_) noneOrder_ << g.root;
  for (int i = 0; i < groups_.size(); ++i)
    for (const auto& p : groups_[i].paths) pathGroup_[p] = i;
  if (!selectedPath.isEmpty()) {
    currentGroup_ = pathGroup_.value(selectedPath, -1);
    if (currentGroup_ < 0) currentFile_.clear();
  } else if (currentGroup_ >= groups_.size()) {
    currentGroup_ = -1; currentFile_.clear();
  }
  // Single clearing point: every rebuild consumes all streamed matches known
  // so far into groups_, so pending data no longer exists afterwards. Direct
  // finish paths (scanFinished/scanFailed) benefit automatically — without
  // this a final drained batch would keep every post-scan tick rebuilding.
  groupsDirty_ = false;
}
QString MainWindow::fmtSize(qulonglong n) const {
  if (n < 1024) return QString("%1 B").arg(n);
  if (n < 1024ULL * 1024) return QString("%1 KB").arg(n / 1024.0, 0, 'f', 1);
  if (n < 1024ULL * 1024 * 1024) return QString("%1 MB").arg(n / (1024.0 * 1024), 0, 'f', 2);
  return QString("%1 GB").arg(n / (1024.0 * 1024 * 1024), 0, 'f', 2);
}
double MainWindow::pathBest(const QString& p) const { return bestPct_.value(p, 0.0); }
qulonglong MainWindow::filePixels(const QString& p) const {
  // 0.9.4.117: cached-only. This runs once per path inside the tie sorting of
  // a full group rebuild; the old requesting variant issued a backend
  // FILE_META request per path (2,000+ requests at the user's group count,
  // with an ffprobe fallback per missing file on the loopback engine) and
  // turned the rebuild into minutes of GUI stall. Tie ordering now uses
  // whatever resolution is already cached and degrades to size order.
  const QString r = resCache_.value(p, QStringLiteral("-"));
  const int x = r.indexOf('x');
  if (x <= 0) return 0;
  bool okW = false, okH = false;
  const qulonglong w = r.left(x).toULongLong(&okW);
  const qulonglong h = r.mid(x + 1).toULongLong(&okH);
  if (!okW || !okH) return 0;
  return w * h;
}
void MainWindow::sortTiedReferencePaths(QStringList& paths, const QHash<QString,qulonglong>& pixels, const QHash<QString,qulonglong>& sizes) {
  std::sort(paths.begin(), paths.end(), [&](const QString& a, const QString& b) {
    const qulonglong pa = pixels.value(a, 0), pb = pixels.value(b, 0);
    if (pa != pb) return pa > pb;
    return sizes.value(a, 0) > sizes.value(b, 0);
  });
}
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
  connect(tree, &QWidget::customContextMenuRequested, this, [this, tree](const QPoint& p) {
    if (QTreeWidgetItem* it = tree->itemAt(p)) tree->setCurrentItem(it);
    showGroupMenu(tree->mapToGlobal(p));
  });
  connect(grid, &QListWidget::currentItemChanged, this, &MainWindow::gridSelected);
  connect(grid, &QListWidget::itemChanged, this, &MainWindow::gridCheckChanged);
  grid->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(grid, &QWidget::customContextMenuRequested, this, [this, grid](const QPoint& p) {
    if (QListWidgetItem* it = grid->itemAt(p)) grid->setCurrentItem(it);
    showGroupMenu(grid->mapToGlobal(p));
  });
  // Scroll-regression gate (0.9.4.61, extended 0.9.4.75): full list refills never run while the
  // user holds a scrollbar, and a short cooldown follows wheel/keyboard
  // navigation. Input is never blocked — only the rebuild is deferred.
  // Programmatic moves (scrollToItem/setValue) do not emit sliderPressed, so
  // anchor restoration and view switches never trip the gate.
  auto hookBar = [this](QAbstractSlider* bar) {
    if (!bar) return;
    connect(bar, &QAbstractSlider::sliderPressed, this, [this] { sliderHeld_ = true; noteUserScroll(); });
    connect(bar, &QAbstractSlider::sliderReleased, this, [this] { sliderHeld_ = false; noteUserScroll(); });
    // Track clicks, gutter pages, and arrow buttons do not emit sliderPressed
    // while they move the view, so they also arm the cooldown directly. These
    // signals come only from user interaction; programmatic setValue calls do
    // not emit them.
    connect(bar, &QAbstractSlider::sliderMoved, this, [this](int) { noteUserScroll(); });
    connect(bar, &QAbstractSlider::actionTriggered, this, [this](int) { noteUserScroll(); });
    // Wheel and keyboard input can arrive with the scrollbar itself focused
    // (including wheel events over the scrollbar), so filter it too.
    bar->installEventFilter(this);
  };
  hookBar(tree->verticalScrollBar());
  hookBar(grid->verticalScrollBar());
  // Keys go to the view widget, wheel to its viewport: watch both so neither
  // navigation path misses the gate feed.
  tree->installEventFilter(this);
  grid->installEventFilter(this);
  tree->viewport()->installEventFilter(this);
  grid->viewport()->installEventFilter(this);
}
void MainWindow::gridSelected(QListWidgetItem* cur, QListWidgetItem*) {
  if (!cur) return;
  const int gi = cur->data(Qt::UserRole).toInt();
  if (gi < 0 || gi >= groups_.size()) return;
  currentGroup_ = gi; currentFile_.clear();
  refreshFileViews(); refreshDetail(); updateStatusCounts();
  updateGroupFoot();
}
void MainWindow::gridCheckChanged(QListWidgetItem* it) {
  if (!it) return;
  setGroupMarked(it->data(Qt::UserRole).toInt(), it->checkState() == Qt::Checked);
}
void MainWindow::applyExecutionMode() {
  // The upper mode drives GPU availability; the CPU detail (preset_/cpu_) is
  // unchanged. CPU-only forces the GPU off; AUTO and GPU-max keep the adaptive
  // GPU path available. The GPU switch now lives in Settings > General
  // (ui/gpuEnabled); the forcing below mirrors the old toolbar checkbox,
  // including its overwrite of the stored switch.
  const bool cpuOnly = (executionStrategy() == ExecutionResourceStrategy::CpuOnly);
  if (cpuOnly && gpuOn_) {
    gpuOn_ = false; QSettings().setValue("ui/gpuEnabled", false);
  } else if (!cpuOnly && !gpuOn_) {
    gpuOn_ = true; QSettings().setValue("ui/gpuEnabled", true);
  }
  // Refresh the stored policy so monitor/scan pick up the GPU decision.
  // Re-apply through the current preset so the user's CPU budget preset is kept.
  resourceChanged(preset_->currentIndex());
}

void MainWindow::updateKindSelection() {
  // 0.9.4.99: single-select dropdown (photos/index 0, videos/1, all/2).
  // Exactly one scope is always active by construction, so the old both-off
  // refusal is gone. Persists ui/kindMask with the same bit values (1/2/3).
  if (!kindBox_) return;
  static const int masks[3] = {1, 2, 3};
  const int idx = std::clamp(kindBox_->currentIndex(), 0, 2);
  QSettings().setValue("ui/kindMask", masks[idx]);
}
void MainWindow::groupViewChanged(int idx) {
  if (idx < 0) idx = 0;
  if (idx > 6) idx = 5;
  QSettings().setValue("ui/groupView", idx);
  if (viewBox_ && viewBox_->currentIndex() != idx) {
    viewBox_->blockSignals(true); viewBox_->setCurrentIndex(idx); viewBox_->blockSignals(false);
  }
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
    // (0.9.4.66) Delegate/size swaps must take effect immediately: force the
    // pass explicitly. This never rebuilds items (see fillPair prohibition)
    // and never touches scroll.
    // 0.9.4.110: Qt's ListMode sets the vertical scrollbar's singleStep
    // explicitly (setSingleStep(1), item units) and that public call latches
    // viewMayChangeSingleStep=false; the icon modes rely on the view's
    // automatic step (one cell per notch), so after List -> icon the wheel
    // kept stepping 1px ("wheel speed never returns"). -1 re-enables the
    // automatic step and the layout pass below re-pushes it for the new mode.
    grid->verticalScrollBar()->setSingleStep(-1);
    grid->horizontalScrollBar()->setSingleStep(-1);
    grid->doItemsLayout();
    // 0.9.4.109: re-apply the icons for the new size right away. Items whose
    // thumb at this size is already in the memory cache get it back here
    // (the catch-up pump used to drop cache hits, so they kept the previous
    // mode's pixmap while the cells were recomputed -> 0.9.4.66 overlap);
    // misses are requested asynchronously and paint through onThumbReady.
    thumbCatchUpVisible();
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
  // 0.9.4.99: checking a group marks every file except the reference image
  // (paths[0]), so move/recycle act on the rest. Unchecking clears the whole
  // group (cancel semantics, including a manually marked reference).
  if (on) {
    const auto& paths = groups_[gi].paths;
    for (int k = 1; k < paths.size(); ++k) marked_.insert(paths[k]);
  } else {
    for (const auto& p : groups_[gi].paths) marked_.remove(p);
  }
  QTreeWidget* trees[2] = {imgTree_, vidTree_};
  QListWidget* grids[2] = {imgGrid_, vidGrid_};
  for (auto* t : trees) t->blockSignals(true);
  for (auto* g : grids) g->blockSignals(true);
  for (auto* t : trees)
    for (int r = 0; r < t->topLevelItemCount(); ++r) {
      auto* it = t->topLevelItem(r);
      it->setCheckState(0, groupMarkState(it->data(0, Qt::UserRole).toInt()));
    }
  for (auto* g : grids)
    for (int r = 0; r < g->count(); ++r) {
      auto* it = g->item(r);
      it->setCheckState(groupMarkState(it->data(Qt::UserRole).toInt()));
    }
  for (auto* t : trees) t->blockSignals(false);
  for (auto* g : grids) g->blockSignals(false);
  refreshFileViews(); updateStatusCounts();
}
// 0.9.4.99: the group checkbox selects every file except the reference
// (paths[0]), so "fully marked" means all non-reference files are marked.
Qt::CheckState MainWindow::groupMarkState(int gi) const {
  if (gi < 0 || gi >= groups_.size() || groups_[gi].paths.size() < 2) return Qt::Unchecked;
  int n = 0;
  const auto& paths = groups_[gi].paths;
  for (int k = 1; k < paths.size(); ++k) if (marked_.contains(paths[k])) ++n;
  if (n == 0) return Qt::Unchecked;
  return n == paths.size() - 1 ? Qt::Checked : Qt::PartiallyChecked;
}
QString MainWindow::groupSig(const DupGroup& g) const {
  // Signature used to detect a changed existing row during an append refresh:
  // the file count and best similarity capture everything the row shows.
  return QString::number(g.paths.size()) + ':' + QString::number(g.best, 'f', 1);
}
void MainWindow::addGroupItems(QTreeWidget* tree, QListWidget* grid, int i, int wantKind, bool syncSel) {
  // 0.9.4.105 (D): every row added here is part of a bulk fill; its icon is
  // served from cache or drawn as a placeholder (no backend request). The
  // fill's caller hands the visible viewport to thumbCatchUpVisible() when
  // the fill completes, so no row is left without a request path.
  const bool bulkPrev = thumbBulkFill_;
  thumbBulkFill_ = true;
  const auto& g = groups_[i];
  qulonglong bytes = 0;
  for (const auto& p : g.paths) bytes += fileSizeCached(p);
  auto* it = new QTreeWidgetItem(tree);
  it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
  it->setCheckState(0, groupMarkState(i));
  it->setText(0, QString("%1 %2").arg(trStr(lang(), "group")).arg(i + 1));
  it->setText(1, QString("%1 %2").arg(g.paths.size()).arg(trStr(lang(), "files")));
  // Pairs = file count choose 2: direct pairwise comparisons a complete
  // group implies. Transitive pairs may exceed verified matches — documented.
  const qulonglong pairs = (qulonglong)g.paths.size() * ((qulonglong)g.paths.size() - 1) / 2;
  it->setText(2, QString("%1 %2").arg(pairs).arg(trStr(lang(), "pairs")));
  it->setText(3, QString("%1%").arg(g.best, 0, 'f', 1));
  it->setText(4, fmtSize(bytes));
  it->setData(0, Qt::UserRole, i);
  if (syncSel && i == currentGroup_) tree->setCurrentItem(it);
  const QString rep = g.paths.isEmpty() ? QString() : g.paths[0];
  auto* li = new QListWidgetItem(fileThumb(rep, grid->iconSize()),
                                 QString("%1 %2\n%3 %4 · %5 %6 · %7%\n%8")
                                     .arg(trStr(lang(), "group")).arg(i + 1)
                                     .arg(g.paths.size()).arg(trStr(lang(), "files"))
                                     .arg(pairs).arg(trStr(lang(), "pairs")).arg(g.best, 0, 'f', 1)
                                     .arg(fmtSize(bytes)));
  li->setFlags(li->flags() | Qt::ItemIsUserCheckable);
  li->setCheckState(groupMarkState(i));
  li->setData(Qt::UserRole, i);
  li->setToolTip(rep);
  grid->addItem(li);
  if (syncSel && i == currentGroup_) grid->setCurrentItem(li);
  thumbBulkFill_ = bulkPrev;
}
void MainWindow::fillPair(QTreeWidget* tree, QListWidget* grid, int wantKind, bool syncSel, bool append) {
  // 0.9.4.117: a new fill (append, rebuild, tab switch, sort, search)
  // supersedes any deferred job; while one is mid-flight the pane is only
  // partially populated, so an append must not extend it - fall back to a
  // full (re)build below, which may defer again.
  constexpr int kDeferFillMin = 800;
  constexpr int kDeferAppendMin = 300;
  const bool hadPending = fillJob_ != nullptr;
  ++fillGen_;
  fillJob_.reset();
  if (hadPending) append = false;
  // Ordered rows for this kind, each with a signature. The append path extends
  // the existing pane; any structural change (merge/removal/order change) fails
  // the prefix check and falls back to a full rebuild below.
  const QString f = groupSearch_->text().trimmed().toLower();
  QVector<QPair<QString,QString>> cur;
  QVector<int> curIndex;
  for (int i = 0; i < groups_.size(); ++i) {
    const auto& g = groups_[i];
    if (g.kind != wantKind) continue;
    if (!f.isEmpty()) {
      bool hit = false;
      for (const auto& p : g.paths) if (p.toLower().contains(f)) { hit = true; break; }
      if (!hit) continue;
    }
    cur.append({g.root, groupSig(g)});
    curIndex.append(i);
  }
  QVector<QPair<QString,QString>>& shown = (wantKind == 2) ? vidShown_ : imgShown_;
  bool canAppend = append && cur.size() >= shown.size();
  if (canAppend) {
    for (int k = 0; k < shown.size(); ++k)
      if (shown[k] != cur[k]) { canAppend = false; break; }
  }
  if (canAppend) {
    if (cur.size() == shown.size()) return; // nothing new: leave the pane untouched
    // Append only the new rows at the bottom. Existing rows and their
    // thumbnails are never recreated, so the scrolled view stays put.
    const int newRows = cur.size() - shown.size();
    // 0.9.4.118: a large append (first scan without a prior Load, or a big
    // streamed batch) defers through the same chunked job; the pane keeps its
    // existing rows (no clear), so the UI stays responsive like the full path.
    if (newRows > kDeferAppendMin) {
      auto job = std::make_shared<GroupFillJob>();
      job->index = curIndex.mid(shown.size());
      job->shown = cur;
      job->wantKind = wantKind;
      job->syncSel = syncSel;
      job->append = true;
      job->tree = tree;
      job->grid = grid;
      job->t0.start();
      fillJob_ = job;
      const int gen = fillGen_;
      QTimer::singleShot(0, this, [this, job, gen] { continueGroupFill(job, gen); });
      return;
    }
    tree->blockSignals(true); grid->blockSignals(true);
    for (int k = shown.size(); k < cur.size(); ++k) addGroupItems(tree, grid, curIndex[k], wantKind, syncSel);
    tree->resizeColumnToContents(0);
    tree->doItemsLayout();
    grid->doItemsLayout();
    tree->blockSignals(false); grid->blockSignals(false);
    shown = cur;
    // 0.9.4.105 (D): the appended rows were bulk-filled without requests;
    // hand the visible viewport to the catch-up pump.
    thumbCatchUpVisible();
    return;
  }
  // Full rebuild: clear and repopulate, restoring the scroll anchors.
  tree->blockSignals(true); grid->blockSignals(true);
  const int treePos = tree->verticalScrollBar() ? tree->verticalScrollBar()->value() : 0;
  const int gridPos = grid->verticalScrollBar() ? grid->verticalScrollBar()->value() : 0;
  QString treeAnchorPath;
  int treeAnchorContent = 0;
  QString gridAnchorPath;
  int gridAnchorContent = 0;
  captureFillAnchors(tree, grid, treePos, gridPos,
                     treeAnchorPath, treeAnchorContent, gridAnchorPath, gridAnchorContent);
  tree->clear(); grid->clear();
  // 0.9.4.117: a fill of thousands of rows must not run synchronously — the
  // field measured minutes of frozen UI for the user's 9,055-group index.
  // Large fills are deferred and completed in chunks across event-loop turns.
  if (curIndex.size() > kDeferFillMin) {
    auto job = std::make_shared<GroupFillJob>();
    job->index = curIndex;
    job->shown = cur;
    job->wantKind = wantKind;
    job->syncSel = syncSel;
    job->tree = tree;
    job->grid = grid;
    job->treePos = treePos;
    job->gridPos = gridPos;
    job->treeAnchorPath = treeAnchorPath;
    job->treeAnchorContent = treeAnchorContent;
    job->gridAnchorPath = gridAnchorPath;
    job->gridAnchorContent = gridAnchorContent;
    job->t0.start();
    fillJob_ = job;
    const int gen = fillGen_;
    tree->blockSignals(false); grid->blockSignals(false);
    QTimer::singleShot(0, this, [this, job, gen] { continueGroupFill(job, gen); });
    return;
  }
  // The middle grids use SinglePass (see construction), so the layout below
  // observes the final scrollbar maximum and the anchor restore is exact.
  // (Batched would collapse the maximum mid-rebuild and clamp deep offsets.)
  for (int k = 0; k < curIndex.size(); ++k) addGroupItems(tree, grid, curIndex[k], wantKind, syncSel);
  tree->resizeColumnToContents(0);
  // Settle the synchronous layout before measuring the rebuilt anchor
  // rectangles. Layout changes geometry; it does not recreate items or move
  // selection.
  tree->doItemsLayout();
  grid->doItemsLayout();
  restoreFillAnchors(tree, grid, treePos, gridPos,
                     treeAnchorPath, treeAnchorContent, gridAnchorPath, gridAnchorContent);
  shown = cur;
  tree->blockSignals(false); grid->blockSignals(false);
  // 0.9.4.105 (D): the rebuilt rows were bulk-filled without requests; the
  // visible-only pump requests exactly what the user can see (0.9.4.104's
  // per-row request storm is gone).
  thumbCatchUpVisible();
}
// 0.9.4.117: shared by the synchronous and deferred full-fill paths.
void MainWindow::captureFillAnchors(QTreeWidget* tree, QListWidget* grid, int treePos, int gridPos,
                                    QString& treeAnchorPath, int& treeAnchorContent,
                                    QString& gridAnchorPath, int& gridAnchorContent) {
  // Rebuilding the widgets can make setCurrentItem() scroll to the selected
  // group (usually group 1) after the old pixel offset was captured. Preserve
  // the group at the viewport's top edge as a semantic anchor instead, along
  // with its exact content offset so the restored view does not drift by a
  // partial row/cell.
  if (auto* top = tree->itemAt(QPoint(2, 2))) {
    const int index = top->data(0, Qt::UserRole).toInt();
    if (index >= 0 && index < groups_.size() && !groups_[index].paths.isEmpty()) {
      treeAnchorPath = groups_[index].paths.front();
      treeAnchorContent = treePos + tree->visualItemRect(top).top();
    }
  }
  // Large icon cells wrap with spacing, so the tree-oriented (2,2) corner
  // can probe empty space. Probe the viewport center instead.
  const int viewportWidth = grid->viewport()->width();
  const int probeX = std::clamp(viewportWidth / 2, 2, std::max(2, viewportWidth - 1));
  if (auto* top = grid->itemAt(QPoint(probeX, 10))) {
    const int index = top->data(Qt::UserRole).toInt();
    if (index >= 0 && index < groups_.size() && !groups_[index].paths.isEmpty()) {
      gridAnchorPath = groups_[index].paths.front();
      gridAnchorContent = gridPos + grid->visualItemRect(top).top();
    }
  }
}
void MainWindow::restoreFillAnchors(QTreeWidget* tree, QListWidget* grid, int treePos, int gridPos,
                                    const QString& treeAnchorPath, int treeAnchorContent,
                                    const QString& gridAnchorPath, int gridAnchorContent) {
  bool restoredTree = false;
  if (!treeAnchorPath.isEmpty()) {
    for (int row = 0; row < tree->topLevelItemCount(); ++row) {
      auto* item = tree->topLevelItem(row);
      const int index = item->data(0, Qt::UserRole).toInt();
      if (index >= 0 && index < groups_.size() && groups_[index].paths.contains(treeAnchorPath)) {
        const QRect rect = tree->visualItemRect(item);
        if (rect.isValid() && tree->verticalScrollBar()) {
          auto* bar = tree->verticalScrollBar();
          bar->setValue(std::clamp(treeAnchorContent - rect.top(), 0, bar->maximum()));
          restoredTree = true;
        }
        break;
      }
    }
  }
  bool restoredGrid = false;
  if (!gridAnchorPath.isEmpty()) {
    for (int row = 0; row < grid->count(); ++row) {
      auto* item = grid->item(row);
      const int index = item->data(Qt::UserRole).toInt();
      if (index >= 0 && index < groups_.size() && groups_[index].paths.contains(gridAnchorPath)) {
        const QRect rect = grid->visualItemRect(item);
        if (rect.isValid() && grid->verticalScrollBar()) {
          auto* bar = grid->verticalScrollBar();
          bar->setValue(std::clamp(gridAnchorContent - rect.top(), 0, bar->maximum()));
          restoredGrid = true;
        }
        break;
      }
    }
  }
  if (!restoredTree && tree->verticalScrollBar()) tree->verticalScrollBar()->setValue(treePos);
  if (!restoredGrid && grid->verticalScrollBar()) grid->verticalScrollBar()->setValue(gridPos);
}
// 0.9.4.117: deferred large-fill continuation. One bounded chunk per
// event-loop turn keeps timers, paints and clicks alive while thousands of
// rows are added; a stale generation (a newer fill started) stops silently.
void MainWindow::continueGroupFill(const std::shared_ptr<GroupFillJob>& job, int gen) {
  if (!job || gen != fillGen_ || fillJob_ != job) return;
  constexpr int kChunk = 100; // ~1 ms/row x two panes: keeps each turn short
  QTreeWidget* tree = job->tree;
  QListWidget* grid = job->grid;
  const std::size_t total = (std::size_t)job->index.size();
  tree->blockSignals(true); grid->blockSignals(true);
  tree->setUpdatesEnabled(false); grid->setUpdatesEnabled(false);
  const std::size_t end = std::min(job->pos + (std::size_t)kChunk, total);
  for (std::size_t k = job->pos; k < end; ++k)
    addGroupItems(tree, grid, job->index[(int)k], job->wantKind, job->syncSel);
  job->pos = end;
  tree->setUpdatesEnabled(true); grid->setUpdatesEnabled(true);
  tree->blockSignals(false); grid->blockSignals(false);
  if (job->pos < job->index.size()) {
    QTimer::singleShot(0, this, [this, job, gen] { continueGroupFill(job, gen); });
    return;
  }
  tree->resizeColumnToContents(0);
  tree->doItemsLayout();
  grid->doItemsLayout();
  if (!job->append) {
    // Full rebuild: restore the pre-fill scroll anchors.
    restoreFillAnchors(tree, grid, job->treePos, job->gridPos,
                       job->treeAnchorPath, job->treeAnchorContent,
                       job->gridAnchorPath, job->gridAnchorContent);
  }
  // Append jobs keep the existing rows (no clear, no anchor restore).
  (job->wantKind == 2 ? vidShown_ : imgShown_) = job->shown;
  thumbCatchUpVisible();
  scanLog(QString("groupFill done rows=%1 ms=%2%3")
              .arg(job->index.size()).arg(job->t0.elapsed())
              .arg(job->append ? QStringLiteral(" append") : QString()));
  fillJob_.reset();
}
void MainWindow::refreshGroupList(bool append) {
  // Test-only instrumentation: every middle-pane refill (full rebuild or the
  // append-only scan path) passes through here (timer path and direct
  // user-action callers alike), while thumbnail-only catch-up never does. Lets
  // the regression test assert "catch-up ticks rebuild nothing, data changes
  // rebuild".
  ++fullRebuildCount_;
  // Coverage bookkeeping lives here for the same reason: any full fill
  // (streaming, finish, sort, search, tab) reflects all matches known, so a
  // post-scan tick must not rebuild again on a stale signature.
  lastFillSig_ = matchSeq_; lastFillMs_ = QDateTime::currentMSecsSinceEpoch();
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
  if (tab == 0) fillPair(imgTree_, imgGrid_, 1, true, append);
  else if (tab == 1) fillPair(vidTree_, vidGrid_, 2, true, append);
  updateGroupFoot();
  updateIgnoreTab();
}
void MainWindow::updateSortEnabled() {
  // 0.9.4.139: type + direction are usable on a group tab whenever no scan is
  // running (Load no longer locks them; a scan restores None at its end). The
  // direction combo is meaningless for the "None" (stored order) type.
  const int t = midTabs_ ? midTabs_->currentIndex() : 0;
  const bool on = (t == 0 || t == 1) && !scanning_;
  if (sortBox_) sortBox_->setEnabled(on);
  if (sortDirBox_)
    sortDirBox_->setEnabled(on && sortBox_ && sortBox_->currentIndex() != kSortNone);
}
void MainWindow::activateTab(int idx) {
  const bool res = (idx == 0 || idx == 1);
  updateSortEnabled(); viewBox_->setEnabled(res); groupSearch_->setEnabled(res);
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
  updateGroupFoot();
}
void MainWindow::groupSearchChanged(const QString&) { refreshGroupList(); }
// ------------------------------------------------------------ right pane: files + detail
// (P4: ffprobe dimension probing moved to src/file_meta.cpp with the rest of
// the Backend-owned decode chain. The GUI only renders EXIF text tags.)
QString MainWindow::fileResolution(const QString& path) const {
  auto it = resCache_.find(path);
  if (it != resCache_.cend()) return it.value();
  // P4: dimensions come from the backend (Type B, async) like thumbnails.
  // No WIC/FFmpeg/QImageReader/ffprobe probing here anymore (directive §3):
  // EXIF text tags below are string metadata, not pixel decode, and stay.
  requestFileMeta(path);
  return QStringLiteral("-");
}
void MainWindow::requestFileMeta(const QString& path) const {
  if (path.isEmpty() || !backend_) return;
  // 0.9.4.105: exactly one request per path per scan/load generation. Without
  // the asked-set, an answer carrying dimensions but no duration (every image
  // file) left fileDur_ empty, so the refreshDetail it triggered asked again;
  // the endless request/rebuild ping-pong deleted and recreated the detail
  // rows continuously (the "detail text appears then disappears/blanks"
  // defect). The set is cleared together with resCache_ at scan/load start.
  if (fileMetaAsked_.contains(path)) return;
  fileMetaAsked_.insert(path);
  ++fileMetaRequestCount_;
  const quint64 id = ++fileMetaRequestId_;
  fileMetaPending_[id] = path;
  backend_->requestFileMeta(path, id);
}
void MainWindow::onFileMetaReady(quint64 requestId, const FileMetaResult& meta) {
  auto it = fileMetaPending_.find(requestId);
  if (it == fileMetaPending_.end()) return; // unknown or duplicate: drop
  const QString path = it.value();
  fileMetaPending_.erase(it);
  if (!meta.ok || meta.path != path) return; // stale or failed: drop
  // 0.9.4.105: rebuild the detail only when the answer changed something it
  // displays. An image answer (dims, no duration) now refreshes once and the
  // asked-set prevents the follow-up request; a duration answer updates the
  // duration row. List rows refresh in place either way.
  bool changed = false;
  if (meta.width > 0 && meta.height > 0) {
    const QString r = QString("%1x%2").arg(meta.width).arg(meta.height);
    if (resCache_.value(path) != r) { resCache_[path] = r; changed = true; }
  }
  if (meta.duration > 0 && fileDur_.value(path, 0.0) != meta.duration) {
    fileDur_[path] = meta.duration;
    changed = true;
  }
  refreshFileMetaRow(path);
  if (changed && currentFile_ == path) refreshDetail();
}
// 0.9.4.138: the preview grid's cell width must follow the ICON size, not the
// widest text line. A full "393.0 KB · 2048x1798" line used to pin every cell
// at ~150 px, so shrinking the preview below 128 px kept the 3-column layout.
// Each line is now fitted to the icon width with priority fallbacks:
//   name: middle-elided (extension stays readable),
//   size · resolution: full -> resolution only -> middle-elided resolution,
//   similarity: full ("100% · 동일 내용") -> "100%" -> middle-elided value.
// A tooltip still carries the full path, and the list view keeps full columns.
QString MainWindow::fileGridItemText(const QString& path, bool ref, double pct) const {
  if (!grid_) return QString();
  const QFileInfo fi(path);
  const int textW = std::max(48, grid_->iconSize().width());
  const QFontMetrics fm(grid_->font());
  const auto fits = [&](const QString& s) { return fm.horizontalAdvance(s) <= textW; };
  const auto elide = [&](const QString& s, Qt::TextElideMode m) {
    return fm.elidedText(s, m, textW);
  };
  QString meta = fmtSize(fi.size()) + QStringLiteral(" · ") + fileResolution(path);
  if (!fits(meta)) {
    const QString res = fileResolution(path);
    meta = fits(res) ? res : elide(res, Qt::ElideMiddle);
  }
  QString sim = pctText(pct, ref, lang(), contentFlag_.value(path, 0));
  if (!fits(sim)) {
    const QString pctOnly = QString("%1%").arg(pct, 0, 'f', 1);
    sim = fits(pctOnly) ? pctOnly : elide(pctOnly, Qt::ElideMiddle);
  }
  const QString name = fm.elidedText(fi.fileName(), Qt::ElideMiddle, textW);
  return name + "\n" + meta + "\n" + sim;
}
void MainWindow::refreshFileMetaRow(const QString& path) {
  // In-place text refresh for every surface showing this path's resolution.
  // Widget identity (and therefore selection, scroll, and check state) is
  // preserved — unlike refreshFileViews(), which recreates everything.
  if (currentGroup_ < 0 || currentGroup_ >= groups_.size()) return;
  const auto& g = groups_[currentGroup_];
  const int gi = g.paths.indexOf(path);
  if (gi < 0) return;
  const bool ref = (gi == 0);
  const double pct = ref ? 100.0 : g.pct.value(path, 0);
  QFileInfo fi(path);
  for (int r = 0; r < grid_->count(); ++r) {
    auto* item = grid_->item(r);
    if (!item || item->data(Qt::UserRole).toString() != path) continue;
    item->setText(fileGridItemText(path, ref, pct));
  }
  for (int r = 0; r < list_->topLevelItemCount(); ++r) {
    auto* item = list_->topLevelItem(r);
    if (!item || item->data(1, Qt::UserRole).toString() != path) continue;
    item->setText(3, fileResolution(path));
  }
}
// (P3: shell thumbnail fast lane removed with the GUI decode paths. The
// Backend owns every decoder now; the GUI shows placeholders until the
// bounded THUMBNAIL arrives.)
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
// 0.9.4.104 (B): batch drop — one sweep over the cache for the whole gone
// set. The per-file version scanned up to 3000 entries per deleted file
// (O(3000xN) QString work on the GUI thread). Keys are "path|WxH" ('|'
// cannot appear in Windows paths); a bare "path" key predates sized
// variants and is dropped too.
void MainWindow::dropThumbCache(const QSet<QString>& paths) {
  if (paths.isEmpty()) return;
  for (auto it = thumbCache_.begin(); it != thumbCache_.end();) {
    const QString& k = it.key();
    const int bar = k.indexOf('|');
    const QString base = (bar < 0) ? k : k.left(bar);
    if (paths.contains(base)) it = thumbCache_.erase(it);
    else ++it;
  }
}
QIcon MainWindow::fileThumb(const QString& path, const QSize& size, bool /*bypassBudget*/) const {
  // 0.9.4.97: never request art no surface can show. A grid whose iconSize
  // was never set (e.g. the hidden middle grid in details/tree view) would
  // otherwise burn backend WIC decodes whose arrivals normalize to null and
  // can never paint. The visible grid re-requests through fill/catch-up.
  if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
    ++thumbStatPlace_;
    return placeholderIcon(path);
  }
  // Cache key carries the requested size: cells follow the delivered pixmap
  // size (probed 90x91..262x283 for one view), so sharing one pixmap across
  // sizes would keep cells uneven.
  const QString key = path + '|' + QString::number(size.width()) + 'x' + QString::number(size.height());
  auto tc = thumbCache_.find(key);
  if (tc != thumbCache_.cend()) { ++thumbStatMem_; return tc.value(); }
  // P3: a miss goes to the backend (Type B, async). No disk/shell/decode
  // happens here anymore (G1.5): the backend owns the disk cache and every
  // decoder and answers with a bounded THUMBNAIL; the arrival paints through
  // onThumbReady. Record the pending request so it paints exactly the
  // requested size (stale-drop); cap the map so a lost backend cannot grow
  // it without bound.
  // 0.9.4.105 (D): a bulk fill (middle-list rebuild/append) must not request
  // art for every row. One rebuild covers thousands of rows; thousands of
  // simultaneous requests overflowed the capped pending map, so in-flight
  // answers arrived as "unknown" and were dropped — the storm left permanent
  // placeholders exactly on the rows the user could see. Bulk fills serve
  // cache hits/placeholders only; the visible-only catch-up pump (run right
  // after every fill and on idle ticks) requests what is actually visible.
  if (thumbBulkFill_) { ++thumbStatPlace_; return placeholderIcon(path); }
  if (backend_) {
    // Visible-item catch-up can revisit the same miss on consecutive ticks
    // before the backend answers. Re-issuing each miss would queue duplicate
    // backend thumbnail work and duplicate arrival layouts; an identical
    // in-flight request already covers this size.
    for (auto it = thumbPendingReq_.cbegin(); it != thumbPendingReq_.cend(); ++it) {
      if (it->path == path && it->size == size) {
        ++thumbStatPlace_;
        return placeholderIcon(path);
      }
    }
    // 0.9.4.105 (D): at the cap, refuse the new request instead of evicting
    // the oldest entry. Eviction dropped the tracking row of an in-flight
    // answer; the answer then arrived as unknown and was discarded, and the
    // cell kept its placeholder until something happened to re-ask. A refused
    // request costs nothing: the cell is still a cache miss, so the next
    // visible catch-up tick retries it once the backlog drains.
    if (thumbPendingReq_.size() >= 2000) { ++thumbStatPlace_; return placeholderIcon(path); }
    const quint64 id = ++thumbRequestId_;
    thumbPendingReq_[id] = {path, size};
    ++thumbRequestCount_;
    backend_->requestThumb(path, size, isVideoExt(path), id);
  }
  ++thumbStatPlace_;
  return placeholderIcon(path);
}
void MainWindow::onThumbReady(quint64 requestId, const ThumbResult& thumb) {
  auto it = thumbPendingReq_.find(requestId);
  if (it == thumbPendingReq_.end()) return; // unknown or duplicate: drop
  const QString path = it->path;
  const QSize size = it->size;
  thumbPendingReq_.erase(it);
  if (!thumb.ok) return;
  const QImage im(reinterpret_cast<const uchar*>(thumb.rgba.constData()),
                  thumb.width, thumb.height, thumb.width * 4, QImage::Format_ARGB32);
  if (im.isNull()) return;
  // Normalize to the exact requested rect: identical cells regardless of
  // source aspect/size (Explorer-like uniform grid), never distorted.
  const QIcon ic = QIcon(squareFittedPixmap(QPixmap::fromImage(im.copy()), size));
  thumbCache_[path + '|' + QString::number(size.width()) + 'x' + QString::number(size.height())] = ic;
  if (thumbCache_.size() > 3000) {
    auto jt = thumbCache_.begin();
    for (int n = 0; n < 750 && jt != thumbCache_.end(); ++n) jt = thumbCache_.erase(jt);
  }
  ++thumbStatBackend_;
  // Stale-drop + paint (0.9.4.66 pattern: a layout pass must follow icon
  // changes under uniformItemSizes, or art paints outside its cell).
  // Coalesced (0.9.4.75): arrivals only mark layout dirty, and the first
  // arrival in a burst schedules one zero-delay layout pass. A thumbnail
  // burst therefore costs one layout instead of one per arrival, while the
  // 0.9.4.66 guarantee still holds even when the periodic UI timer is
  // stopped: the event loop, not the next 600ms tick, runs the pass.
  bool painted = false;
  QListWidget* grids[2] = {imgGrid_, vidGrid_};
  for (auto* grid : grids) {
    if (!grid || grid->iconSize() != size) continue;
    bool gridPainted = false;
    for (int r = 0; r < grid->count(); ++r) {
      auto* item = grid->item(r);
      const int gi = item->data(Qt::UserRole).toInt();
      if (gi < 0 || gi >= groups_.size() || groups_[gi].paths.isEmpty()) continue;
      if (groups_[gi].paths.front() != path) continue;
      if (item->icon().cacheKey() == ic.cacheKey()) continue;
      item->setIcon(ic);
      gridPainted = true;
    }
    if (gridPainted) {
      if (!thumbLayoutPending_) {
        thumbLayoutPending_ = true;
        QTimer::singleShot(0, this, [this] { flushThumbLayout(); });
      }
      painted = true;
    }
  }
  if (currentFile_ == path && size == QSize(220, 190)) {
    preview_->setPixmap(ic.pixmap(QSize(220, 190)));
    painted = true;
  }
  // 0.9.4.97: the right file grid paints on arrival too. It used to heal
  // only on the next manual refresh (selection change), so after a Load —
  // with no clicks following — it kept placeholders forever. Same
  // stale-drop discipline as the middle grids (stored path + current icon
  // size); a group switch recreates the rows, so a late arrival can never
  // paint the wrong files.
  if (grid_ && grid_->iconSize() == size) {
    bool filePainted = false;
    for (int r = 0; r < grid_->count(); ++r) {
      auto* item = grid_->item(r);
      if (!item || item->data(Qt::UserRole).toString() != path) continue;
      if (item->icon().cacheKey() == ic.cacheKey()) continue;
      item->setIcon(ic);
      filePainted = true;
    }
    if (filePainted) {
      grid_->doItemsLayout();
      painted = true;
    }
  }
  // The right-pane file list recreates its items on every selection, so it
  // heals through the memory cache on next refresh without special handling.
  if (painted) ++thumbPaintedCount_;
}
void MainWindow::setViewMode(int i) {
  viewGrid_->setChecked(i == 0); viewList_->setChecked(i == 1);
  viewStack_->setCurrentIndex(i == 1 ? 1 : 0);
  // 0.9.4.136: the preview-size gauge only applies to the icon grid; in list
  // view it is disabled and renders gray (Qt's disabled style).
  zoom_->setEnabled(i == 0);
}
void MainWindow::zoomChanged(int v) { grid_->setIconSize(QSize(v, v)); refreshFileViews(); }
static QString pctText(double v, bool ref, UiLang l, int identity) {
  if (ref) return QString("100% · ") + trStr(l, "reference");
  QString t = QString("%1%").arg(v, 0, 'f', 1);
  // 0.9.4.106: a displayed 100.0% now says WHAT the 100 means — byte-identical
  // content (size + quick hash) or only a perceptually identical derivative
  // (watermark/sticker/re-encode/crop). The number itself is unchanged.
  if (v >= 99.9) {
    if (identity & 1) t += QString(" · ") + trStr(l, "identicalContent");
    else if (identity & 2) t += QString(" · ") + trStr(l, "perceptualMatch");
  }
  return t;
}
void MainWindow::refreshFileViews() {
  grid_->blockSignals(true); list_->blockSignals(true);
  // 0.9.4.94: recreating every row resets both scrollbars to 0. A refill that
  // shows the same group (streaming, finish, zoom) must keep the file
  // position; only a newly selected group starts at the top.
  const int gridPos = grid_->verticalScrollBar() ? grid_->verticalScrollBar()->value() : 0;
  const int listPos = list_->verticalScrollBar() ? list_->verticalScrollBar()->value() : 0;
  grid_->clear(); list_->clear();
  detailTitle_->setText(trStr(lang(), "group"));
  detailSim_->clear(); detailCount_->clear();
  if (currentGroup_ < 0 || currentGroup_ >= groups_.size()) {
    lastFileSig_.clear();
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
    // 0.9.4.138: every line is fitted to the icon width (see
    // fileGridItemText), so the cell narrows with the preview size and more
    // columns fit instead of the longest text line pinning the layout. The
    // full path stays in the tooltip.
    gi->setText(fileGridItemText(p, ref, pct));
    gi->setToolTip(p);
    grid_->addItem(gi);
    auto* li = new QTreeWidgetItem(list_);
    li->setCheckState(0, marked_.contains(p) ? Qt::Checked : Qt::Unchecked);
    li->setText(1, fi.fileName()); li->setData(1, Qt::UserRole, p);
    li->setText(2, pctText(pct, ref, lang(), contentFlag_.value(p, 0)));
    li->setText(3, fileResolution(p));
    li->setText(4, fi.suffix().toUpper());
    li->setText(5, fmtSize(fi.size()));
    li->setText(6, fi.lastModified().toString("yyyy-MM-dd hh:mm"));
    li->setToolTip(1, p);
  }
  list_->resizeColumnToContents(1);
  if (!currentFile_.isEmpty() && !g.paths.contains(currentFile_)) currentFile_.clear();
  grid_->doItemsLayout();
  list_->doItemsLayout();
  // Identity is the selected group plus the file-set shape. setValue clamps,
  // so a zoom-driven geometry change still lands on the nearest valid offset.
  const QString sig = QString::number(currentGroup_) + QLatin1Char('|') +
      QString::number(g.paths.size()) + QLatin1Char('|') +
      (g.paths.isEmpty() ? QString() : g.paths.first()) + QLatin1Char('|') +
      (g.paths.isEmpty() ? QString() : g.paths.last());
  if (sig == lastFileSig_) {
    if (grid_->verticalScrollBar()) grid_->verticalScrollBar()->setValue(gridPos);
    if (list_->verticalScrollBar()) list_->verticalScrollBar()->setValue(listPos);
  }
  lastFileSig_ = sig;
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
  // 0.9.4.101: every currentFile_ writer must refresh the detail pane.
  // fileActivated used to skip it, leaving stale "-" rows while a later
  // thumbnail arrival painted the preview (blank text + live preview).
  if (it) { showFileDetails(it->data(Qt::UserRole).toString()); openSelected(); }
}
void MainWindow::showFileDetails(const QString& path) {
  currentFile_ = path;
  refreshDetail();
}
void MainWindow::detailTabChanged(int) { refreshDetail(); }
void MainWindow::refreshDetail() {
  // QFormLayout::takeAt() orphans emptied rows: rowCount() grows without
  // bound and pushes live rows below the visible area (the "detail info
  // disappears after repeated check/uncheck/select" defect). Rebuild the
  // form object instead: harvesting widgets first, then letting the layout
  // destructor take its items. (removeRow() item ownership is version
  // sensitive; this path depends only on the stable QLayout contract:
  // the destructor deletes items, never widgets.)
  while (detailForm_->count() > 0) {
    if (QLayoutItem* it = detailForm_->takeAt(0)) delete it->widget();
  }
  if (QWidget* box = qobject_cast<QWidget*>(detailForm_->parent())) {
    delete detailForm_;
    detailForm_ = new QFormLayout;
    box->setLayout(detailForm_);
  }
  exifLabel_->clear(); simLabel_->clear(); simBar_->setValue(0); hashLabel_->clear();
  // Always show the field labels, even with no selection (values become "-").
  // The folder row looks like every other row (plain label, wraps to two
  // lines inside the capped pane) and shows the containing folder only — the
  // filename is already one row above, no need to repeat it.
  auto* pathLabel = new QLabel(this);
  pathLabel->setWordWrap(true);
  // 0.9.4.100: user-controlled text renders literally. QLabel auto-detects
  // rich text, so a filename carrying <>& would mangle or blank the row.
  pathLabel->setTextFormat(Qt::PlainText);
  // Ignored horizontal size: wrap inside the allotted width instead of
  // stretching the pane (the old sudden widen/narrow jumps).
  pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  // 0.9.4.121: every other value row follows the same wrap-inside discipline
  // (name/path above already do). Short values render exactly as before;
  // long ones can no longer force the right pane - and through it the middle
  // group list - wider on selection.
  auto wrapVal = [&](const QString& text) {
    auto* lb = new QLabel(text, this);
    lb->setWordWrap(true);
    lb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return lb;
  };
  if (currentFile_.isEmpty()) {
    preview_->setText("—");
    detailForm_->addRow(trStr(lang(), "fileName"), new QLabel("-", this));
    detailForm_->addRow(trStr(lang(), "fullPath"), pathLabel);
    detailForm_->addRow(trStr(lang(), "fileSize"), new QLabel("-", this));
    detailForm_->addRow(trStr(lang(), "format"), new QLabel("-", this));
    detailForm_->addRow(trStr(lang(), "modified"), new QLabel("-", this));
    detailForm_->addRow(trStr(lang(), "created"), new QLabel("-", this));
    detailForm_->addRow(trStr(lang(), "resolution"), new QLabel("-", this));
    detailForm_->addRow(trStr(lang(), "duration"), new QLabel("-", this));
    detailForm_->addRow(trStr(lang(), "similarity"), new QLabel("-", this));
    exifLabel_->setText("-"); simLabel_->setText("-"); hashLabel_->setText("-");
    return;
  }
  QFileInfo fi(currentFile_);
  preview_->setPixmap(fileThumb(currentFile_, QSize(220, 190), true).pixmap(220, 190));
  const bool ref = (currentGroup_ >= 0 && !groups_[currentGroup_].paths.isEmpty()
                    && groups_[currentGroup_].paths[0] == currentFile_);
  const double pct = ref ? 100.0 : pathBest(currentFile_);
  auto* nameLabel = new QLabel(fi.fileName(), this);
  nameLabel->setWordWrap(true);
  nameLabel->setTextFormat(Qt::PlainText); // see pathLabel: filenames are user-controlled
  nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  nameLabel->setToolTip(fi.fileName());
  // Same selection policy as the full-path value below: mouse + keyboard.
  nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  detailForm_->addRow(trStr(lang(), "fileName"), nameLabel);
  pathLabel->setText(QDir::toNativeSeparators(fi.absolutePath()));
  pathLabel->setToolTip(QDir::toNativeSeparators(fi.absolutePath()));
  detailForm_->addRow(trStr(lang(), "fullPath"), pathLabel);
  detailForm_->addRow(trStr(lang(), "fileSize"),
                      wrapVal(QString("%1 (%2 bytes)").arg(fmtSize(fi.size())).arg(fi.size())));
  detailForm_->addRow(trStr(lang(), "format"), wrapVal(fi.suffix().toUpper()));
  detailForm_->addRow(trStr(lang(), "modified"),
                      wrapVal(fi.lastModified().toString("yyyy-MM-dd hh:mm:ss")));
  detailForm_->addRow(trStr(lang(), "created"),
                      wrapVal(fi.birthTime().isValid() ? fi.birthTime().toString("yyyy-MM-dd hh:mm:ss") : "-"));
  detailForm_->addRow(trStr(lang(), "resolution"), wrapVal(fileResolution(currentFile_)));
    double dur = fileDur_.value(currentFile_, 0.0);
  if (dur <= 0 && backend_) {
    // Live or cancelled scans never reach onResults, so fileDur_ stays empty
    // and the row shows "-" until the backend answer arrives (Type B).
    for (const auto& f : backend_->requestFiles())
      if (f.path == currentFile_ && f.duration > 0) {
        dur = f.duration;
        fileDur_[currentFile_] = dur;
        break;
      }
    if (dur <= 0) requestFileMeta(currentFile_);
  }
  detailForm_->addRow(trStr(lang(), "duration"),
                      wrapVal(dur > 0 ? QString("%1:%2").arg(int(dur) / 60, 2, 10, QChar('0')).arg(int(dur) % 60, 2, 10, QChar('0')) : "-"));
  detailForm_->addRow(trStr(lang(), "similarity"),
                      wrapVal(pctText(pct, ref, lang(), contentFlag_.value(currentFile_, 0))));
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
    // 0.9.4.121: break opportunities for the eye. The layout is already
    // stabilized by the Ignored policy above; zero-width spaces after each
    // separator let the wrapped label fold a separator-less path instead of
    // painting past the pane edge. The stored path and tooltip keep the
    // exact text.
    QString shownPath = bestPath;
    shownPath.replace('/', QStringLiteral("/") + QChar(0x200B));
    shownPath.replace('\\', QStringLiteral("\\") + QChar(0x200B));
    simLabel_->setText(QString("%1\n%2\n%3%").arg(trStr(lang(), "bestMatch")).arg(shownPath).arg(best, 0, 'f', 1));
    simLabel_->setToolTip(bestPath);
    simBar_->setValue(int(std::clamp(best, 0.0, 100.0)));
  }
  // hash tab
  const QString fp = fileFp_.value(currentFile_);
  hashLabel_->setText(QString("%1\n%2").arg(trStr(lang(), "phash")).arg(fp.isEmpty() ? "-" : ("0x" + fp)));
  // 0.9.4.102: activate synchronously. The rebuilt form is virgin (never
  // laid out); under thumbnail burst traffic the deferred activation can be
  // postponed indefinitely, leaving valid rows unpainted (blank text beside
  // a live fixed-size preview). Middle/right grids force their own layout
  // passes for the same reason. Cheap (9 rows) and idempotent.
  detailForm_->activate();
  // 0.9.4.100: remote-diagnosis tripwire for "detail text all blank".
  // Healthy runs never log here: ~9 rows, a visible form box with width,
  // tab 0. Fires only on row-count divergence, a collapsed/invisible form
  // box, or an unexpected tab — exactly the states that render blank.
  // 0.9.4.102: gated on the main window being visible. A hidden window
  // (background fileMeta refresh while minimized/covered) reports vis=0
  // normally; logging that would drown the real signal.
  {
    QWidget* box = qobject_cast<QWidget*>(detailForm_->parent());
    const int rows = detailForm_->rowCount();
    const bool wild = rows < 0 || rows > 24;
    const bool narrow = box && box->isVisible() && box->width() < 60;
    const bool homeless = !box || !box->isVisible() || box->layout() != detailForm_;
    if ((wild || narrow || homeless) && isVisible())
      scanLog(QString("detailAnomaly file=%1 rows=%2 box=%3x%4 vis=%5 tab=%6").arg(
          currentFile_.isEmpty() ? QStringLiteral("<empty>") : QFileInfo(currentFile_).fileName()).arg(rows).arg(
          box ? box->width() : -1).arg(box ? box->height() : -1).arg(
          box ? (int)box->isVisible() : -1).arg(
          detailTabs_ ? detailTabs_->currentIndex() : -1));
  }
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
// 0.9.4.103: copy/cut/move/recycle prioritize checked files. When any box is
// checked, the action targets the whole checked set (sorted for determinism);
// otherwise it falls back to the highlighted selection. Open/reveal/paste
// intentionally stay out: open/reveal act on one file, paste on the clipboard.
QStringList MainWindow::actionFiles() const {
  if (!marked_.isEmpty()) {
    QStringList out(marked_.begin(), marked_.end());
    std::sort(out.begin(), out.end());
    return out;
  }
  return selectedFiles();
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
  // QuickLook preview goes on top, only when usable (installed exe found or
  // its pipe already live). Missing entirely when QuickLook is absent.
  if (!quickLookTarget().isEmpty())
    m.addAction(trStr(lang(), "quickLook"), this, &MainWindow::previewSelectedQuickLook);
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
#ifdef _WIN32
// ---- QuickLook integration (optional): preview the selected file through an
// installed QuickLook (QL-Win) instead of opening a full application.
// Detection: well-known install locations + Uninstall registry. Trigger:
// QuickLook's own named-pipe protocol (PipeServerManager.cs) — pipe
// \\.\pipe\QuickLook.App.Pipe.<UserSID>, UTF-8 message
// "QuickLook.App.PipeMessages.Toggle|<native path>|\n". Pipe presence also
// reports a running server, so a Store install (no launchable exe) still
// works once the user has started it.
QString quickLookToggleMessage(const QString& filePath) {
  return QStringLiteral("QuickLook.App.PipeMessages.Toggle|") +
         QDir::toNativeSeparators(filePath) + QStringLiteral("|\n");
}
// 0.9.4.125: bounded write. The synchronous version blocked the GUI thread
// indefinitely when a wedged server held the pipe open without reading (the
// small Toggle message normally fits the buffer and returns at once, so this
// only triggers on a stuck server - exactly the unresponsive-UI class this
// change eliminates). Overlapped WriteFile + a 5 s wait; on timeout the
// pending write is cancelled and the send fails like any other error, which
// the caller already reports. The open itself stays synchronous: with no
// instance - or all instances busy - CreateFileW fails immediately, so only
// a server that created an instance but never listens could still block the
// open (a server-side bug outside our reach; documented, not handled).
static const DWORD kQuickLookIoMs = 5000;
bool quickLookSendMessage(const QString& pipeName, const QByteArray& payload) {
  HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(pipeName.utf16()), GENERIC_WRITE,
                         0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ev) { CloseHandle(h); return false; }
  OVERLAPPED ov{};
  ov.hEvent = ev;
  DWORD written = 0;
  BOOL ok = WriteFile(h, payload.constData(), (DWORD)payload.size(), &written, &ov);
  bool done = false;
  if (ok) {
    done = true; // completed synchronously
  } else if (GetLastError() == ERROR_IO_PENDING) {
    if (WaitForSingleObject(ev, kQuickLookIoMs) == WAIT_OBJECT_0)
      done = GetOverlappedResult(h, &ov, &written, FALSE) != FALSE;
    if (!done) CancelIo(h); // best-effort unblock before close
  }
  CloseHandle(ev);
  CloseHandle(h);
  return done && written == (DWORD)payload.size();
}
static QString quickLookUserSid() {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return QString();
  DWORD len = 0;
  GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
  std::vector<unsigned char> buf(len ? len : 1);
  QString out;
  if (len && GetTokenInformation(tok, TokenUser, buf.data(), len, &len)) {
    LPWSTR s = nullptr;
    if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &s)) {
      out = QString::fromWCharArray(s);
      LocalFree(s);
    }
  }
  CloseHandle(tok);
  return out;
}
static QString quickLookPipeName() {
  // Prefer this user's pipe: with Fast User Switching or several instances,
  // the first wildcard hit could belong to another session. Fall back to the
  // first hit only when the SID itself is unavailable.
  const QString want = QStringLiteral("QuickLook.App.Pipe.") + quickLookUserSid();
  const bool exact = !want.endsWith(QLatin1Char('.'));
  QString first;
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW(L"\\\\.\\pipe\\QuickLook.App.Pipe.*", &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const QString name = QString::fromWCharArray(fd.cFileName);
      if (first.isEmpty()) first = name;
      if (exact && name == want) { first = name; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  if (first.isEmpty()) return QString();
  return QStringLiteral("\\\\.\\pipe\\") + first;
}
// _wgetenv returns null when the variable is absent; fromWCharArray(null)
// would AV, so map missing vars to empty (no candidate from that root).
static QString qlEnv(const wchar_t* name) {
  const wchar_t* v = _wgetenv(name);
  return v ? QString::fromWCharArray(v) : QString();
}
static QString quickLookExePath() {
  // Cache hits only: an empty or stale result re-probes, so installing (or
  // moving) QuickLook mid-session is picked up without a restart.
  static QString cached;
  if (!cached.isEmpty() && QFileInfo(cached).isFile()) return cached;
  cached.clear();
  QStringList cands;
  const QString pf = qlEnv(L"ProgramFiles");
  const QString pf86 = qlEnv(L"ProgramFiles(x86)");
  const QString local = qlEnv(L"LOCALAPPDATA");
  const QString user = qlEnv(L"USERPROFILE");
  if (!pf.isEmpty()) cands << pf + "\\QuickLook\\QuickLook.exe";
  if (!pf86.isEmpty()) cands << pf86 + "\\QuickLook\\QuickLook.exe";
  if (!local.isEmpty()) cands << local + "\\Programs\\QuickLook\\QuickLook.exe"
                              << local + "\\QuickLook\\QuickLook.exe";
  if (!user.isEmpty()) cands << user + "\\scoop\\apps\\quicklook\\current\\QuickLook.exe";
  for (const auto& c : cands)
    if (QFileInfo(c).isFile()) { cached = QDir::toNativeSeparators(c); return cached; }
  // 0.9.4.122: negative-cache the Uninstall-registry walk below. While
  // QuickLook is absent it re-ran on every right-click (the well-known-path
  // checks above are microsecond file stats, but the two-hive enumeration is
  // tens of milliseconds of synchronous GUI-thread I/O per menu open).
  // Skipping the re-walk for 120 s keeps a mid-session install discoverable
  // while the click path stays cheap. The pipe check in quickLookTarget()
  // is untouched, so a QuickLook start/stop still reflects immediately, and
  // the send path (previewSelectedQuickLook) is unchanged.
  static qint64 lastRegProbeMs = 0;
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (nowMs - lastRegProbeMs < 120000) return cached; // still empty
  lastRegProbeMs = nowMs;
  // Uninstall registry (machine + user).
  for (const char* root : {"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                           "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"}) {
    QSettings reg(QString::fromLatin1(root), QSettings::NativeFormat);
    for (const auto& g : reg.childGroups()) {
      reg.beginGroup(g);
      const QString disp = reg.value("DisplayName").toString();
      const QString loc = reg.value("InstallLocation").toString();
      reg.endGroup();
      if (disp.contains("QuickLook", Qt::CaseInsensitive) && !loc.isEmpty()) {
        const QString exe = QDir(loc).filePath("QuickLook.exe");
        if (QFileInfo(exe).isFile()) { cached = QDir::toNativeSeparators(exe); return cached; }
      }
    }
  }
  return cached;
}
QString MainWindow::quickLookTarget() const {
  if (!quickLookPipeName().isEmpty()) return QStringLiteral("pipe");
  return quickLookExePath();
}
void MainWindow::previewSelectedQuickLook() {
  const auto ps = selectedFiles();
  if (ps.isEmpty()) return;
  const QString pipe = quickLookPipeName();
  if (!pipe.isEmpty()) {
    if (!quickLookSendMessage(pipe, quickLookToggleMessage(ps.first()).toUtf8()))
      statusMsg_->setText(trStr(lang(), "quickLookFail"));
    return;
  }
  // Installed but not running: launch without blocking the GUI thread (the old
  // msleep loop froze input for up to 2.5s). Poll for the server pipe on the
  // event loop instead; bounded, cancel-safe by window lifetime.
  const QString exe = quickLookExePath();
  if (exe.isEmpty() || !QProcess::startDetached(exe, {})) {
    statusMsg_->setText(trStr(lang(), "quickLookFail"));
    return;
  }
  qlPendingPath_ = ps.first(); qlPollLeft_ = 25; // ~2.5s at 100ms
  statusMsg_->setText(trStr(lang(), "quickLookStarting"));
  QTimer::singleShot(100, this, &MainWindow::pollQuickLookPipe);
}
void MainWindow::pollQuickLookPipe() {
  if (qlPendingPath_.isEmpty() || qlPollLeft_ <= 0) return;
  const QString pipe = quickLookPipeName();
  if (!pipe.isEmpty()) {
    const QString path = qlPendingPath_; qlPendingPath_.clear(); qlPollLeft_ = 0;
    if (!quickLookSendMessage(pipe, quickLookToggleMessage(path).toUtf8()))
      statusMsg_->setText(trStr(lang(), "quickLookFail"));
    return;
  }
  if (--qlPollLeft_ <= 0) {
    qlPendingPath_.clear();
    statusMsg_->setText(trStr(lang(), "quickLookFail"));
    return;
  }
  QTimer::singleShot(100, this, &MainWindow::pollQuickLookPipe);
}
#else
QString quickLookToggleMessage(const QString&) { return QString(); }
bool quickLookSendMessage(const QString&, const QByteArray&) { return false; }
#endif
void MainWindow::revealSelected() {
  const auto ps = selectedFiles();
  if (ps.isEmpty()) return;
  revealPath(ps.first());
}
#ifdef _WIN32
enum class ExplorerReveal { Selected, FocusOnly, NoWindow, Failed };
static QString explorerFolderFromView(IFolderView* view, PIDLIST_ABSOLUTE* outPidl) {
  *outPidl = nullptr;
  if (!view) return QString();
  IPersistFolder2* persist = nullptr;
  if (FAILED(view->GetFolder(IID_PPV_ARGS(&persist))) || !persist) return QString();
  PIDLIST_ABSOLUTE pidl = nullptr;
  const HRESULT hr = persist->GetCurFolder(&pidl);
  persist->Release();
  if (FAILED(hr) || !pidl) return QString();
  QString path;
  PWSTR raw = nullptr;
  if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, &raw)) && raw)
    path = QString::fromWCharArray(raw);
  if (raw) CoTaskMemFree(raw);
  if (path.isEmpty()) { ILFree(pidl); return QString(); }
  *outPidl = pidl;
  return path;
}
static QString explorerLocationFolder(IWebBrowser2* browser) {
  if (!browser) return QString();
  BSTR raw = nullptr;
  if (FAILED(browser->get_LocationURL(&raw)) || !raw) { if (raw) SysFreeString(raw); return QString(); }
  const QUrl url(QString::fromWCharArray(raw));
  SysFreeString(raw);
  return url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : QString();
}
static bool sameExplorerFolder(const QString& a, const QString& b) {
  if (a.isEmpty() || b.isEmpty()) return false;
  return QString::compare(QDir::cleanPath(a), QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
}
static bool selectExplorerFile(IFolderView* folderView, IShellView* shellView,
                               PIDLIST_ABSOLUTE basePidl, const QString& targetFile) {
  if (!folderView || !shellView || !basePidl) return false;
  int count = 0;
  if (FAILED(folderView->ItemCount(SVGIO_ALLVIEW, &count))) return false;
  const QString target = QDir::cleanPath(targetFile);
  for (int i = 0; i < count; ++i) {
    PITEMID_CHILD child = nullptr;
    if (FAILED(folderView->Item(i, &child)) || !child) continue;
    PIDLIST_ABSOLUTE absolute = ILCombine(basePidl, child);
    PWSTR raw = nullptr;
    QString itemPath;
    if (absolute && SUCCEEDED(SHGetNameFromIDList(absolute, SIGDN_FILESYSPATH, &raw)) && raw)
      itemPath = QDir::cleanPath(QString::fromWCharArray(raw));
    if (raw) CoTaskMemFree(raw);
    const bool match = !itemPath.isEmpty() &&
                       QString::compare(itemPath, target, Qt::CaseInsensitive) == 0;
    bool selected = false;
    if (match) {
      selected = SUCCEEDED(shellView->SelectItem(
          child, static_cast<SVSIF>(SVSI_SELECT | SVSI_DESELECTOTHERS |
                                     SVSI_ENSUREVISIBLE | SVSI_FOCUSED))) ||
                 SUCCEEDED(shellView->SelectItem(
          child, static_cast<SVSIF>(SVSI_SELECT | SVSI_ENSUREVISIBLE)));
    }
    if (absolute) ILFree(absolute);
    ILFree(child);
    if (selected) return true;
  }
  return false;
}
static ExplorerReveal revealInOpenExplorer(const QString& file) {
  const QFileInfo fi(file);
  if (!fi.exists()) return ExplorerReveal::Failed;
  const QString targetDir = QDir::cleanPath(fi.absolutePath());
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninitCom = SUCCEEDED(com);
  IShellWindows* windows = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(ShellWindows), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&windows))) || !windows) {
    if (uninitCom) CoUninitialize();
    return ExplorerReveal::Failed;
  }
  long count = 0;
  windows->get_Count(&count);
  ExplorerReveal result = ExplorerReveal::NoWindow;
  for (long i = 0; i < count && result != ExplorerReveal::Selected; ++i) {
    VARIANT index; VariantInit(&index); index.vt = VT_I4; index.lVal = i;
    IDispatch* disp = nullptr;
    const HRESULT hrItem = windows->Item(index, &disp);
    VariantClear(&index);
    if (FAILED(hrItem) || !disp) continue;
    IWebBrowser2* browser = nullptr;
    disp->QueryInterface(IID_PPV_ARGS(&browser));
    disp->Release();
    if (!browser) continue;
    IServiceProvider* provider = nullptr;
    browser->QueryInterface(IID_PPV_ARGS(&provider));
    IShellBrowser* shellBrowser = nullptr;
    if (provider) provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&shellBrowser));
    IShellView* shellView = nullptr;
    if (shellBrowser) shellBrowser->QueryActiveShellView(&shellView);
    IFolderView* folderView = nullptr;
    if (shellView) shellView->QueryInterface(IID_PPV_ARGS(&folderView));
    PIDLIST_ABSOLUTE viewFolderPidl = nullptr;
    const QString viewFolder = explorerFolderFromView(folderView, &viewFolderPidl);
    const bool match = sameExplorerFolder(viewFolder, targetDir)
                       || sameExplorerFolder(explorerLocationFolder(browser), targetDir);
    if (match && shellView) {
      HWND hwnd = nullptr;
      SHANDLE_PTR rawHwnd = 0;
      if (SUCCEEDED(browser->get_HWND(&rawHwnd)) && rawHwnd)
        hwnd = reinterpret_cast<HWND>(rawHwnd);
      if (hwnd) {
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
      }
      PIDLIST_ABSOLUTE basePidl = viewFolderPidl;
      viewFolderPidl = nullptr;
      if (!basePidl) basePidl = ILCreateFromPathW(reinterpret_cast<LPCWSTR>(targetDir.utf16()));
      PIDLIST_ABSOLUTE filePidl = nullptr;
      IShellItem* fileItem = nullptr;
       const QString nativeFile = QDir::toNativeSeparators(fi.absoluteFilePath());
       if (SUCCEEDED(SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(nativeFile.utf16()),
                                                 nullptr, IID_PPV_ARGS(&fileItem))) && fileItem)
         SHGetIDListFromObject(fileItem, &filePidl);
       bool selected = false;
      if (basePidl && filePidl) {
        PUIDLIST_RELATIVE child = ILFindChild(basePidl, filePidl);
        if (child) {
           selected = SUCCEEDED(shellView->SelectItem(child, static_cast<SVSIF>(SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_ENSUREVISIBLE | SVSI_FOCUSED)))
                      || SUCCEEDED(shellView->SelectItem(child, static_cast<SVSIF>(SVSI_SELECT | SVSI_ENSUREVISIBLE)));
         }
       }
       if (!selected) {
         selected = selectExplorerFile(folderView, shellView, basePidl, nativeFile);
       }
       if (filePidl) ILFree(filePidl);
      if (basePidl) ILFree(basePidl);
      if (fileItem) fileItem->Release();
      result = selected ? ExplorerReveal::Selected : ExplorerReveal::FocusOnly;
    }
    if (viewFolderPidl) ILFree(viewFolderPidl);
    if (folderView) folderView->Release();
    if (shellView) shellView->Release();
    if (shellBrowser) shellBrowser->Release();
    if (provider) provider->Release();
    browser->Release();
  }
  windows->Release();
  if (uninitCom) CoUninitialize();
  return result;
}
#endif
void MainWindow::revealPath(const QString& path) {
  if (path.isEmpty()) return;
#ifdef _WIN32
  switch (revealInOpenExplorer(path)) {
    case ExplorerReveal::Selected: break;
    case ExplorerReveal::FocusOnly: statusMsg_->setText(trStr(lang(), "revealFocusFail")); break;
    case ExplorerReveal::NoWindow:
      QProcess::startDetached("explorer.exe", {QString("/select,%1").arg(QDir::toNativeSeparators(path))});
      break;
    case ExplorerReveal::Failed: statusMsg_->setText(trStr(lang(), "revealFail")); break;
  }
#else
  QProcess::startDetached("explorer.exe", {QString("/select,%1").arg(QDir::toNativeSeparators(path))});
#endif
}
QString MainWindow::gpuStateText() const {
  if (scanning_ && scanPaused_) return trStr(lang(), "gpuStop");
  if (scanning_ && backendStatus_.gpuActive) return trStr(lang(), "gpuAccel");
  return trStr(lang(), "gpuWait");
}
void MainWindow::updateSysLabels() {
  // P4 display contract: the CPU/RAM rows show the WORKING process only
  // (Backend in production, GUI process under loopback) from the pushed
  // status snapshot. This function keeps the GPU state label; CPU/RAM come
  // from onStatusSnapshot so two writers never alternate meanings here.
  sumGpu_->setVisible(true); sumValGpu_->setVisible(true);
  sumValGpu_->setText(gpuStateText());
}
void MainWindow::renameSelected() {
  const auto ps = selectedFiles();
  if (ps.size() != 1) return;
  QFileInfo fi(ps.first());
  bool ok = false;
  const QString n = QInputDialog::getText(this, trStr(lang(), "rename"), trStr(lang(), "newName"),
                                          QLineEdit::Normal, fi.fileName(), &ok);
  if (ok && !n.isEmpty() && n != fi.fileName()) {
    const QString oldPath = fi.absoluteFilePath();
    if (!QFile::rename(fi.filePath(), fi.dir().filePath(n)))
      QMessageBox::warning(this, trStr(lang(), "rename"), trStr(lang(), "renameFail"));
    else {
      // 0.9.4.105 (F): the old path left the filesystem; drop its stored row.
      if (backend_) backend_->forgetPaths(QStringList{oldPath});
      refreshAfterFileOperation(QFileInfo(fi.dir().filePath(n)).absoluteFilePath());
    }
  }
}
void MainWindow::deleteSelected() {
  const auto ps = actionFiles();
  if (ps.isEmpty() || fileOpBusy_) return;
  if (QMessageBox::question(this, trStr(lang(), "delTitle"),
                            trStr(lang(), "delAsk").arg(ps.size())) != QMessageBox::Yes) return;
  // 0.9.4.105 (C): the shell recycle runs on a worker thread with a progress
  // dialog, so a large checked set never freezes the window. Results are
  // applied back on the GUI thread (prune + stored-index cleanup + timing log).
  fileOpBusy_ = true;
  QProgressDialog* dlg = newFileOpDialog(this, trStr(lang(), "delTitle"), 0, false, QString());
  auto gone = std::make_shared<QStringList>();
  auto ms = std::make_shared<qint64>(0);
  QThread* th = QThread::create([ps, gone, ms] {
    QElapsedTimer t; t.start();
#ifdef _WIN32
    // Shell APIs need COM on the calling thread; this worker is a raw OS
    // thread, so it initializes an apartment for the duration of the call.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    *gone = recycleFiles(ps); // 0.9.4.104 (A): one shell call for the whole set
    if (SUCCEEDED(hr)) CoUninitialize();
#else
    for (const auto& p : ps) if (QFile::remove(p)) *gone << p;
#endif
    *ms = t.elapsed();
  });
  connect(th, &QThread::finished, this, [this, th, dlg, ps, gone, ms] {
    th->deleteLater();
    if (dlg) { dlg->close(); dlg->deleteLater(); }
    fileOpBusy_ = false;
    if (gone->size() != ps.size())
      QMessageBox::warning(this, trStr(lang(), "delTitle"), trStr(lang(), "delFail"));
    if (!gone->isEmpty()) {
      prunePaths(QSet<QString>(gone->begin(), gone->end()));
      // 0.9.4.105 (F): reflect the removal in the stored index immediately
      // (files + match pairs + thumbnails), not on the next scan.
      if (backend_) backend_->forgetPaths(*gone);
    }
    scanLog(QString("delete files=%1 gone=%2 recycleMs=%3").arg(ps.size()).arg(gone->size()).arg(*ms));
  });
  th->start();
}
void MainWindow::copySelected() {
  const auto ps = actionFiles();
  if (ps.isEmpty()) return;
  auto* d = new QMimeData; QList<QUrl> urls;
  for (const auto& p : ps) urls << QUrl::fromLocalFile(p);
  d->setUrls(urls);
  QApplication::clipboard()->setMimeData(d);
}
void MainWindow::cutSelected() {
  copySelected();
  cutPaths_ = actionFiles();
}
void MainWindow::pasteFiles() {
  const QMimeData* d = QApplication::clipboard()->mimeData();
  if (!d || !d->hasUrls() || folder_->text().isEmpty()) return;
  const QDir dst(folder_->text());
  QStringList movedSrcs;
  for (const auto& u : d->urls()) {
    const QString src = u.toLocalFile();
    const QString target = dst.filePath(QFileInfo(src).fileName());
    if (cutPaths_.contains(src)) {
    if (QFile::rename(src, target)) { cutPaths_.removeOne(src); movedSrcs << src; }
    else if (QFile::copy(src, target) && QFile::remove(src)) { cutPaths_.removeOne(src); movedSrcs << src; }
    } else QFile::copy(src, target);
  }
  // 0.9.4.105 (F): cut sources left the filesystem; drop their stored rows.
  if (!movedSrcs.isEmpty() && backend_) backend_->forgetPaths(movedSrcs);
  refreshAfterFileOperation(QString());
}
void MainWindow::moveSelected() {
  const auto ps = actionFiles();
  if (ps.isEmpty() || fileOpBusy_) return;
  const QString dst = QFileDialog::getExistingDirectory(this, trStr(lang(), "chooseDest"), folder_->text());
  if (dst.isEmpty()) return;
  // 0.9.4.105 (C): per-file rename/copy (cross-volume fallback included) runs
  // on a worker thread with a determinate progress dialog. Cancel stops
  // between files; only completed moves are pruned / dropped from the index.
  fileOpBusy_ = true;
  QProgressDialog* dlg = newFileOpDialog(this, trStr(lang(), "move"), ps.size(), true,
                                         trStr(lang(), "stop"));
  auto gone = std::make_shared<QStringList>();
  auto ms = std::make_shared<qint64>(0);
  auto done = std::make_shared<std::atomic<int>>(0);
  auto cancel = std::make_shared<std::atomic<bool>>(false);
  connect(dlg, &QProgressDialog::canceled, this, [cancel] { cancel->store(true); });
  auto* poll = new QTimer(this);
  poll->setInterval(100);
  // QPointer: a queued timer event may still fire after the finish handler
  // deleted the dialog; never touch a dangling pointer.
  QPointer<QProgressDialog> dlgPtr(dlg);
  connect(poll, &QTimer::timeout, this, [dlgPtr, done] { if (dlgPtr) dlgPtr->setValue(done->load()); });
  poll->start();
  QThread* th = QThread::create([ps, dst, gone, ms, done, cancel] {
    QElapsedTimer t; t.start();
    for (const auto& p : ps) {
      if (cancel->load()) break;
      const QString target = QDir(dst).filePath(QFileInfo(p).fileName());
      if (QFile::rename(p, target)) *gone << p;
      else if (QFile::copy(p, target) && QFile::remove(p)) *gone << p;
      done->fetch_add(1);
    }
    *ms = t.elapsed();
  });
  connect(th, &QThread::finished, this, [this, th, dlg, poll, ps, gone, ms, cancel] {
    poll->stop(); poll->deleteLater();
    if (dlg) { dlg->close(); dlg->deleteLater(); }
    th->deleteLater();
    fileOpBusy_ = false;
    const bool wasCancelled = cancel->load();
    if (!wasCancelled && gone->size() != ps.size())
      QMessageBox::warning(this, trStr(lang(), "move"), trStr(lang(), "moveFail"));
    if (!gone->isEmpty()) {
      prunePaths(QSet<QString>(gone->begin(), gone->end()));
      // 0.9.4.105 (F): the moved sources left the filesystem; drop their
      // stored index rows now (their new locations index on the next scan).
      if (backend_) backend_->forgetPaths(*gone);
    }
    scanLog(QString("move files=%1 gone=%2 moveMs=%3 cancelled=%4")
        .arg(ps.size()).arg(gone->size()).arg(*ms).arg(wasCancelled ? 1 : 0));
  });
  th->start();
}
void MainWindow::prunePaths(const QSet<QString>& gone) {
  QElapsedTimer t; t.start(); // 0.9.4.104 (G): phase timing, see scanLog below
  for (const auto& p : gone) {
    marked_.remove(p); resCache_.remove(p); fileSize_.remove(p); fileFp_.remove(p);
    fileDur_.remove(p); bestPct_.remove(p); pathKind_.remove(p);
    contentFlag_.remove(p); // 0.9.4.106
    pathParent_.remove(p);
    if (currentFile_ == p) currentFile_.clear();
  }
  dropThumbCache(gone); // 0.9.4.104 (B): single sweep, was one full scan per file
  const qint64 dropMs = t.elapsed();
  rebuildGroups(); refreshGroupList(); refreshFileViews(); refreshDetail(); updateStatusCounts();
  scanLog(QString("prune files=%1 dropCacheMs=%2 rebuildMs=%3 totalMs=%4")
      .arg(gone.size()).arg(dropMs).arg(t.elapsed() - dropMs).arg(t.elapsed()));
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
                                   const QString& path, qint64 elapsedMs) const {  const QString base = QString("%1 / %2 (%3%) — %4 — %5").arg(done).arg(total).arg(pct)
                           .arg(QFileInfo(path).fileName()).arg(fmtElapsed(elapsedMs));
  // ETR is a naive linear extrapolation; early in a scan `done` is tiny against
  // `total`, so the raw ratio reported absurd values (7000+ minutes). Only
  // publish it once there is a meaningful sample: at least 3 s elapsed and
  // about 2% of the work done. Below that, show the count line alone
  // (0.9.4.72).
  if (elapsedMs < 3000 || done == 0 || total <= done) return base;
  const qulonglong minDone = std::max<qulonglong>(20, total / 50);
  if (done < minDone) return base;
  const qint64 remainMs = elapsedMs * (qint64)(total - done) / (qint64)done;
  return base + QString(" — %1 %2").arg(trStr(lang(), "remain")).arg(fmtElapsed(remainMs));
}
qint64 MainWindow::elapsedActiveMs() const {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 tail = (scanPaused_ && pauseStartMs_ > 0) ? (now - pauseStartMs_) : 0;
  return std::max<qint64>(0, now - scanStartMs_ - pausedAccumMs_ - tail);
}
void MainWindow::refreshSummary(const msf::SearchReport*) {
  if (!hasReport_ || lastStats_.size() < 5) {
    // Preserve live progress from a cancelled scan instead of blanking: a stop
    // during fingerprint/walk did real work that "-" would erase.
    if (lastReadN_ == 0) {
      sumValTotal_->setText("-"); sumValDone_->setText("-"); sumValIndexed_->setText("-"); sumValGroups_->setText("-");
      sumValDup_->setText("-"); sumValVerify_->setText("-"); sumValTime_->setText("-"); sumValGpu_->setText(gpuStateText());
      sumValCpu_->setText("-"); sumValRam_->setText("-");
    }
    return;
  }
  const qulonglong completed = lastStats_[0].toULongLong();
  // 0.9.4.84: "인덱스 완료" is the number of files that hold a usable index row:
  // newly analyzed (this scan) plus unchanged (already indexed). Showing only
  // `analyzed` made a fully-unchanged rescan report 0 even though the whole
  // folder is indexed. Matches the live formula in updateStatusCounts().
  const qulonglong analyzed = lastStats_.size() > 1 ? lastStats_[1].toULongLong() : 0;
  const qulonglong unchanged = lastStats_.size() > 2 ? lastStats_[2].toULongLong() : 0;
  const qulonglong indexed = analyzed + unchanged;
  const qulonglong total = targetKnown_ ? targetTotal_ : completed;
  sumValTotal_->setText(QString::number(total));
  // "읽기 완료" is files admitted to the pipeline (r.scanned); "인덱스 완료"
  // is files with an index row (analyzed + unchanged). They differ when files
  // fail analysis.
  sumValDone_->setText(QString::number(completed));
  sumValIndexed_->setText(QString::number(indexed));
  // 0.9.4.131: the row shows the group count of the list on screen (the same
  // live formula updateStatusCounts already used during scans). It must not
  // switch back to the finished-message pair count here: on rescans that
  // recompute nothing (all-unchanged / quick-load) that number is 0 while the
  // list shows thousands of groups.
  sumValGroups_->setText(QString::number(groups_.size()));
  qulonglong dupFiles = 0;
  for (const auto& g : groups_) dupFiles += (qulonglong)g.paths.size();
  sumValDup_->setText(QString::number(dupFiles));
  // 0.9.4.95: pair-verify totals (candidates / confirmed duplicate pairs,
  // result-oriented) from the finished scan's telemetry record.
  // 0.9.4.132: a load verified nothing this session, so its reading is the
  // stored pair total with an unmeasured scan side ("- / N").
  sumValVerify_->setText(loadedSummary_
      ? (lastVerifyDone_ > 0 ? QString("- / %1").arg(lastVerifyDone_) : QStringLiteral("-"))
      : (lastVerifyTotal_ > 0
             ? QString("%1 / %2").arg(lastVerifyTotal_).arg(lastVerifyDone_)
             : QStringLiteral("-")));
  sumValTime_->setText(fmtElapsed(repElapsedMs_));
  sumValGpu_->setText(gpuStateText());
}
void MainWindow::scanHeartbeat() {
  static qint64 lastBeat = 0;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - lastBeat < 10000) return;
  lastBeat = now;
  Q_UNUSED(now); // elapsed comes from elapsedActiveMs() (pause-excluded)
  // walked/listed expose the otherwise invisible enumeration phases: when a
  // rescan shows lastPct=0 for many minutes, growing walked/listed counts
  // prove the walk is progressing instead of hung (0.9.4.62 crash analysis).
  scanLog(QString("alive elapsed=%1 lastPct=%2 lastPath=%3 walked=%4 listed=%5 groups=%6 marked=%7")
              .arg(fmtElapsed(elapsedActiveMs())).arg(lastPct_).arg(lastPath_)
              .arg(lastTotalN_).arg(lastListN_)
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
void MainWindow::updateStatusCounts() {  qulonglong files = 0;
  for (const auto& g : groups_) files += (qulonglong)g.paths.size();
  statusCount_->setText(QString("%1: %2 · %3: %4 · %5: %6")
                            .arg(trStr(lang(), "groups")).arg(groups_.size())
                            .arg(trStr(lang(), "files")).arg(files)
                            .arg(trStr(lang(), "marked")).arg(marked_.size()));
  // Live summary during scans (previously only time+GPU moved): totals from
  // the pre-walk count, done/scanned from the worker, groups/dups live.
  // Read-complete shows walked files; index-complete shows analyzed files from
  // the engine's live counter. Plain numbers match the post-scan panel format;
  // the old combined "done / total" shape was part of the confusion.
  // Read-complete is monotonic across the fingerprint->walk handoff: the two
  // phases count different sequences, so the max is shown rather than letting
  // the walk restart the row at 0.
  if (scanning_) {
    sumValTime_->setText(fmtElapsed(elapsedActiveMs()));
    sumValTotal_->setText(targetKnown_ ? QString::number(targetTotal_) : "-");
    if (lastTotalN_ > lastReadN_) lastReadN_ = lastTotalN_;
    sumValDone_->setText(QString::number(lastReadN_));
    qulonglong liveAnalyzed = 0;
    if (backend_) liveAnalyzed = backendStatus_.analyzed + backendStatus_.unchanged;
    sumValIndexed_->setText(QString::number(liveAnalyzed));
    sumValGroups_->setText(QString::number(groups_.size()));
    sumValDup_->setText(QString::number(files));
  }
  updateGpuLabel();
  sumValGpu_->setText(gpuStateText());
}
// Live GPU status for the status bar. During a scan this shows how many images
// the CUDA backend already hashed; at rest it reports whether the backend is on.
void MainWindow::updateGpuLabel() {
  if (!gpuLbl_) return;
  const UiLang l = lang();
  const bool avail = backend_ ? backendStatus_.gpuAvailable : false;
  const qulonglong n = backend_ ? backendStatus_.gpuDone : 0;
  QString txt;
  if (scanning_ && n > 0) txt = trStr(l, "gpuLive").arg(n);
  else if (gpuOn_ && avail) txt = trStr(l, "gpuOn");
  else txt = trStr(l, "gpuOff");
  gpuLbl_->setText("⚡ " + txt);
  gpuLbl_->setStyleSheet(gpuOn_ && avail
                              ? QStringLiteral("color:#2e7d32;") : QStringLiteral("color:#999;"));
}
  void MainWindow::showHelp() {
    QMessageBox::about(this, trStr(lang(), "help"), trStr(lang(), "about"));
  }

// ------------------------------------------------------------ monitor (kept behavior)
void MainWindow::configureMonitor() {
  QSettings st;
  QDialog dlg(this);
  dlg.setObjectName("settingsDlg"); // automation hook, see folder_
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
  generalLay->addLayout(langRow);
  // Show Detailed Logs checkbox (0.9.4.63+): toolbar checkbox visibility only,
  // telemetry wiring untouched. Stored in portable QSettings like the rest.
  auto* showLog = new QCheckBox(trStr(lang(), "showDetailLog"), generalTab);
  showLog->setObjectName("showDetailLogBox"); // automation hook, see folder_
  showLog->setChecked(QSettings().value("ui/showDetailLog", true).toBool());
  generalLay->addWidget(showLog);
  // CPU usage % display checkbox (0.9.4.96+): the toolbar CPU box is a
  // read-only preset display, hidden unless this is on (default OFF).
  // Stored in portable QSettings like the rest.
  auto* showCpu = new QCheckBox(trStr(lang(), "showCpuPercent"), generalTab);
  showCpu->setObjectName("showCpuBox"); // automation hook
  showCpu->setChecked(QSettings().value("ui/showCpuPercent", false).toBool());
  generalLay->addWidget(showCpu);
  // GPU acceleration checkbox (0.9.4.96+): moved here from the toolbar, where
  // it stood next to the monitor button. Same adaptive ON/OFF meaning
  // (no manual utilization control); persists across launches.
  auto* gpuAccel = new QCheckBox(trStr(lang(), "monitorGpu"), generalTab);
  gpuAccel->setObjectName("gpuAccelBox"); // automation hook
  gpuAccel->setChecked(QSettings().value("ui/gpuEnabled", true).toBool());
  generalLay->addWidget(gpuAccel);
  // Test-button visibility checkbox (0.9.4.98+): the far-right Test toggle
  // stays available without crowding the toolbar. Stored like the rest;
  // the toggle state itself (ui/testMode) is untouched.
  auto* showTest = new QCheckBox(trStr(lang(), "showTestButton"), generalTab);
  showTest->setObjectName("showTestBox"); // automation hook
  showTest->setChecked(QSettings().value("ui/showTestButton", true).toBool());
  generalLay->addWidget(showTest);
  // Analyze-mode selector (0.9.4.81): B(안정형) default on top, then A(고속형),
  // then A+B(자동). An option inside Settings. The verdict is identical across
  // modes; only verify scheduling/checkpointing differs.
  auto* analyzeRow = new QHBoxLayout;
  auto* analyzeLabel = new QLabel(trStr(lang(), "analyzeModeLabel"), generalTab);
  auto* analyzeModeBox = new QComboBox(generalTab);
  analyzeModeBox->setObjectName("analyzeModeBox"); // automation hook, see folder_
  analyzeModeBox->addItem(trStr(lang(), "analyzeModeStable")); // index 0 == B
  analyzeModeBox->addItem(trStr(lang(), "analyzeModeFast"));   // index 1 == A
  analyzeModeBox->addItem(trStr(lang(), "analyzeModeAuto"));   // index 2 == A+B
  analyzeModeBox->setToolTip(trStr(lang(), "analyzeModeTip"));
  {
    const int m = QSettings().value("ui/analyzeMode", 0).toInt();
    analyzeModeBox->setCurrentIndex(m >= 0 && m <= 2 ? m : 0);
  }
  analyzeRow->addWidget(analyzeLabel); analyzeRow->addWidget(analyzeModeBox); analyzeRow->addStretch(1);
  generalLay->addLayout(analyzeRow);
  // 0.9.4.143: recent-folder count in Favorites (1..20, default 3).
  auto* recentRow = new QHBoxLayout;
  auto* recentLabel = new QLabel(trStr(lang(), "recentLimitLabel"), generalTab);
  auto* recentSpin = new QSpinBox(generalTab);
  recentSpin->setObjectName("recentFolderLimitSpin"); // automation hook
  recentSpin->setRange(1, 20);
  recentSpin->setValue(recentFolderLimit());
  recentSpin->setToolTip(trStr(lang(), "recentLimitTip"));
  recentRow->addWidget(recentLabel); recentRow->addWidget(recentSpin); recentRow->addStretch(1);
  generalLay->addLayout(recentRow);
  // 0.9.4.134 fast-rescan (integrity mode). Strict default: the walk keeps
  // reading the 64 KiB quick hash of unchanged files.
  auto* fastRescan = new QCheckBox(trStr(lang(), "fastRescan"), generalTab);
  fastRescan->setObjectName("fastRescanBox"); // automation hook
  fastRescan->setChecked(QSettings().value("ui/fastRescan", false).toBool());
  fastRescan->setToolTip(trStr(lang(), "fastRescanTip"));
  generalLay->addWidget(fastRescan);
  generalLay->addStretch(1);
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
  gpu->setChecked(st.value("monitor/gpuEnabled", gpuOn_).toBool());
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
  st.setValue("ui/showDetailLog", showLog->isChecked());
  st.setValue("ui/showCpuPercent", showCpu->isChecked());
  st.setValue("ui/gpuEnabled", gpuAccel->isChecked());
  st.setValue("ui/showTestButton", showTest->isChecked());
  // 0.9.4.143: persist the recent-folder limit and trim the stored list now.
  st.setValue("ui/recentFolderLimit", recentSpin->value());
  {
    QStringList rf = st.value("ui/recentFolders").toStringList();
    while (rf.size() > recentSpin->value()) rf.removeLast();
    st.setValue("ui/recentFolders", rf);
  }
  refreshFolders();
  gpuOn_ = gpuAccel->isChecked();
  applyDetailLogVisibility();
  applyCpuPercentVisibility();
  applyTestButtonVisibility();
  // Push the GPU decision so monitor/scan pick it up without a restart.
  if (backend_) backend_->updateResourcePolicy(currentExecPolicy());
  st.setValue("ui/analyzeMode", analyzeModeBox->currentIndex());
  st.setValue("ui/fastRescan", fastRescan->isChecked());
  applyDetailLogVisibility();
  st.setValue("ui/language", langSel->currentData().toString());
  setLanguage(langSel->currentIndex());
  statusMsg_->setText(QString("%1 — %2 / %3").arg(trStr(lang(), "monSaved")).arg(watches.size()).arg(compares.size()));
}
void MainWindow::applyDetailLogVisibility() {
  // Visibility only: the telemetry wiring behind logTgl_ is untouched, and a
  // hidden checkbox keeps its checked state so re-showing restores behavior.
  // Through the toolbar action: QToolBar owns child visibility via the action
  // created by addWidget, so hiding the widget directly would be reverted on
  // the next toolbar layout.
  const bool show = QSettings().value("ui/showDetailLog", true).toBool();
  if (logTglAct_) logTglAct_->setVisible(show);
  else if (logTgl_) logTgl_->setVisible(show);
}
void MainWindow::applyCpuPercentVisibility() {
  // Visibility only: the preset policy behind cpu_ is untouched, and a hidden
  // box keeps its preset-synced value so re-showing restores the display.
  // Through the toolbar action: QToolBar owns child visibility via the action
  // created by addWidget, so hiding the widget directly would be reverted on
  // the next toolbar layout.
  const bool show = QSettings().value("ui/showCpuPercent", false).toBool();
  if (cpuAct_) cpuAct_->setVisible(show);
  else if (cpu_) cpu_->setVisible(show);
}
void MainWindow::applyTestButtonVisibility() {
  // Visibility only: the Test Mode toggle state (ui/testMode) is untouched,
  // and a hidden button keeps its checked state so re-showing restores
  // behavior. Same toolbar-action pattern as applyDetailLogVisibility.
  const bool show = QSettings().value("ui/showTestButton", true).toBool();
  if (testBtnAct_) testBtnAct_->setVisible(show);
  else if (testBtn_) testBtn_->setVisible(show);
}
void MainWindow::paintTestButton() {
  if (!testBtn_) return;
  // Same checked-highlight idiom as the monitor toggle: an unchecked button
  // is native, a checked button is unmistakably on.
  testBtn_->setStyleSheet(testBtn_->isChecked()
      ? QStringLiteral("background:#6b46c1; color:white; font-weight:bold;")
      : QString());
}

void MainWindow::toggleTestMode(bool on) {
  QSettings().setValue("ui/testMode", on);
  paintTestButton();
  // The choice applies to the next scan start; it never alters a run already
  // in progress or any stored index by itself.
  statusMsg_->setText(trStr(lang(), on ? "testModeOn" : "testModeOff"));
}
void MainWindow::toggleMonitor() {
  // The monitor button is a pure on/off toggle with highlight feedback.
  // First-run configuration lives in the far-right menu, not here: with no
  // roots the button only reports what is missing.
  auto paintMonBtn = [this] {
    monBtn_->setStyleSheet(monBtn_->isChecked()
        ? QStringLiteral("background:#2e7d32; color:white; font-weight:bold;")
        : QString());
  };
  if (monitorEnabled_) {
    backend_->stopMonitor(); monitorEnabled_ = false;
    monBtn_->setChecked(false); paintMonBtn();
    tray_->setToolTip(trStr(lang(), "app"));
    statusMsg_->setText(trStr(lang(), "monStop"));
    return;
  }
  QSettings st;
  auto ws = st.value("monitor/watchRoots").toStringList();
  auto cs = st.value("monitor/compareRoots").toStringList();
  if (ws.isEmpty() || cs.isEmpty()) { statusMsg_->setText(trStr(lang(), "monNeedCfg")); return; }
  // P2: the monitor lives in the backend (loopback for now). Roots and
  // thresholds travel as plain values; the policy object goes with them.
  backend_->startMonitor(ws, cs, QApplication::applicationDirPath(),
                         st.value("monitor/thresholdPercent", 90).toDouble(),
                         st.value("monitor/stableSeconds", 3).toInt(),
                         st.value("monitor/pollSeconds", 2).toInt(),
                         st.value("monitor/gpuEnabled", gpuOn_).toBool(),
                         policy_);
  monitorEnabled_ = true;
  monBtn_->setChecked(true); paintMonBtn();
  tray_->setToolTip(trStr(lang(), "monRun"));
  statusMsg_->setText(trStr(lang(), "monRun"));
}
void MainWindow::onBackendConnection(bool available, QString message) {
  backendAvailable_ = available;
  if (!available) {
    backendRestarting_ = message.contains(QStringLiteral("restarting"));
    statusMsg_->setText(message);
    // 0.9.4.105 (D): in-flight thumbnail/metadata requests die with the
    // backend. Clearing the tracking maps keeps a restarted backend's answers
    // from being matched against stale ids, and lets the next catch-up tick
    // re-request once READY is back (the cells are still cache misses). The
    // asked-set clears too so metadata that was lost with the crash is asked
    // again; resolved entries still answer from resCache_ without a request.
    thumbPendingReq_.clear();
    fileMetaPending_.clear();
    fileMetaAsked_.clear();
    // The supervisor sends a separate failed() event for an interrupted scan.
    // Do not open a modal here: restart timers and auto-resume must keep running.
    if (!scanning_) scan_->setEnabled(false);
  } else {
    backendRestarting_ = false;
    if (!scanning_) scan_->setEnabled(true);
    statusMsg_->setText(message);
    // Auto-resume a scan the backend crash aborted, once, now that the
    // supervisor has proven a fresh backend READY. The engine checkpoints
    // progress, so the re-walk skips indexed files and continues the update.
    if (scanCrashed_ && autoResumeTries_ < 1) {
      ++autoResumeTries_;
      scanCrashed_ = false;
      scanLog(QStringLiteral("auto-resume scan after backend restart (attempt %1)").arg(autoResumeTries_));
      beginScan(true);
    }
  }
}
void MainWindow::monitorEvent(const BackendMonitorEvent& e) {
  if (e.type == static_cast<int>(BackendMonitorEventType::Match)) { showMonitorMatch(e); return; }
  if (e.type == static_cast<int>(BackendMonitorEventType::Deferred)) {
    statusMsg_->setText(QString("Monitor delayed: %1").arg(e.path)); return;
  }
  if (e.type == static_cast<int>(BackendMonitorEventType::Error)) {
    statusMsg_->setText(QString("Monitor error: %1").arg(e.path)); return;
  }
  if (e.type == static_cast<int>(BackendMonitorEventType::Started) ||
      e.type == static_cast<int>(BackendMonitorEventType::Stopped))
    statusMsg_->setText(e.detail);
}
void MainWindow::onStatusSnapshot(BackendStatus st) {
  backendStatus_ = st;
  // 0.9.4.88 combined CPU: GUI process (guiCpu_) + Backend process tree
  // (backendCpu = Backend + its children, e.g. nvidia-smi/ffprobe). Under the
  // loopback guiCpu_ is 0 (same process), so the value stays single-source.
  // User-action spawns (Explorer etc.) and unrelated OS processes (Defender)
  // are not included: they are neither the GUI process nor the Backend tree.
  const double combinedCpu = std::clamp(st.backendCpu + guiCpu_, 0.0, 100.0);
  sumValCpu_->setText(QString("%1%").arg(combinedCpu, 0, 'f', 0));
  sumValRam_->setText(fmtSize(st.backendRssMB * 1024ULL * 1024ULL));
}
void MainWindow::onMonitorSnapshot(const BackendMonitorStatus& s) {
  backendMonStatus_ = s;
  if (!monitorEnabled_) return;
  auto state = [](int x) {
    switch (x) {
    case 0: return "Idle"; case 1: return "Light";
    case 2: return "Busy"; case 3: return "Heavy";
    default: return "Critical";
    }
  };
  const QString gpu = s.gpuPercent < 0 ? "n/a" : QString::number(s.gpuPercent, 'f', 0) + "%";
  // P4: the summary CPU/RAM rows belong to onStatusSnapshot (working-process
  // numbers) — the monitor snapshot no longer overwrites them with
  // system-wide percentages. Tray keeps monitor state (its own tooltip).
  tray_->setToolTip(QString("%1 | %2 | GPU %3 | Q %4").arg(state(s.loadState)).arg(s.analyzed).arg(gpu).arg(s.pending));
}
void MainWindow::showMonitorMatch(const BackendMonitorEvent& e) {
  if (e.matches.empty()) return;
  QSettings st; const BackendMonitorMatch* selected = nullptr;
  for (const auto& candidate : e.matches) {
    QString raw = candidate.newPath + "|" + candidate.existingPath;
    QString key = QString::fromLatin1(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (!st.value("monitor/skipped/" + key, false).toBool()) { selected = &candidate; break; }
  }
  if (!selected) return; const auto& m = *selected;
  QMessageBox box(this);
  box.setWindowTitle(trStr(lang(), "dupTitle"));
  box.setText(QString("New file:\n%1\n\nExisting file:\n%2\n\nSimilarity: %3%")
                  .arg(m.newPath).arg(m.existingPath).arg(m.percent, 0, 'f', 1));
  auto* openNew = box.addButton(trStr(lang(), "open"), QMessageBox::ActionRole);
  auto* openOld = box.addButton(trStr(lang(), "reveal"), QMessageBox::ActionRole);
  auto* delNew = box.addButton(trStr(lang(), "del"), QMessageBox::DestructiveRole);
  auto* skip = box.addButton("Skip", QMessageBox::RejectRole);
  box.exec();
  if (box.clickedButton() == openNew) QDesktopServices::openUrl(QUrl::fromLocalFile(m.newPath));
  else if (box.clickedButton() == openOld)
    revealPath(m.existingPath);
#ifdef _WIN32
  else if (box.clickedButton() == delNew) {
    if (!recycleFile(m.newPath))
      QMessageBox::warning(this, trStr(lang(), "del"), trStr(lang(), "delFail"));
  }
#else
  else if (box.clickedButton() == delNew)
    QMessageBox::information(this, trStr(lang(), "del"), trStr(lang(), "delFail"));
#endif
  else if (box.clickedButton() == skip) {
    QString raw = m.newPath + "|" + m.existingPath;
    QString key = QString::fromLatin1(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256).toHex());
    st.setValue("monitor/skipped/" + key, true);
  }
}

















