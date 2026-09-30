# S5 Console benchmark execution 검증 기록 (2026-10-01)

기준 커밋: `f4c3fdd` (S5-1 CLI) → `fcace68` (S5-2 renderer) → `842ba01` (S5-3 실행 연결 + MediaKind 수정)
버전: `0.9.4.43` (`CMakeLists.txt` `project(... VERSION 0.9.4.43)` 단일 source)
설계 문서: `docs/implementation-briefs/S5-console-benchmark-execution.ko.md` / `.en.md`

---

## 1. 구현된 파일

| 단계 | 파일 | 내용 |
| --- | --- | --- |
| S5-1 | `src/command_line.{h,cpp}` | `CommandMode::Benchmark`, `--benchmark/--mode/--suite/--log-dir/--log` 파싱, canonical 정규화 |
| S5-1 | `tests/command_line_test.cpp` | 40 → **95** checks |
| S5-2 | `src/benchmark_console_renderer.{h,cpp}` | Qt-free 표시 전용 renderer, presentation model, TTY/non-TTY 분리 |
| S5-2 | `tests/benchmark_console_renderer_test.cpp` | **100** checks |
| S5-3 | `src/console_benchmark_cli.{h,cpp}` | `runConsoleBenchmark()` orchestration, suite ID, cancellation, sink |
| S5-3 | `tests/console_benchmark_cli_test.cpp` | **32** checks |
| S5-3 | `cmake/build_version.h.in`, `CMakeLists.txt` | `MSF_BUILD_GIT` additive (D 결정) |
| S5-3 | `src/scanner.cpp` | `FileState.kind` 미설정 결함 수정 (아래 3장) |
| S5-3 | `tests/scanner_test.cpp` | media kind 회귀 검증 |

## 2. 실제 검증 결과 (예상값 아님)

### benchmark 자체 검사

| 대상 | checks |
| --- | --- |
| `scanner_test` (S1) | media_files=4, images=2, videos=2 |
| `command_line_test` (S5-1) | **95** |
| `benchmark_console_renderer_test` (S5-2) | **100** |
| `console_benchmark_cli_test` (S5-3) | **32** |
| `benchmark_core_test` (S2) | 63 |
| `benchmark_journal_test` (S3) | 51 |
| `benchmark_store_test` (S3) | 51 |
| `benchmark_integration_test` (S3) | 137 |
| `benchmark_gui_store_test` (S4) | 110 |
| `benchmark_worker_test` (S4) | 35 |
| `ui_benchmark_test` (S4) | 34 |
| `ui_benchmark_e2e_test` (S4, 실제 엔진) | 40 |

### CTest

- **CPU: 96/96 PASS**
- **GPU: 97/97 PASS**
- CPU/GPU build 모두 exit 0
- stale object 확인: CPU/GPU `scanner.obj`, `scanner_test.obj`, `console_benchmark_cli.obj`,
  `benchmark_console_renderer.obj`, `console_benchmark_cli_test.obj`, `main.obj` 모두 소스보다 최신

### S1 CLI 회귀 (변경 없음 확인)

- `--version` → `Media Similarity Finder 0.9.4.43 (CUDA/CPU)` (exit 0)
- `--scan <없는 폴더>` → `Error: target folder does not exist` (exit 1)
- `--media bogus` → `Error: invalid --media value: bogus` (exit 2)
- 인자 없음 → GUI 유지
- `--benchmark` 는 이제 거부하지 않고 **실제 실행**한다

## 3. E2E 에서 발견하고 수정한 결함: `FileState.kind` 미설정

### 3-1. 정확한 원인

`--media images`, `--media videos`, `--media all` 이 **세 경우 모두 동일한 10개 파일**을
처리했다. 조사 결과:

```text
src/scanner.cpp
Scanner::scan_stream()
FileState.kind 미설정  ->  Unknown(0)
        ↓
benchmark_core.cpp: toMediaKind(fs.kind) == Unknown
        ↓
benchmark_core.cpp 의 기존 media filter 가 한 번도 발동하지 않음
```

`FileState` 를 만들면서 `s.path` / `s.size` / `s.modified` / `s.quickHash` 는 설정하지만
`s.kind` 는 설정하지 않아 기본값 `0` 이 남았다.

### 3-2. 수정 (최소, 1줄)

직전 줄에서 `isMediaPath()` 가 이미 통과했으므로 이 지점의 파일은 image 또는 video이며
Unknown 일 수 없다. 제품에 **이미 존재하던** 분류 규칙을 그대로 재사용했다.

```cpp
s.kind=(int)(isVideoPath(it->path())?MediaKind::Video:MediaKind::Image);
```

