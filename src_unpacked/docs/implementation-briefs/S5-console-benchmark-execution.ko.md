# Implementation Brief — S5 Console Benchmark Execution (Pre-register)

Status: **DESIGN BASELINE / REVALIDATION REQUIRED 2026-10-03** — CLI 개발 Benchmark의 현재 설계 계약이다. 기존 S5 구현은 역사적 구현 상태로 존재하지만, GUI 상세 로그 semantic reset 이후 S4 코드 정리와 함께 재검증해야 한다.
Version 기준: v0.9.4.43

---

## 1. 목적

S1~S3의 benchmark infrastructure가 확정되었고, 현재 S4는 GUI Detailed Logging으로 설계가 재정의된 상태다. GUI는 benchmark 실행 경로가 아니다.
CLI에서는 `--benchmark` controlled execution을 구현/검증한다.

S5는 Console benchmark 실행과 renderer를 담당한다.

1. **Console benchmark execution** — `--benchmark` 를 실제 실행 경로에 연결한다.
2. **Console renderer** — 실행 중/후 상황을 터미널에 표시한다.

둘 다 **기존 S2/S3/S4 계약을 그대로 재사용**한다.
새로운 benchmark engine, 별도의 검색/스캔 엔진, GUI 상태 모델의 Console 강제 재사용은 없다.

## 2. 시작 조건

- S1 CLOSED: `MediaScope`, `--scan`, CLI 파서 단위 테스트, MainWindow 비생성 경로
- S2 CLOSED: `BenchmarkRunner`, `BenchmarkExecutor` 경계, per-file × per-mode 결과 모델,
  aggregate precedence, 취소 상태 모델
- S3 CLOSED: `BenchmarkSession`(suite lock + journal + summary 재생성),
  `BenchmarkStorePaths`, replay/recovery, E2E 137 checks
- S4 current design: GUI Detailed Logging with `TelemetryRecorder/UserDiagnostic`; S5 does not depend on GUI benchmark execution UI or `BenchmarkGuiStorage`

## 3. 재사용 결정 (새 engine 없음)

```text
Console entry (gui/main.cpp) ─ runConsoleBenchmark()
  └ src/benchmark_console_renderer.*     ← NEW (표시 전용, Qt 없음)
  └ msf::BenchmarkSession  (S3)          ← suite lock + runs.jsonl + summary 재생성
      └ msf::BenchmarkRequest
          └ msf::BenchmarkRunner (S2)    ← run(request, selectedModes) 단일 호출
              └ ProductionBenchmarkExecutor
                  └ MediaSearchEngine (ignoredPaths 로 파일 1개만 분석)
```

- **S3 `BenchmarkSession` 을 그대로 쓴다.** Console 용으로 설계된 계약이며
  `begin/attach/finalize/regenerateSummary/replay` 를 이미 제공한다.
- **S4 `BenchmarkGuiStorage` 와 `gui/benchmark_worker` 는 쓰지 않는다.**
  GUI 스냅샷 저장소와 Console suite journal 은 분리된 계층이며, 섞지 않는다.
- **새로운 benchmark 실행 엔진은 만들지 않는다.** 기존 production scan 경로 재사용.

## 4. 반드시 유지하는 실행 계약

- 파일별 mode 실행 순서: **AUTO → CPU → GPU-max**
- GPU-max 는 **GPU-only 가 아니다.** 필수 CPU 작업과 fallback 은 기존 S2 의미 유지
- 각 mode 결과를 서로의 분석용 중간 결과로 공유하지 않는다
- 각 파일의 결과는 **완료 즉시 journal 에 기록**(S3 append + flush)
- aggregate precedence 불변: `Cancelled > Failed > Success > Skipped`
- S2/S3 의 mode result / case_complete / run_finished / run_cancelled / recovery
  semantics 그대로

## 5. Media scope

기존 CLI selector 계약을 그대로 사용한다.

```text
--media images
--media videos
--media all
```

