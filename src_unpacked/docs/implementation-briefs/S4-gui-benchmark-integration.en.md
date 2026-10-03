# Implementation Brief — S4 GUI Benchmark Integration (Pre-register)

Status: **PRE-REGISTERED** — this document contains the brief and the Phase 1 investigation results only; S4 implementation code must not land before it.
Version basis: v0.9.4.43

> **Actual state, corrected 2026-10-03 (commit `030aaf2`)** — at Phase 3-4 regression/doc refresh time the shipped implementation differs from some statements in the body below.
>
> - **The GUI benchmark entry point is only the `[Benchmark]` checkbox (`benchTgl_`).** The separate `Run benchmark / Stop / Status` toolbar was **removed** (`benchRun_`/`benchStop_`/`benchStatus_`). The GUI no longer has a standalone benchmark-execution workflow.
> - `AUTO / CPU only / GPU MAX` are **no longer "benchmark mode execution selection".** They are the **upper entries of the execution resource/performance setting**, single-select (mutually exclusive). The fine CPU budget (`Balanced…` preset and CPU usage) sits below them. `preset_`/`cpu_`/`gpuEnabled_` are integrated into the same resource setting area.
> - Photo/video scope is **two independent toggle buttons** (`mediaImgBtn_`/`mediaVidBtn_`) instead of a `kindBtn_` dropdown. Both OFF is forbidden (the last one left ON cannot be turned off). The `kindMask`/`benchMediaScope()`/`ScanWorker` wiring is kept.
> - **Actual behaviour of the `benchTgl_` path:** during a scan the legacy `BenchmarkRecorder` (`MediaSearchEngine::beginBenchmark`) collects per-file timings and at scan end yields `engine_.benchmarkJson()` — a **legacy JSON** summary. It does **not** write the append-only S3 journal, does **not** record `datasetFingerprint`, and is therefore **not** ingestible by S6. That durable-journal role belonged to the removed S4 startBenchmark path (`BenchmarkGuiStorage`), now disconnected. This is a follow-up item (see "Open" below).
> - `selectedBenchModes()`/`startBenchmark()`/`cancelBenchmark()` are **DORMANT** now that the entry points were removed. They remain compilable but are not used for GUI behaviour.
>
> ### Open (recorded from Phase 3-4)
> - A GUI search performed with `[Benchmark]` ON does **not** produce a durable S3 journal (legacy JSON only). To satisfy "benchmark data collected/recorded", the `benchTgl_` scan path must be wired to `BenchmarkGuiStorage`/the S3 journal — follow-up work. This Phase 3-4 only records that fact rather than forcing a workaround.
> - `datasetFingerprint` can be empty on the GUI journal path. A reusable generator exists in the S3 console benchmark but is not yet wired into the GUI path. Record it as "unimplemented/unconnected", not as a successful identity proof.
> - CLI `--benchmark` stays `rejected` until S5.

---

*The body below is kept as the original S4 design (pre-registered) text. The 2026-10-03 implementation is governed by the correction above.*

## 1. Purpose

S1 (Console entry), S2 (Benchmark core) and S3 (Storage/Journal) are CLOSED. S3 verified that
benchmark results can be recorded in a durable journal and recovered, but **a real user still
cannot run a benchmark**: `--benchmark` belongs to S5, is not implemented, and is deliberately
rejected.

S4 creates the path that **runs a benchmark inside the GUI and stores/shows its result**.
It has two goals.

1. Let the user measure the selected modes from the GUI and keep the result.
2. Keep that result as a latest snapshot in a GUI-only store that is completely separate
   from the Console suite.

S4 is a **storage and presentation layer**. Benchmark execution reuses the already verified
`BenchmarkRunner` / `ProductionBenchmarkExecutor` from S2. **No new benchmark engine is created.**

## 2. Entry conditions

- S1 CLOSED: `MediaScope` (Images/Videos/All), `--scan` wiring, CLI parser unit tests
- S2 CLOSED: `BenchmarkRunner`, the `BenchmarkExecutor` boundary, the per-file × per-mode result
  model, aggregate precedence `Cancelled > Failed > Success > Skipped`, the cancellation state model
- S3 CLOSED: suite storage/lock, the append-only journal, replay/summary, 137 E2E checks
- The GUI already links `msf_core`, so `BenchmarkRunner` is usable with **no CMake change**
- F/NVDEC is in a separate investigation state and is out of S4 scope