- 재사용한 기존 계약: `Scanner::isVideoPath()` / `Scanner::isMediaPath()` (scanner.cpp:11, 16).
  확장자 기반이며 대소문자를 정규화한다. `media_search_engine.cpp:23` 의 `kindOf()` 도
  **이 `isVideoPath()` 위에 구축**되어 있다.
- **새 classifier 를 만들지 않았다.** benchmark 전용 판정 로직도 없다.
- `toMediaKind()` 는 변경하지 않았다.
- Scanner 전체 refactor / media classification framework 도입 없음.

### 3-3. 영향 범위 — 실제 코드 추적 결과

초기에는 "제품 전체 image/video 구분이 이미 깨져 있을 가능성"으로 추정했으나,
**코드 추적 결과 이 추정은 틀렸으며 정정한다.**

Scanner `FileState.kind` 의 실제 소비 지점은 **한 곳뿐**이다.

```text
Scanner.scan_stream()  ->  FileState.kind
        ↓ 읽는 곳: benchmark_core.cpp:127  toMediaKind(fs.kind)   (유일)
```

반면 production 검색 경로는 scanner 의 `kind` 를 **한 번도 읽지 않고** 경로에서 자체 재계산한다.

```text
media_search_engine.cpp:610  cb.onFile(FileState&&)  ->  queue
        ↓
media_search_engine.cpp:569  processOne(FileState&& x)
   L571  const bool isVid=(kindOf(x.path)==MediaKind::Video);
   L584  sk.kind=(int)kindOf(x.path);
   L588  if(kindOf(x.path)==MediaKind::Image)
   L592  v.kind=(int)MediaKind::Video;
   ->  kindOf(p) = isVideoPath(p) ? Video : Image
```

따라서 실제 확인된 결과는 **production indexing / search 영향 없음** 이다.

이를 뒷받침한 기존 테스트 (모두 PASS):
`search_engine_test`, `search_report_test`, `scan_ignore_test`, `scan_streaming_test`,
`match_revalidate_test`, `index_manager_test`, 그리고 전체 CPU/GPU CTest.

### 3-4. 회귀 테스트가 실제로 결함을 잡는지 확인

수정을 임시로 비활성화하고 재빌드한 뒤 복원했다.

```text
수정 비활성화 시  scanner_test exit=3   (FAIL: Unknown 이 존재)
수정 복원 후      scanner_test exit=0   (PASS)
```

즉 이 테스트는 결함을 실제로 잡는다.

## 4. `--media` 최종 실제 결과

E2E dataset 실제 구성 (추정 아님):

```text
Image = 8   (jpg 5, png 3)
Video = 2   (mp4 2)
Total = 10
```

| 명령 | case 수 | journal `media` | exit |
| --- | --- | --- | --- |
| `--benchmark <ds> --media images` | **8** | `Image=8` | 0 |
| `--benchmark <ds> --media videos` | **2** | `Video=2` | 0 |
| `--benchmark <ds> --media all` | **10** | `Image=8, Video=2` | 0 |

모두 실제 dataset 구성과 일치한다. journal 의 `case_complete` 레코드에
`"media":"Image"` / `"media":"Video"` 가 기록된다.

### 옵션 순서 독립성 (S5-1 parser 계약이 실제 실행에서도 유지됨)

| 명령 | case 수 | journal `media` |
| --- | --- | --- |
| `--benchmark <ds> --media images` | 8 | `Image=8` |
| `--media images --benchmark <ds>` | 8 | `Image=8` |
| `--mode auto --media videos --benchmark <ds>` | 2 | `Video=2` |

## 5. S5-3 실제 실행 결과

### 5-1. 기본 benchmark (3 mode)

```text
exit 0 / Cases 10 / Success 10 / Skipped 0 / Records 42
Mode : AUTO -> CPU -> GPU-MAX
```

journal record 수 = `run_started` 1 + `mode_result` 30 + `case_complete` 10 + `run_finished` 1.

### 5-2. Mode — canonical order 확인

| 옵션 | 헤더 표시 | journal records |
| --- | --- | --- |
| (기본) | `AUTO -> CPU -> GPU-MAX` | 42 |
| `--mode auto` | `AUTO` | 22 |
| `--mode auto,cpu` | `AUTO -> CPU` | 32 |
| `--mode gpu-max,auto` | **`AUTO -> GPU-MAX`** | 32 |

입력 순서와 무관하게 canonical order 로 정규화됨을 실제 실행에서 확인했다.

### 5-3. 실제 E2E 관측된 capability 결과

