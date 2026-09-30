# Implementation Brief — S4 GUI Benchmark Integration (Pre-register)

Status: **PRE-REGISTERED** — 이 문서는 brief 와 Phase 1 조사 결과만 포함하며, S4 구현 코드는 이 문서보다 먼저 들어가면 안 된다.
Version 기준: v0.9.4.43

---

## 1. 목적

S1(Console entry)·S2(Benchmark core)·S3(Storage/Journal)가 CLOSED 이다. S3 는 benchmark
결과를 durable journal 에 기록하고 복구하는 것까지 검증되었지만, **실사용자는 Console CLI 로
benchmark 를 실행할 수 없다**(`--benchmark` 는 S5 책임이며 현재 미구현이고 의도적으로 거부된다).

S4 는 **GUI 안에서 benchmark 를 실행하고 그 결과를 저장·표시하는 경로**를 만든다.
목적은 두 가지다.

1. 사용자가 GUI 를 통해 선택한 mode 들의 성능을 측정하고 결과를 남긴다.
2. 그 결과가 Console suite 와 완전히 분리된 GUI 전용 저장소에 latest snapshot 으로 보존된다.

S4 는 **저장·표시 계층**이며, benchmark execution 은 이미 S2 가 검증한
`BenchmarkRunner` / `ProductionBenchmarkExecutor` 를 그대로 재사용한다. **새 benchmark engine 을
만들지 않는다.**

## 2. 시작 조건

- S1 CLOSED: `MediaScope`(Images/Videos/All) 정의, `--scan` 연결, CLI 파서 단위 테스트
- S2 CLOSED: `BenchmarkRunner`, `BenchmarkExecutor` 경계, per-file × per-mode 결과 모델,
  aggregate precedence `Cancelled > Failed > Success > Skipped`, 취소 상태 모델
- S3 CLOSED: suite storage/lock, append-only journal, replay/summary, E2E 137 checks
- 기존 GUI 가 이미 `msf_core` 를 링크하므로 **CMake 변경 없이** `BenchmarkRunner` 를 쓸 수 있다
- F/NVDEC 는 별도 investigation 상태이며 S4 범위 밖이다

## 3. Phase 1 조사 결과 — 기존 GUI 구조

### 3-1. toolbar 실제 구성 (`MainWindow::buildToolbar()`, gui/mainwindow.cpp:861-869)

```text
folder_ | browse_ | refresh_ | SEP |
scan_ | benchTgl_ | pause_ | cancel_ | SEP |
preset_ | cpu_ | kindBtn_ | SEP |
monBtn_ | gpuEnabled_ | logBtn_ | <spacer> | utilBtn_(우측 고정)
```

canonical mockup `uimock/mockup-B-ko-list.html` 은 HTML 주석에 이 순서를 명시하고 있고
(`buildToolbar() 838~846` 대응), 헤더 주석으로 "gui/mainwindow.cpp 의 `trStr()` 문자열과 레이아웃을
기준으로 한다"고 스스로 선언한다. 즉 **mockup-B 와 실제 MainWindow 는 현재 동기화되어 있고**,
S4 UI 는 이 체계를 그대로 따른다.

### 3-2. media selection

`kindBtn_`(QToolButton) → `kindMenu_` 의 checkable `kindImgAct_` / `kindVidAct_` →
bitmask `(img?1:0)|(vid?2:0)` → `ScanWorker(scanImages, scanVideos)` →
`ScanControl::scanImages/scanVideos` + `Scanner::count(..., scanImages_, scanVideos_, ...)`.

이것이 GUI 의 이미지/동영상 semantics 다. **S1 의 `MediaScope` 은 아직 GUI 에서 쓰이지 않는다.**
S4 는 이 bool 2개를 `MediaScope` 1개로 매핑할 뿐, **GUI 전용 enum 을 새로 만들지 않는다.**

### 3-3. resource policy

`preset_`(Maximum 90% / High 75% / Balanced 55% / … / Manual, 기본 Balanced) ,
`cpu_`(QSpinBox 10-90), `gpuEnabled_`(QCheckBox, objectName `gpuToggle`),
`policy_`(msf::ResourcePolicy). `resourceChanged(int)` → `ResourceMode(i+1)`,
`customResourceChanged()` → `ResourceMode::Custom`.

`ResourceMode { Maximum=1, High=2, Balanced=3, Gaming=4, Light=4, Custom=5 }`.

**benchmark mode 와는 다른 축이며 지금도 별개 위젯이다. 둘을 합치지 않는다.**

### 3-4. scan lifecycle / worker / cancel / progress

- `startScan()` 은 스캔마다 새 `QThread` + `ScanWorker` 를 만들고 `moveToThread` +
  `connect(QThread::started → ScanWorker::run)` + `thread_->start()` 한다.
- `ScanWorker::run()` 은 policy 적용 → `openIndexForRoot` → `Scanner::count`(targetCount) →
  `revalidateMatches` → `loadMatches` quick-load → `engine.scan()` → `results`/`finished`.