## 3. Phase 1 investigation — existing GUI structure

### 3-1. Actual toolbar composition (`MainWindow::buildToolbar()`, gui/mainwindow.cpp:861-869)

```text
folder_ | browse_ | refresh_ | SEP |
scan_ | benchTgl_ | pause_ | cancel_ | SEP |
preset_ | cpu_ | kindBtn_ | SEP |
monBtn_ | gpuEnabled_ | logBtn_ | <spacer> | utilBtn_(pinned right)
```

The canonical mockup `uimock/mockup-B-ko-list.html` states this order in an HTML comment
(`buildToolbar() 838~846` correspondence) and declares in its header comment that it follows the
`trStr()` strings and layout of `gui/mainwindow.cpp`. In other words **mockup-B and the real
MainWindow are currently in sync**, and S4 UI follows that system.

### 3-2. Media selection

`kindBtn_` (QToolButton) → `kindMenu_` with checkable `kindImgAct_` / `kindVidAct_` →
bitmask `(img?1:0)|(vid?2:0)` → `ScanWorker(scanImages, scanVideos)` →
`ScanControl::scanImages/scanVideos` + `Scanner::count(..., scanImages_, scanVideos_, ...)`.

That is the GUI's image/video semantics. **S1's `MediaScope` is not used by the GUI yet.**
S4 only maps those two booleans onto one `MediaScope`; **it creates no GUI-specific enum.**

### 3-3. Resource policy

`preset_` (Maximum 90% / High 75% / Balanced 55% / … / Manual, default Balanced),
`cpu_` (QSpinBox 10-90), `gpuEnabled_` (QCheckBox, objectName `gpuToggle`),
`policy_` (msf::ResourcePolicy). `resourceChanged(int)` → `ResourceMode(i+1)`,
`customResourceChanged()` → `ResourceMode::Custom`.

`ResourceMode { Maximum=1, High=2, Balanced=3, Gaming=4, Light=4, Custom=5 }`.

**This is a different axis from benchmark mode and is already a separate widget. They are not merged.**

### 3-4. Scan lifecycle / worker / cancel / progress

- `startScan()` creates a fresh `QThread` + `ScanWorker` per scan, then `moveToThread` +
  `connect(QThread::started → ScanWorker::run)` + `thread_->start()`.
- `ScanWorker::run()` applies the policy → `openIndexForRoot` → `Scanner::count` (targetCount) →
  `revalidateMatches` → `loadMatches` quick-load → `engine.scan()` → `results`/`finished`.
- **Cancel is a direct call.** `MainWindow::cancelScan()` calls `worker_->cancel()` directly
  rather than queued, and the existing comment states why: while `run()` occupies the worker
  event loop, a queued slot can never run in time. `control_.cancel` is a `std::atomic_bool`.
- Progress uses `statusProg_` (QProgressBar), `statusMsg_`/`statusCount_`,
  `scanStatusText(done,total,pct,path,elapsedMs)` (done/total/%, current filename, elapsed,
  remaining time), and the left-hand `sumDone_`/`sumGroups_`/`sumTime_`/`sumGpu_`/`sumCpu_`/`sumRam_`
  labels. The worker throttles progress to ~150 ms.

### 3-5. Existing benchmark UI: `benchTgl_`

`benchTgl_` (QCheckBox, objectName `benchTgl`, checked by default) drives
`ScanWorker::benchmark_` → `ScanControl::benchmarkEnabled` →
`MediaSearchEngine::beginBenchmark()/abortBenchmark()/benchmarkJson()` (legacy `BenchmarkRecorder`,
schema 9) → signal `benchmarkReady(QString)` → `MainWindow::onBenchmark` → `lastBenchJson_` plus
an automatic `showBenchmarkDialog(json)`. `logBtn_` reopens the last result.

**Important determination**: this path does **not** perform a separate benchmark execution. The
execution is the ordinary scan that is already happening; `benchTgl_` only decides whether legacy
telemetry instrumentation is attached to it (`bcfg.detail = benchmark_`). So **the only benchmark
execution engine that exists is still `BenchmarkRunner`**, and no second engine has appeared.

### 3-6. `MediaSearchEngine` / runner reuse path

