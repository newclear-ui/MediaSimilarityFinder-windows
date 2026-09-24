# 런타임 감사: v0.9.2.63 계열 API·호출·스레드·수명 일관성

> 감사 기준: `25095f1` (v0.9.2.64 개발 트리). QuickLook 항목을 제외한 모든 발견은
> `origin/main` (`cb68b3c`)에도 동일하게 적용된다. 빌드·수정 없이 코드 읽기 감사만 수행.
> 약어: GUI=메인 스레드, worker=스캔 스레드.

## 1. Critical Runtime Issues

| 파일 | 위치 | 문제 | 발생 조건 | 영향 |
|---|---|---|---|---|
| `gui/mainwindow.cpp` `cancelScan()` / `togglePauseScan()` | 987-991, 974-986 | 일시정지/중지 버튼이 스캔 중 절대 동작 안 함. `invokeMethod(..., QueuedConnection)` 슬롯은 `run()`이 worker 스레드 이벤트루프를 점유하므로 스캔 종료 후에나 실행됨. 버튼 표시만 바뀜 | 스캔 중 클릭 시 항상 | 사용자는 멈춘 줄 알지만 계속됨 → 강제종료 → 미저장분 유실. `scanFinished` CANCELLED 분기는 GUI 경로에서 도달 불가. v0.9.2.57 StopCheck·체크포인트·CANCELLED UI가 함께 무력화. `scan_cancel_test`는 control 직접 조작이라 결함을 가림 |

수정: `worker_->pause()/resume()/cancel()` 직접 호출 (atomic store만 하므로 스레드 안전. 소멸자의 직접 `worker_->cancel()`이 선례).

## 2. Potential Runtime Issues

| 파일 | 위치 | 잠재 문제 | 근거 | 위험도 |
|---|---|---|---|---|
| `gui/mainwindow.cpp` `ffprobeSize()` + `src/video_decoder.cpp:39,156` | 1468 근처 | `ffmpeg`/`ffprobe` PATH 전용 조회. 포터블 번들(exe 옆)을 못 찾음 | bare-name `_popen`. 앱-디렉토리 우선 조회 없음 | 중 (포터블) |
| `src/media_search_engine.cpp` `processOne` | fp==0 재시도 규칙 | 손상 동영상 매 재검색마다 FFmpeg open+probe 전량 재시도 (엔진 skip-list 없음, `.62`는 썸네일 한정) | `changed` 조건 무조건 재시도 | 중 (성능) |
| `gui/mainwindow.cpp` `scanStatusText()` | 2130-2137 | 일시정지 중 ETR 증가 (pause 가드 없음) | 분기 없음 | 하 |
| QuickLook 감지 (`25095f1` 한정) | `_wgetenv` | env NULL 시 `fromWCharArray(NULL)` AV 가능 | 방어 없음 (실무상 항상 존재) | 하 |
| `src/media_search_engine.cpp` | 276 | 취소 스캔도 `updateLastScan` 기록 | 무조건 호출 | 하 |

## 3. API Consistency

| Class | Header | CPP | Call Site | 상태 |
|---|---|---|---|---|
| `ScanPipeline` | `analyze` 3종 + `StopCheck` + `ScanCancelled`(12행) | `LocalCancel` 내부 사용, `ScanCancelled` 미사용 | engine 3인자 호출, 테스트 부분통계 확인 | ⚠️ 헤더 잔재 (6항) |
| `MediaSearchEngine` | 전 API 정의됨 | 일치 | `upsertFingerprint`→테스트, `removePath`/`compareFingerprint`→monitor, 나머지 worker/GUI. `onMatchRef` 설정자는 테스트만 (문서화된 옵션, 정상) | 정상 |
| `Database` | `putThumb/getThumb/pruneThumbs` 정의됨 | 일치 | GUI `fileThumb`/종료 prune. autocommit 단문 | 정상 |
| `ScanWorker` | signal 8종 정의 | `run()` 내 emit 전부 존재 | `startScan` 8개 전부 연결 + finished/failed→thread quit | 정상 |
| `MainWindow` | `matches_`, `lastDone_`, `lastTotal_` 선언 | `clear`/대입 각 1회 외 사용 없음 (실사용은 `lastDoneN_` 계열) | — | ⚠️ 데드 멤버 3개 |