- **취소는 직접 호출**이다. `MainWindow::cancelScan()` 이 `worker_->cancel()` 을 queued 가 아닌
  직접 호출하는데, 기존 주석이 이유를 명시한다 — `run()` 이 worker event loop 를 점유하는 동안에는
  queued slot 이 영영 실행되지 않는다. `control_.cancel` 은 `std::atomic_bool`.
- progress 는 `statusProg_`(QProgressBar), `statusMsg_`/`statusCount_`,
  `scanStatusText(done,total,pct,path,elapsedMs)`(done/total/%, 현재 파일명, elapsed, 남은 시간),
  좌측 `sumDone_`/`sumGroups_`/`sumTime_`/`sumGpu_`/`sumCpu_`/`sumRam_` 라벨.
  worker 는 progress 를 ~150ms 로 스로틀한다.

### 3-5. 기존 benchmark UI: `benchTgl_`

`benchTgl_`(QCheckBox, objectName `benchTgl`, 기본 checked)는
`ScanWorker::benchmark_` → `ScanControl::benchmarkEnabled` →
`MediaSearchEngine::beginBenchmark()/abortBenchmark()/benchmarkJson()` (legacy `BenchmarkRecorder`,
schema 9) → signal `benchmarkReady(QString)` → `MainWindow::onBenchmark` → `lastBenchJson_` +
자동 `showBenchmarkDialog(json)`. `logBtn_` 가 마지막 결과를 다시 띄운다.

**중요한 판정**: 이 경로는 **별도의 benchmark 실행을 하지 않는다.** 실행은 이미 일어나고 있는
일반 스캔이고, `benchTgl_` 는 그 스캔에 legacy telemetry 계측을 붙일지 말지만 정한다
(`bcfg.detail = benchmark_`). 즉 **현재 존재하는 benchmark execution engine 은 여전히
`BenchmarkRunner` 하나뿐**이고, 두 번째 engine 이 생기지 않았다.

### 3-6. `MediaSearchEngine` / runner 재사용 경로

```text
MainWindow
  └─ benchmark worker (QThread, ScanWorker 와 동일한 패턴)
       └─ BenchmarkRequest  (S2 모델, hooks 없음)
            └─ BenchmarkRunner          ← S2/S3 에서 검증된 실행 경계
                 └─ ProductionBenchmarkExecutor
                      └─ MediaSearchEngine (ignoredPaths 로 파일 1개만 분석)
```

`MediaSimilarityFinder` 는 `msf_core` 를 PRIVATE 링크하고 `msf_core` 에
`benchmark_core.cpp` / `benchmark_store.cpp` / `benchmark_journal.cpp` /
`benchmark_session.cpp` / `command_line.cpp` 가 이미 포함되어 있다. include 경로도 `src` 가
등록되어 있다. → **S4 는 CMake 을 바꿀 필요가 없다(테스트 타깃 제외).**

## 4. 최종 결정

### 4-1. mode checkbox = execution selection

세 mode checkbox 는 **표시 옵션이 아니라 실행 선택**이다. 근거로 확정된 문서 3곳:

- `docs/architecture/benchmark-telemetry-roadmap.ko.md` L591
  "S4 GUI integration: 세 mode checkbox, **기본 모두 선택**, 기존 media 선택 결합, 최신 3개 보존"
- 같은 문서 L396 "**Run: 하나의 benchmark mode를 한 번 측정한 결과**" → Run 은 mode 1개 측정.
  L397 Suite 가 AUTO / CPU 단독 / GPU 최대화를 묶는다. L398
  "GUI는 각 JSON에 동일한 suiteId를 저장하면 되며 별도 suite.json은 필수가 아니다".
- `docs/architecture/storage-design.md` L88 / L126
  "GUI retains only the latest result for each of the three modes" / "per source folder".

가능한 선택: `AUTO` / `CPU` / `GPU-max` / `AUTO+CPU` / `AUTO+GPU-max` / `CPU+GPU-max` /
3개 모두. **최소 1개는 선택되어야 하며**, 0개면 benchmark 시작 버튼을 비활성화한다.
기본값은 3개 모두 선택이다.

### 4-2. Runner 는 한 번만 호출한다

`BenchmarkRunner::run(request, modes)` 는 이미 mode 벡터를 인자로 받으므로 **S2 core 를 변경하지
않는다.** GUI 는 선택된 subset 을 **한 번의 호출로** 전달한다.

```text
selectedModes = [AUTO, GPU-max]

실제 실행:
  file1 → AUTO → GPU-max
  file2 → AUTO → GPU-max
  ...
```

### 4-3. 파일 단위 mode ordering 유지 (금지 사항)

```text
AUTO 전체 → CPU 전체 → GPU 최대화 전체
```

이 형태는 **금지**된다. mode 별로 Runner 를 세 번 호출해 파일 순서를 깨뜨리지 않는다.
기준 순서는 `AUTO → CPU → GPU 최대화` 이며, 선택되지 않은 mode 는 그 자리가 건너뛰어진다.
이는 S2 의 per-file ordering 계약을 그대로 유지하는 것이며, S4 는 이 순서를 바꾸지 않는다.

