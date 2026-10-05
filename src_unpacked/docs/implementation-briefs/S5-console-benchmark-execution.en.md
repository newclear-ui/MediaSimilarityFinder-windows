# Implementation Brief — S5 Console Benchmark Execution (Design Baseline)

Status: **REVALIDATED against the 0.9.4.44 validation baseline on 2026-10-04** — CLI `--benchmark` re-verified against the
S4 semantic-reset tree: real `--benchmark` execution exit=0 with
`suite.json`/`runs.jsonl`/`summary.json` written, S2 executor + S3
journal/storage contracts unchanged, CPU CTest 100/100 and GPU CTest 101/101.
S5 is still not declared CLOSED (see §16).
Current product baseline: v0.9.4.45; validation evidence baseline: v0.9.4.44

---

## 1. Purpose

S1-S3 benchmark infrastructure is established, and S4 has been redesigned as GUI Detailed Logging. The GUI is not a benchmark execution path.

S5 implements and verifies controlled Console benchmark execution through `--benchmark` and its renderer.

1. **Console benchmark execution** — connect `--benchmark` to a real execution path.
2. **Console renderer** — show progress and results in the terminal.

Both **reuse the existing S2/S3/S4 contracts unchanged**. No new benchmark engine,
no separate search/scan engine, and no forcible reuse of the GUI state model in the
Console.

## 2. Entry conditions

- S1 CLOSED: `MediaScope`, `--scan`, CLI parser unit tests, the no-MainWindow path
- S2 CLOSED: `BenchmarkRunner`, the `BenchmarkExecutor` boundary, the per-file ×
  per-mode result model, aggregate precedence, the cancellation state model
- S3 CLOSED: `BenchmarkSession` (suite lock + journal + summary regeneration),
  `BenchmarkStorePaths`, replay/recovery, 137 E2E checks
- S4 current design: GUI Detailed Logging with `TelemetryRecorder/UserDiagnostic`; S5 does not depend on GUI benchmark execution UI or `BenchmarkGuiStorage`

## 3. Reuse decision (no new engine)

```text
Console entry (gui/main.cpp) - runConsoleBenchmark()
  +- src/benchmark_console_renderer.*     <- NEW (display only, no Qt)
  +- msf::BenchmarkSession  (S3)          <- suite lock + runs.jsonl + summary
      +- msf::BenchmarkRequest
          +- msf::BenchmarkRunner (S2)    <- single run(request, selectedModes)
              +- ProductionBenchmarkExecutor
                  +- MediaSearchEngine (analyses one file via ignoredPaths)
```

- **S3 `BenchmarkSession` is used as is.** It was designed for the Console and
  already provides `begin/attach/finalize/regenerateSummary/replay`.
- **S4 `BenchmarkGuiStorage` and `gui/benchmark_worker` are NOT used.** The GUI
  snapshot store and the Console suite journal are separate layers and are not mixed.
- **No new benchmark execution engine.** The existing production scan path is reused.

## 4. Execution contracts that must hold

- File-level mode order: **AUTO -> CPU -> GPU-max**
- GPU-max is **not GPU-only.** The required CPU work and the fallback keep the
  existing S2 meaning.
- Mode results are not shared with one another as analysis intermediates.
- Each file's results are **appended to the journal as soon as they complete**
  (S3 append + flush).
- Aggregate precedence unchanged: `Cancelled > Failed > Success > Skipped`
- S2/S3 mode result / case_complete / run_finished / run_cancelled / recovery
  semantics unchanged

## 5. Media scope

The existing CLI selector contract is used unchanged.

```text
--media images
--media videos
--media all
```

No new media selector name and no separate interpretation rule.
`msf::MediaScope` is reused and mapped onto `ScanControl::scanImages/scanVideos`.

## 6. CLI entry conditions

### Existing behaviour that must be preserved (no change)

```text
--help
--version
--smoke
--scan <folder> [--media images|videos|all]
no arguments -> GUI launch
```

- CLI execution **never constructs a GUI `MainWindow`**
  (`CommandLineOptions::needsMainWindow()` stays true only for Gui/Smoke).