```text
MainWindow
  └─ benchmark worker (QThread, identical pattern to ScanWorker)
       └─ BenchmarkRequest  (S2 model, no hooks)
            └─ BenchmarkRunner          ← the execution boundary verified in S2/S3
                 └─ ProductionBenchmarkExecutor
                      └─ MediaSearchEngine (ignoredPaths analyses exactly one file)
```

`MediaSimilarityFinder` links `msf_core` PRIVATE, and `msf_core` already contains
`benchmark_core.cpp` / `benchmark_store.cpp` / `benchmark_journal.cpp` /
`benchmark_session.cpp` / `command_line.cpp`. The include path registers `src` as well.
→ **S4 needs no CMake change (test targets excepted).**

## 4. Final decisions

### 4-1. Mode checkbox = execution selection

The three mode checkboxes are **not a display option but an execution selection**. The documents
that settle this:

- `docs/architecture/benchmark-telemetry-roadmap.ko.md` L591
  "S4 GUI integration: three mode checkboxes, **all selected by default**, combined with the
  existing media selection, retain the latest 3"
- The same document L396 "**Run: the result of measuring one benchmark mode once**" → a Run is one
  mode measurement. L397 has the Suite binding AUTO / CPU-only / GPU-max. L398
  "the GUI only has to store the same suiteId in each JSON; a separate suite.json is not required".
- `docs/architecture/storage-design.md` L88 / L126
  "GUI retains only the latest result for each of the three modes" / "per source folder".

Possible selections: `AUTO` / `CPU` / `GPU-max` / `AUTO+CPU` / `AUTO+GPU-max` / `CPU+GPU-max` /
all three. **At least one mode must be selected**, and with zero selected the benchmark start
button is disabled. The default is all three.

### 4-2. The Runner is invoked exactly once

`BenchmarkRunner::run(request, modes)` already takes a mode vector, so **S2 core is not changed.**
The GUI passes the selected subset **in a single invocation**.

```text
selectedModes = [AUTO, GPU-max]

actual execution:
  file1 → AUTO → GPU-max
  file2 → AUTO → GPU-max
  ...
```

### 4-3. File-level mode ordering is preserved (forbidden alternative)

```text
all AUTO → all CPU → all GPU-max
```

is **forbidden**. The Runner is not called three times separately to break the file order.
The reference order is `AUTO → CPU → GPU-max`, and unselected modes are skipped in place.
This keeps the S2 per-file ordering contract intact; S4 does not change it.

### 4-4. Per-mode snapshot and recomputed aggregate

The **selected modes only** are extracted from the Runner result's
`BenchmarkCaseResult::modeResults[]` to produce per-mode snapshots.

- Snapshots of selected modes are updated.
- Existing snapshots of **unselected** modes are **preserved as-is and never deleted**.

```text
AUTO + GPU-max selected  →  auto.json, gpu-max.json updated; cpu.json preserved
```

**The aggregate is not copied from the Case aggregate.** For each Case, only the matching mode is
extracted from `modeResults[]` and the per-mode aggregate is **recomputed** with the same
precedence (`Cancelled > Failed > Success > Skipped`).

```text
one Case reports
  AUTO        SUCCESS
  CPU-only    FAILED
  GPU-max     SUCCESS
  → Case aggregate = FAILED

file results:
  auto.json      → SUCCESS
  cpu.json       → FAILED
  gpu-max.json   → SUCCESS
```

The single Case aggregate `FAILED` is not copied into all three files.

### 4-5. GUI snapshot storage policy — atomic replacement

A new result for the same source and same mode **atomically replaces** the existing snapshot.

```text
Benchmark/GUI/<label>_<shortid>/auto.json
  → temp file in the same directory
  → flush
  → atomic rename/replace
```

**On a storage failure the existing snapshot is kept.** An interrupted write must never destroy
the latest result.

Reuse: `msf::writeFileAtomic()` in `src/benchmark_store.cpp` already implements this pattern
(temp + rename like `ProfileStore::save`, with a remove + rename fallback on failure).
S4 **creates no new atomic utility** and reuses that function. Because a GUI snapshot is a whole
file replacement, it actually has a stronger guarantee than the Console journal's append + flush.

Raw full source paths are never used as filenames. The directory name is
`<source-label>_<root-id-short>` built from `msf::sanitizeSourceLabel()` + `msf::shortRootId()`,
and the canonical full `sourceRoot` is stored in the JSON metadata.

### 4-6. GUI benchmark index / cache location

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