### 4-4. per-mode snapshot 과 aggregate 재계산

Runner 결과의 `BenchmarkCaseResult::modeResults[]` 에서 **선택된 mode 만** 추출해 mode 별
snapshot 을 만든다.

- 선택한 mode 의 snapshot 은 갱신한다.
- 선택하지 않은 mode 의 기존 snapshot 은 **그대로 보존**하며, **삭제하지 않는다.**

```text
AUTO + GPU 최대화 선택  →  auto.json, gpu-max.json 갱신, cpu.json 보존
```

**aggregate 는 Case aggregate 를 복사하지 않는다.** 각 Case 의 `modeResults[]` 중 해당 mode 만
추출해 mode 별 aggregate 를 **다시 계산**하며, precedence 는 동일하다
(`Cancelled > Failed > Success > Skipped`).

```text
한 Case 가
  AUTO       SUCCESS
  CPU 단독    FAILED
  GPU 최대화  SUCCESS
  → Case aggregate = FAILED

파일 결과:
  auto.json      → SUCCESS
  cpu.json       → FAILED
  gpu-max.json   → SUCCESS
```

Case aggregate 인 `FAILED` 하나를 세 파일에 복사하지 않는다.

### 4-5. GUI snapshot 저장 정책 — atomic replacement

동일 source + 동일 mode 의 새 결과는 기존 snapshot 을 **atomic replace** 한다.

```text
Benchmark/GUI/<label>_<shortid>/auto.json
  → 같은 디렉터리에 temp file
  → flush
  → atomic rename/replace
```

**저장 실패 시 기존 snapshot 을 유지한다.** 중간 쓰기 실패로 최신 결과가 사라지는 일이 없어야 한다.

재사용: `msf::writeFileAtomic()` (`src/benchmark_store.cpp`) 가 이미 이 패턴을 구현하고 있다
(ProfileStore::save 와 동일한 temp + rename, 실패 시 remove + rename fallback).
S4 는 **새 atomic 유틸을 만들지 않고** 이 함수를 재사용한다. GUI 스냅샷은 전체 파일 교체이므로
Console journal 의 append + flush 보다 오히려 강한 보장을 갖는다.

파일명에 raw 전체 source path 를 쓰지 않는다. 디렉터리 이름은
`msf::sanitizeSourceLabel()` + `msf::shortRootId()` 로 만든
`<source-label>_<root-id-short>` 이며, canonical 전체 `sourceRoot` 는 JSON metadata 에 저장한다.

### 4-6. GUI benchmark index / cache 위치

```text
Application data root/
└─ Benchmark/
   └─ GUI/
      └─ <source-label>_<root-id-short>/
         ├─ auto.json
         ├─ cpu.json
         ├─ gpu-max.json
         └─ runtime/
            └─ run-<run-id>/
               ├─ auto/Index/
               ├─ cpu/Index/
               └─ gpu-max/Index/
```

- `auto.json` / `cpu.json` / `gpu-max.json` 은 **최신 결과 snapshot**
- `runtime/` 은 **실행용 isolated index/cache**
- normal Search Index 를 절대 사용하지 않는다
- source folder 에 artifact 생성을 금지한다
- Console benchmark storage 와 분리한다
- S3 의 `BenchmarkRequest::modeIndexApplicationDirectory` 와
  `IndexManager::indexRootFor()` 구조를 재사용한다

base directory 는 `storage-design.md` L90 이 명시한 대로 **기존 portable-aware 경로 정책을
따른다**: "The exact application-data base directory continues to follow the existing
portable-aware path policy; this design does not create a second unrelated root policy."
이 제품의 기존 정책은 `initAppSettings()` 가
`QSettings::setPath(IniFormat, UserScope, applicationDirPath())` 로 설정 INI 를 exe 옆에 두는
portable 구조이므로 root 은 `QApplication::applicationDirPath()` 이며 `%APPDATA%` 로 갈라지지 않는다.

**GUI runtime index 는 durable benchmark evidence 가 아니다.** 정상 완료 후 cleanup 할 수 있으나,
recovery 가 필요한 경우 **상태를 먼저 판정한 뒤** 삭제한다.

### 4-7. Pause 없음 — Cancel 만

benchmark 에 **독립적인 Pause/Resume 을 추가하지 않는다.**

- 기존 일반 검색의 Pause semantics 는 유지한다.
- benchmark 실행 중 Pause control 은 disable 하거나 노출하지 않는다.
- benchmark 는 Cancel/Stop 만 지원한다.
- cancellation 은 기존 S2/S3 semantics 를 그대로 사용한다
  (`Cancelled` / `Skipped` / journal recovery semantics 유지).
- Pause/Resume 은 별도 후속 작업으로 분리한다.
- `BenchmarkRecorder` 의 기존 paused 관련 legacy 필드는 임의로 삭제하거나 재정의하지 않는다.

