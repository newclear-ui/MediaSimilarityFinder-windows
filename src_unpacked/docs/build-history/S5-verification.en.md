# S5 Console benchmark execution verification record (2026-10-01)

Baseline commits: `f4c3fdd` (S5-1 CLI) -> `fcace68` (S5-2 renderer) -> `842ba01` (S5-3 execution wiring + MediaKind fix)
Version: `0.9.4.43` (single source: `CMakeLists.txt` `project(... VERSION 0.9.4.43)`)
Design document: `docs/implementation-briefs/S5-console-benchmark-execution.ko.md` / `.en.md`

---

## 1. Implemented files

| Stage | File | Content |
| --- | --- | --- |
| S5-1 | `src/command_line.{h,cpp}` | `CommandMode::Benchmark`, `--benchmark/--mode/--suite/--log-dir/--log` parsing, canonical normalization |
| S5-1 | `tests/command_line_test.cpp` | 40 -> **95** checks |
| S5-2 | `src/benchmark_console_renderer.{h,cpp}` | Qt-free display-only renderer, presentation model, TTY/non-TTY split |
| S5-2 | `tests/benchmark_console_renderer_test.cpp` | **100** checks |
| S5-3 | `src/console_benchmark_cli.{h,cpp}` | `runConsoleBenchmark()` orchestration, suite id, cancellation, sink |
| S5-3 | `tests/console_benchmark_cli_test.cpp` | **32** checks |
| S5-3 | `cmake/build_version.h.in`, `CMakeLists.txt` | `MSF_BUILD_GIT`, additive (decision D) |
| S5-3 | `src/scanner.cpp` | `FileState.kind` unset defect fix (section 3) |
| S5-3 | `tests/scanner_test.cpp` | media kind regression coverage |

## 2. Actual verification results (not expected values)

### Benchmark self-checks

| Target | checks |
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
| `ui_benchmark_e2e_test` (S4, real engine) | 40 |

### CTest

- **CPU: 96/96 PASS**
- **GPU: 97/97 PASS**
- CPU and GPU build both exit 0
- Stale object check: CPU/GPU `scanner.obj`, `scanner_test.obj`, `console_benchmark_cli.obj`,
  `benchmark_console_renderer.obj`, `console_benchmark_cli_test.obj`, `main.obj` all newer than source

### S1 CLI regression (unchanged confirmed)

- `--version` -> `Media Similarity Finder 0.9.4.43 (CUDA/CPU)` (exit 0)
- `--scan <missing folder>` -> `Error: target folder does not exist` (exit 1)
- `--media bogus` -> `Error: invalid --media value: bogus` (exit 2)
- No arguments -> GUI still launches
- `--benchmark` is no longer refused and now **actually runs**

## 3. Defect found during E2E and fixed: `FileState.kind` unset

### 3-1. Exact cause

`--media images`, `--media videos` and `--media all` all processed **the same 10 files**.
Investigation:

```text
src/scanner.cpp
Scanner::scan_stream()
FileState.kind not set  ->  Unknown(0)
        |
        v
benchmark_core.cpp: toMediaKind(fs.kind) == Unknown
        |
        v
benchmark_core.cpp's existing media filter never fired at all
```

`scan_stream()` set `s.path`, `s.size`, `s.modified` and `s.quickHash` when building each
`FileState`, but never set `s.kind`, so the default `0` remained.

### 3-2. Fix (minimal, one line)

`isMediaPath()` already passed on the line above, so a file reaching this point is an
image or a video and can never be Unknown. The classifier that **already existed in the
product** is reused as-is.

```cpp
s.kind=(int)(isVideoPath(it->path())?MediaKind::Video:MediaKind::Image);
```

- Reused existing contract: `Scanner::isVideoPath()` / `Scanner::isMediaPath()`
  (scanner.cpp:11, 16). Extension based, case normalized. `kindOf()` at
  `media_search_engine.cpp:23` is itself **built on this `isVideoPath()`**.
- **No new classifier was created.** No benchmark-specific classification either.
- `toMediaKind()` was not changed.
- No Scanner refactor and no media classification framework.

### 3-3. Impact scope -- actual code trace

It was initially estimated that "image/video classification may already be broken
product-wide". **Code tracing showed that estimate was wrong, and it is corrected here.**

`Scanner` `FileState.kind` has exactly **one** consumer.

```text
Scanner.scan_stream()  ->  FileState.kind
        |
        v read only by: benchmark_core.cpp:127  toMediaKind(fs.kind)   (the only place)
```

The production search path never reads the scanner's `kind` at all. It recomputes it
from the path itself.

```text
media_search_engine.cpp:610  cb.onFile(FileState&&)  ->  queue
        |
        v
media_search_engine.cpp:569  processOne(FileState&& x)
   L571  const bool isVid=(kindOf(x.path)==MediaKind::Video);
   L584  sk.kind=(int)kindOf(x.path);
   L588  if(kindOf(x.path)==MediaKind::Image)
   L592  v.kind=(int)MediaKind::Video;
   ->  kindOf(p) = isVideoPath(p) ? Video : Image
```

