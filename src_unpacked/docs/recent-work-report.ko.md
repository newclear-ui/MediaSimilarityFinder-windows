# 최근 중요 3건 작업 결과 보고 (2026-10-08)

기준 커밋: `8f3ba47` (`origin/main`과 동기화 완료)
버전: `0.9.4.71` / CPU CTest 116/116 / GPU CTest 117/117

이 문서는 최근 완료된 중요 작업 3건을 **한 문서에서 확인**하기 위한 요약 보고다.
각 작업의 상세 근거는 Build History / Work Log에 이미 있고, 이 문서는
"무엇을 했고 지금 상태가 어떤가"만 중복 없이 모은다.

---

## 작업 1 — `0.9.4.69` P3: 실 Backend 프로세스 spawn + Supervisor + IPC

커밋 `9985ac2` / 상세 `docs/build-history/0.9.4.69.{ko,en}.md`

### 무엇을 했나

P2까지의 `BackendClient` 추상화는 in-process `LoopbackBackendClient`만 있었다.
P3는 **GUI와 검색 엔진을 서로 다른 OS 프로세스로 분리**하고, 그 사이를
line-oriented JSON IPC로 연결했다.

- GUI 프로세스: `MainWindow` + `BackendClient` + `BackendSupervisor`.
- Backend 프로세스: `MediaSimilarityFinderBackend.exe` (`msf_core` + `Qt6::Core`만 링크).
- `ScanWorker`를 `src/scan_worker.*`로 이동, `BackendSession`이 스레드/워커/모니터를
  소유하는 공용 세션. Loopback은 그 위의 thin forwarder.
- IPC 계약: stdin 명령 / stdout JSONL 이벤트(메시지마다 flush) / stderr 진단.
  spawn마다 nonce를 발급하고 stale 이벤트는 nonce 불일치로 폐기. MATCHES 500/batch,
  RESULTS 2000/page chunking. `THUMBNAIL`(JPEG base64) 전달.

### Supervisor 안전장치

- QProcess spawn(shell 조립 없음), Win32 Job Object `KILL_ON_JOB_CLOSE`.
- bounded restart 3회 / backoff 2s·5s·10s, terminate→kill 에스컬레이션은 QTimer 기반(비차단).
- health 1s / heartbeat timeout 10s / READY timeout 15s.
- `shutdown()`만 3초 bounded 대기(문서화된 G3 예외).

### 결과 / 검증

- `backend_ipc_test` 7 checks(UTF-8 경로 round-trip, malformed/oversize/protocol reject).
- `backend_e2e_test`(Windows 실프로세스): PID 분리, kill→restart, FAILED, DB reopen 실측.
- `MSF_TEST_BACKEND_FAIL_FAST` / `MSF_TEST_BACKEND_SILENT` crash-injection seam.
- CPU CTest 116/116, GPU CTest 117/117.

---

## 작업 2 — `0.9.4.70` P4: hardening + FILE_META

커밋 `ed4c0c1` / 상세 `docs/build-history/0.9.4.70.{ko,en}.md`

### 무엇을 했나

P3 종료 시점에 GUI에 decoder 잔재 2건(상세 pane의 resolution/duration 직접 probe)이
남아 있었다. 지시의 FFmpeg 직접 호출 금지에 따라 Backend 요청으로 옮겼다.

- 신규 `src/file_meta.*` (`FileMeta`, std `ffprobeSize`).
- `BackendSession::requestFileMeta`: engine 기록 → video info → dimensionsFast →
  ffprobe 순으로 Backend에서 수행.
- IPC `GET_FILE_META`/`FILE_META` + supervisor 전달 + GUI `requestFileMeta`/
  `onFileMetaReady`(pending 맵, stale-drop, 도착 시 repaint). 썸네일과 동일한 Type B 패턴.
- GUI에 남는 QtGui 사용은 표현용으로 한정(QIcon 표시, EXIF text tag — 픽셀 decode 아님).

### 결과 / 검증

- **GUI 프로세스는 어떤 media pixel decode도 수행하지 않는다.** WIC/FFmpeg/CUDA/native
  decode는 전부 Backend에 있다.
- §15 15항 체크리스트를 증거와 대조하고 숫자(heartbeat/backoff/cap)를 확정.
- **dumpbin 실측: Backend 의존성 = `Qt6Core.dll` + `turbojpeg.dll` (+ffmpeg/sqlite).
  `Qt6Widgets`/`Qt6Gui` 없음.**
- CPU CTest 116/116, GPU CTest 117/117.

---

## 작업 3 — `0.9.4.71` 백엔드 결함 수정 + ThumbnailStore

커밋 `8f3ba47` / 상세 `docs/build-history/0.9.4.71.{ko,en}.md`

### 무엇을 했나

P3/P4 프로세스 분리 후 2차 독립 재검토에서 확정된 결함 7건을 수정했다.