전파 방식: `BenchmarkRequest::isCancelled` 는 `std::function<bool()>`, `ScanControl::cancel` 은
`std::atomic_bool` 이므로 `isCancelled = []{ return control.cancel.load(); }` 로 래핑한다.
S2 core 는 매 파일·매 mode 직전에 이를 재조회하므로 실행 중 취소가 S2 계약대로 동작한다
(진행 중 mode = `Cancelled`, 미시작 mode = `Skipped`).

### 4-8. legacy `benchTgl_` 취급

- `benchTgl_` **entry 자체는 즉시 삭제하지 않는다.**
- S4 의 새 benchmark UI 를 **canonical benchmark entry** 로 정의한다.
- **판정 근거(§3-5)**: `benchTgl_` 는 별도의 benchmark **실행**을 하지 않는다. 실행은 이미
  일어나고 있는 일반 스캔이며, 이 checkbox 는 그 스캔에 legacy telemetry 계측을 붙일지만
  정한다. 따라서 **현재 두 번째 benchmark execution engine 이 존재하지 않으며**, 이를 없애기 위해
  `benchTgl_` 를 disable 할 필요가 없다. S5/S6 가 legacy telemetry 를 소비할 수 있으므로 기능은
  유지한다.
- 대신 **UI 명칭 중복을 만들지 않는다.** 새 entry 는 checkbox 가 아니라 **실행 버튼**이므로
  위젯 종류와 semantics 가 다르며, label 도 "benchmark" 계열로 중복되지 않는 명사를 쓴다.
- `benchTgl_` 의 기존 label 과 동작은 변경하지 않는다.

**최종 결정 (착수 전 확정)**: 위 코드 추적 판정을 그대로 채택한다.

- `benchTgl_` 기존 기능은 **유지**하고 **임시 disable 하지 않는다.**
- 기존 checkbox 를 새로운 benchmark 실행 선택 UI 로 **재사용하지 않는다.**
- S4 의 새로운 benchmark 진입점은 **별도의 실행 버튼**으로 만든다.
- 새 실행 버튼은 benchmark 실행을 명확히 나타내는 이름/표현을 쓴다.
- 기존 `benchTgl_` 과 새 실행 버튼은 **서로 다른 기능으로 유지**한다.
- 별도의 두 번째 benchmark execution engine 을 만들지 않는다.
- 새 benchmark 실행은 반드시 기존 `BenchmarkRunner` 를 사용한다.
- 향후 legacy telemetry 제거 여부는 **S4 범위를 벗어난 별도 작업**으로 취급한다.

즉 **기존 checkbox 유지 + 새로운 benchmark 실행 버튼 추가**가 최종 결정이다.

### 4-9. GUI / Console storage isolation

```text
Benchmark/GUI/       최신 mode snapshot
Benchmark/Console/   장기 누적 suite / journal
```

- GUI 는 `Benchmark/Console/` journal 을 **자동으로 읽지 않는다.**
- Console 은 `Benchmark/GUI/` snapshot 을 장기 benchmark history 로 취급하지 않는다.
- GUI 는 journal 을 쓰지 않고 suite lock 을 쓰지 않는다. 재사용하는 것은
  `writeFileAtomic` / `sanitizeSourceLabel` / `shortRootId` 뿐이다.

### 4-10. 기존 media scope 재사용

GUI 의 기존 Images/Videos 선택 semantics 를 그대로 재사용하고 `MediaScope`
(`Images` / `Videos` / `All`) 으로 매핑한다. **새 enum 을 만들지 않는다.**
benchmark mode 와 media scope 는 **독립 축**이다. snapshot JSON 에는 실제 `mediaScope` 값을
저장한다.

### 4-11. CPU Resource Policy 와 benchmark mode 분리

- **benchmark mode**: `AUTO` / `CPU 단독` / `GPU 최대화`
- **CPU Resource Policy**: `Maximum` / `High` / `Balanced` / `Gaming` / `Manual`

두 축을 하나의 enum 이나 checkbox 로 합치지 않는다. S4 는 기존 `ResourcePolicy` 를 재사용한다.
**GPU 최대화를 NVDEC 와 연결하지 않는다.** 현재 GPU 최대화는 기존 CUDA/backend execution
semantics 를 사용한다.

### 4-12. S3 suite lock 재사용 (GUI 인스턴스 간 상호 배제)

같은 benchmark suite 에 대한 **동시 실행을 금지**한다. S3 가 정의한 suite lock 의미를 GUI 에서도
그대로 사용한다.

- 동일한 GUI benchmark suite 경로에 대해 다른 GUI 인스턴스가 benchmark 를 시작하려 하면
  **거부**한다.
- 거부 시 **기존 실행 중인 benchmark 의 journal / snapshot / runtime 을 건드리지 않는다.**
- 사용자에게 "해당 benchmark 가 이미 실행 중"이라는 **명확한 상태를 표시**한다.
- **서로 다른 source/suite 는 동시에 실행할 수 있다.**
- GUI lock 을 **별도의 새로운 locking system 으로 만들지 않는다.** S3 의
  `msf::BenchmarkSuiteLock` 계약을 그대로 재사용한다.