- `auto.json` / `cpu.json` / `gpu-max.json` are **latest result snapshots**
- `runtime/` is the **isolated execution index/cache**
- The normal Search Index is never used
- No artifacts are created inside the source folder
- It is separated from the Console benchmark storage
- It reuses S3's `BenchmarkRequest::modeIndexApplicationDirectory` and the
  `IndexManager::indexRootFor()` structure

The base directory follows the **existing portable-aware path policy**, as stated in
`storage-design.md` L90: "The exact application-data base directory continues to follow the
existing portable-aware path policy; this design does not create a second unrelated root policy."
This product's existing policy is the portable structure where `initAppSettings()` calls
`QSettings::setPath(IniFormat, UserScope, applicationDirPath())` to keep the settings INI beside
the executable, so the root is `QApplication::applicationDirPath()` and does not diverge to
`%APPDATA%`.

**The GUI runtime index is not durable benchmark evidence.** It may be cleaned up after normal
completion, but when recovery is needed the **state must be determined first and only then
deleted**.

### 4-7. No Pause — Cancel only

No **independent Pause/Resume is added for benchmark.**

- The Pause semantics of the existing normal search are kept.
- During a benchmark run the Pause control is disabled or not exposed.
- Benchmark supports Cancel/Stop only.
- Cancellation uses the existing S2/S3 semantics unchanged
  (`Cancelled` / `Skipped` / journal recovery semantics retained).
- Pause/Resume is split off into separate follow-up work.
- The existing legacy paused-related fields of `BenchmarkRecorder` are not arbitrarily deleted
  or redefined.

Propagation: `BenchmarkRequest::isCancelled` is a `std::function<bool()>` and
`ScanControl::cancel` is a `std::atomic_bool`, so the wrapper is
`isCancelled = []{ return control.cancel.load(); }`. The S2 core re-reads it before every file and
every mode, so cancelling during a run behaves exactly per the S2 contract (interrupted mode =
`Cancelled`, unstarted mode = `Skipped`).

### 4-8. Handling legacy `benchTgl_`

- The `benchTgl_` **entry itself is not deleted immediately.**
- S4's new benchmark UI is defined as the **canonical benchmark entry**.
- **Determining evidence (§3-5)**: `benchTgl_` does not perform a separate benchmark
  **execution**. The execution is the ordinary scan already taking place, and this checkbox only
  decides whether legacy telemetry instrumentation is attached to it. Therefore **no second
  benchmark execution engine exists today**, and there is no need to disable `benchTgl_` to
  eliminate one. The feature is kept because S5/S6 may consume the legacy telemetry.
- Instead, **no duplicate naming is created in the UI.** The new entry is a **run button**, not a
  checkbox, so the widget type and semantics differ, and its label avoids a duplicate
  "benchmark" wording.
- The existing label and behaviour of `benchTgl_` are not changed.

**Final decision (confirmed before start)**: the code-tracing determination above is adopted as is.

- The existing `benchTgl_` feature is **kept** and **not temporarily disabled**.
- The existing checkbox is **not reused** as the new benchmark execution selection UI.
- S4's new benchmark entry point is a **separate run button**.
- The new run button uses a name/expression that clearly denotes benchmark execution.
- The existing `benchTgl_` and the new run button **remain separate features**.
- No separate second benchmark execution engine is created.
- New benchmark execution must use the existing `BenchmarkRunner`.
- Whether the legacy telemetry is removed later is treated as **separate work outside S4 scope**.

In other words **keeping the existing checkbox and adding a new benchmark run button** is the final
decision.

### 4-9. GUI / Console storage isolation

```text
Benchmark/GUI/       latest mode snapshots
Benchmark/Console/   long-lived cumulative suite / journal
```

- The GUI **does not automatically read** the `Benchmark/Console/` journal.
- Console does not treat `Benchmark/GUI/` snapshots as long-term benchmark history.
- The GUI writes no journal and takes no suite lock. What it reuses is only
  `writeFileAtomic` / `sanitizeSourceLabel` / `shortRootId`.

### 4-10. Reuse of the existing media scope

The existing GUI Images/Videos selection semantics are reused as-is and mapped onto `MediaScope`
(`Images` / `Videos` / `All`). **No new enum is created.** Benchmark mode and media scope are
**independent axes**. The actual `mediaScope` value is stored in the snapshot JSON.