새로운 media selector 이름이나 별도 해석 규칙을 만들지 않는다.
`msf::MediaScope` 을 재사용하고 `ScanControl::scanImages/scanVideos` 로 매핑한다.

## 6. CLI 진입 조건

### 유지해야 하는 기존 동작 (변경 없음)

```text
--help
--version
--smoke
--scan <folder> [--media images|videos|all]
인자 없음 → GUI 실행
```

- CLI 실행 시 **GUI `MainWindow` 를 생성하지 않는다**
  (`CommandLineOptions::needsMainWindow()` 는 Gui/Smoke 만 true 로 유지)
- 잘못된 인자는 기존 CLI error convention 을 따른다: stderr + usage +
  `Error: ...` + **exit 2** (`kCommandLineErrorExitCode`)

### 신규 옵션

| 옵션 | 의미 |
| --- | --- |
| `--benchmark <folder>` | benchmark 실행 대상 폴더 (필수 인자) |
| `--mode <auto,cpu,gpu-max>` | 실행할 mode 쉼표 목록. 기본 `auto,cpu,gpu-max` |
| `--suite <id>` | suite id. 생략 시 자동 생성 (§9-E) |
| `--log-dir <dir>` | benchmark durable storage root override (§9-B) |
| `--log <file>` | Console renderer 의 사람이 읽는 출력 file sink (§9-B) |

`--benchmark` 은 `--scan` 과 함께 주어지지 않는다(동일 실행 경로 충돌).
중복 지정은 오류로 처리한다.

## 7. Console renderer

`src/benchmark_console_renderer.{h,cpp}` — **신규, 순수 C++ , Qt 없음.**
그래서 단위 테스트가 가능하다.

### 7-1. 화면 구조 (storage-design.md terminal rendering contract)

```text
1. 고정 3-line 실행 헤더      ← 줄바꿈으로 밀리지 않는다
2. CURRENT FILE 상세 영역
3. 완료된 파일 compact history
4. 최종 또는 partial summary
```

### 7-2. 고정 헤더 표시 필수 항목

```text
Target
Scope
IMG / VID progress
Mode
CPU Resource
GPU
Distance
Suite ID
Build
Git
```

헤더는 **줄바꿈으로 계속 밀려 내려가는 형태를 사용하지 않는다.**

### 7-3. 긴 값의 표시 규칙

- source/path 등 긴 값은 **화면 표시에서만** middle-ellipsis 로 축약한다.
- **journal/JSON 에 기록되는 원본 값은 절대 축약하지 않는다.**
  축약은 renderer 의 출력 계층에서만 일어난다.

### 7-4. CURRENT FILE 영역

§9-A 결정에 따라 다음만 표시한다.

- 실제로 **완료된** case 의 mode 결과 3행 (AUTO / CPU / GPU-max)
  - requested mode, effective backend, status, elapsedMs
- **진행 중인 파일/모드의 이름·시간은 표시하지 않는다**
- 다음 파일이 처리 중이라는 사실은 **카운트로만** 알린다

### 7-5. 완료 파일 history

파일 한 건당 한 줄의 compact 형태. 완료 순서를 유지한다.

### 7-6. TTY / non-interactive 분리

- **TTY**: ANSI 로 상단 영역만 덮어쓰는 갱신
- **non-interactive** (CI, redirection, pipe): line-oriented 로 동일 정보를 순서대로 출력

두 환경 모두 **동일한 journal/storage 계약**을 사용하며,
표시 방식 때문에 journal schema 나 benchmark result semantics 를 변경하지 않는다.

## 8. Cancellation

- Console 은 **Ctrl+C 를 통한 cancellation**을 지원한다.
- Windows `SetConsoleCtrlHandler` 로 atomic flag 를 설정하고,
  그 flag 를 S2 의 `BenchmarkRequest::isCancelled` 가 읽는다.
  (`run()` 이 동기 호출이므로 별도 스레드를 새로 만들지 않는다.)