- **CPU 빌드**: `AUTO` = `SKIPPED`, `CPU` = `SUCCESS`(실측 640–1304 ms), `GPU-MAX` = `SKIPPED`
- **GPU 빌드**: `AUTO` → `effectiveMode=CUDA` `SUCCESS` ×10, `CPU` = `SUCCESS` ×10,
  `CUDA` = `SUCCESS` ×10 (실측 약 590 ms)

이는 S2 의 `modeAvailable` 규칙이 두 빌드에서 올바르게 반영된 결과이며,
GPU 가 없는 빌드에서 성공으로 기록하지 않는다.

### 5-4. Suite

- explicit `--suite TEST-SUITE-001` → `suite.json` / `runs.jsonl` / `summary.json` **3곳 모두 동일 값**
- 자동 생성 → `20260930-2014-19` (형식 `YYYYMMDD-HHMM-SS`, UTC 기준)
- suite 디렉터리가 이미 있으면 `-2`, `-3` … 로 이동 (orchestrator unit test 32 checks 로 검증)
- 경로 탈락 거부: `--suite ..\..\evil`, `--suite a/b` → **exit 2**
  (`benchmarkSuitePaths()` 가 id 를 디렉터리 이름에 직접 연결하므로 CLI 경계에서 검증.
  S3 저장 계층은 변경하지 않았다.)

### 5-5. `--log-dir` / `--log`

- `--log-dir` → 지정한 storage root 에 `Benchmark/Console/suite-*/` 생성,
  기본 저장소(app 디렉터리)는 오염되지 않음
- `--log` → 4006 byte human-readable text 산출물, journal 복사본이 아니며 JSONL 이 아님,
  **ANSI escape 없음**

### 5-6. non-TTY

stdout redirect / pipe 환경에서 **ANSI escape 없음** 확인. line-oriented 출력.

## 6. S5-2 renderer 구현 결과

- Qt 없음, `msf_core` 소속. storage 생성 / journal 기록 / 실행 없음.
- presentation model 5종 (`ConsolePresentationInput`, `ConsoleCaseRow`, `ConsoleModeRow`,
  `ConsoleProgress`, `ConsoleSummary`) — 실행·스캔·journal·취소·resource 계산 책임 없음.
- **S2 enum 재사용**: `GpuBackendKind` / `BenchmarkStatus` / `MediaKind` 를 복사하지 않고 재사용.
  display 전용 enum 이 없으므로 renderer 가 S2 가 생산하지 않은 상태를 표현할 수 없다.
- 모든 측정/보고 값은 `std::optional`. 부재한 값은 **생략**되며 0 이나 `-` 로 대체되지 않는다.
  (측정된 `0.0` 과 미관측은 다른 사실)
- TTY / non-TTY: Interactive 는 ANSI 로 고정 영역만 repaint, LineOriented 는 cursor sequence 를
  전혀 출력하지 않음. 두 경로 모두 **같은 presentation input → 같은 content**.
- width 처리: Target 만 middle ellipsis, Build/Git 는 축약 전에 탈락, media 카운터는
  관측된 run-level pair 로 대체, mode order 는 축약하지 않음(실행 순서 오독 방지),
  마지막 resort 로 오른쪽부터 필드 탈락. **줄바꿈 없음.**
- **CURRENT FILE = 완료된 case 만.** 진행 중 파일/mode 는 표시하지 않는다 (A1).
- **ETA 미사용.** S2 에 total remaining work 계약이 없다.
- **CPU FB 미구현** (A2). `effectiveMode == Cpu`, `gpuEnabled == false`,
  `errorMessage` 문자열 검색 어느 것도 fallback 추론으로 쓰지 않았다.

## 7. NOT RUN — PASS 로 기록하지 않는다

이번 검증 환경은 Windows console 이 없었다.

```text
GetConsoleWindow() == NULL
```

### 실제 TTY ANSI repaint: **NOT RUN**

이유: 콘솔이 없어 실제 터미널에서 repaint / scrollback / 줄바꿈 여부를 볼 수 없었다.
프로세스 kill 로 대체하지 않았다.

확인된 것은 다음까지다:

- renderer unit test 의 ANSI 형식 검증 — **PASS** (100 checks)
- non-TTY 실제 E2E (ESC 없음, line-oriented) — **PASS**
- 짧은 경로 실행에서 `Files : IMG 1/8  VID 0/2` 로 media 분할이 실제 표시되는 것 — **PASS**

### 실제 Windows Ctrl+C trigger: **NOT RUN**

이유: 같은 환경에서 실제 console control event 를 전달할 수 없었다.
`GenerateConsoleCtrlEvent` 는 성공을 반환했지만 전달할 console 이 없었다.
**프로세스 kill 은 Ctrl+C 검증으로 표현하지 않았다.**