목적은 GUI 인스턴스 간 `journal`, `summary/snapshot`, `runtime index` 충돌 방지다.
suite 경로 자체가 달라지므로(§4-5, §4-6 의 `<source-label>_<root-id-short>`) lock 은 스냅샷과
같은 suite 단위 key 로 잡는다.

### 4-13. 스캔과 benchmark 는 직렬 실행

- 일반 Search/Scan 이 진행 중이면 **benchmark 실행 버튼을 비활성 상태**로 둔다.
- benchmark 가 진행 중이면 일반 스캔 실행도 충돌하지 않도록 **적절히 제한**한다.
- 기존 Pause/Resume 의미는 **변경하지 않는다.**
- benchmark 에는 Pause/Resume 을 추가하지 않는다(§4-7).
- benchmark 는 Cancel/Stop 만 제공한다.

스캔과 benchmark 동시 실행은 CPU/GPU 자원 경쟁, UI 상태, index lifecycle, progress reporting 이
복잡해지므로 **S4 에서는 직렬 실행으로 고정**한다.

### 4-14. O(N²) 을 사용자에게 노출하지 않는다

S2/S3 구현의 benchmark 실행 비용 특성(O(N²) folder walk)은 **그대로 유지**한다. S4 GUI 는
알고리즘 복잡도를 **표시하거나 경고하지 않는다.**

GUI 가 제공하는 진행 정보는 다음 정도에 한정한다.

- 현재 파일 / 전체 파일
- 현재 실행 중인 mode
- 전체 진행률
- 경과 시간
- Cancel 상태
- 완료 / 실패 / 취소 결과

일반 사용자가 보는 UI 에는 `O(N²)`, "매우 느릴 수 있음", 복잡도 경고 같은 문구를 **넣지 않는다.**
필요하면 **내부 로그 또는 개발 문서**에만 현재 구현의 O(N²) 특성을 명시할 수 있다.
성능 개선은 **향후 별도 Node/작업**으로 다룬다.

## 5. GUI snapshot JSON 스키마

새 결과 모델을 다시 만들지 않는다. S2 의 `BenchmarkRun` / `BenchmarkCaseResult` /
`BenchmarkModeResult` / `BenchmarkScanSummary` / `BenchmarkStatus` 를 재사용하고, snapshot 은
그 run 의 **per-mode 투영**이다.

필수 필드:

```text
schemaVersion          (별도 버전. legacy kBenchmarkSchemaVersion(9) 재사용 금지,
                        journal schema 1 도 주장 금지 — journal 이 아님)
appVersion             (생성 build version 헤더에서 주입, 하드코딩 금지)
suiteId                (세 mode 가 동일)
runId
mode                   (auto | cpu | gpu-max)
mediaScope             (images | videos | all — 실제 선택값)
sourceRoot             (canonical 전체 경로)
sourceRootLabel
sourceRootId
datasetFingerprint
startedAt
completedAt
status                 (해당 mode 의 aggregate, §4-4 재계산 결과)
file counts            (scanned / completed / remaining 등)
elapsed
summary
failure / cancellation 정보
```

`status` 는 `Cancelled` / `Failed` / `Success` / `Skipped` 를 그대로 사용하고,
`FAILED` 와 `CANCELLED` 를 구분한다. 취소 사유와 실패 메시지는 mode 단위로 보존한다.

## 6. Mockup 참조 — missing asset 상태 기록

canonical 위치: `uimock/mockup-C-gui-benchmark-layout.html`

복원 이력: `2c16f52`("docs: preserve exploratory GUI benchmark mockup C") 가 잘못된
`project/uimock/` 경로에 추가했고, `ef84827` 이 그 경로의 중복 파일을 삭제했다. canonical mockup
directory 는 저장소 루트 `uimock/` 이므로 본 brief 작성 시 `2c16f52` 의 blob 을 **바이트 동일하게**
그대로 복원했다(blob hash `840fc93098db45398ece23c2484ea8879d4f3fa8` 로 검증).

**missing asset 상태 — 반드시 기록한다:**

- `mockup-C-gui-benchmark-layout.jpg` 는 **git history 어디에도 존재한 적이 없다.**
  (`git log --all` 로 `uimock` 하위 `.jpg` 를 조회하면 `uimock/mockup-B-ko-list.jpg` 만 나온다.)
- 복원된 HTML 은 실제 목업 마크업이 아니라 **`<img src="mockup-C-gui-benchmark-layout.jpg">`
  래퍼 페이지**다(547자, 1줄). 즉 HTML 복원만으로는 목업의 시각 내용을 얻을 수 없다.
- **이미지를 새로 만들어 "복원했다고" 기록하지 않는다.** 없는 자산이다.
- 따라서 **S4 UI 레이아웃의 근거는 mockup-C 가 아니라** `mockup-B`(목록 뷰 GUI 목업, 시각 체계
  포함)와 실제 `MainWindow` 다. §3-1 에서 확인했듯 mockup-B 는 실제 `buildToolbar()` 순서와
  현재 동기화되어 있고 toolbar/진행 UI 는 여기에 맞춘다.