- Invalid arguments follow the existing CLI error convention: stderr + usage +
  `Error: ...` + **exit 2** (`kCommandLineErrorExitCode`).

### New options

| Option | Meaning |
| --- | --- |
| `--benchmark <folder>` | benchmark target folder (required argument) |
| `--mode <auto,cpu,gpu-max>` | comma list of modes to execute. Default `auto,cpu,gpu-max` |
| `--suite <id>` | suite id. Auto-generated when omitted (§9-E) |
| `--log-dir <dir>` | benchmark durable storage root override (§9-B) |
| `--log <file>` | file sink for the Console renderer's human-readable output (§9-B) |

`--benchmark` and `--scan` must not be given together (same execution path).
Giving either more than once is an error.

## 7. Console renderer

`src/benchmark_console_renderer.{h,cpp}` — **NEW, pure C++, no Qt**, so it can be
unit tested.

### 7-1. Screen structure (storage-design.md terminal rendering contract)

```text
1. fixed three-row execution header   <- never scrolls down with newlines
2. CURRENT FILE detail area
3. compact completed-file history
4. final or partial summary
```

### 7-2. Required header fields

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

The header **does not use a form that keeps being pushed down by newlines.**

### 7-3. Long-value display rule

- Long values such as source paths are abbreviated **only on screen**, with a
  middle-ellipsis.
- **The original value recorded in the journal/JSON is never abbreviated.**
  Abbreviation happens only in the renderer's output layer.

### 7-4. CURRENT FILE area

Per the §9-A decision, only the following is displayed.

- The mode results of a case that has actually **completed** (AUTO / CPU / GPU-max)
  - requested mode, effective backend, status, elapsedMs
- The name and elapsed time of an **in-flight** file/mode is **not** displayed.
- That the next file is being processed is signalled **only by a count**.

### 7-5. Completed-file history

One compact line per file, in completion order.

### 7-6. TTY / non-interactive separation

- **TTY**: refresh the top area in place using ANSI.
- **non-interactive** (CI, redirection, pipe): line-oriented output of the same
  information, in order.

Both environments use the **same journal/storage contract**, and the display
method never changes the journal schema or the benchmark result semantics.

## 8. Cancellation

- The Console supports **cancellation via Ctrl+C**.
- A Windows `SetConsoleCtrlHandler` sets an atomic flag that S2's
  `BenchmarkRequest::isCancelled` reads.
  (`run()` is a synchronous call, so no new thread is introduced.)
- A cancelled benchmark **preserves progress into the journal as far as possible.**
- A case that reached its commit marker follows the existing S3 recovery semantics.
- Program exit may leave an incomplete journal tail, and the existing recovery rules
  are kept: a final line without a newline is discarded, mid-file corruption is fatal.
- The OS filesystem cache and process isolation are not newly treated as benchmark
  result optimisation targets.

## 9. The five decisions confirmed before starting

### A. Progress display — S2 observable contract wins

**No live mode / file / time display.** Only completed real mode results and
**actually observable progress** are shown.

- **No real-time callback is added to S2.**
- Observable: completed case count / total case count (from `onRunStarted`'s
  `filesStarted`), IMG/VID counts derived from a case's `media`, the renderer's own
  wall clock, and the final mode results of completed cases.
- ETA is a **derived** value from the above, and is either omitted or explicitly
  labelled as an estimate. It is not a benchmark measurement.

### B. `--log-dir` / `--log`

```text
--log-dir <dir>  benchmark durable storage root override
                (default = the existing portable app data root, same S3 contract)
--log <file>     file sink for the Console renderer's "human-readable" output
```

- `--log` is **not a journal copy.**
- A file written by `--log` is not regenerated from the journal; it is a display
  artifact.
- Neither option changes the journal schema.

### C. `--mode` syntax

```text
--mode auto,cpu,gpu-max      (comma list)
default                      auto,cpu,gpu-max
```

- **Execution order is always `AUTO -> CPU -> GPU-max` regardless of input order.**
  The S2 order contract wins, and the renderer prints in that order too.
- Unknown value -> error exit 2.
- Empty list -> error exit 2.