확인된 사실:

- `SetConsoleCtrlHandler` 등록 — 실행 로그에 `Ctrl+C : cancellation wired` 출력
- atomic cancellation flag wiring — 확인
- `BenchmarkRequest::isCancelled` 연결 — 확인
- deterministic cancellation 계약 — `benchmark_integration_test` **137 checks PASS**
  (`req.isCancelled` 를 실행 중 반전 → `run_cancelled` record + replay 검증)

## 8. S3 timestamp 불일치 — 이번 S5 에서 수정하지 않음

S5 E2E 중 발견한 기존 S3 문제:

```text
src/benchmark_store.cpp: benchmarkNowStamp()
  localtime_s(...) 로 로컬 시각을 얻고
  strftime("%Y-%m-%dT%H:%M:%SZ") 로 literal "Z" 를 붙인다
```

즉 journal timestamp 는 **로컬 시각 값을 UTC 로 표기**한다. 반면 S5 suite ID 는
`gmtime` 기반 진짜 UTC 라서 두 값이 로컬 오프셋만큼 어긋난다 (실측 약 9시간).

이번 S5 closeout 에서 `benchmark_store.cpp` 는 **변경하지 않았다.**
S3 후속 기술 부채로 별도 기록한다. suite ID 생성 방식도 변경하지 않았다.

## 9. A2 CPU FB 보존

아래 문서를 그대로 유지한다. S5 에서 CPU FB 를 구현하지 않았고,
별도 fallback inference 도 사용하지 않았다.

```text
docs/architecture/legacy/S5_REJECTED_CPU_FB_INDICATOR.ko.md
docs/architecture/legacy/S5_REJECTED_CPU_FB_INDICATOR.en.md
```

## 10. S5-3 구현 중 발견한 계층 결합 문제

`BenchmarkSession::attach()` 는 request 의 hook 을 `std::move` 로 옆으로 옮겨 두고
session 자체 journal writer 를 설치하며, 원래 hook 은 `detach()` 에서만 복원한다.
따라서 session 을 붙인 호출자가 `onCaseComplete` 를 직접 관찰하면 **아무것도 오지 않는다.**

S5-3 는 이 구조를 변경하지 않고, attach 가 설치한 hook 을 Console 이 감싸도록 구성했다.
journal 기록이 먼저 실행되고 표시가 뒤따르므로, **durable evidence 는 표시 계층에 의존하지 않는다.**
S3 는 그대로 유지되었다.

## 11. OS 임시 산출물 정리 결과

휴지통으로 보냈다 (`scripts/safe_remove.ps1`, 35건).

```text
%TEMP%\msf_s5_store            (--log-dir 로 만든 임시 benchmark storage)
%TEMP%\msf_s5_suiteid_test     (orchestrator unit test 가 만든 storage root)
캡처한 stdout/stderr 14개 파일
E2E / Ctrl+C / demo / 검증 스크립트 및 커밋 메시지 파일
demo.obj
```

유지했다:

```text
%TEMP%\msf_s5_e2e      (10개 파일) — S5 media 회귀 dataset
%TEMP%\msf_s5_cancel   (50개 파일) — 향후 실제 console cancellation 검증 dataset
```

프로젝트 `scratch/` 는 그대로 두었다.

`C:\m5s5` 는 `msf_s5_e2e` 대상 디렉터리 junction 이었다. Reparse point 를 휴지통으로
옮길 때 대상 데이터셋까지 지울 위험이 있어 **정리하지 않고 남겨 두었다** (현재도 존재).
대상 dataset 은 10개 파일 그대로 유지되어 있다.

## 12. S5 상태 판정

```text
S5 구현 및 자동/비대화형 E2E 검증 완료
실제 TTY ANSI repaint 검증        : NOT RUN
실제 Windows Ctrl+C trigger 검증 : NOT RUN
```

기능 구현은 완료 상태다. 외부 Windows console 환경에서만 가능한 두 검증은
미실행으로 보존한다. NOT RUN 항목은 7장에 근거와 함께 기록했다.

## 13. 남은 사항

- O(N²) folder walk — S2 특성, 그대로 유지 (S4 에서 이미 감수 결정)
- CPU 빌드에서 AUTO / GPU-max 가 `SKIPPED` 로 기록되는 표현이 UX 적정한지 — 제품 결정 필요
- S3 timestamp 표기 불일치 (8장) — S3 후속 작업
- 실제 TTY / Ctrl+C 검증 — 실제 Windows console 환경에서 수행 필요
- `C:\m5s5` junction 제거 — 사용자가 `rmdir C:\m5s5` 로 정리