Therefore the actually confirmed result is **no impact on production indexing / search**.

Existing tests backing that conclusion (all PASS):
`search_engine_test`, `search_report_test`, `scan_ignore_test`, `scan_streaming_test`,
`match_revalidate_test`, `index_manager_test`, plus the full CPU/GPU CTest.

### 3-4. Confirming the regression test actually catches the defect

The fix was temporarily disabled and rebuilt, then restored.

```text
fix disabled   scanner_test exit=3   (FAIL: Unknown present)
fix restored   scanner_test exit=0   (PASS)
```

So the test does catch the defect.

## 4. Final actual `--media` results

Actual E2E dataset composition (not assumed):

```text
Image = 8   (jpg 5, png 3)
Video = 2   (mp4 2)
Total = 10
```

| Command | case count | journal `media` | exit |
| --- | --- | --- | --- |
| `--benchmark <ds> --media images` | **8** | `Image=8` | 0 |
| `--benchmark <ds> --media videos` | **2** | `Video=2` | 0 |
| `--benchmark <ds> --media all` | **10** | `Image=8, Video=2` | 0 |

All match the real dataset composition. The journal `case_complete` records now carry
`"media":"Image"` / `"media":"Video"`.

### Option order independence (the S5-1 parser contract holds in real execution too)

| Command | case count | journal `media` |
| --- | --- | --- |
| `--benchmark <ds> --media images` | 8 | `Image=8` |
| `--media images --benchmark <ds>` | 8 | `Image=8` |
| `--mode auto --media videos --benchmark <ds>` | 2 | `Video=2` |

## 5. S5-3 actual execution results

### 5-1. Default benchmark (3 modes)

```text
exit 0 / Cases 10 / Success 10 / Skipped 0 / Records 42
Mode : AUTO -> CPU -> GPU-MAX
```

Journal record count = `run_started` 1 + `mode_result` 30 + `case_complete` 10 + `run_finished` 1.

### 5-2. Mode -- canonical order confirmed

| Option | Header shown | journal records |
| --- | --- | --- |
| (default) | `AUTO -> CPU -> GPU-MAX` | 42 |
| `--mode auto` | `AUTO` | 22 |
| `--mode auto,cpu` | `AUTO -> CPU` | 32 |
| `--mode gpu-max,auto` | **`AUTO -> GPU-MAX`** | 32 |

Normalization to canonical order regardless of input order was confirmed in real execution.

### 5-3. Capability results observed in real E2E

- **CPU build**: `AUTO` = `SKIPPED`, `CPU` = `SUCCESS` (measured 640-1304 ms), `GPU-MAX` = `SKIPPED`
- **GPU build**: `AUTO` -> `effectiveMode=CUDA` `SUCCESS` x10, `CPU` = `SUCCESS` x10,
  `CUDA` = `SUCCESS` x10 (about 590 ms measured)

This is S2's `modeAvailable` rule correctly reflected in both builds. A build without a
GPU is never recorded as success.

### 5-4. Suite

- explicit `--suite TEST-SUITE-001` -> identical value in **all three** of `suite.json` / `runs.jsonl` / `summary.json`
- auto generated -> `20260930-2014-19` (format `YYYYMMDD-HHMM-SS`, UTC based)
- when a suite directory already exists, it walks to `-2`, `-3`, ... (covered by the 32 orchestrator unit checks)
- path traversal refused: `--suite ..\..\evil`, `--suite a/b` -> **exit 2**
  (`benchmarkSuitePaths()` concatenates the id straight into a directory name, so it is
  validated at the CLI boundary. The S3 storage layer was not changed.)

### 5-5. `--log-dir` / `--log`

- `--log-dir` -> creates `Benchmark/Console/suite-*/` under the given storage root;
  the default storage (application directory) is not polluted
- `--log` -> 4006 byte human-readable text artifact, not a journal copy and not JSONL,
  and **no ANSI escape**

### 5-6. non-TTY

**No ANSI escape** confirmed under stdout redirect / pipe. Line-oriented output.

## 6. S5-2 renderer implementation results

- No Qt, member of `msf_core`. No storage creation, no journal writing, no execution.
- Five presentation models (`ConsolePresentationInput`, `ConsoleCaseRow`, `ConsoleModeRow`,
  `ConsoleProgress`, `ConsoleSummary`) -- no execution, scanning, journal, cancellation
  or resource calculation responsibility.
- **S2 enums reused**: `GpuBackendKind` / `BenchmarkStatus` / `MediaKind` are reused, not
  copied. With no display-only enum, the renderer cannot express a state S2 never produced.
- Every measured or reported value is `std::optional`. An absent value is **omitted** and
  never replaced by 0 or a dash. (A measured `0.0` and an unobserved value are different facts.)
- TTY / non-TTY: Interactive repaints only the fixed region with ANSI; LineOriented emits
  no cursor sequence at all. Both paths render **the same presentation input to the same content**.