- 취소된 benchmark 는 **가능한 범위까지 진행 상황을 journal 에 보존**한다.
- commit marker 까지 도달한 case 는 기존 S3 recovery semantics 를 따른다.
- 프로그램 종료로 journal tail 이 불완전해질 수 있으며, 기존 recovery 규칙을 유지한다.
  마지막 개행 없는 tail 은 폐기, 중간 record 손상은 fatal.
- OS filesystem cache / process isolation 을 benchmark 결과 최적화 대상으로
  새롭게 다루지 않는다.

## 9. 착수 전 확정 결정 5건

### A. 진행 표시 — S2 observable contract 우선

**live mode / file / time 표시 없음.**
완료된 실제 mode 결과와 **실제로 관찰 가능한 progress** 만 표시한다.

- S2 에 실시간 callback 을 **추가하지 않는다**.
- 관찰 가능한 것: 완료된 case 수 / 전체 case 수(`onRunStarted` 의 `filesStarted`
  기준), case 의 `media` 에 의한 IMG/VID 카운트, renderer 자체가 잰 wall clock,
  그리고 완료된 case 의 최종 mode 결과.
- ETA 는 위 관찰값으로부터의 **파생값**이며 표시하지 않거나 "추정치"로 명시한다.
  benchmark 측정값이 아니다.

### B. `--log-dir` / `--log`

```text
--log-dir <dir>  benchmark durable storage root override
                (기본값 = 기존 portable app data root, S3 계약과 동일)
--log <file>     Console renderer 가 출력하는 "사람이 읽는 출력"의 file sink
```

- `--log` 는 **journal 복사본이 아니다.**
- `--log` 로 쓴 파일은 journal 로부터 재생성되지 않는다(표시 산출물).
- 두 옵션 모두 journal schema 를 변경하지 않는다.

### C. `--mode` 문법

```text
--mode auto,cpu,gpu-max      (쉼표 목록)
기본값                        auto,cpu,gpu-max
```

- **입력 순서와 무관하게 실행 순서는 항상 `AUTO → CPU → GPU-max`.**
  (S2 의 순서 계약이 우선이며, Renderer 도 이 순서로만 출력한다.)
- 알 수 없는 값 → 오류 exit 2.
- 빈 목록 → 오류 exit 2.

### D. `MSF_BUILD_GIT` (additive)

- `msf_build_version.h` 에 `MSF_BUILD_GIT` 을 **additive** 로 추가한다.
- git 사용 가능 → **short commit ID**
- git 사용 불가 → `unknown`
- **Git 문제로 build 가 실패하면 안 된다.** configure 단계에서 실패를 절대 반환하지 않는다.
- renderer 는 이 값을 헤더 `Git` 칸에 그대로 표시한다.

### E. `--suite` 생략 시 자동 Suite ID

- mockup 과 호환되는 human-readable 형식: **`YYYYMMDD-HHMM-SS`**
  (mockup 예시 `20260930-0801-01`)
- **고유성은 기존 S3 identity/storage 계약을 우선한다.**
  동일 시각에 같은 suite 디렉터리가 이미 존재하면 suffix 를 붙여 다음으로 넘어간다
  (`-2`, `-3`, …)그리고 journal 안의 `suiteId` 와 반드시 일치시킨다.

## 10. Resource Policy

S4 에서 추가된 `BenchmarkRequest::resourcePolicy`(optional) 계약을 그대로 쓴다.

- Console benchmark 는 **별도의 policy 생성 로직을 중복 구현하지 않는다.**
- 기본값은 `msf::make_policy(msf::ResourceMode::Balanced)` 로,
  기존 policy helper 를 그대로 사용한다.
- **Balanced 의 기본값 등 기존 policy semantics 를 임의로 변경하지 않는다.**
- GPU enablement 는 **현재 S2 계약(gpuEnabled 는 mode 파생)**을 따른다.
  이번 S5 에서 GUI GPU toggle 의 의미를 임의로 확장하지 않는다.

## 11. Dataset fingerprint

- S4 에서 검증된 경로를 재사용한다:
  `msf::computeDatasetFingerprint(root).fingerprint` 를 그대로 사용.