### D. `MSF_BUILD_GIT` (additive)

- `MSF_BUILD_GIT` is **added** to `msf_build_version.h`.
- Git available -> **short commit ID**
- Git unavailable -> `unknown`
- **A git problem must never fail the build.** The configure step must never return
  failure because of git.
- The renderer prints this value verbatim in the header's `Git` field.

### E. Auto suite id when `--suite` is omitted

- Human-readable format compatible with the mockup: **`YYYYMMDD-HHMM-SS`**
  (mockup example `20260930-0801-01`)
- **Uniqueness follows the existing S3 identity/storage contract.** If the derived
  suite directory already exists, a suffix is appended and the next free one is used
  (`-2`, `-3`, ...), and it must match the `suiteId` written into the journal.

## 10. Resource Policy

Uses the `BenchmarkRequest::resourcePolicy` (optional) contract added in S4.

- The Console benchmark **does not duplicate policy creation logic.**
- The default is `msf::make_policy(msf::ResourceMode::Balanced)`, i.e. the existing
  policy helper as is.
- **Existing policy semantics such as the Balanced default are not changed
  arbitrarily.**
- GPU enablement follows the **current S2 contract** (`gpuEnabled` is derived from
  the mode). This S5 does not arbitrarily extend the GUI GPU toggle's meaning.

## 11. Dataset fingerprint

- Reuses the S4-verified path: `msf::computeDatasetFingerprint(root).fingerprint`
  used as is.
- **No new fingerprint algorithm and no new hash serialisation is created.**
- GUI and Console must not generate different fingerprints for the same source
  dataset. Both call the same function.

## 12. Boundaries with the existing benchmark storage

- The S3 journal (`runs.jsonl`) is the **durable source of truth**.
- The **legacy benchmark schema is neither deleted nor changed.**
- The Console output does **not** change the JSONL journal schema.
- `summary.json` is **not an authoritative source.** When needed, the existing
  journal-based regeneration semantics (`regenerateSummary`) is followed.
- The Console never reads GUI snapshots (`Benchmark/GUI/`) as benchmark history.

## 13. Scope limits

Not done in this S5:

- **Benchmark performance optimisation.** The O(N^2) file-by-file scan is already
  approved in S2 as a correctness-first limitation; no separate search engine and no
  structural change is made.
- **NVDEC production adoption** (stays `NO`).
- Promoting the three items S4 left open into implementation blockers:
  1. O(N^2) walk acceptance
  2. whether `SKIPPED` is adequate UX for gpu-max on a CPU build
  3. whether the GUI GPU toggle should affect benchmark runs
  These remain **separate product decisions.**
- UI expansion beyond the terminal renderer, S6 data mining, S7 help/usability
- S2/S3 core semantics changes, S2 progress hook additions
- A new benchmark execution engine

## 14. `CPU FB` — REJECTED (not implemented in the product)

The `CPU FB` display item the Console mockup placed on every mode result row
(whether a CPU fallback occurred) is **not adopted**.

- The S2 contract has no structured fallback verdict indicator.
- Other values such as `effectiveMode == Cpu` are **not** reinterpreted as `CPU FB`.
- **The "implement temporarily then remove" approach is not used, and it is never
  implemented in the product at all.**
- Detailed rationale and revisit conditions:
  `docs/architecture/legacy/S5_REJECTED_CPU_FB_INDICATOR.ko.md` / `.en.md`
  (see the worklog Performance / Tuning Experiment Index row `S5-CPUFB`)

## 15. Test plan

### CLI

- `--benchmark` alone, with `--media`, with `--mode`
- Invalid benchmark arguments -> stderr + exit 2 (existing convention)
- `--mode` unknown value / empty list -> exit 2
- `--benchmark` together with `--scan` -> error
- Regression for `--help` / `--version` / `--smoke` / `--scan` / no arguments (GUI)
- Confirm **CLI execution constructs no MainWindow**

### Renderer