- Width handling: only Target is middle-ellipsized, Build/Git are dropped before anything
  is shortened, media counters fall back to the observed run-level pair, the mode order is
  never abbreviated (abbreviating it would misstate the execution order), and fields are
  dropped from the right as a last resort. **No line wrapping.**
- **CURRENT FILE = completed case only.** In-flight file/mode is not shown (A1).
- **No ETA.** S2 has no total remaining work contract.
- **No CPU FB** (A2). Neither `effectiveMode == Cpu`, nor `gpuEnabled == false`, nor an
  `errorMessage` string search is used to infer fallback.

## 7. NOT RUN -- not recorded as PASS

This verification environment had no Windows console.

```text
GetConsoleWindow() == NULL
```

### Real TTY ANSI repaint: **NOT RUN**

Reason: with no console, repaint, scrollback and line wrapping could not be observed in
a real terminal. A process kill was not substituted.

What was confirmed is only the following:

- renderer unit test ANSI format verification -- **PASS** (100 checks)
- real non-TTY E2E (no ESC, line-oriented) -- **PASS**
- a short-path run displaying the media split as `Files : IMG 1/8  VID 0/2` -- **PASS**

### Real Windows Ctrl+C trigger: **NOT RUN**

Reason: the same environment could not deliver a real console control event.
`GenerateConsoleCtrlEvent` returned success but there was no console to deliver to.
**A process kill was not represented as Ctrl+C verification.**

Confirmed facts:

- `SetConsoleCtrlHandler` registration -- run log shows `Ctrl+C : cancellation wired`
- atomic cancellation flag wiring -- confirmed
- `BenchmarkRequest::isCancelled` connection -- confirmed
- deterministic cancellation contract -- `benchmark_integration_test` **137 checks PASS**
  (flips `req.isCancelled` mid-run -> `run_cancelled` record + replay verified)

## 8. S3 timestamp mismatch -- not fixed in this S5

Existing S3 problem found during S5 E2E:

```text
src/benchmark_store.cpp: benchmarkNowStamp()
  obtains local time with localtime_s(...)
  and appends a literal "Z" via strftime("%Y-%m-%dT%H:%M:%SZ")
```

So a journal timestamp carries a **local time value labelled as UTC**. The S5 suite id is
based on real UTC via `gmtime`, so the two differ by the local offset (about 9 hours
measured).

`benchmark_store.cpp` was **not changed** in this S5 closeout. This is recorded as a
separate S3 technical debt item. The suite id generation method was not changed either.

## 9. A2 CPU FB preserved

The documents below are kept as-is. CPU FB was not implemented in S5, and no separate
fallback inference was used.

```text
docs/architecture/legacy/S5_REJECTED_CPU_FB_INDICATOR.ko.md
docs/architecture/legacy/S5_REJECTED_CPU_FB_INDICATOR.en.md
```

## 10. Layer coupling problem found while implementing S5-3

`BenchmarkSession::attach()` `std::move`s the request's hooks aside, installs the session's
own journal writers, and only restores the original hooks in `detach()`. A caller that
attaches a session and sets `onCaseComplete` itself therefore **observes nothing**.

S5-3 did not change that structure. Instead the Console wraps the hooks that `attach()`
installs. The journal write still runs first and the display follows, so **durable
evidence never depends on the display layer**. S3 was left unchanged.

## 11. OS temporary artifact cleanup result

Moved to the Recycle Bin (`scripts/safe_remove.ps1`, 35 items).

```text
%TEMP%\msf_s5_store            (temporary benchmark storage made by --log-dir)
%TEMP%\msf_s5_suiteid_test     (storage root made by the orchestrator unit test)
14 captured stdout/stderr files
E2E / Ctrl+C / demo / validation scripts and commit message files
demo.obj
```

Kept:

```text
%TEMP%\msf_s5_e2e      (10 files) -- S5 media regression dataset
%TEMP%\msf_s5_cancel   (50 files) -- dataset for future real console cancellation verification
```

The project `scratch/` was left untouched.

`C:\m5s5` was a directory junction pointing at `msf_s5_e2e`. Because moving a reparse
point to the Recycle Bin risks following it into the dataset that must be kept, it was
**deliberately not cleaned up and left in place** (it still exists). The target dataset
is intact with all 10 files.

## 12. S5 status verdict

```text
S5 implementation and automated/non-interactive E2E verification complete
Real TTY ANSI repaint verification        : NOT RUN
Real Windows Ctrl+C trigger verification  : NOT RUN
```

The feature implementation is complete. The two verifications possible only in an
external Windows console environment are preserved as not run. The NOT RUN items are
recorded in section 7 with their reasons.

## 13. Remaining items

- O(N^2) folder walk -- an S2 characteristic, kept as-is (already accepted in S4)
- Whether `SKIPPED` for AUTO / GPU-max on a CPU build is the right UX -- product decision
- S3 timestamp labelling mismatch (section 8) -- S3 follow-up work
- Real TTY / Ctrl+C verification -- needs an actual Windows console environment
- `C:\m5s5` junction removal -- the user can clean it with `rmdir C:\m5s5`