| # | 결함 | 수정 |
|---|---|---|
| 1 | 분리 이후 GUI 자원 모드가 IPC로 전달되지 않아 워커가 항상 `make_policy(Custom)` | `ExecutionPolicy` 신설 → `START_SCAN.exec` → `ScanWorker::setResourceMode` → `make_policy(mode)`. 라이브 갱신은 `UPDATE_RESOURCE_POLICY`/`POLICY_APPLIED`(partial) |
| 2 | "Index Complete"가 `analyzedCount_`만 표시 | 엔진 `unchangedCount_` + `BackendStatus.unchanged`, GUI `liveAnalyzed = analyzed + unchanged` |
| 3 | 요약 CPU/RAM 라벨을 GUI 프로세스/시스템 전체가 번갈아 덮어씀 | `updateSysLabels`는 GPU 전용, CPU/RAM은 작업 프로세스 status snapshot 단일 writer(`sampleOwnProcess`) |
| 4 | 느린 파일 목록에 cache-hit(~0ms)/실패 decode(0ms) 패딩 | `addImage` 0-cost 제외, `addVideo` `cacheHit` 제외 |
| 5 | `ScanWorker::allMatches_`가 최종 persist 후에도 상주 | `clear()` + `shrink_to_fit()`(정상·실패 경로, 핸들러 무throw 규칙 준수) |
| 6 | Backend 썸네일이 엔진 `thumbMap_`만 조회 → 분리 후 대부분 빈 미리보기 | 신규 `src/thumbnail_store.*`(engine art → shell `IThumbnailCache` → WIC → FFmpeg → gray, SQLite 영속 + LRU256) + `backend_thumb` msf_core 이동 + JPEG end-to-end |
| 7 | `onFileMetaReady`가 `refreshFileViews()` 전체 재생성 → 선택 파괴 | `QMap<id,path>` dedup + `refreshFileMetaRow` in-place, 위젯 재생성 0 |

### 검증 중 발견한 회귀 — Qt JPEG 플러그인 미배포

ThumbnailStore 도입 후 `ui_scroll_regression_test`/`view_mode_probe`가 실패했다.
원인은 loopback/supervisor가 JPEG을 `QImage::fromData`로 디코드하는데,
Qt JPEG 플러그인(`qjpeg.dll`)이 의존하는 **`jpeg62.dll`이 배포 세트에 없어**
`QImageReader::supportedImageFormats()`에 jpeg가 없고 디코드가 null을 반환한 것이었다.

- 수정: 신규 `msf::decodeJpegArgb32`(libjpeg-turbo, 이미 동봉된 `turbojpeg.dll`)로
  loopback·supervisor를 교체. Qt JPEG 플러그인 의존을 완전히 제거.
- 재확인: 두 GUI 테스트 통과.

### 결과 / 검증

- CPU CTest **116/116**, GPU CTest **117/117**.
- 양쪽 GUI/Backend exe `--version 0.9.4.71`.
- 변경 소스 U+FFFD 0 / CJK 0.
- 소스 zip 774파일 HEAD와 byte 단위 일치(0 mismatches), portable zip 86 엔트리
  (GUI+Backend exe + turbojpeg), smoke PASS.

---

## 현재 전체 상태 요약

| 항목 | 상태 |
|---|---|
| 버전 | `0.9.4.71` |
| 커밋 | `8f3ba47`, `origin/main`과 동기화 |
| CPU CTest | 116/116 PASS |
| GPU CTest | 117/117 PASS |
| 프로세스 분리 | P1–P4 완료 |
| Backend Qt 의존성 | Qt6Core only (dumpbin 실측) |
| XMP production acceptance | CONDITIONAL |
| `color_thumb` production 수정 | 미수행 (사전 등록만) |
| S4 최종 GUI visual/save acceptance | DEFERRED (수동 acceptance 필요) |
| S5 product benchmark | DEFERRED |
| S6 | DEFERRED |
| NVDEC production adoption | NO (F-1 CONDITIONAL) |

## 남은 후보 / 다음 단계

- 실제 Windows GUI manual acceptance: 미리보기 표시, selection, kill 시 UI 가드,
  restart 후 복귀, Tiles/ListMode, 대규모 dataset traversal.
- GPU MAX의 GPU share boost 미구현(문서화된 공백) 검토.
- Backend 400MB의 정확한 비중은 VMMap/힙 스냅샷 필요.
- `color_thumb` R1 fixture 및 skip/pass 처리 → 그다음 R2~R6은 별도 결정.
- XMP full scan regression — S4 functional acceptance 완료 후.
- 백업 zip(`backup_src.ps1` / `package_portable.ps1`)은 0.9.4.71에서 수행 완료
  (src·portable 각 3개 유지, `.68` portable 회전).

## 관련 문서

- `docs/build-history/0.9.4.69.{ko,en}.md`
- `docs/build-history/0.9.4.70.{ko,en}.md`
- `docs/build-history/0.9.4.71.{ko,en}.md`
- `docs/worklog/0.9.4.{ko,en}.md`
- `docs/architecture/process-architecture-0.9.4.{ko,en}.md`
- `docs/implementation-briefs/process-backend-isolation-0.9.4.{ko,en}.md`
- `docs/development-progress.{ko,en}.md`