## 4. Thread / Lifetime

```text
MainWindow (GUI 스레드, main 스택 → 소멸자 보장)
 ├─ thread_ = new QThread(this) → 먼저 정리 (자식)
 ├─ worker_ = plain new + moveToThread → quit/wait 후 delete
 │    ├─ engine_/control_/allMatches_ = 값 멤버 (동일 수명)
 │    ├─ onMatch/progress 람다는 worker 스레드 전용, 위젯 접근 없음
 │    └─ takePending만 mutex, gpuDone_ atomic
 ├─ thumbDb_ (GUI 전용 sqlite 핸들) → worker 정리보다 먼저 close
 └─ monitor_ (자체 스레드/커넥션) → 마지막 stop
```

- QtSql 미사용(raw sqlite3 핸들 1개/스레드) → 스레드 귀속 failure mode 원천 없음. WAL + busy_timeout(5000) 공유.
- 소멸 순서 안전. 단, 단일 거대 비디오 디코드 중 `wait()` 블로킹은 잔여 리스크.
- 썸네일/해상도 디코드는 전부 GUI 동기 실행 → 백그라운드·삭제 레이스 없음. FFmpeg 핸들은 스코프 내 open/close.
- 종료 후 늦은 콜백 재가동 불가 (재시작은 사용자 액션만).

## 5. v0.9.2.63 신규 기능 검사

- **Disk thumbnail cache**: 10개 시나리오(hit/miss/수정/size변경/교체/삭제/고아/prune/재실행/동시요청) 전수 추적, 설계대로. mtime ms 일치. 동시 요청은 GUI 단일 스레드.
- **mtime ms**: scanner/DB/GUI 일치.
- **ffprobe fallback**: 1회성·영속 캐시. 단 위 2항 PATH 문제.
- **favorite 정규화 / filename elide / file size fallback**: 정상 (툴팁 전체경로, 줌 리프레시 연결).
- **second DB connection**: `startScan` 가드 + 소멸 순서 + WAL로 커버.

## 6. Stale / Misleading Code

- **`ScanCancelled` (`scan_pipeline.h:12`)**: 미사용. 실제 중단은 cpp `LocalCancel` (내부 포착, partial 반환). 헤더 주석이 제거된 설계를 설명 → 삭제 + 주석 정리.
- **`saveMatches` 주석** ("deleted files disappear"): union 의미론(loaded 재저장)과 불일치.
- **데드 멤버**: `matches_`, `lastDone_`, `lastTotal_`.
- **Pairs `n(n-1)/2`**: union-find 컴포넌트 완전그래프 가정. 추이쌍 과다 계상 가능 → 가정 문서화.

## 7. Recommended Fix Order (의존성순)

```text
1. pause()/resume()/cancel() 직접 호출 (gui 수 줄 + 주석)
   └─ 없이는 StopCheck·체크포인트·CANCELLED UI 전부 무의미
2. ScanCancelled 헤더 제거 + saveMatches 주석 정정 + 데드 멤버 3개 제거
3. ffprobe/ffmpeg 앱-디렉토리 우선 조회 (포터블 필수)
4. 엔진 손상파일 skip (fail_count 컬럼 + 마이그레이션 + 재시도/리셋 + 테스트)
5. ETR pause 동결 + updateLastScan-on-cancel + Pairs 가정 문서화
6. QuickLook _wgetenv null 가드 (재개 시 함께, 25095f1 브랜치 기준)
7. GUI lifecycle 테스트 (1~4 이후에 의미 있음)
```