- 목업 자체가 말하는 성격("기존 GUI 화면과는 별도의 탐색안이며, 이번 Console 최종 목업과는
  독립된 참고 자료")을 존중해, benchmark UI 는 Console terminal UI(S5)와 혼동되지 않는
  별개 surface 로 둔다.

**확정 상태 (착수 전 확정 지시)**:

> mockup-C는 HTML wrapper까지는 history에서 복구되었으나 실제 referenced JPG가
> repository/git history에 존재하지 않아 시각 자산을 복원할 수 없다. 따라서 S4 구현은
> mockup-B와 현재 실제 MainWindow를 canonical UI reference로 사용한다.
> mockup-C의 신규 재설계 또는 추정 복원은 S4 범위에 포함하지 않는다.

즉 mockup-C 를 **복원하지도 새로 설계하지도 않는다.** 확인된 사실만 기록한다.

## 7. 취소 / 실패 semantics (S2 그대로)

- 실행 중 취소된 mode → `Cancelled`
- 취소 전에 시작되지 않은 mode → `Skipped`
- 한 mode 실패 → 그 mode 는 `Failed`, 같은 Case 의 다른 mode 는 계속 실행
- Case aggregate = `Cancelled > Failed > Success > Skipped`
- snapshot 은 선택된 mode 만 갱신하고, 취소/실패가 났더라도 **partial 결과를 기록한다**
  (미측정 = 0 이 아니라 `Skipped` / `Cancelled` 로 명시)

## 8. 범위 밖 (이번 S4 에서 하지 않는 것)

- `--benchmark` / `--mode` / `--suite` / `--log-dir` / `--log` (S5)
- CLI parser 수정 (S1 그대로, 미구현 옵션은 계속 거부)
- terminal renderer (S5)
- S6 suite 자동 실행 / dataset fingerprint 검증
- S7 help / usability / exit code
- NVDEC 통합, FFmpeg dependency 변경, CPU/GPU dependency 분리
- S2 execution semantics 변경
- 새로운 benchmark engine
- Pause / Resume

## 9. 테스트 계획 (구현 전 확정)

### Mode selection

- AUTO 단독 / CPU 단독 / GPU 최대화 단독
- AUTO + CPU / AUTO + GPU 최대화 / CPU + GPU 최대화
- 3개 모두
- 0개 선택 시 시작 버튼 비활성

### Ordering

선택된 mode 순서가 기준 `AUTO → CPU 단독 → GPU 최대화` 를 유지하는지 확인한다.
GPU 최대화 단독/부분 선택 시에도 **파일 단위 순서**가 유지되는지 확인한다.

### Snapshot

- `auto.json` / `cpu.json` / `gpu-max.json` 생성
- 재실행 시 **선택한 mode 만** 갱신
- **선택하지 않은 mode snapshot 보존**(삭제 없음)
- atomic replacement(temp → flush → replace), 저장 실패 시 기존 값 유지
- 파일명에 raw 전체 source path 없음, canonical sourceRoot 가 JSON metadata 에 있음

### Aggregate

- 한 Case 에서 CPU 만 실패 → `cpu.json` 은 `FAILED`, `auto.json` / `gpu-max.json` 은 `Success`
- Case aggregate 가 세 mode 파일에 복사되지 않음

### Cancellation

- 진행 중 mode 취소 → `Cancelled`
- 남은 mode → `Skipped`
- GUI 상태 표시
- snapshot 의 status 보존

### Failure

- 한 mode 실패 후 다른 선택 mode 계속 실행

### Isolation

- source 폴더 무오염
- normal Search Index 무변경
- GUI runtime index 가 Console / production index 와 분리
- **같은 suite 동시 실행 거부** — 두 번째 GUI 인스턴스가 같은 suite 경로의 benchmark 를 시작하면
  거부되고, 기존 journal/snapshot/runtime 이 그대로다
- **다른 suite 동시 실행 허용** — 서로 다른 source/suite 는 함께 실행 가능
- 거부 시 "이미 실행 중" 상태 표시
- suite lock 은 S3 `BenchmarkSuiteLock` 재사용이며 별도 locking system 을 만들지 않는다

### Serial execution (§4-13)

- 스캔 진행 중 benchmark 실행 버튼 비활성
- benchmark 진행 중 일반 스캔 제한
- Pause/Resume 의미 불변, benchmark 에 Pause 없음

### Progress presentation (§4-14)

- 현재 파일/전체, 현재 mode, 진행률, 경과 시간, Cancel 상태,
  완료/실패/취소 결과가 제공된다
- **UI 에 `O(N²)` / "매우 느릴 수 있음" 같은 복잡도 경고 문구가 나오지 않는다**
  (문자열 부재까지 검증)

### Regression

- normal GUI launch / GUI smoke
- 기존 image/video scope 동작
- 기존 scan 동작 (scan regression 0)
- `--version` / `--help` / `--smoke` / `--scan` / 잘못된 옵션 거부 / `--benchmark` 계속 거부
- CPU build + CTest 90/90 유지
- GPU build + CTest 91/91 유지
- `--tr-keys` 통과: 새 `trStr` 키는 KO/EN 양쪽에 추가

## 10. 착수 전 확정 계약 (5건 전부 확정됨)

아래 다섯 항목은 **모두 확정**되었다. 본 brief 는 이제 미확정 항목이 없다.

1. **`benchTgl_` 판정 — 확정(§4-8).** `benchTgl_` 은 이미 수행 중인 일반 스캔에 legacy telemetry
   를 연결할지 결정하는 기존 기능이며, benchmark 실행을 시작하지 않는다. 따라서 **기존 checkbox
   유지 + 새 benchmark 실행 버튼 추가**, 임시 disable 없음. 새 버튼은 benchmark 실행을 명확히
   나타내는 명칭을 쓰고 둘은 서로 다른 기능으로 남는다. legacy telemetry 제거는 S4 범위 밖.
2. **다중 GUI 인스턴스 lock — 확정(§4-12).** 같은 benchmark suite 동시 실행 금지. S3 의
   `msf::BenchmarkSuiteLock` 계약을 그대로 재사용하고, 새 locking system 을 만들지 않는다.
   거부 시 기존 journal/snapshot/runtime 을 건드리지 않고 "이미 실행 중" 상태를 표시하며,
   서로 다른 source/suite 는 동시 실행 가능.
3. **O(N²) 노출 — 확정(§4-14).** S2/S3 의 O(N²) 비용 특성은 유지하되 GUI 는 이를 표시하거나
   경고하지 않는다. UI 에는 현재 파일/전체, 현재 mode, 진행률, 경과 시간, Cancel 상태,
   완료/실패/취소 결과만 제공한다. 성능 개선은 향후 별도 작업.
4. **스캔 중 benchmark 시작 — 확정(§4-13).** 허용하지 않는다. 스캔 중에는 benchmark 버튼 비활성,
   benchmark 중에는 일반 스캔 제한. **직렬 실행 고정.** Pause/Resume 의미 불변.
5. **mockup-C missing asset — 확정(§6).** 복원하지도 새로 설계하지도 않는다. HTML wrapper 까지만
   history 에서 복구됐고 referenced JPG 가 존재하지 않으므로 시각 자산 복원이 불가하다.
   canonical UI reference 는 **mockup-B + 현재 실제 MainWindow** 다.

### S4 구현 공통 원칙 (확정 계약)

위 다섯 항목과 함께 아래 사항을 S4 구현의 확정 계약으로 유지한다.

- `BenchmarkRunner::run(request, selectedModes)` **단일 실행**
- **파일별 mode 실행 순서 유지**
- **선택되지 않은 mode snapshot 은 기존 파일을 보존**
- `auto.json` / `cpu.json` / `gpu-max.json` 은 **mode 별 결과만 기록**
- GUI benchmark runtime index 는 일반 Search Index 와 **완전히 분리**
- benchmark Cancel 은 기존 **S2/S3 cancellation semantics 를 그대로 사용**
- **Pause/Resume 은 추가하지 않음**
- **새로운 GUI benchmark engine 을 별도로 만들지 않음**
- **S3 suite lock 을 재사용**
- **mockup-C 를 추정하여 구현하지 않음**
- **기존 `benchTgl_` telemetry 기능을 제거하지 않음**
- 새로운 benchmark 진입점은 **별도 실행 버튼**으로 제공

## 11. 구현 순서

1. **Phase 1** — 기존 GUI 구조와 mockup 대조 → **완료(§3)**
2. **Phase 2** — S4 pre-register brief 생성 → **본 문서**
3. **Phase 3** — 구현(§10 의 1~6 확정 후 착수)
4. **Phase 4** — GUI functional test + CPU/GPU regression
5. **Phase 5** — 문서 업데이트(roadmap / progress / worklog, KO/EN)

## 12. 변경하지 않는 것 — 요약

- S2 execution semantics 전체 (ordering, aggregate precedence, 취소 상태 모델, executor 경계)
- S3 journal / suite lock / summary 계약
- S1 CLI parser 와 `--scan` 경로
- 기존 `BenchmarkRecorder` (legacy schema 9) 와 `benchTgl_` 동작
- normal Search Index 구조
- F/NVDEC 상태

## 13. 구현 상태 (Phase 3-1 ~ 3-4 반영, 2026-09-30)

**상태: CLOSED.** (Resource Policy 전달과 datasetFingerprint 해결 완료)

### 구현 파일

| Phase | 파일 |
| --- | --- |
| 3-1 | `src/benchmark_gui_store.{h,cpp}` — §4-5 §4-6 §4-9 §4-12 |
| 3-2 | `gui/benchmark_worker.{h,cpp}` — §6 §7 실행 경로 |
| 3-3 | `gui/mainwindow.{h,cpp}` — §1 §2 §4 §5 §6 §7 §8 UI |
| 3-4 | `tests/ui_benchmark_e2e_test.cpp` — 실제 엔진 E2E |

### 계약 충족 상태

- **§4-1 checkbox = 실행 선택** 충족. 유효 조합 7개, 0개면 실행 버튼 비활성.
- **§4-2 Runner 단일 호출** 충족. `benchmark_worker_test` 가 `a:auto a:gpu-max b:auto b:gpu-max` 순서를 검증.
- **§4-3 파일 단위 ordering** 충족. 전체 3개면 `auto>cpu>gpu-max`.
- **§4-4 per-mode snapshot + aggregate 재계산** 충족. `modeResults[]` 필터 후 S2 `aggregateStatus()` 재사용. CPU 만 실패한 Case → auto SUCCESS / cpu FAILED / gpu SUCCESS 를 저장 계층 테스트가 검증.
- **§4-5 atomic replace** 충족. `writeFileAtomic()` 재사용, 미선택 mode 보존을 byte 비교로 검증.
- **§4-6 GUI runtime 경로** 충족. `runtime/run-<id>/{auto,cpu,gpu-max}` 에만 인덱스 생성 확인.
- **§4-7 Pause 없음** 충족. Cancel/Stop 만 존재하며, benchmark 중 `pause_` 는 비활성.
- **§4-8 `benchTgl_` 유지** 충족. label 변경 없음, tooltip 만 추가. mode selector 로 재사용하지 않음.
- **§4-9 GUI/Console 격리** 충족. E2E 가 `Benchmark/Console` 미생성 확인.
- **§4-10 media scope 재사용** 충족. 기존 `kindImgAct_`/`kindVidAct_` → S1 `MediaScope` 매핑.
- **§4-12 S3 suite lock 재사용** 충족. 실제 E2E 가 외부 holder 상태에서 실행 미시작 + snapshot 불변 확인.
- **§4-13 직렬 실행** 충족. 스캔 중 benchmark 비활성, benchmark 중 스캔/pause/checkbox 잠금, stop 활성.
- **§4-14 O(N²) 비노출** 충족. UI 문구에 복잡도 경고 없음.
- **§6 진행 표시** 충족. "완료 k/N · 마지막 <파일>". 진행 중 파일 추측 없음.
- **§7 저장 기준** 충족. `finished` 결과만 사용.
- **§8 상태 구분** 충족. 완료 / 취소 / 실패 / lock busy 를 서로 다른 문구로 표시.

### 검증 결과

storage 110 · worker 35 · UI 34 · **실제 엔진 GUI E2E 40** checks.
**CPU CTest 94/94 · GPU CTest 95/95.**
CPU 빌드 `gpu-max` = `SKIPPED`, GPU 빌드 `gpu-max` = `SUCCESS`.

### 미충족 / 미구현

- **§4-11 Resource Policy 재사용: 충족.** `BenchmarkRequest::resourcePolicy`(optional, additive) 를
  추가했고 `ProductionBenchmarkExecutor::runMode()` 이 전달된 policy 에서 출발한다.
  미지정 시 기존 S2 동작(엔진 기본 policy + mode 별 gpuEnabled)이 그대로 유지되어
  기존 호출자에 영향이 없다. GUI 는 MainWindow 가 이미 `make_policy()` 로 해석해 둔 `policy_` 를
  그대로 전달하므로 preset/CPU 해석을 복제하지 않았다.
  실제 E2E 에서 toolbar preset "Maximum 90%" 선택 → snapshot `cpuPercent: 90` 기록을 확인한다.
  **남는 제약**: `gpuEnabled` 는 mode 가 결정하므로 GUI 의 `gpuEnabled_` 는 AUTO/GPU-max 실행에
  영향을 주지 않는다(지시대로 S2 규칙 유지).
- **§10 mode 조합의 직접 검증:** 간접 검증만. `selectedBenchModes()` 는 private 유지.
- **O(N²) walk 유지.** S4 는 저장·UI 계층이므로 scan 을 최적화하지 않는다.

**`datasetFingerprint` 도 해결됨.** worker 가 fingerprint 미지정 시 기존
`msf::computeDatasetFingerprint(root).fingerprint` 를 verbatim 으로 사용한다.
새 해시도 새 직렬화 형식도 만들지 않았고, `DatasetFingerprint` 는 공개 멤버 구조체라
accessor 추가가 필요 없었다. 계산은 worker 스레드에서 수행한다(UI 비차단).
실제 E2E 에서 64자 hex 값이 기록되고 `computeDatasetFingerprint(root).fingerprint` 와
완전히 동일함을 확인했다.

**상태: CLOSED.** 남은 확인 사항은 O(N²) 감수 여부, CPU 빌드 SKIPPED 표현의 UX,
GUI GPU 토글 반영 필요성 3가지이며 구현 결함이 아니라 제품 결정 사항이다.

상세 판정: `docs/build-history/S4-phase3-4-verification.ko.md` / `.en.md`