- The fixed header contains every required field
- The header does not get pushed down by newlines
- Middle-ellipsis abbreviates **only** for display and preserves the original
- IMG/VID counts are computed from the real `MediaKind`
- The mode table prints in `AUTO -> CPU -> GPU-max` order
- Both TTY and non-interactive outputs are valid
- An unfinished file is never displayed as the "current file"

### Execution (E2E)

- Real execution on ffmpeg-generated media -> journal records present
- `AUTO -> CPU -> GPU-max` order preserved
- Only the selected modes are recorded in the journal
- The summary is a journal replay result
- Ctrl+C preserves partial results
- A non-interactive (piped) environment does not corrupt results
- GUI detailed logging remains a separate S4 user-workload path; S5 does not require GUI Benchmark UI

### Regression

- benchmark_core / journal / store / integration tests pass
  (the retired GUI benchmark worker/storage tests live under
  docs/architecture/legacy/ and are not part of the active suite)
- Full CPU CTest and full GPU CTest
- Confirm `--benchmark` is wired to a real benchmark execution
- `git diff --check`, no stale objects, no leftover benchmark build artifacts

## 16. Completion conditions

S5 is not declared CLOSED until all of the following hold.

- Console `--benchmark` actually runs
- The existing S2 executor is reused
- The existing S3 journal/storage contract is preserved
- The `AUTO -> CPU -> GPU-max` execution contract holds
- Cancellation and partial preservation work
- Both TTY and non-interactive output are correct
- The Console renderer displays the defined information
- GUI detailed logging remains a separate user-workload path
- S1/S2/S3 test semantics are unchanged
- Full CPU CTest PASS / full GPU CTest PASS
- `git diff --check` PASS, no stale objects, no leftover build artifacts
- The documents match the actual verification results

## 17. Implementation order

1. CLI extension + unit tests
2. Renderer implementation + unit tests
3. main.cpp wiring + console E2E
4. Full CPU/GPU regression + real CLI execution verification
5. Documentation update (**only after actual verification**)

## 18. Summary of what is not changed

- All S2 execution semantics (ordering, aggregate precedence, cancellation state
  model, executor boundary, no progress hook additions)
- The S3 journal / suite lock / summary contract and its JSON schema
- S4 GUI detailed-logging design and the `TelemetryRecorder/UserDiagnostic` boundary
- Existing S1 CLI behaviour (`--help`/`--version`/`--smoke`/`--scan`/no arguments)
- The legacy `BenchmarkRecorder` schema
- The F/NVDEC state (stays `NO`)

## 19. Current status and verification reference

This is the **CLI developer Benchmark design baseline**. Historical S5 implementation and verification evidence remains recorded, but S5 must be revalidated when development resumes after the S4 semantic reset. The actual
implementation and verification results are kept separate, in the documents below.

```text
f4c3fdd  S5: add benchmark CLI parsing
fcace68  S5: add console benchmark renderer
842ba01  S5: connect console benchmark execution
```

Verification record: `docs/build-history/S5-verification.ko.md` / `.en.md`

Status: feature implementation complete, automated/non-interactive E2E verification
complete. Real TTY ANSI repaint and real Windows Ctrl+C trigger verification are
**NOT RUN** (the verification environment had no Windows console).

There are exactly two confirmed differences between the design and the actual result.
The design was not changed for either.

1. `Scanner::scan_stream()` did not set `FileState.kind`, so the benchmark `--media`
   filter did not work. This is an S1 defect and was minimally fixed in S5-3 by
   reusing the `isVideoPath()` rule that already existed. No new classifier.
2. S3 `benchmarkNowStamp()` appends a literal `Z` to `localtime_s` output. This is an
   S3 problem, was not fixed in this S5, and is recorded as separate debt.
---

## Current status (2026-10-03, at cf126c4)

- S5 infrastructure: PASS / REVALIDATED (parser, renderer, BenchmarkRunner/BenchmarkSession wiring, journal/storage, cancellation, live --benchmark wiring, S2/S3 contract reuse).
- S5 product benchmark validation: DEFERRED. The implemented Search/Index/Comparison path still requires final product acceptance, so pre-acceptance benchmark numbers must not be treated as final performance/correctness evidence. A working `--benchmark` does not mean S5 CLOSED.