- **새로운 fingerprint 계산 알고리즘이나 새로운 hash serialization 을 만들지 않는다.**
- 동일 source dataset 에 대해 GUI 와 Console 이 서로 다른 fingerprint 를
  생성하는 구조를 만들지 않는다. (둘 다 같은 함수를 호출한다.)

## 12. 기존 benchmark storage 와의 경계

- S3 journal(`runs.jsonl`)이 **durable source of truth** 다.
- **legacy benchmark schema 를 삭제하거나 변경하지 않는다.**
- Console 출력 때문에 **JSONL journal schema 를 변경하지 않는다.**
- `summary.json` 은 **authoritative source 가 아니다.**
  필요하면 기존 journal 기반 regeneration semantics(`regenerateSummary`)를 따른다.
- GUI 스냅샷(`Benchmark/GUI/`)을 Console 이 benchmark history 로 읽지 않는다.

## 13. 범위 제한

이번 S5 에서 **하지 않는 것**:

- benchmark **성능 최적화** (O(N²) file-by-file scan 은 S2 에서
  correctness-first limitation 으로 이미 승인됨. 별도 검색 엔진이나 구조 변경 없음)
- **NVDEC production adoption** (현재 `NO` 유지)
- S4 에서 남은 다음 3개 항목의 구현 blocker 승격
  1. O(N²) walk acceptance
  2. CPU build 에서 GPU-max 가 `SKIPPED` 로 표시되는 UX 판단
  3. GUI GPU toggle 이 benchmark 에 영향을 줄 것인지 여부
  → 이 세 항목은 **별도 product decision** 으로 유지한다.
- terminal renderer 외의 UI 확장, S6 data mining, S7 help/usability
- S2/S3 core semantics 변경, S2 진행 hook 추가
- 새로운 benchmark execution engine

## 14. `CPU FB` — REJECTED (제품 코드에 구현하지 않음)

Console mockup 이 mode 결과 행마다 두던 `CPU FB`(CPU fallback 발생 여부) 표시는
**채택하지 않는다**.

- S2 계약에 구조화된 fallback 판정 지표가 없다.
- `effectiveMode == Cpu` 같은 다른 값을 `CPU FB` 로 재해석하지 않는다.
- **제품 코드에 임시 구현 후 삭제하는 방식을 쓰지 않으며, 실제로 한 번도 구현하지 않는다.**
- 상세 근거와 재검토 조건:
  `docs/architecture/legacy/S5_REJECTED_CPU_FB_INDICATOR.ko.md` / `.en.md`
  (worklog Performance / Tuning Experiment Index `S5-CPUFB` 색인 참조)

## 15. 테스트 계획

### CLI

- `--benchmark` 단독 / `--media` 결합 / `--mode` 결합
- 잘못된 benchmark 인자 → stderr + exit 2 (기존 convention)
- `--mode` 알 수 없는 값 / 빈 목록 → exit 2
- `--benchmark` 과 `--scan` 동시 지정 → 오류
- 기존 `--help` / `--version` / `--smoke` / `--scan` / 인자 없음(GUI) 회귀
- **CLI 실행이 MainWindow 를 생성하지 않음** 을 확인

### renderer

- 고정 헤더가 요구 필드를 모두 포함하는지
- 헤더가 줄바꿈으로 밀리지 않는지
- middle-ellipsis 축약이 **표시만** 하고 원본이 보존되는지
- IMG/VID 카운트가 실제 `MediaKind` 에서 계산되는지
- mode 표가 `AUTO → CPU → GPU-max` 순서로 출력되는지
- TTY / non-interactive 두 출력이 모두 유효한지
- 완료되지 않은 파일을 "current file" 로 표시하지 않는지

### 실행 (E2E)