### 4-11. Separating CPU Resource Policy from benchmark mode

- **Benchmark mode**: `AUTO` / `CPU-only` / `GPU-max`
- **CPU Resource Policy**: `Maximum` / `High` / `Balanced` / `Gaming` / `Manual`

The two axes are not merged into one enum or checkbox. S4 reuses the existing `ResourcePolicy`.
**GPU-max is not connected to NVDEC.** The current GPU-max uses the existing CUDA/backend
execution semantics.

### 4-12. Reusing the S3 suite lock (mutual exclusion between GUI instances)

**Concurrent execution of the same benchmark suite is forbidden.** The GUI uses the suite lock
meaning defined in S3 exactly as it is.

- When another GUI instance tries to start a benchmark for the same GUI benchmark suite path,
  it is **rejected**.
- On rejection, the **journal / snapshot / runtime of the already running benchmark are not
  touched.**
- A **clear "this benchmark is already running" state** is shown to the user.
- **Different sources/suites may run at the same time.**
- The GUI lock is **not made into a separate new locking system.** S3's
  `msf::BenchmarkSuiteLock` contract is reused as is.

The goal is to prevent `journal`, `summary/snapshot` and `runtime index` collisions between GUI
instances. Because the suite path itself already differs (§4-5, §4-6
`<source-label>_<root-id-short>`), the lock is keyed on the same suite unit as the snapshots.

### 4-13. Scan and benchmark run serially

- While a normal Search/Scan is running, the **benchmark run button is disabled.**
- While a benchmark is running, normal scan execution is **appropriately restricted** as well so
  that it does not conflict.
- The existing Pause/Resume meaning is **not changed.**
- Pause/Resume is not added to benchmark (§4-7).
- Benchmark provides Cancel/Stop only.

Allowing a scan and a benchmark at the same time complicates CPU/GPU resource contention, UI
state, index lifecycle and progress reporting, so **S4 fixes this as serial execution.**

### 4-14. O(N²) is not exposed to the user

The benchmark execution cost characteristic of the S2/S3 implementation (O(N²) folder walk) is
**kept as is.** The S4 GUI **does not display or warn about the algorithmic complexity.**

The progress information the GUI provides is limited to roughly the following.

- current file / total files
- the mode currently running
- overall progress
- elapsed time
- cancel state
- complete / failed / cancelled result

Phrases such as `O(N²)`, "this may be very slow", or complexity warnings are **not put into the UI
seen by ordinary users.** If needed, the current O(N²) characteristic may be recorded only in
**internal logs or development documents.** Performance improvement is handled as a **separate
future Node/task.**

## 5. GUI snapshot JSON schema

No new result model is created. S2's `BenchmarkRun` / `BenchmarkCaseResult` /
`BenchmarkModeResult` / `BenchmarkScanSummary` / `BenchmarkStatus` are reused, and a snapshot is a
**per-mode projection** of that run.

Required fields:

```text
schemaVersion          (its own version; reusing legacy kBenchmarkSchemaVersion(9) is forbidden,
                        and journal schema 1 must not be claimed — this is not a journal)
appVersion             (injected from the generated build version header, never hardcoded)
suiteId                (identical across the three modes)
runId
mode                   (auto | cpu | gpu-max)
mediaScope             (images | videos | all — the actual selection)
sourceRoot             (canonical full path)
sourceRootLabel
sourceRootId
datasetFingerprint
startedAt
completedAt
status                 (that mode's aggregate, recomputed per §4-4)
file counts            (scanned / completed / remaining, etc.)
elapsed
summary
failure / cancellation information
```

`status` uses `Cancelled` / `Failed` / `Success` / `Skipped` as-is and keeps `FAILED` and
`CANCELLED` distinct. The cancellation reason and failure messages are preserved per mode.

## 6. Mockup reference — missing asset state

Canonical location: `uimock/mockup-C-gui-benchmark-layout.html`

Restoration history: `2c16f52` ("docs: preserve exploratory GUI benchmark mockup C") added the file
under the incorrect `project/uimock/` path, and `ef84827` deleted the duplicates at that path.
Since the canonical mockup directory is the repository root `uimock/`, the blob from `2c16f52` was
**restored byte-identically** when this brief was written (verified with blob hash
`840fc93098db45398ece23c2484ea8879d4f3fa8`).

**Missing asset state — must be recorded:**