- ffmpeg 실제 미디어로 실제 실행 → journal 기록 확인
- AUTO → CPU → GPU-max 순서 유지 확인
- 선택된 mode 만 journal 에 기록되는지
- summary 가 journal 재생성 결과인지 확인
- Ctrl+C 시 partial result 보존 확인
- non-interactive(파이프) 환경에서 결과 손상 없음 확인
- GUI 상세 로그는 별도 S4 사용자 작업 경로이며 S5가 GUI Benchmark UI를 요구하지 않는지 확인

### 회귀

- benchmark_core / journal / store / integration / gui_store / worker /
  ui_benchmark / ui_benchmark_e2e 전부 통과
- CPU 전체 CTest, GPU 전체 CTest
- `--benchmark` 가 실제 benchmark 실행으로 연결되었는지 확인
- `git diff --check`, stale object 없음, benchmark build artifact 잔여 없음

## 16. 완료 조건

아래를 모두 충족하기 전에는 S5 를 CLOSED 로 선언하지 않는다.

- Console `--benchmark` 가 실제 실행된다
- 기존 S2 executor 를 재사용한다
- 기존 S3 journal/storage 계약을 유지한다
- AUTO → CPU → GPU-max 실행 계약이 유지된다
- cancellation 과 partial preservation 이 동작한다
- TTY / non-interactive 출력이 모두 정상이다
- Console renderer 가 정의된 정보를 표시한다
- GUI 상세 로그는 독립된 S4 사용자 작업 경로로 유지된다
- S1/S2/S3 테스트 semantics 가 변경되지 않는다
- CPU 전체 CTest PASS / GPU 전체 CTest PASS
- `git diff --check` PASS / stale object 없음 / build artifact 잔여 없음
- 문서가 실제 검증 결과와 일치함

## 17. 구현 순서

1. CLI 확장 + 단위 테스트
2. renderer 구현 + 단위 테스트
3. main.cpp 연결 + console E2E
4. CPU/GPU 전체 회귀 + CLI 실제 실행 검증
5. 문서 갱신 (**실제 검증 후에만**)

## 18. 변경하지 않는 것 — 요약

- S2 execution semantics 전체 (ordering, aggregate precedence, 취소 상태 모델,
  executor 경계, 진행 hook 추가 금지)
- S3 journal / suite lock / summary 계약과 JSON schema
- S4 GUI 상세 로그 설계와 `TelemetryRecorder/UserDiagnostic` 경계
- S1 CLI 기존 동작 (`--help`/`--version`/`--smoke`/`--scan`/인자 없음 GUI)
- legacy `BenchmarkRecorder` schema
- F/NVDEC 상태 (`NO` 유지)

## 19. 현재 상태와 verification reference

이 문서는 **CLI 개발 Benchmark의 설계 기준 문서**다. 기존 S5 구현과 검증 결과는 역사적 증거로 보존하지만, S4 semantic reset 이후 실제 S5 재개 시 다시 검증한다. 실제 구현·검증 결과는 아래 문서에 분리되어 있다.

```text
f4c3fdd  S5: add benchmark CLI parsing
fcace68  S5: add console benchmark renderer
842ba01  S5: connect console benchmark execution
```

검증 기록: `docs/build-history/S5-verification.ko.md` / `.en.md`

상태: 기능 구현 완료, 자동/비대화형 E2E 검증 완료. 실제 TTY ANSI repaint 와 실제
Windows Ctrl+C trigger 검증은 **NOT RUN** 이다(검증 환경에 Windows console 이 없었음).

설계 문서와 실제 결과 사이에 확인된 차이는 두 가지뿐이며, 설계는 수정하지 않았다.

1. `Scanner::scan_stream()` 이 `FileState.kind` 를 설정하지 않아 benchmark 의
   `--media` 필터가 동작하지 않았다. S1 결함이며 S5-3 에서 최소 수정했다.
   제품에 이미 있던 `isVideoPath()` 규칙을 재사용했으며 새 classifier 는 없다.
2. S3 `benchmarkNowStamp()` 가 `localtime_s` 결과에 literal `Z` 를 붙인다.
   S3 문제이므로 이번 S5 에서 수정하지 않고 별도 부채로 기록한다.