- `mockup-C-gui-benchmark-layout.jpg` has **never existed anywhere in git history.**
  (`git log --all` over `.jpg` under `uimock` returns only `uimock/mockup-B-ko-list.jpg`.)
- The restored HTML is not actual mockup markup but an
  **`<img src="mockup-C-gui-benchmark-layout.jpg">` wrapper page** (547 characters, 1 line).
  Restoring the HTML alone therefore cannot yield the mockup's visual content.
- **No image is fabricated and recorded as "restored".** It is a missing asset.
- Consequently **the basis for the S4 UI layout is not mockup-C** but `mockup-B` (the list-view
  GUI mockup, which includes the visual system) and the real `MainWindow`. As confirmed in §3-1,
  mockup-B is currently in sync with the real `buildToolbar()` order, and the toolbar/progress UI
  follows it.
- Respecting what the mockup itself says ("an alternative separate from the existing GUI screen,
  and independent reference material from this Console final mockup"), the benchmark UI is kept
  as a surface separate from, and not confusable with, the Console terminal UI (S5).

**Confirmed state (confirmed before start directive)**:

> mockup-C는 HTML wrapper까지는 history에서 복구되었으나 실제 referenced JPG가
> repository/git history에 존재하지 않아 시각 자산을 복원할 수 없다. 따라서 S4 구현은
> mockup-B와 현재 실제 MainWindow를 canonical UI reference로 사용한다.
> mockup-C의 신규 재설계 또는 추정 복원은 S4 범위에 포함하지 않는다.

That is, mockup-C is **neither restored nor newly designed.** Only the confirmed facts are
recorded.

## 7. Cancellation / failure semantics (unchanged from S2)

- A mode cancelled during execution → `Cancelled`
- A mode not started before cancellation → `Skipped`
- One mode fails → that mode is `Failed`, and the other modes of the same Case keep running
- Case aggregate = `Cancelled > Failed > Success > Skipped`
- Snapshots update only the selected modes, and **partial results are recorded** even when
  cancelled or failed (unmeasured is never 0; it is explicitly `Skipped` / `Cancelled`)

## 8. Out of scope (not done in this S4)

- `--benchmark` / `--mode` / `--suite` / `--log-dir` / `--log` (S5)
- Modifying the CLI parser (S1 stays as is, unimplemented options keep being rejected)
- Terminal renderer (S5)
- S6 suite auto-execution / dataset fingerprint verification
- S7 help / usability / exit code
- NVDEC integration, FFmpeg dependency changes, CPU/GPU dependency separation
- Changing S2 execution semantics
- A new benchmark engine
- Pause / Resume

## 9. Test plan (fixed before implementation)

### Mode selection

- AUTO only / CPU only / GPU-max only
- AUTO + CPU / AUTO + GPU-max / CPU + GPU-max
- all three
- with zero selected the start button is disabled

### Ordering

Confirm the selected mode order follows the reference `AUTO → CPU-only → GPU-max`.
Also confirm the **file-level order** is preserved for GPU-max-only and partial selections.

### Snapshot

- `auto.json` / `cpu.json` / `gpu-max.json` created
- a re-run updates **only the selected modes**
- **unselected mode snapshots are preserved** (no deletion)
- atomic replacement (temp → flush → replace), existing values kept on failure
- no raw full source path in the filename, canonical sourceRoot present in JSON metadata

### Aggregate

- only CPU fails on one Case → `cpu.json` is `FAILED`, `auto.json` / `gpu-max.json` are `Success`
- the Case aggregate is not copied into the three mode files

### Cancellation

- cancel a running mode → `Cancelled`
- remaining modes → `Skipped`
- GUI state indication
- snapshot status preserved

### Failure

- one mode fails and the other selected modes keep running

### Isolation

- no source folder contamination
- no change to the normal Search Index
- GUI runtime index separated from Console / production index
- **same-suite concurrent execution rejected** — a second GUI instance starting a benchmark for the
  same suite path is rejected and the existing journal/snapshot/runtime is left untouched
- **different-suite concurrent execution allowed** — different sources/suites may run together
- an "already running" state is shown on rejection
- the suite lock reuses S3's `BenchmarkSuiteLock` and creates no separate locking system

### Serial execution (§4-13)

- the benchmark run button is disabled while a scan is running
- normal scan is restricted while benchmarking
- Pause/Resume meaning unchanged, no Pause on benchmark

### Progress presentation (§4-14)

- current/total file, the running mode, progress, elapsed time, cancel state and
  complete/failed/cancelled results are provided
- **no complexity warning wording such as `O(N²)` or "this may be very slow" appears in the UI**
  (verified by asserting the strings are absent)

### Regression

- normal GUI launch / GUI smoke
- existing image/video scope behaviour
- existing scan behaviour (zero scan regression)
- `--version` / `--help` / `--smoke` / `--scan` / invalid option rejected / `--benchmark` still rejected
- CPU build + CTest 90/90 maintained
- GPU build + CTest 91/91 maintained
- `--tr-keys` passes: new `trStr` keys added on both the KO and EN sides

## 10. Pre-start confirmed contracts (all five settled)

The five items below are **all confirmed**. This brief now has no open items.

1. **`benchTgl_` determination — confirmed (§4-8).** `benchTgl_` is an existing feature that decides
   whether to attach legacy telemetry to the normal scan that is already running; it does not
   start benchmark execution. Therefore **keep the existing checkbox and add a new benchmark run
   button**, with no temporary disable. The new button uses a name that clearly denotes benchmark
   execution, and the two remain separate features. Removing the legacy telemetry is outside S4.
2. **Multiple GUI instances lock — confirmed (§4-12).** Concurrent execution of the same benchmark
   suite is forbidden. S3's `msf::BenchmarkSuiteLock` contract is reused as is, with no new locking
   system. On rejection the existing journal/snapshot/runtime is not touched, an "already running"
   state is shown, and different sources/suites may run at the same time.
3. **O(N²) exposure — confirmed (§4-14).** The O(N²) cost characteristic of S2/S3 is kept, but the
   GUI neither displays nor warns about it. The UI provides only current/total file, the running
   mode, overall progress, elapsed time, cancel state and complete/failed/cancelled results.
   Performance improvement is separate future work.
4. **Starting a benchmark during a scan — confirmed (§4-13).** Not allowed. The benchmark button is
   disabled while scanning, and the normal scan is restricted while benchmarking. **Serial
   execution is fixed.** Pause/Resume meaning is unchanged.
5. **mockup-C missing asset — confirmed (§6).** Neither restored nor newly designed. Only the HTML
   wrapper was recovered from history and the referenced JPG does not exist, so the visual asset
   cannot be restored. The canonical UI references are **mockup-B + the current real MainWindow**.

### S4 implementation common principles (confirmed contracts)

Together with the five items above, the following are maintained as confirmed S4 contracts.

- A **single** `BenchmarkRunner::run(request, selectedModes)` invocation
- **File-level mode execution order preserved**
- **Snapshots of unselected modes keep their existing files**
- `auto.json` / `cpu.json` / `gpu-max.json` record **only that mode's results**
- The GUI benchmark runtime index is **completely separated** from the normal Search Index
- Benchmark Cancel uses the **existing S2/S3 cancellation semantics** unchanged
- **No Pause/Resume is added**
- **No separate new GUI benchmark engine is created**
- **The S3 suite lock is reused**
- **mockup-C is not implemented by inference**
- **The existing `benchTgl_` telemetry feature is not removed**
- The new benchmark entry point is provided as a **separate run button**

## 11. Implementation order

1. **Phase 1** — compare the existing GUI structure against the mockup → **done (§3)**
2. **Phase 2** — create the S4 pre-register brief → **this document**
3. **Phase 3** — implement (start after items 1-5 in §10 are settled)
4. **Phase 4** — GUI functional tests + CPU/GPU regression
5. **Phase 5** — update documentation (roadmap / progress / worklog, KO/EN)

## 12. Summary of what is not changed

- All S2 execution semantics (ordering, aggregate precedence, cancellation state model,
  executor boundary)
- The S3 journal / suite lock / summary contracts
- The S1 CLI parser and the `--scan` path
- The existing `BenchmarkRecorder` (legacy schema 9) and `benchTgl_` behaviour
- The normal Search Index structure
- The F/NVDEC state

## 13. Implementation status (Phase 3-1 to 3-4 reflected, 2026-09-30)

**Status: CLOSED.** (Resource Policy delivery and datasetFingerprint resolved)

### Files

| Phase | File |
| --- | --- |
| 3-1 | `src/benchmark_gui_store.{h,cpp}` — §4-5 §4-6 §4-9 §4-12 |
| 3-2 | `gui/benchmark_worker.{h,cpp}` — §6 §7 execution path |
| 3-3 | `gui/mainwindow.{h,cpp}` — §1 §2 §4 §5 §6 §7 §8 UI |
| 3-4 | `tests/ui_benchmark_e2e_test.cpp` — real-engine E2E |

### Contract compliance

- **§4-1 checkbox = execution selection** met. 7 valid combinations; zero disables the run button.
- **§4-2 single Runner call** met. `benchmark_worker_test` verifies the
  `a:auto a:gpu-max b:auto b:gpu-max` order.
- **§4-3 file-level ordering** met. Full set gives `auto>cpu>gpu-max`.
- **§4-4 per-mode snapshot + recomputed aggregate** met. Filters `modeResults[]` then reuses S2's
  `aggregateStatus()`. The storage test verifies a CPU-only failure yields auto SUCCESS / cpu FAILED /
  gpu SUCCESS.
- **§4-5 atomic replace** met. Reuses `writeFileAtomic()`; unselected mode preservation verified by
  byte comparison.
- **§4-6 GUI runtime path** met. Index confirmed created only under `runtime/run-<id>/{auto,cpu,gpu-max}`.
- **§4-7 no Pause** met. Only Cancel/Stop exist, and `pause_` is disabled during a benchmark.
- **§4-8 `benchTgl_` kept** met. Label unchanged, only a tooltip added, not reused as a mode selector.
- **§4-9 GUI/Console isolation** met. The E2E confirms `Benchmark/Console` is not created.
- **§4-10 media scope reuse** met. Existing `kindImgAct_`/`kindVidAct_` mapped onto S1's `MediaScope`.
- **§4-12 S3 suite lock reuse** met. The real E2E confirms an external holder prevents start and
  leaves the snapshot unchanged.
- **§4-13 serial execution** met. Scan blocks benchmark; benchmark locks scan/pause/checkbox and
  enables stop.
- **§4-14 O(N²) not exposed** met. No complexity warning in the UI.
- **§6 progress display** met. "Completed k/N · last <file>". No guessing of the in-flight file.
- **§7 storage source** met. Only the `finished` outcome is used.
- **§8 state distinction** met. Completed / cancelled / failed / lock busy each get distinct wording.

### Verification

storage 110 · worker 35 · UI 34 · **real-engine GUI E2E 40** checks.
**CPU CTest 94/94 · GPU CTest 95/95.**
CPU build `gpu-max` = `SKIPPED`, GPU build `gpu-max` = `SUCCESS`.

### Not met / remaining

- **§4-11 Resource Policy reuse: MET.** Added `BenchmarkRequest::resourcePolicy` (optional,
  additive) and `ProductionBenchmarkExecutor::runMode()` now starts from the supplied policy. When
  unset the existing S2 behaviour (engine default policy + mode-derived gpuEnabled) is preserved
  exactly, so existing callers are unaffected. The GUI passes the `policy_` MainWindow already
  resolved through `make_policy()`, so no preset/CPU interpretation is duplicated. The real E2E shows
  choosing "Maximum 90%" in the toolbar recording `cpuPercent: 90` in the snapshot.
  **Remaining constraint**: `gpuEnabled` is decided by the mode, so the GUI `gpuEnabled_` does not
  affect AUTO/GPU-max runs (the S2 rule was kept as instructed).
- **§10 direct mode-combination verification:** indirect only. `selectedBenchModes()` stays private.
- **The O(N²) walk is retained.** S4 is a storage/UI layer and does not optimise the scan.

**`datasetFingerprint` is also resolved.** When no fingerprint is supplied the worker uses the
existing `msf::computeDatasetFingerprint(root).fingerprint` verbatim. No new hash and no new
serialisation format were invented, and `DatasetFingerprint` is a public-member struct so no accessor
was needed. It is computed on the worker thread so the GUI is not blocked. The real E2E records a
64-character hex value that is **exactly equal** to `computeDatasetFingerprint(root).fingerprint`.

**Status: CLOSED.** The three remaining questions (accepting the O(N²) walk, the UX of SKIPPED on a
CPU build, whether the GUI GPU toggle should apply) are product decisions, not implementation defects.

Detailed judgements: `docs/build-history/S4-phase3-4-verification.ko.md` / `.en.md`
