# Development Progress — 0.9.4 Development Line

> One-page summary of the overall node status:
> → [`docs/node-status-gate-matrix.en.md`](node-status-gate-matrix.en.md)
> The status board is an index; this document together with Roadmap/Build History is the origin of verdicts and figures.

## Purpose

This document records the **actual execution state** of development-roadmap.en.md.

The Roadmap is the structural direction. Progress records the actual position, problems, and recovery branches.

## Mandatory Session-Start Entry Point

This document is a **mandatory read for OpenCode and ChatGPT agents when starting a new session**.
If `docs/node-status-gate-matrix.en.md` is the one-page Node/Gate index, this document restores the **active execution queue, actual current state, completed major milestones, and current blockers**.
At session restart, read this document immediately after the Matrix and follow its current execution priority first.
Detailed experiments, measurements, and decision lineage remain in `docs/worklog/`; version-specific changes and evidence remain in `docs/build-history/`.
A separate `workprogress` document is intentionally not created; this document is the single entry point for current work progress.
**Update this document immediately whenever Node or Build progress, verdict, gate, or priority changes.** Starting or completing implementation, verification results, PASS/CONDITIONAL/DEFERRED/NOT ACCEPTED/CLOSED changes, and confirmation of the next build must be reflected in the Active Build Queue and current-state sections in the same session in which the change occurs. Preserve the detailed evidence in Work Log / Build History.

## Active Build Queue / Current Work Priority

> This section is the **single execution waiting list for both live work and intentionally deferred work**.
> Completed items are removed from the live queue; deferred/lower-priority items remain in a separate backlog.
> **Updated 2026-10-07:** 0.9.4.62 is **the crash-response baseline, not the GUI usability build**.
> GUI usability is therefore assigned to **0.9.4.63**. If another crash occurs, collect evidence first
> according to `docs/architecture/crash-response-runbook.{ko,en}.md`; do not make speculative root-cause changes.
>
> The 0.9.4.61 similar-group scroll regression is now observed as normal in the real Windows GUI,
> so it is no longer a current functional blocker. The thumbnail catch-up full-list traversal remains
> a separate performance risk.

| Priority | Baseline / Target | Work item | Purpose / Next Gate | Status |
| --- | --- | --- | --- | --- |
| 0 | **0.9.4.64+** | **GUI usability manual acceptance (incl. 0.9.4.66)** | Settings dialog general tab (Show Detailed Logs), resize behavior, filename selection, Tiles/ListMode, large-dataset traversal, plus overlap re-check after mode switches | **IMPLEMENTED / MANUAL ACCEPTANCE PENDING** |
| 1 | **0.9.4.62** | **Crash-response follow-up** | On recurrence, collect scan/Qt/WER/dump evidence and identify fault thread/root cause | **GUARDRAIL / immediate on evidence** |
| 2 | Product acceptance | **Final Search / Index / Comparison acceptance recheck** | Resolve the latest real-dataset edge-case semantics around silent analysis failures / modified state | **NOT ACCEPTED / recheck after GUI work** |
| 3 | S4 | **Final real-screen + save acceptance for GUI Detailed Logs** | Validate Search/Update → Detailed Logs → save/finish on the real user path | **PENDING / tied to product acceptance** |
| 4 | I-XMP | **XMP Orientation real-dataset/full-scan coverage** | Extend fixture PASS to real-dataset evidence and clear the CONDITIONAL production acceptance | **PENDING** |
| 5 | color_thumb | **R1 fixture + no-FFmpeg skip/pass handling** | Define correct PASS/SKIP semantics for `color_thumb_test` without FFmpeg | **PENDING / before production correction** |
| 6 | S5 | **Real-dataset product benchmark** | Run the controlled benchmark only after product acceptance | **GATE PENDING** |
| 7 | S6 | **Measurement / data-mining gate** | Generate build/case comparison candidates and evaluate the measurement gate from real S5 samples | **DEFERRED / S5 first** |

### Deferred Backlog

| Category | Work | Current status / principle |
| --- | --- | --- |
| Developer feature | **GUI Test Mode** | The old GUI Benchmark is not revived. Investigation showed that UI surface, scratch lifecycle, telemetry distinction, and acceptance design are required, so this build leaves it **DEFERRED**. Only the KO/EN name is fixed: ‘테스트 모드’ / ‘Test Mode’. A separate version and acceptance plan are required before resuming. |
| GUI performance | **Visible-range thumbnail catch-up traversal** | Actual decoding is limited to visible items, but every tick still scans the whole list. Change only if a safe visible-range access path is available. **DEFERRED / risk-contained** |
| Product feature | **Video comparison semantics / GUI video acceptance** | Video comparison semantics and equivalent real-use acceptance are not closed at the same level as the image path. **DEFERRED** |
| Post-1.0 | **Burst-shot similarity refinement** | Separate burst shots from true near-duplicates and later expose algorithm choice in Settings. Authority: `docs/architecture/image-burst-shot-similarity.{ko,en}.md`. **POST-1.0** |
| Post-1.0 | **CPU/I/O/decode/GPU pipeline throughput** | Optimize end-to-end throughput, backpressure, overlap, and queue starvation; do not optimize GPU utilization as a vanity metric. **POST-1.0** |
| Post-1.0 | **Scheduler bottleneck awareness** | Distinguish stage bottlenecks, own resource usage, and I/O wait rather than reacting only to system-wide CPU/GPU utilization. **POST-1.0** |

> **Boundary:** Do not reopen sparse production, NVDEC production adoption, or utilization-only GPU tuning.
> Existing rejection/deferred evidence remains authoritative.
>
> **Execution summary:** 0.9.4.69 P3 done → P4 hardening + final acceptance → manual GUI acceptance → final acceptance recheck
> → S4 final acceptance → XMP coverage / color_thumb R1 → S5 product benchmark → S6 measurement gate.
> Test Mode and traversal remain lower-priority work and must not block the main validation track.

## Completed Major Milestones

This section keeps only a **compressed completion state** for long-term orientation. Detailed measurements, failed experiments, and implementation changes remain in Work Log / Build History.

| Area | Completion state | Key result |
| --- | --- | --- |
| A | **CLOSED** | Foundation / Terminology / Instrumentation baseline established |
| B | **CLOSED** | Adaptive Scheduler completed |
| C | **CLOSED** | Calibration / INI Performance Profile C1–C4 completed |
| D | **CLOSED** | Pipeline / Queue Optimization completed; measured addressable ceiling 0.044% |
| I | **COMPLETE** | Analyze / Matching Performance completed |
| E | **CLOSED / NOT ACCEPTED** | Production exactness was not proven for sparse decode; `ExactnessPolicy::RefuseAll`, production Sequential retained |
| F-1 | **CONDITIONAL / PRODUCTION ADOPTION NO** | NVIDIA NVDEC investigation completed; production integration prohibited |
| S0–S3 | **CLOSED** | Initial Validation / Benchmark track infrastructure completed |
| S4 | **IMPLEMENTED / VERIFICATION IN PROGRESS** | GUI Detailed Logs implementation and user UI confirmation complete; final functional acceptance remains |
| product acceptance | **NOT ACCEPTED -> defects fixed in 0.9.4.46** | Search/Index/Comparison boundaries were proven PASS by an actual product run. DEFECT-A/B were fixed in `0.9.4.46` by introducing an analysis-failure state model (`files.analysis_failed`), and unchanged `candidates`/`groups` measurements confirm normal semantics are preserved |
| GUI non-ASCII scan | **fixed in 0.9.4.47** | Clicking Search/Update raised a `Search error`. The cause was the narrow `fs::path(string)` construction in `dataset_fingerprint.cpp:118` reinterpreting UTF-8 as ACP. The CLI silent crash was the same exception. Replaced with `path_from_utf8()` and audited the product for the same pattern with no other instances. Added a dataset fingerprint check to `unicode_path_test` |
| GUI Stop unresponsiveness | **fixed in 0.9.4.48 (engine level)** | The Stop flag was set correctly, but the scan-start dataset fingerprint read every file to the end while ignoring cancellation, so I/O continued 10+ minutes. Fixed by plumbing `cancel` through with an immediate `state="cancelled"` return. New `cancel_fingerprint_test` 11 checks. **The actual GUI button click is NOT_VALIDATED (needs user-side verification)** |
| Detailed Logs user summary | **added in 0.9.4.49** | Plenty of benchmark internals but no user-facing summary, and the duplicate-file total existed nowhere. Per-kind scanned/analyzed/throughput plus duplicate groups/files/pairs split, totals, and time split. `summary_breakdown_test` 21 checks. **GUI screen rendering NOT_VALIDATED** |
| S4 telemetry Phase-A | **reinforced in 0.9.4.50** | Finish time, cancel location, fingerprint cost/volume, summed bytes, series absolute time, system total RAM. Phase frozen after cancel. One finalize-time measurement guaranteed for fast scans. Regressions extend existing tests |
| fingerprint progress | **added in 0.9.4.51** | Closed the minutes-long no-report blackout. Separate signal with 150ms throttle. Skipped when telemetry is off. `cancel_fingerprint` 21 checks. **GUI screen NOT_VALIDATED** |
| embedded image-path issues | **fixed in 0.9.4.52** | XMP VT_LPSTR mbstowcs locale dependence replaced with explicit CP_ACP. parallelFor catch intent pinned. Verified zero fingerprint impossible, state model sound. **XMP 38 checks direct cover** |
| summary panel 3-row split | **done in 0.9.4.53** | Resolved "검색 완료" ambiguity. Total / read-complete (scanned) / index-complete (analyzed), in order. readDone/indexDone keys registered. **GUI screen NOT_VALIDATED** |
| live read counter | **done in 0.9.4.54** | Fixed fingerprint-phase panel freeze. Engine analyzedCount + live index display. scanFinished exception guard. **GUI screen and large Stop NOT_VALIDATED** |
| monotonic panel counters | **done in 0.9.4.55** | Fixed fingerprint-to-walk reset (lastReadN_ max). Live preservation instead of final 0 after Stop. **GUI screen NOT_VALIDATED** |
| scanFinished diagnostics | **added in 0.9.4.56** | Entry/exit trace for the unobserved popup section. No indexing on fingerprint-cancel is correct |
| fingerprint scope awareness | **done in 0.9.4.57** | Images-only no longer hashes videos into walk unreachable. In-scope media only. Section G regression |
| drain on Stop | **done in 0.9.4.58** | Read images complete indexing before exit. Undrained video is a documented limit. Drain 5 checks |
| fingerprint timing relocation | **done in 0.9.4.58** | Telemetry-only hash no longer blocks the walk. Moved past match/group, skipped on cancel/failure/off. Empty cancelledDuring |
| Video Cancelled/Failed split | **done in 0.9.4.59** | Stop no longer promotes to failed/rollback. Completions persist, seen-conditional stamp. Video 26 checks |
| Generation scope split plus explicit terminal | **done in 0.9.4.60** | Seen reuse fixed (videoScopeSeen). Image-only/ignored promotion blocked. DB-failure-then-Stop still Failed. Genscope 35 checks |
| Similar-group scroll regression fix | **done in 0.9.4.61** | Starvation-only rebuilds removed. In-place catch-up plus scroll gate. 0.9.3.10 anchor kept. Scroll 23 checks |
| Crash response trio | **done in 0.9.4.62** | Catch-all recorded-failure downgrade. Qt message file sink. Enumeration-phase heartbeat. Crashdiag 5 checks |
| GUI usability trio | **done in 0.9.4.63** | Show Detailed Logs setting. Splitter middle-first. Filename selectable. Test Mode and traversal deferred. Usability 13 checks |
| Display-option settings consolidation | **done in 0.9.4.64** | Separate display dialog removed, moved to the settings general tab. Round-trip unchanged. Test closer-race fix. Usability 13 checks |
| Non-throwing scan failure handlers | **done in 0.9.4.65** | Third 0xC0000409 dump pinned the handler persist path. Independent try/catch on persist and emit in both handlers. Crashdiag 7 checks |
| View-mode overlap fix | **done in 0.9.4.66** | Post-switch thumb arrival ran no layout pass, pinned by probe. doItemsLayout on catch-up change and at switch end. Viewmode 18 scenarios 201 checks |
| P1 BackendCore boundary | **done in 0.9.4.67** | msf_core confirmed Qt-free, boundary declared. Backend exe (Qt6::Core only, --help/--version). GUI behavior unchanged. 3 backend tests |
| P2 BackendClient+Loopback | **done in 0.9.4.68** | Direct worker/thread/monitor ownership removed. Pull-to-push, snapshot cache, requestThumb. ScanWorker untouched. No new tests |
| P3 real spawn+Supervisor+IPC | **done in 0.9.4.69** | ScanWorker to src, shared Session, JSONL IPC, THUMBNAIL, async fileThumb, thumbDb moved. E2E measures PID split, restart, FAILED |


## 2026-10-06 — 0.9.4.59 review and next correction order

The CPU/GPU CTest results for 0.9.4.59 are passing, but code semantics and the real user diagnostic run show that **build completion is not equivalent to completed cancellation semantics**. The next work is intentionally split into two phases.

### Phase 1: engine / diagnostics / verification

- **Confirmed semantic bug:** the video sampling-generation stamp depends on the general `seen` set. `processOne()` inserts videos into `seen` even when `scanVideos=false`, so an image-only scan can advance video generation. The successful-scan `r.completed` branch can also stamp generation without regard to video scope. Video generation must only advance when in-scope video admission/processing has crossed the required boundary, and it must **never change when `scanVideos=false`**.
- **Diagnostic semantic hardening:** `finishScan(false)` currently infers Cancelled from the live cancel flag. A real DB failure followed by a late Stop can therefore be reported as Cancelled even though the transaction failed. Pass an explicit terminal state (Completed/Cancelled/Failed) so DB failures remain Failed.
- **Verification additions:** add an image-only generation-stamp regression, deterministic DB-error → rollback/Failed + `failed=true) telemetry coverage, and explicit handling/verification of generation-stamp write failure.

### Real GUI performance observations

In the 0.9.4.59 GUI run, all 32,494 images used the GPU image path, but reported GPU duty was only about **0.2%**. The image stage took 567.7 s; verify decode consumed about 25.4 s and WIC copy about 25.1 s, while the GPU H2D/kernel/D2H work was only about 0.31 s. This does not by itself indicate a CUDA defect; the stronger performance hypothesis is **insufficient pipeline overlap between CPU/WIC decode/crop/I/O and the GPU hashing stage**.

Even with the Maximum (90%) CPU policy, observed process CPU usage oscillated roughly across 20–70% and averaged 30.8%. Observed drive I/O also varied around the 15–40% range. These are classified as **post-1.0 throughput/parallelism work**, not as 0.9.4.59 correctness defects.

### Explicit post-1.0 performance backlog

The image search engine is now considered **first-pass complete as a product path**, but after 1.0 it should be improved in these areas:

- tighter mapping between CPU worker occupancy and the user CPU policy
- I/O backpressure and read/decode concurrency
- better overlap across decode/crop/hash stages and longer-lived GPU batches
- reduced GPU queue starvation and more host↔device overlap
- scheduler awareness of stage-level bottlenecks, self-attributed usage, and I/O wait rather than relying mainly on aggregate system utilization
- one admission/backpressure model for both the low initial Balanced worker occupancy and the oscillatory/idle behavior still observed under Maximum

This backlog is **not mixed into the 0.9.4.59 correctness repair**. GPU utilization itself is not the objective; end-to-end throughput and system stability remain the objectives.

### Post-1.0 image-verdict refinement backlog

- **Burst-shot similarity verdicts (DEFERRED)**: burst shots of the same person and background group at 95.8% similarity. The current pHash+SSIM structure measures the whole scene and never attenuates pose differences. No discriminator exists besides the threshold, so this waits until after 1.0. Detail: `docs/architecture/image-burst-shot-similarity.en.md`.
- **Planned Settings option**: an on/off option in similar-image search settings to not judge burst shots as similar. Default keeps current behavior.
- **Planned dual-algorithm selectable search**: the current algorithm plus a strict path with stronger burst separation, selectable per the option. Model-based approaches such as face landmarks stay candidates only.

### Phase 2: GUI regression

The real GUI reproduced a regression where scrolling the similar-group list downward with mouse dragging, the vertical scrollbar, or keyboard navigation such as End becomes slow and eventually returns the list to the top. This is treated as a separate phase: compare the historical **0.9.3.10 semantic scroll-anchor fix** with the current 0.9.4 thumbnail catch-up/full-list rebuild path, then fix the regression. Do not mix this GUI change into Phase 1 engine semantic fixes.

## 2026-10-06 — 0.9.4.60 verification complete and GUI scroll regression analysis

0.9.4.60 completed the phase-1 semantic/diagnostic repair. CPU CTest 108/108 and GPU CTest 109/109 passed, including 35 checks in the new `scan_generation_scope_test`; engine/DB/schema/cache remain 1.5.0 / 1.0.4 / 9 / 9.

### Phase-1 result

- Video generation admission is separated from general deletion `seen` through `videoScopeSeen`.
- `scanVideos=false` never changes video generation, and ignored stale videos cannot promote the generation.
- `ScanTerminal { Completed, Cancelled, Failed }` makes the terminal reason explicit, so a real DB failure cannot be relabeled Cancelled by a later Stop.
- Generation metadata write failure keeps the previous value and does not invalidate already-committed search results.
- The new scope/stamp/DB-error regression has 35 checks; the existing video-cancel 26 and image-drain 7 checks remain green.

### Phase-2 GUI scroll regression — source-level cause established

The historical `0.9.3.10` scroll-anchor fix is still present in the current source. The regression is therefore not caused by the anchor code being removed.

The current structure contains a more direct problem:

- `uiTimer_` continues running after scan completion; `setRunning(false)` intentionally leaves the timer alive.
- The thumbnail budget is only four fresh decodes per UI tick. When cache misses exceed that budget, `fileThumb()` sets `thumbStarved_`.
- The next timer tick lets `refreshStreaming()` perform a full `rebuildGroups()` + `refreshGroupList()` solely because `thumbStarved_` is true.
- `refreshGroupList()` -> `fillPair()` clears and recreates the entire middle tree/grid.
- Therefore the application can destructively rebuild the very view the user is actively navigating with the scrollbar, mouse, wheel, or End/Home/Page keys.
- The 0.9.3.10 anchor is a safety net after such a rebuild; it does not make destructive rebuilds during an active scroll interaction safe.

The source review does not yet prove which exact Qt internal layout/scrollbar step finally clamps the position to zero. That should be established by the GUI regression test/instrumentation rather than guessed.

### Phase-2 correction direction

1. When group data itself has not changed, `thumbStarved_` catch-up must not rebuild all group widgets.
2. Keep existing tree/grid items and update thumbnails for currently visible groups in place.
3. Add a short interaction/cooldown gate around scrollbar `sliderPressed/sliderReleased` and wheel/key scrolling so a full rebuild can never happen during active user navigation.
4. Keep the existing `0.9.3.10` semantic anchor as the fallback for cases where a real full rebuild is still required.
5. Make End/Home/PageUp/PageDown, mouse wheel/drag, and vertical scrollbar drag explicit regression acceptance cases.

### Scope

This phase-2 GUI task must not change engine cancellation semantics. Scheduler/worker/GPU performance tuning, `color_thumb`, NVDEC, sparse, and S4 real-screen acceptance remain separate gates.

## Current Status

| Item | Status |
| --- | --- |
| Reference code | 0.9.4.69 (P3 real spawn+Supervisor+IPC) |
| Official preserved baseline | 0.9.2.32 |
| Development line | 0.9.4 |
| Current node | **0.9.4.69 P3 done → P4 hardening + final acceptance next** — CPU 116/116, GPU 117/117, --version 0.9.4.69 on both exes, warning/error/C4819 clean; separate display dialog removed and folded into the settings general tab). What remains is the real-Windows eyeball of settings/resize/filename plus Tiles/ListMode and large-dataset traversal. S4 implementation and visible UI are PASS, but final acceptance is tied to product acceptance and remains PENDING. The 0.9.4.62 crash-response containment/observability patch is complete, while the root cause remains unproven; the next crash must follow the runbook evidence order. XMP Orientation has fixture PASS but no real-dataset coverage, so production acceptance remains CONDITIONAL. `color_thumb` R1 is not started. S5 infrastructure is REVALIDATED and the real product benchmark remains gate-pending. F-1 remains CONDITIONAL with NVDEC production adoption NO. |
| Current phase | **P3 done → P4 hardening + final acceptance** to finish the split. Manual GUI acceptance (incl. overlap re-check) → final acceptance recheck → S4 → XMP/color_thumb → S5 → S6 stay queued. The latest real-dataset acceptance audit still leaves Search/Index/Comparison **NOT ACCEPTED**, even though some execution paths are PASS, so S4/S5 must not be promoted to final PASS prematurely. On any new crash, collect logs/WER/Qt/dump evidence using `crash-response-runbook` before speculative code changes. F-1 remains `CONDITIONAL`, NVDEC production adoption remains `NO`, and sparse production remains blocked by `ExactnessPolicy::RefuseAll`. |
| Current version | 0.9.4.69 |
| GPU implementation baseline | NVIDIA CUDA |
| CPU fallback | retained |
| Project-local vcpkg | retained; no migration |

## 2026-10-04 — Current-State Audit and Next Gate

The 0.9.4.44 `6ada90f` code and current documentation were re-audited together.

- The search engine is not an unimplemented stub. The production path is already wired: `MainWindow/ScanWorker -> MediaSearchEngine::scan() -> CandidateIndex/ScanPipeline -> image/video verification -> database/matches`.
- `search_engine_test`, `scan_workflow_test`, `video_scan_e2e_test`, `match_revalidate_test`, and `ui_detailed_log_test` cover the related paths at their respective regression boundaries.
- The remaining blocker is therefore not "the search algorithm does not exist." It is final product acceptance of the implemented Search/Index/Comparison semantics and end-to-end acceptance of the real GUI Search/Update -> Detailed Logs result/save path.
- S4 remains not CLOSED until that acceptance is complete.
- S5 product benchmark validation follows the acceptance audit and should use the real dataset.
- F-1/F-2 and S6 existing boundaries and deferred conditions remain unchanged.

### Node E closure record (per 0.9.4.42)

Node E is closed by user decision.

- **E-3C**: not promoted to a separate roadmap Stage, and not migrated to F. It is
  closed within Node E's scope, based on the current structure where `RefuseAll` makes
  the sparse production path unreachable. Only the fact that it may inform F's
  architecture in future is recorded as a note; **it is not migrated as a work item.**
- **E-4**: production integration + end-to-end validation is likewise closed within
  Node E's scope. Under `RefuseAll` no "qualitative" path is left to integrate, so E-4
  must not be read as grounds for reintroducing sparse.
- The `ExactnessPolicy::RefuseAll` default and the production sequential decode path are
  **retained**.
- The basis for the conclusion and the revisit conditions are in
  `docs/build-history/0.9.4.42.*`.

**No change was made to the E-3C-related code state in `src/video_sampling_planner.h`
during this documentation pass.** Whether one is needed is to be judged and reported
separately.

### Node F entry state

- Pre-register brief: `docs/implementation-briefs/F-hardware-video-decode-backend.*`
- Actual investigation, experiment and implementation scope of this F: **NVIDIA NVDEC
  alone**
- Actual implementation/verification of other hardware decode backends is out of scope
  for this F
- The architecture is not fixed to NVIDIA (direction accommodates additional backends)
- `tools/` (untracked, undocumented) is **on hold**: not deleted, not added, not
  committed, not registered in `.gitignore`. Its status is left as is, and whether it
  should be formally included is a separate decision.

### I-3 result summary (v0.9.4.37)

```text
BASELINE   v0.9.4.35 / 6c981f7
CANDIDATE  v0.9.4.36 / 4ba2a35
Driver     msf_dataset_baseline -> MediaSearchEngine::scan() (same path as the GUI)
Condition A (cold process, decode-active, BCBCBCBCBC interleaved)
  CPU  10 paired  full scan -10.17 % (9/10)   decode stage -22.42 %
  GPU   5 paired  full scan -11.13 % (5/5, non-overlapping)  decode stage -22.29 %
Condition B (warm / repeat scan)  GPU 12 rows  full scan -26.73 % (faster in 24/24)
                          but a process-level slowdown is unexplained, so it is not
                          used as the headline
Consistency  decode share 50.7 % x 22.3 % decode saving = 11.3 % ~= observed 11.13 %
Decomposition 28-30 % of the saving is removal of the duplicated, measurement-only
              D1 reference probe (not product work). The genuine product gain is
              WIC decoder creation (open): 25,924 -> 12,962 calls
Correctness   all 39 executions identical: groups=156211 / misses=12962 / hits=14524
              exactness 3341/3341 diff_px=0, EXIF 8/8, exhaustive 5,579,470 unchanged
              CPU CTest 81/81, GPU CTest 82/82

Verdict      I-3 END-TO-END VALIDATION = PASS
             I NODE = COMPLETE, I-1 = DEFERRED, I-2 = PRODUCTION, NEXT = E
```

Open items (do not block the I node): in condition B, both versions become 30-60 %
slower from the second scan in the same process onward. v0.9.4.36 is still faster in
24/24 runs there, so it is **not a regression introduced by I-2.** The cause sits in
the decode `copyMs`, and WIC instance-structure dependence is at the **hypothesis
stage.** `vcpkg.json` `version-string` is still `0.9.4.35`; changing it would alter
the manifest hash and trigger a full 2.2 GB vcpkg rebuild, so it was deliberately
left alone in this measurement-only version.

(The table above carried a stale 0.9.4.24 state and is corrected to the actual
0.9.4.32 repository state. Historical body records below are not deleted.)

## Development flow status

[A] Foundation / Terminology / Instrumentation
 |
 v
[B] Adaptive Scheduler
 |
 v
[C] Calibration / INI Performance Profile
 |
 v
[D] Pipeline / Queue Optimization
 |
 v
[I] Analyze / Matching Performance (opened after D8b evidence; Node D closed on measurement)
 |
 v
[E] Adaptive Video Decode Planner
 |
 v
[F] Hardware Video Decode Backend
 |
 v
[G] Additional GPU Backends
 |
 v
[H] Regression / Stability / Performance Validation

The documentation work established the A baseline; 0.9.4.0 completes the Node A source implementation and validation (details: docs/build-history/0.9.4.0.en.md).

## Completed preparation

### A0 — Development framework

Completed:
- Development Roadmap KO/EN
- Development Progress KO/EN
- CPU/GPU Adaptive Resource Scheduling design
- GPU Backend Roadmap
- Benchmark / Telemetry Roadmap
- 0.9.4 work rules in AGENTS.md
- new documentation model in STRUCTURE.md / llms.txt / README files

These are documentation-structure changes; the 0.9.3.19 scheduler/backend source has not been replaced.

### A1 — Node A source implementation (→ 0.9.4.0, validated)

- `GpuBackendKind { Auto, Cuda, Cpu }` + `backendName()`; Auto resolves to
  CUDA when present, otherwise CPU fallback. No unimplemented backends.
- `MSF_ENABLE_GPU` / `MSF_GPU_BACKEND` canonical; `MSF_ENABLE_CUDA` kept as
  a deprecated alias; `windows-gpu` preset + `build_windows_gpu.ps1`; clean
  `build-windows-gpu` tree (legacy `build-windows-cuda` preserved).
- Toolbar `GPU %` spinbox removed; GPU ON/OFF checkbox only. CPU preset
  semantics and the deprecated internal `gpuPercent` cap are frozen.
- Benchmark `schemaVersion: 1` + `runId`; `MeasureState`
  (measured/not_measured/not_available/partial/failed/fallback);
  `decodedFrames`/`sampledFrames` split; `stages`, `scheduler`,
  `calibration`, cancellation/partial/file-progress records; disk
  availability latched from the sampling period. All existing keys kept.
- Validation: CPU 62/62, GPU 63/63 (clean tree, CUDA discovery green),
  `--version`/`--smoke` on both, extended `benchmark_test` and
  `gpu_backend_policy_test`. Search semantics unchanged (engine 1.5.0,
  DB 1.0.3, cache v9).
- Evidence split (post-0.9.4.0 check, extended in 0.9.4.1): automated
  build/CTest/CLI-smoke = PASS; real windowed launch = PASS (OS handle +
  version title observed); slot-path workflow = automated PASS
  (`scan_workflow_test`: real MainWindow slot path offscreen, modal
  benchmark closer, 1-group render asserted on both trees); automated
  validation of human operation (real clicks, screen reading) =
  NOT_VALIDATED (automation-environment limit) — however, the development
  lead reports having directly confirmed run → search → report display
  working normally in a real Windows session. Automation non-validation
  and user-direct confirmation are recorded separately.

### C — Calibration / INI Performance Profile (C1–C4 all complete)

- The C brief is now concretized into C1–C4 stages.
- C1 covers Profile Foundation only; actual calibration execution is excluded.
- C2 covers short initial calibration and Profile → Scheduler initial-estimate integration.
- C3 covers repeated runtime deviation, opportunistic recalibration, and confidence updates.
- C4 validates the full Profile lifecycle, live precedence, CPU/GPU parity, failure/partial state, and persistence.
- Portable Profile location is defined as Index/PerformanceProfile.ini.
- Profile is an initial estimate; live runtime state always has precedence.
- D queue/worker topology and F hardware decode are not pulled into C.
- Metrics not yet measurable are recorded with explicit measurement states.
- **Actual state: C1 (0.9.4.9) → C2 (0.9.4.10) → C3 (0.9.4.11) → C4 (0.9.4.13)
  are all complete and passed.** The bullets above are the original stage
  definitions; the per-stage results are in the C4/C3/C2/C1 subsections below.
  The C4 gate records "Node C complete, next gate D".

### C4 — Calibration Gate (→ 0.9.4.13, PASS)

- No new features: the prompt's 17 combinations tied to evidence
  (mapping in `docs/build-history/0.9.4.13.en.md`).
- One minimal extraction: `calibrationUsableForUpdate()` — the exact
  rule the engine used, now unit-testable. Engine lambda calls it
  (behavior identical).
- C4 assertions: 10-run Exact silence, failed/partial rejection,
  file-untouched reload after failure, usable-candidate pass.
- Validation: CPU 66/66, GPU 67/67; UI parity via
  `scan_workflow_test`; `--version`/`--smoke` on both. Search semantics
  unchanged. Node C done; next gate D.

### C3 — Opportunistic Recalibration (→ 0.9.4.11, validated)

- `DeviationTracker`: per-scan live-vs-feeding-baseline check, fires on
  3 consecutive >25% deviations, resets after firing and on profile
  switch/invalid input. Fixed policy (25/3/40%/±0.1, cap 0.95/floor 0.1).
- Candidate must agree with firing live observation (40%) to replace;
  mismatch keeps metrics and steps confidence down; failures leave the
  file untouched. `[lastUpdate]` persisted (absent = never updated).
- Engine `finishScan`: trigger evaluated on completed, profile-fed scans
  with both live rates; bounded re-measure in try/catch; cancelled scans
  excluded. CPU-only scans never enter evaluation.
- Validation: CPU 66/66, GPU 67/67 (tracker, consistency, update/keep
  reloads, `[lastUpdate]` round-trips); UI parity via
  `scan_workflow_test`; `--version`/`--smoke` on both. Search semantics
  unchanged.

### C2 — Initial Calibration (→ 0.9.4.10, validated)

- Bounded `Calibrator::run` (`src/calibration.h/.cpp`): synthetic CPU
  hashes, GPU batches with discarded warmup; resize/decode/transfer/
  queue/HW-decode carry explicit states (no user files, per-phase
  budgets, 8 s guard). Failures never reach scans.
- Engine: Missing/Hard verdicts calibrate once, save, and apply to the
  same scan; Exact/Soft reuse. Policy-off vs no-device distinguished
  via `videoGpu_`. Telemetry initial tier reports the profile pair.
- Schema v2 (calibration metric states). Scheduler precedence
  live > profile asserted in `scheduler_test`.
- Validation: CPU 66/66, GPU 67/67 (new `calibration_test`, incl. real
  GPU measurement); UI parity via `scan_workflow_test`;
  `--version`/`--smoke` on both. Search semantics unchanged.
- C3 blockers: measured bandwidth unresolved; resize/decode need an
  opportunistic design.

### C1 — Profile Foundation (→ 0.9.4.9, validated)

- `PerformanceProfile` + `ProfileStore` (`src/profile.h/.cpp`, Qt-free):
  FNV-1a stable id, Missing/Hard/Stale/Soft/Exact verdicts (stale only on
  caller maxAge; C1 sets no default age), pair-or-nothing initial
  estimates, manual INI codec with atomic temp+rename save. QSettings not
  reused (non-atomic). CPU-only machines never yield estimates.
- Scheduler priority live → profile → hardware with a `profile_baseline`
  reason; policy untouched. Engine hook loads the machine-wide
  `<appDir>/Index/PerformanceProfile.ini`; dormant live (no writer yet).
- Validation: CPU 65/65, GPU 66/66 (new `profile_test`, 12 groups);
  UI parity via `scan_workflow_test`; `--version`/`--smoke` on both.
  Search semantics unchanged. C2/C3 numbers deliberately undecided.

### B7 — Final Scheduler Gate (→ 0.9.4.8, PASS)

- External review finding fixed: `const bool schedUseGpu` pinned the
  scan-start verdict while re-evaluations moved shares. Replaced with
  `schedUseGpuNow()` fresh published reads per image batch and per video
  range (3 read sites, stale const deleted and grep-verified). No worker,
  queue, or barrier changes — no D upfront.
- Safety: only hold/band-approved publications flip, so execution cannot
  flap; sub-2 s scans never re-evaluate (byte-identical).
- Coverage audit tied every B7 item to evidence; deliberate non-gates
  recorded: slow-hardware and under-load Gaming execution (units +
  parity instead), wall-clock long runs (synthetic ticks instead),
  comparative throughput gates (observed evidence instead of assertions).
- `scan_streaming_test` asserts engine-level scheduler record
  (`scheduler.measured` + `selectedBackend`).
- Validation: CPU 64/64, GPU 65/65; `--version`/`--smoke` on both.
  Search semantics unchanged. Node B done; next gate C.

### B6 — Resource Mode Integration (→ 0.9.4.7, validated)

- `paramsForMode()`: per-mode (cpuFloor, holdMs, killAt, relieveAbove).
  Maximum nimble/self-prioritizing, Gaming early-yield/late-return/calm,
  Balanced/Custom = B4 values, explicit `setHoldMs()` wins, unset (-1)
  takes the mode default.
- Engine connects `schedHw.mode = policy_.mode` once per scan; Manual's
  CPU cap stays upstream (scheduler-side Manual == Balanced, tested).
- Validation: CPU 64/64, GPU 65/65 (B6 additions: mode table, mode
  divergence, asymmetric relief, per-mode holds, Manual == Balanced);
  UI parity via `scan_workflow_test`; `--version`/`--smoke` on both.
  Search semantics unchanged. GPU suite: 8 failures on the first
  post-build run, green twice after on identical binaries (environment
  contention, not a regression; uncaptured names kept as an ops lesson).

### B5 — Transfer / Workload Cost (→ 0.9.4.6, validated)

- Total-cost rule on the observed path only:
  `effGpu = 1/(1/effGpu + transferSecPerUnit)`. Transfer volume (1032 B)
  fixed by packing layout; bandwidth a coarse 12000 MB/s default for Node
  C to calibrate. Workload variation lives inside observed rates;
  explicit workload model belongs to Node E.
- `GpuBackend::kTransferBytesPerUnit` unifies the VRAM-budget magic;
  one-line pipeline forward, once-per-scan engine injection.
- Validation: CPU 64/64, GPU 65/65 (B5 additions in `scheduler_test`);
  UI parity via `scan_workflow_test`; `--version`/`--smoke` on both.
  Search semantics unchanged. Production transfer (≈86 ns) is
  effectively zero today — structure first, weight later.

### B4 — Stability Control (→ 0.9.4.5, validated)

- SMA-4 on observed rates + loads (feed-if-known-else-clear; baselines
  static). Kill-band hysteresis (kill ≤ 0.02, relieve > 0.05, keep
  previous between). Minimum hold 10 s default on published decisions;
  first change after `decide()` exempt; adjustments counts publishes.
- Scheduler-internal only: engine gating reads the published decision,
  so execution stability is inherited with zero engine changes.
- Validation: CPU 64/64, GPU 65/65 (B4 additions: SMA glide, hold
  freeze/release, 5-step kill band, unknown-lane clear); UI parity via
  `scan_workflow_test`; `--version`/`--smoke` on both. Search semantics
  unchanged.

### B3 — Live Load Awareness (→ 0.9.4.4, validated)

- System-load inputs on `SchedulerHardware` (cpu/gpu/mem + known flags;
  queue depths ride along D1-reserved, ignored by evaluation).
  Headroom rule: CPU floored at 0.05, GPU floorless, mem recorded only.
  Full GPU kill → `external_load_throttle` + edge count.
- Engine holds one `SystemLoadMonitor` per scan, refreshed at decide /
  re-evaluation points. `externalLoadThrottling` filled from the edge
  counter; `throttlingEvents` stays 0 for B4+.
- Validation: CPU 64/64, GPU 65/65 (B3 additions in `scheduler_test`);
  UI parity via `scan_workflow_test`; `--version`/`--smoke` on both.
  Search semantics unchanged. No smoothing/hysteresis (B4), no
  transfer/workload cost (B5).

### B2 — Runtime Throughput Feedback (→ 0.9.4.3, validated)

- `ThroughputWindow` (30 s recent window, unknown below 2 samples or on
  expiry, no smoothing). `SchedulerHardware` gains observed image-path
  rates; both known and positive → `observed_throughput` shares, else the
  baseline path. One-sided unknown falls back, never zero.
- Engine attributes completed images per hashing backend per batch and
  refreshes rates before re-evaluation. Video excluded (decode side is
  C/E). `currentCapacities()` reports observed rates or baselines.
- Validation: CPU 64/64, GPU 65/65 (extended `scheduler_test` in place,
  counts unchanged); UI parity via `scan_workflow_test`;
  `--version`/`--smoke` on both. Search semantics unchanged.

### B1 — Minimal Adaptive Allocation (→ 0.9.4.2, validated)

- `CpuGpuScheduler` (`src/scheduler.h/.cpp`): baseline-only inputs (CPU
  threads, GPU ON/OFF, availability, SM count), proportional shares,
  reason codes, 2000 ms re-evaluation cadence at existing phase points.
  No moving average / hysteresis / transfer / workload / external-load
  models; no pipeline, worker, or queue changes.
- Engine wiring: one `decide()` per scan; image/video gates read the
  decision (behavior-identical to the old flag). `finishScan` records the
- scheduler section as `measured` (shares, backend, adjustments,
  image + video fallback sum).
- Validation: CPU 64/64, GPU 65/65 (incl. new `scheduler_test`: B1
  decision table, cadence, telemetry JSON on both trees); UI parity via
  `scan_workflow_test`; `--version`/`--smoke` on both. Search semantics
  unchanged (engine 1.5.0, DB 1.0.3, cache v9).

## Node B/C/D detailed-design state

Node B and D are **new design work**. Node C is **partial reuse and extension of the existing profile/benchmark concepts**.

Detailed implementation contracts live under `docs/implementation-briefs/`. The Roadmap keeps direction and boundaries; implementation stages are not duplicated there.

Current B/C/D briefs:
- `docs/implementation-briefs/B-adaptive-scheduler.ko.md / .en.md`
- `docs/implementation-briefs/C-calibration-profile.ko.md / .en.md`
- `docs/implementation-briefs/D-pipeline-queue.ko.md / .en.md`

Node I is documented in the Roadmap (change-management record) and its first
step in `docs/build-history/0.9.4.23.ko.md / .en.md`.

The active implementation target is B. B begins with small, verifiable B1 steps. D pipeline internals must not be implemented prematurely inside B.

## Node A — Foundation / Terminology / Instrumentation

### Current goals

- vendor-neutral GPU terminology at higher layers
- GPU ON/OFF only in the user UI
- preserve CPU Resource Mode semantics
- prepare MSF_ENABLE_GPU / MSF_GPU_BACKEND
- establish build-windows-gpu naming
- retain current CUDA as a concrete backend
- benchmark schemaVersion
- measurement states
- scheduler / queue / transfer / decoder telemetry
- sampled-vs-decoded frame separation
- cancellation / partial state
- preserve search correctness

### Node A exit criteria (all verified in 0.9.4.0 → next gate B)

- CPU-only works — Release build PASS, CTest 62/62 PASS
- GPU OFF works — CPU fallback path untouched, monitor/CPU suites green
- GPU ON reaches the existing CUDA path through backend abstraction —
  `cuda_backend_test` green on the clean `build-windows-gpu` tree
- manual GPU utilization UI removed — toolbar `GPU %` spinbox deleted
- unmeasured=0 ambiguity removed — `MeasureState`, `null` + state, latch
- benchmark instrumentation does not change search results — verdict paths
  untouched; parity suites green on both trees
- baseline regression tests pass — CPU 62/62, GPU 63/63
- build-tree / CMake naming aligned — `MSF_ENABLE_GPU`/`MSF_GPU_BACKEND`,
  `windows-gpu` preset, `build_windows_gpu.ps1`, clean tree
- code/docs/tests report matching version state — 0.9.4.0 everywhere

## Recovery branch recording

When a problem occurs, record it as a substep of the current node rather than using the problem itself as a version meaning.

Example:

A
|
+-- A1: CMake migration error
|
+-- A2: option compatibility fix
|
+-- A3: CPU build regression
|
+-- A4: GPU smoke
|
+-- A5: final A gate
|
+---- fail --> A1/A2/...
+---- pass --> B

Each substep records:
- symptom
- reproduction conditions
- root cause
- chosen fix
- changed files
- tests
- failed attempts
- result after the fix
- impact on the next gate

### B6 records from the implementation

- B6 (naming): "Manual" is the UI label; the enum value is
  `ResourceMode::Custom` (C2838). Fixed the test, noted for future UI work.
- B6 (splice damage): a test insertion orphaned the B5 xfer block outside
  its function (C2447/C2059). Restored into `b4checks()`, duplicate line
  removed, structure verified by read-back.
- B6 (SMA interaction): mode-hold tests first flipped rates before the
  first observation, so SMA mixed decide-time values (50/50, not 90).
  Rewrote to the B4 pattern (rateless decide, then observe). Test design
  must respect SMA memory.
- B6 (suite): GPU suite failed 8 right after the fresh build, then 65/65
  twice on identical binaries. Recorded as environment contention with
  the honesty note that failure names were not captured.

### B5 records from the implementation

- No code issues; one tooling note: a version-string edit reported
  "identical strings" spuriously and was re-applied with wider context.
  Verified by grep sweep afterwards.

### B4 records from the implementation

- B4 (test setup): the kill-band test armed `gpuLoadKnown=true` with value
  0 at `decide()`, injecting a phantom 0 into the SMA lane (averages never
  reached the band). Fixed by arming known-flags only with real readings —
  the same unknown-vs-zero principle as Node A telemetry. Test-only.
- B3 kill test now pins `setHoldMs(0)` (it exercises the kill rule, not
  stability), proving hold and kill rules independent.

### B3 records from the implementation

- B3 (member duplication): a header edit duplicated `last_`/`decided_`/
  `lastHw_` (C2086). Removed the duplicate block; no logic change.
- Known B3 behavior: load-driven share jitter is recorded as-is
  (adjustment counter moves with the system). Damping is B4's scope.

### B2 records from the implementation

- B2 (linkage): the first B2 test draft declared `b2checks()` in one
  anonymous namespace and defined it at global scope (LNK2019). Fixed by
  moving the forward declaration to file scope. Test-only, no gate impact.

### B1 records from the implementation

- B1 (test premise): `scheduler_test` initially expected an unarmed
  `decide()` to hold the cadence; the first `maybeReevaluate` arms the
  clock by design. Fixed the test (explicit arm step), not the code.
- B1 (flake): `reveal_window_test` failed once inside the full CPU suite
  and passed standalone and on suite re-run — Explorer foreground
  contention, unrelated to scheduler paths. Recorded, no gate impact.

### A1 records from the Node A implementation

- A1 (build break): `C2001: newline in string literal` in the new
  `gpuExecState` JSON line — a dropped closing quote during editing.
  Fixed by restoring the `<< "\""` terminator; CPU build green after.
- A1 (test env): `benchmark_test` expected `"diskState":"measured"`, but
  this machine has no PDH LogicalDisk counters, so `not_available` is the
  correct record. The test now asserts state/flag agreement instead of one
  environment's value. No gate impact.
- A1 (encoding): a bulk PowerShell version-bump rewrote UTF-8 BOM/Korean
  literals in `gui/main.cpp`/`mainwindow.cpp`. Reverted and re-applied via
  surgical edits; diff verified minimal. Lesson: never bulk-rewrite
  non-ASCII sources with plain `Set-Content`.
- A1 (script default): new `build_windows_gpu.ps1` required `VCPKG_ROOT`
  while `build_windows_cpu.ps1` defaults to `C:\src\vcpkg`. Aligned to the
  existing convention. No gate impact.

## Version progression policy

A version represents a **validated code state**.

Example:
- 0.9.4.0 = A initial implementation baseline
- 0.9.4.1 = A fix/verification complete
- 0.9.4.2 = validated state ready to enter B
- 0.9.4.3 = next stabilization point inside B

These numbers are examples only. The corresponding build-history document is the final evidence for each real version.

## Roadmap / Progress / Build History

Development Roadmap
        |
        v
Development Progress
        |
        v
Build / Test
        |
        v
docs/build-history/<version>.ko.md
docs/build-history/<version>.en.md

Roadmap → Progress → Build History provides design intent → current position → actual code/test evidence.

## OpenCode working rule

When starting new work, OpenCode reads:

1. AGENTS.md
2. development-roadmap.ko.md or .en.md
3. development-progress.ko.md or .en.md
4. relevant architecture documentation
5. required source and tests

When an implementation brief exists for the active node, OpenCode also reads that KO/EN brief with the relevant architecture documentation.

It checks the current node's exit criteria before implementation.

When a problem occurs, record an A1/B1/C1-style substep, resolve it, and return to the same node gate.

## Updating the next state

Once source implementation begins, update:

- Current Node
- Current Version
- Active Substep
- Blocker
- Validation Result
- Next Gate

**This is not a prediction document; it is the state record that prevents loss of the current development position.**


### C4.1 — Calibration Lifecycle Fix (→ 0.9.4.13, validated in 0.9.4.14)
- **GPT Fix:** GPU OFF→ON incomplete-profile lifecycle, default 30-day stale enforcement, CPU model/GPU driver identity completion, and failed-retry preservation.
- Windows CPU/GPU Release build and full CTest validation: **PASS in 0.9.4.14** (CPU 66/66, GPU 67/67) after moving `kDefaultMaxAgeDays` to `PerformanceProfile` and prefixing the test reference. Details: `docs/build-history/0.9.4.14.en.md`.

### D1a — Image-Path Observability Gate (→ 0.9.4.16, PASS)
- Remote 0.9.4.15 implementation validated locally: packMs excludes decode, cpuHashMs records executed CPU hash work (mirror-inclusive on GPU path, separate field), batchMaxDepth is structurally 1 (documented, D3 activates the counter), batchItems counts attempted inputs, transfer stays not_measured.
- Gate fix inside: schema v2 → v3 for the 6 added keys (Node A rule), plus completion of the partial 0.9.4.15 version sweep (vcpkg/index/GUI-about-titles/package/tests).
- Validation: CPU 66/66, GPU 67/67; `--version`/`--smoke` on both. Next: D1b (walker + video async).

### D1b — Walker/Video Observability Gate (→ 0.9.4.17, PASS)
- Walker handoff: enqueue/dequeue counts with exact depths, maxDepth, starved ticks (timeout + empty + walker-alive only). No producer-block counter (unbounded by construction, documented).
- Video async granularity: ranges/rangeFiles/rangeState per launched range (no timing split; range walls already in `videoStageMs`).
- Schema v4 for the new keys (Node A rule). No topology change (4 hooks).
- Validation: CPU 66/66, GPU 67/67; `--version`/`--smoke` on both. Next: D2 (barrier review on D1a/D1b evidence, pre-register first).

### D2 — Barrier Review (→ 0.9.4.18, done)
- Pre-registered before code: straggler-wait candidate, (max−min) gain hypothesis, parity-or-revert criterion.
- Completion-order harvest in video ranges (same threads/joins; 5 ms idle bound; cancel semantics kept). `maxRangeFileMs` per range for JSON-only quantification; failed ranges unrecorded.
- Non-candidates documented with dependency reasons: image phase joins, walker poll, DB single-writer, final matching boundary.
- Schema v5 (new-key rule). No topology change.
- Validation: CPU 66/66, GPU 67/67 (parity proves order-independence); `--version`/`--smoke` on both. Next: D3 (queues on D1b depth evidence).

### D3-Minimal — Bounded Walker Queue (→ 0.9.4.19, PASS)
- `WalkerQueue` (~90 lines): capacity + cancel-aware backpressure only. Pause stays scanner-side; waits re-check on a 100 ms bound. Capacity 4096 as safety bound (never optimal claim).
- Engine: bounded push with cancel drop semantics, pop notify, single `shutdown()` before join (all exits). `ScanControl::walkerQueueCapacity` test override (0 = default).
- Telemetry: `walker.capacity` (config) + `walker.blockedTicks` (waits while full). Schema v6.
- Validation: CPU 67/67, GPU 68/68 (unit: capacity/FIFO/block-resume/cancel/shutdown; 60-file cap-16 integration with bound + 1770-pair parity; pause-toggled parity); `--version`/`--smoke` on both.
- Pre-register outcome: no throughput regression observed; memory bound holds by construction; rollback triggers untouched. Full D3 stays deferred (no measured imbalance).

### D4a — CUDA Backend Internal Timing (→ 0.9.4.20, PASS)
- **Measure only.** No overlap, no double/triple buffering, no pinned memory, no new stream topology, no scheduler change. D4b remains undecided.
- Pre-register committed before any code change (`ec7cff6`), so the ordering is auditable.
- `cuda_backend.cu`: six `cudaEvent_t` recorded inline at each boundary (H2D, kernel, D2H) on the same stream; created once at backend create, destroyed at destroy. Event record/elapsed failure disables timing only — the hash result and the function's return value are untouched (`measurement failure != GPU processing failure`).
- New vendor-neutral `GpuBackend::HashTiming` (out-param, defaulted) is the only thing crossing the abstraction boundary; no CUDA type reaches the engine, pipeline, or recorder.
- `syncHostMs` is a **directly measured** host wall time inside `cudaStreamSynchronize`, deliberately *not* `hostTotal - device sum` (that subtraction mixes enqueue and scheduling overhead into device numbers).
- Telemetry: `gpuH2dDeviceMs` / `gpuKernelDeviceMs` / `gpuD2hDeviceMs` / `gpuSyncHostMs` / `gpuHostTotalMs` / `gpuTimedBatches`, each with its own state. Schema v7. Existing `addGpuBatchMs()` key and meaning preserved.
- New `gpu_timing_test` registered in **both** trees (so the recorder contract is proven where CUDA does not exist): key existence, measured/not_measured state flips, non-negativity plus one loose upper bound, CPU-build `measured == false`, and hash-vs-CPU parity as the instrumentation-does-not-change-results guard. No exact host-vs-device summation equality is asserted.
- Validation: CPU 68/68, GPU 69/69; `--version` 0.9.4.20 and `--smoke` PASS on both. Pre-register rollback triggers: none fired.
- Real measurement (RTX 3080 Ti, synthetic, pageable): kernel device time ~0.24 ms at both 16 and 256 images (does not scale with batch), H2D scales with data, `syncHost` ~0.013 ms, and a constant ~0.27 ms host-side gap. Observation only — not a D4b verdict, because a small synthetic probe is not a representative dataset.

### D8a — Reproducible Dataset Foundation / Dataset Fingerprint (→ 0.9.4.21, PASS)
- **Foundation, not optimization.** The blocker was never code: "compare two
  states under identical conditions" was not expressible. D4b, Full D3, and D8
  were all blocked on the same missing evidence.
- Pre-register committed before any fixture or code change (`26cef86`).
- Investigation first, as the task required. Key findings that shaped the
  design: the repo commits **no binary assets** (495 tracked files, all text);
  every test generates media at runtime; `msf_core` has **no cryptographic
  hash** (Qt `QCryptographicHash` is GUI-only and not exposed, no openssl);
  every video fixture shells out to ffmpeg, whose output is **not byte
  reproducible**.
- Dataset is therefore **generated, not committed**
  (`scripts/prepare_dataset.ps1`): 60 hand-rolled 8x8 BMP files, two
  structures — `images/exact` 48 (12 groups × 4 byte-identical) and
  `images/varied` 12 (distinct seeds). Reads no clock, environment, or random
  source; every byte is a pure function of `(seed, x, y)`.
- **Video excluded at this stage** and documented as separate future work:
  ffmpeg muxes encoder/creation-time metadata, so a content hash over it would
  drift between runs and defeat the point of the whole step.
- `src/dataset_fingerprint.{h,cpp}`: self-contained SHA-256 (validated against
  4 NIST vectors — a subtly wrong digest would make every fingerprint drift
  silently), manifest of `canonical relative path + size + SHA-256(content)`
  sorted by byte order, then hashed. No absolute path, timestamp, pid, or
  hardware name, so two copies of the same content under different roots
  share a fingerprint. Whole files are read: the scanner's `quick()` covers
  only the first 64 KiB and serves file identity, not dataset identity.
- Benchmark JSON: `meta.dataset` with `fingerprint` / `fingerprintVersion` /
  `fileCount` / `totalBytes` / `state`. `root` stays as the *location* and is
  untouched. Unmeasured is `"fingerprint": null` + `not_available`, never 0.
  Schema v8. Engine attaches it right after `bench_.start()`.
- New: `dataset_fingerprint_test` (A–F, 37 checks, both trees) and
  `dataset_e2e_test` (real engine scan records the same fingerprint an
  independent computation produces; also asserts `scanned != 0`).
- Reported dataset: fingerprint `f01d5c77…d7c`, 60 files, 14,760 bytes,
  fingerprintVersion 1. Reproduced across separate runs.
- Two fixes found by the tests rather than assumed: the SHA-256 test vectors
  I first wrote had a wrong expected value (the implementation was right —
  checked against a second vector set), and a reporting file initially
  written *inside* the dataset root would have joined the next manifest walk
  and changed the fingerprint it described, so it is written beside the root.
- Validation: CPU 70/70, GPU 71/71; `--version` 0.9.4.21 and `--smoke` PASS on
  both. All seven pre-register rollback criteria clear.
- **D4b overlap and Full D3 topology are still not implemented.** Both are
  now decidable against this fingerprint.

### D8b — Scaled Dataset / Walker Queue Evidence (→ 0.9.4.22, PASS, measurement only)
- Pre-register committed before any fixture change (`8f52587`).
- **No product code change.** The generator gained `-Scale full`, and the
  review probe gained stage-breakdown reporting.
- Why it was needed: D8a's "no gain" verdict came from an **absent
  measurement subject** — 60 tiny 8×8 files in 2 directories make the
  pipeline do no real work, so `maxDepth = 1` described the dataset, not the
  architecture.
- Dataset v2 = D8a's 60 files **kept byte-identical** (parity anchor) plus two
  deliberately asymmetric axes, because the queue only grows when consumer
  cost exceeds producer cost: `tree/` 2,400 tiny files across 240 leaf
  directories (walk cost) and `bulk/` 240 images at 256×192 (decode + crop +
  color-thumbnail cost). Total 2,700 files / 36,007,560 B / 95 dirs.
  Fingerprint `9b113848…4253c`; the algorithm did not change, only content.
- Measured (RTX 3080 Ti, 3 runs, cold index each):
  - `walker maxDepth` **1 → 964 mean / 1076 max** (23.5 % of capacity), so
    D8a's depth of 1 is now shown to have been a dataset-size artifact
  - `blocked_ticks` **still 0** — the producer never blocked; depth 1076 is
    3.8× below the capacity of 4096
  - `gpu_batch share of engine wall` 0.34 % → **0.026 %**
  - `kernel device` 17.1 ms of a 20.5 ms device sum (84 %), so there is
    little for overlap to hide
- Stage breakdown of the 109,737 ms engine wall: **`analyze` 98.62 %**,
  walk 1.40 %, image stage 0.59 %, gpu batch 0.04 %.
  **D3+D4 addressable ceiling: 0.044 %.**
- `analyze` is `MediaPipeline::analyze()` — the final matching/grouping stage
  at `media_search_engine.cpp:702-703` — which is **outside** the
  queue/transfer/overlap scope Node D owns.
- Therefore: **D4b and Full D3 are deferred on evidence, not on assumption**,
  and continuing D yields no meaningful gain on this workload.
- Validation: CPU 70/70, GPU 71/71; no product code touched.
- Honest scope: the `bulk` images were sized to *favour* observing the queue,
  and GPU share was still 0.026 %. For the GPU path to matter, images would
  need to be far larger (no crop/thumbnail cost) or hash batches far more
  numerous.

### D9a — Analyze Internal Observability (→ v0.9.4.23, pre-register committed, implementation pending)
- **New node, opened because D ran out of owned work.** D8b proved the
  bottleneck (`analyze`, 98.62 % of engine wall) is outside the D brief, so
  continuing D could not produce a meaningful gain.
- Pre-register committed before any code change (`0fc3344`).
- **Measurement only.** No cache enlargement, no parallelization, no SSIM
  algorithm change, no candidate-generation change, no threshold change.
- Current state: `analyze` is recorded as a **single** number
  (`bench_.addAnalyzeMs` at `media_search_engine.cpp:702-703`). The
  benchmark `matches` section gives counts but **no internal time split**, so
  "too many pairs" / "each pair is slow" / "the cache never hits" cannot be
  told apart.
- Code inspection produced a grounded hypothesis to test, not to act on:
  `verifyImagePair` (`image_verify.cpp`) re-decodes both files (up to 4
  decodes/pair) and runs up to 20 `frame_ssim` evaluations per grey-zone
  pair, while `kVerifyCacheMax = 32` hard-caps the verify cache — against
  2,700 files that is effectively a miss per pair. **Still a hypothesis;
  the code is not touched until per-stage times exist.**
- Planned telemetry: `analyzeIndexMs` / `analyzeScanMs` / `analyzeVerifyMs` /
  `analyzeVideoMs`, plus `verifyCalls` / `verifyDecodeMisses` /
  `verifyCacheHits` / `ssimEvals` / `frameSsimEvals` / `videoTemporalPairs`,
  and two derived values that make the call — **`verifyHitRate`** (≈0 confirms
  the cache-size cause) and **`msPerVerifyCall`** (separates "many pairs" from
  "each pair is slow").
- Success criterion: the instrumentation **identifies which bottleneck it
  is**, whichever it turns out to be. Being right about the cause is the
  deliverable, not a speedup.
- Current version stays 0.9.4.22 — a version represents a *validated* code
  state, and no code has changed yet.

### D9a — Analyze Internal Observability (→ 0.9.4.23, PASS, measurement only)
- Pre-register committed before any code change (`0fc3344`).
- **Measurement only.** Verify cache still 32, no parallelization, no
  SSIM/index/threshold/grouping change, no Scheduler/CUDA change.
- Architecture: `src/analyze_telemetry.h` is a plain Qt-free data carrier that
  depends on nothing — not the recorder, not the pipeline, not the verify
  code. `image_verify` and `scan_pipeline` fill it; the engine copies it in.
  **No recorder pointer ever travels downward**, satisfying the brief's
  explicit no-strong-coupling requirement.
- **Stage sum cannot exceed analyzeMs by construction.** `flushVideo()` also
  runs inside the candidate loop, so nested timers would overlap. Video time
  is accumulated and excluded, and `scanMs` is defined as the remainder:
  `scanMs = total - index - verify - video` (clamped at 0). The four slices
  therefore sum to the total exactly, and scan honestly absorbs unattributed
  control overhead instead of pretending it belongs to an unmeasured stage.
- Counters count events, never estimates. `verifyCalls` excludes video pairs
  (they return before doing work); misses/hits are per *lookup* (2 per call);
  `ssimEvals` counts invocations while `frameSsimEvals` counts actual
  `frame_ssim` executions, so the gap between them is itself a measurement.
  Derived values are `null` + `not_measured` when their denominator is zero —
  no 0.0 is manufactured by dividing by zero.
- Schema v8 → v9. New `analyze_telemetry_test` in both trees, **41 checks**.
- **Measured** (RTX 3080 Ti, D8b dataset `9b113848…4253c`, cold index):
  - `index` 3.2 ms (0.00 %), `scan` 350.8 ms (0.32 %), **`verify` 109,142.3 ms
    (99.67 %)**, `video` `not_measured` (no video in the dataset — not a fake 0)
  - substage sum 109,496.3 ≤ analyzeMs 109,501.9 → the rule holds
  - `verifyCalls` 158,020 · `misses` 12,949 · `hits` 14,519 · **`verifyHitRate`
    0.5286** · `msPerVerifyCall` 0.69–0.74 ms · `ssimEvals` 137,340 ·
    `frameSsimEvals` 274,680
  - **Counters are internally consistent**: `ssimEvals/10 == (misses+hits)/2 ==
    13,734` exactly, so 13,734 calls did the full decode+SSIM path
- **Hypothesis verdicts.** "verifyImagePair dominates" → **confirmed**
  (99.67 %). "kVerifyCacheMax = 32 causes re-decodes" → **refuted**: the hit
  rate is 52.9 %, not ≈0, because candidate pairs arrive clustered in index
  order. A code-reading hypothesis overturned by measurement — the reason the
  pre-register demanded measurement first.
- **Actual dominant structure:** of the 158,020 calls reaching the gate, ~91.3 %
  short-circuit at kFast and ~8.7 % (13,734) run decode + SSIM at ~8.5 ms each,
  producing 117 s. So the bottleneck is **candidate-pair volume reaching the
  gate, not per-call cost**.
- Parity: `groups` 156,152 identical to 0.9.4.22, and `analyze_telemetry_test`
  asserts instrumented vs uninstrumented `verifyImagePair` are double-identical.
- Validation: CPU 71/71, GPU 72/72; `--version` 0.9.4.23 and `--smoke` PASS on
  both. All six pre-register rollback criteria clear.
- Next candidates identified but **not** implemented: lower the 4.3 % arrival
  rate, or lower the 8.5 ms per expensive verification (4 decodes + 20
  `frame_ssim`, of which 9 of 10 `ssimBuf` calls are aspect combinations with
  no buffer reuse). Caveat: this dataset uses synthetic deterministic
  fingerprints, so a real library's candidate ratio will differ.

### 0.9.4.24 — QSettings Organization Name Change (PASS, identity only)
- Not part of the D telemetry sequence; a narrow portable-UI-identity change.
- The QSettings organization name **is** the settings folder name, so the old
  value `newclear-ui` was a GitHub account name shipping in the product.
  Renamed to `MediaSimilarityFinder-ui`; application name, executable, and
  CMake target all unchanged. `setDefaultFormat`/`setPath` untouched.
- Investigation first: there was exactly **one** active-code occurrence; the
  other hits were repository-owner labels and historical build-history docs.
  The QuickLook registry lookup uses `NativeFormat` with an explicit path, so
  it is unrelated and was deliberately not touched.
- A plain rename would make existing users' UI state look reset, so migration
  is the substance of this work. It runs inside `initAppSettings()` right
  after the identity is set, because `MainWindow` immediately reads
  `ui/ignored`.
- Deletion policy is strictly ordered: copy → open the new INI through a real
  QSettings → assert `status() == NoError` → assert `ui/mainGeom` and
  `ui/splitter` exist → **only then** remove the legacy INI, and remove the
  legacy directory only if it is empty. On any failure the legacy file is
  preserved, so no path can lose a user's settings. When the new location
  already exists it wins and legacy is left untouched, which also makes the
  migration idempotent.
- Tests: 5 cross-process phases in `ui_settings_test` (plant / verify /
  both-present / none), split across processes because a same-process
  read-back would pass from QSettings' in-memory cache even with broken disk
  persistence. The existing cross-process round-trip still passes under the
  new identity.
- Also validated for real: a temporary portable directory with the actual
  `--smoke` run for cases A (new only), B (legacy only, 11 keys preserved and
  legacy removed), and C (both present, new wins, legacy preserved).
- Validation: CPU 76/76, GPU 77/77; `--version` 0.9.4.24 and `--smoke` PASS on
  both. Engine 1.5.0 / DB 1.0.3 / cache v9 unchanged.

### D9b — Locked future-build comparison baseline and candidate priority

After D9b, all related builds must **remember and compare against the 0.9.4.24 D9a baseline**.

#### 1. 0.9.4.24 D9a baseline

| Item | Baseline |
| --- | ---: |
| dataset | D8b standard, 2,700 files, fingerprint `9b113848…4253c` |
| verifyCalls | 158,020 |
| verifyHitRate | 0.5286 |
| expensive verify | 13,734 |
| average expensive-verify cost | ~8.5 ms/call |
| expensive-verify aggregate | ~117 s |
| decode | up to 4/call |
| frame_ssim | up to 20/call |
| ssimBuf | 10/call, 9 aspect-combination calls |
| groups | 156,152 |

These values are the baseline for both **performance comparison and correctness/parity comparison**.

#### 2. D9b primary candidate — B

**B: reduce the cost of one expensive verification** is the D9b primary target.

The optimization may reduce redundant decode work, repeated buffer work, repeated SSIM input preparation/copies, and repeated buffer work in aspect combinations.

The following semantics are invariant by default:

- verdict semantics
- candidate semantics
- similarity threshold
- grouping semantics
- Engine / DB / cache semantics

Do not assume parity merely because the verdict formula is untouched. **Existing/new result parity must be measured explicitly.**

#### 3. Required future-build comparisons

After D9b, compare against 0.9.4.24 using, where reproducible:

- verifyCalls
- expensive verify count
- total verify time
- ms per expensive verify
- verifyDecodeMisses / verifyCacheHits
- decode count
- frameSsimEvals
- ssimBuf-related cost or equivalent telemetry
- groups
- final match/verdict parity
- CPU/GPU parity

When dataset, hardware, or cold-index conditions differ, document the difference and do not present the result as a direct performance improvement.

#### 4. Candidate A remains deferred

**A: reduce candidate-pair arrival rate** could have a large effect because 158,020 calls reach the gate, but it can affect candidate/verdict semantics.

Do not implement A in D9b.

Any future A work requires a separate pre-register first, including candidate recall, false-positive/false-negative risk, candidate count, verifyCalls, and final grouping/verdict parity.

A is not discarded; it is an **intentionally deferred candidate**.

#### 5. Relationship that must remain visible across future builds

```
0.9.4.24 D9a
    ├─ baseline: 158,020 verifyCalls
    ├─ expensive: 13,734 × ~8.5 ms ≈ 117 s
    │
    ├─ D9b primary: B
    │      └─ per-expensive-verify cost reduction
    │
    └─ deferred candidate: A
           └─ candidate-arrival reduction
              (verdict semantics risk)
```

When recording a new build, include the **change relative to the 0.9.4.24 D9a baseline** whenever a valid comparison is possible.

### D9b — Candidate B (expensive verify cost reduction) → 0.9.4.25, **NOT ACCEPTED**

- Pre-register commit `b33154b` (before code), baseline is 0.9.4.24 D9a.
- **Verdict: failed.** Stop conditions 7 (measurement noise) and 8 (B is not
  the real bottleneck) both apply.
- Implementation: `verifyScorePlan` reduces `centerCropResize` from 8 to 6
  calls and reuses each aspect buffer across three `ssimBuf` calls. The
  reference implementation is preserved as `verifyImagePairReference`.
- **Not one counter changed** — which simultaneously proves verdict, grouping,
  and parity are all preserved, and says the optimized target was not the
  dominant cost.
  ```
  verifyCalls 158,020 · expensive 13,734 · hits 14,519 · misses 12,949
  ssimEvals 137,340 · frameSsimEvals 274,680 · groups 156,152  (all identical)
  total verify ms  109,142 / 117,291  ->  110,717   no reduction
  ```
- **Cause (measurement-based)**: `frame_ssim` runs 4,096 inner-loop iterations
  per call on 64x64 (20 calls = 81,920 ops), and that is essentially the whole
  cost. D9b only cut `centerCropResize` from 8,192 to 6,144 (2.3 % of total
  work). **The "duplication" the hypothesis pointed at did not exist** — the
  original already computed `rA`/`rB` once outside the loop.
- D9a's own run-to-run variation (7.5 %) is the same magnitude as the D9b
  observation, so it sits inside measurement noise.
- Accuracy: `verify_parity_test` 25 checks **double-identical** (5 seeds x 5
  aspect shapes). CPU 77/77, GPU 78/78 PASS. Engine/DB/cache/schema unchanged.
- **Candidate A stays deferred.** This failure does not raise its relative
  priority — what A touches is verdict semantics.
- Remaining questions: where does the 8.5 ms actually go, and can
  `frame_ssim` itself be reduced? (window size, precision, and early exit all
  affect results, so they need their own pre-register.)

### D9c — Expensive verify internal cost accounting → 0.9.4.26, **PASS**

- Pre-register commit `39d2442` (before any code), baseline remains 0.9.4.24 D9a.
- **An instrumentation build. Performance improvement was not the goal.**
- **Headline: decode is 94.90 % of the 8.564 ms expensive verify.**
  `frame_ssim` — the stage D9b optimized — is **0.55 %**.
  ```
  decode             111,621.7 ms   94.90 %   8.1274 ms/verify
  key (stat+64KB)      4,421.9 ms    3.76 %   0.3220 ms/verify
  frame_ssim             645.2 ms    0.55 %   0.0470 ms/verify
  crop/aspect             201.9 ms    0.17 %   0.0147 ms/verify
  mirror flip             188.7 ms    0.16 %   0.0137 ms/verify
  cache store+copy         53.9 ms    0.05 %   0.0039 ms/verify
  other (remainder)       481.4 ms    0.41 %   0.0351 ms/verify
  total                117,614.6 ms  100.00 %   8.564 ms/verify
  ```
  `sum == verifyMs` holds exactly (overflow 0.0000 ms).
- **D9b's failure now has evidence behind it.** The whole scoring path is
  0.55 %, and decode is roughly 173x frame_ssim. A candidate that does not touch
  decode cannot address more than the remaining 5.1 %.
- The "~8.5 ms" D9a derived is for the first time a direct measurement (8.564 ms).
- Two structural facts from the counters:
  - **Each miss decodes the same file twice** (`verifyDecodes` 25,898 = 2x12,949),
    because both `decode` and `decodePreserveAspect` are called on the same path.
  - **Cache hits still read and hash 64 KiB.** Of 27,468 lookups, 14,519 are
    hits. 363.3 MB read and hashed. (3.76 % of cost)
- Not measured, and not estimated: `frame_ssim` internals, decode internals
  (the 4.31 ms breakdown).
- Accuracy: every existing counter identical to D9a — verifyCalls 158,020,
  expensive 13,734, misses 12,949, hits 14,519, ssimEvals 137,340,
  frameSsimEvals 274,680, **groups 156,152**. `verify_parity_test` 25 checks
  PASS, new `verify_instrumentation` PASS (stage sum <= total, instrumented ==
  uninstrumented). CPU 77/77, GPU 78/78. `--smoke` exit 0, `--version` 0.9.4.26.
- Engine/DB/cache/schema unchanged; SSIM, threshold, verdict, candidate, and
  grouping semantics untouched.
- **Next candidate: D (image decode cost).** It must first decompose the
  4.31 ms decode. Candidate A stays deferred. A standalone frame_ssim
  optimization is not worth a pre-register at 0.55 %.
- D9b remains **NOT ACCEPTED**. It is not reclassified as a success.

### D9d — Decode internal decomposition + cache mutex accounting → 0.9.4.26, **PASS**

- Pre-register commit `6f0ee90`. **Measurement build, no optimization.**
- **89.55 % of decode is two WIC COM construction calls: file open + factory.**
  ```
  open    (CreateDecoderFromFilename)  71,865.2 ms  62.15 %  2.7749 ms/call
  factory (CoCreateInstance WIC)       31,674.5 ms  27.39 %  1.2230 ms/call
  copy    (CopyPixels = real decode)    5,956.9 ms   5.15 %  0.2300 ms/call
  comInit                                492.7 ms   0.43 %
  resize  (Fant)                         369.0 ms   0.32 %
  convert                                311.7 ms   0.27 %
  metadata (EXIF orientation)            194.0 ms   0.17 %
  orient                                 15.4 ms   0.01 %
  total                               115,628.4 ms  100.00 %  4.4654 ms/call
  ```
  `open + factory` = 89.55 % of decode = **84.9 % of the whole expensive verify**.
- **Three targets refuted, with measurements**
  - `WICBitmapInterpolationModeFant` 0.32 %. Expensive by name, 369 ms in
    reality, and changing it would change resize quality, a verdict property.
  - EXIF skip 0.17 %.  - `frame_ssim` 0.53 % of the verify.
  - The actual decompression, `copy`, is 5.15 %. Where "decode is expensive"
    intuition points is a twentieth of the cost.
- **Cache mutex: not a bottleneck, closed with evidence**
  ```
  acquires 40,417 (27,468 lookups + 12,949 stores)
  waitMs   8.4  (0.0002 ms/acquire)   wait share 10.38 %
  holdMs  72.4  (0.0018 ms/acquire)   hold share 89.62 %
  ```
  Total lock time 80.8 ms = **0.07 %** of decode. This closes the question D9c
  could not answer, and confirms there is no hidden contention inside D9c's
  `other` (0.42 %).
- 25,898 decodes = 12,949 x 2 (each miss decodes the same file twice).
  WIC ok 25,898, PGM fallback 0, failures 0, orientation applied 0.
- `sub-sum 110,879.4 vs total 115,628.4` — the 4.1 % gap is not a mis-scoped
  timer but unmeasured code between the instrumented calls (allocation,
  GetSize, HRESULT checks). Reported rather than folded into a stage.
- Accuracy: all counters identical to D9a, **groups 156,152**, identical across
  all 5 runs. D9c's exclusive sum still holds (`sum 121,906.2 == verifyMs`,
  overflow 0.0000). Parity 25 PASS, CPU 79/79, GPU 80/80, `--smoke` exit 0.
- Not measured and not estimated: 4.1 % of decode, the internals of `open` and
  `factory`; `decodeColorAspect` deliberately uninstrumented (UI thread).
- Wall clock is above the D9a/D9b range but that is **not a regression** — this
  is an instrumented build and D9c already sat at the top of the range. The
  trustworthy signal is the stage shares.
- Noise 6.3 % (5 runs, 117,392.9–124,839.3 ms). Call counts identical in all 5.
- **Next: Candidate D1 — image decoder construction and file open.** The 2.77 ms
  `open` and 1.22 ms `factory` must be decomposed first. Not implemented here.

### D1 — File open / WIC factory root-cause measurement → 0.9.4.27, **PASS**

- Pre-register commit `f4f8ebd` (docs only, no source change).
- **D9d figures re-verified (§7 of the directive)**: `open+factory` =
  103,539.7 ms, i.e. **89.55 %** of decode 115,628.4 (the D9d documents said
  89.54 %, the truncation of 89.5452 — **corrected to 89.55 %**) and
  **84.93 %** of the 121,906.2 verify total (D9d's 84.9 % was correct). Both
  documents updated.
- **Question B — Factory2 works, the fallback never fires**
  ```
  Factory2 attempts  25,898
  successes          25,898  (100.00 %)
  fallbacks               0  (0.00 %)
  ```
  The measured factory cost is one activation, not two. The "the first attempt
  is wasted" hypothesis is **refuted**. There is nothing to remove on this
  machine. The fallback stays in the code as a portability guard.
- **Question A — 97.8 % of `open` is WIC-internal, not OS file I/O**
  ```
  CreateDecoderFromFilename  68,000.8 ms  2.6257 ms/call
  OS CreateFileW reference    1,490.5 ms  0.0576 ms/call
  WIC-specific remainder     66,510.3 ms  2.5682 ms/call  = 97.8% of open
  ```
  The real OS cost of opening the file is 0.0576 ms (2.2 %); the rest is WIC's
  own work. The name "file open" was misleading. Without the reference probe,
  2.6 ms of COM work would have been attributed to opening a file and a file
  handle optimization would have looked reasonable while doing nothing.
- **A bug this build caught**: the first implementation reported
  `factoryMs = 235,023,895 ms`. The helper accumulates into running totals and
  the call site added those totals again on every call, growing quadratically.
  Pre-register stop condition 5 (split does not reconstruct factoryMs) caught
  it exactly. The fix takes the per-call delta. Recorded rather than quietly
  fixed.
- factory split: Factory2 attemptMs 17,769.0 (0.6861 ms/attempt), fallback 0,
  `split 17,769.0 == factoryMs 17,769.0` over 0.0000.
- **The factory reduction vs D9d (31,674.5 -> 17,769.0) is not a performance
  improvement.** It is the same double-counting bug; recorded as a measurement
  difference (neither regression nor improvement).
- Run statistics (5): mean 119,608.3 / median 120,346.2 / min 116,839.9 /
  max 121,273.4 / range 4,433.5 ms = 3.68 % of median.
- Accuracy: all counters identical to D9a, **groups 156,152**, identical across
  all 5 runs. D9c's verify sum holds (`sum 116,676.9 == verifyMs`, over
  0.0000). The 12,949x2 double decode remains, as the Non-goals required.
- Verification: parity 25 + instrumentation PASS, tr_keys 21 PASS (the GUI fix
  did not regress), schema 29 `additive-read-confirmed` (v9 kept),
  analyze_telemetry 41 PASS, CPU 79/79. GPU: core and all test targets pass.
  **The GUI exe could not be relinked because a user session of 0.9.4.26 holds
  the file lock** — left running rather than terminated (not a code failure, a
  build-environment constraint).
- **Two candidates removed by measurement**: removing the Factory2 fallback
  (0 occurrences) and file handle/stream management (OS 2.2 %).
- **Next**: WIC decoder construction. The 2.5682 ms WIC-internal portion must
  be decomposed first.

## Long-term record system for performance experiments (after 0.9.4.27 D1)

Performance tuning and profiling experiments are preserved as technical
records **regardless of whether they succeeded**. No new document kind is
introduced; the roles are split across the existing set.

```
Implementation Brief  pre-defines what will be tested
Build History         actual numbers, run conditions, refutation evidence,
                      revisit conditions            <- the detailed record
Work Log Index        links the experiment lineage and surviving/refuted candidates
Development Progress  current state summary only (no repeated numbers)
```

**Refuted candidates are not deleted.** `NOT ACCEPTED` / `REJECTED` /
`DEFERRED` / `LOW PRIORITY` are verdicts under the current conditions, not
permanent retirements, and can become valid again when the environment changes.
The lineage and candidate states are in the **Performance / Tuning Experiment
Index** in `docs/worklog/0.9.4.*.md`.

```
D9a BASELINE -> D9b NOT ACCEPTED -> D9c PASS -> D9d PASS -> D1 PASS -> D2 PASS
Refuted by measurement: cache 32 · scoring duplication · Fant · EXIF
                        cache mutex · Factory2 fallback · raw OS file open
Deferred: candidate A (DEFERRED) · candidate C (Path C, DEFERRED)
```

Current state (details in Build History)

```text
D9b = NOT ACCEPTED   (correctness preserved, performance objective not met)
D9c = PASS           (decode 94.90 %)
D9d = PASS           (open 62.15 % + factory 27.39 %)
D1  = PASS           (Factory2 100 % success / 0 fallbacks, 97.8 % of open is WIC-internal)
D2  = PASS           (Stream path shortest for all 7 formats, but the absolute gain
                       is small and HandleStream adds maintenance cost
                       -> Path C = DEFERRED, not adopted)
D3  = PASS           (second decode = 49.60 % of verifyDecodeMs, 39.19 % of verifyMs,
                       but f and a are different geometries so "removal" would be an
                       accuracy regression)

Current active investigation:
a follow-up optimization pre-register (written, before measurement)
  candidate: produce both f (fixed 32x32) and a (aspect) from one high-resolution
             decode plus two resizes
  contract: `docs/implementation-briefs/I-decode-once-resize-twice.en.md`
  D3 only measured. No de-duplication, no cache, no call merging, no HandleStream
  adoption. This candidate carries a high accuracy risk (unchanged groups is the
  acceptance condition), so results are recorded first and production adoption is
  judged separately.
```

## D2 — WIC Decoder Entry-Path Comparison (0.9.4.28, **PASS**)

- Goal: compare the decoder-creation cost of `CreateDecoderFromFilename` vs
  `CreateDecoderFromFileHandle` vs `CreateDecoderFromStream`.
  **Measurement-only.**
- Result: `CreateDecoderFromStream` is the shortest at the decoder step for all
  7 formats (bmp/jpg/png/webp/gif/tiff/ico), roughly 16–30 % below A on the
  combined metric.
- **Not adopted into the product.** The absolute gain is small (~0.02 ms per
  file), the hand-written `HandleStream` carries maintenance cost, and there is
  no production full-path benchmark yet. -> **Path C = `DEFERRED`** (not
  discarded; revisit conditions recorded).
- **Do not compare D1 and D2 directly.** D1's 2.6257 ms/call includes the
  one-time codec DLL load while D2 measured a warm state. The conditions
  differ, so they are not comparable and this is **not** a performance
  improvement.
- Three real bugs found while implementing, all permanently recorded: Path B
  handle lifetime (use-after-close), Path C connecting the wrong stream, and
  the probe calling `CoUninitialize()` (COM lifetime follows caller ownership).
- Dataset extended with TIFF 14 + ICO 12 -> 3,347 files / 102,475,315 bytes,
  fingerprint `e8f8fa6a…e2640a`. Because the dataset changed, D2 absolute
  totals are not directly comparable to D1.
- Verification: CPU 79/79 PASS, GPU 80/80 PASS, `--version` 0.9.4.28.
- **Next**: a separate D3 pre-register in `docs/build-history/0.9.4.29.*.md`.

## D3 — Duplicate decode / decodePreserveAspect Cost (0.9.4.29, **PASS**)

- Goal: measure the real wall-clock cost of the verify-miss path decoding the
  same file twice. **Measurement-only.**
- Dataset: the D2 final dataset, fixed (3,347 files, `e8f8fa6a…e2640a`),
  1 warm-up excluded plus 5 measured runs.
- The pre-implementation code check found that both calls receive the **same
  `&tel->decode`** at `image_verify.cpp:78`, so D9c/D9d's "decode 94.90 %" was
  the sum of two calls. D3 split that accumulator per call and folds it back
  with `mergeDecodeTelemetry`, preserving the D9d key meanings exactly.

| metric | value |
|---|---:|
| `decode()` / `decodePreserveAspect()` calls | 12,962 / 12,962 (identical across 5 runs) |
| `decode()` per call | 0.8700 ms |
| `decodePreserveAspect()` per call | 0.8568 ms |
| second / first | 98.49 % |
| second / `verifyDecodeMs` | **49.60 %** |
| second / `verifyMs` | 39.19 % |
| second / `analyzeMs` | 38.47 % |
| (both calls) / `verifyDecodeMs` | 99.96 % |
| split identity `decodeSplitOverMs` | 0.000000 |

- **The word "duplicate" is inaccurate.** `f` is fixed 32x32 and `a` is an
  aspect-preserving downscale, and `verifyScorePlan` consumes both. Removing the
  second decode would delete `a` and change similarity/verdict.
- The follow-up candidate is therefore **not** removal but **producing both from
  one high-resolution decode**, and it is **not implemented in D3** (pre-register
  §11).
- groups 156,211 across all 5 runs (the D9a 156,152 differs only because of the
  D2 dataset addition).
- Two defects recorded permanently: the pre-register §5.1 counter error, and a
  missing member-by-member telemetry transfer in `ScanPipeline::analyze` that made
  the first run read a silent 0.
- **Next**: a follow-up optimization pre-register (written,
  `docs/implementation-briefs/I-decode-once-resize-twice.en.md`).

The recording obligation and the required field list are defined in
`AGENTS.md` item 9 and `docs/document-naming.*.md` section 2-1.

## 0.9.4.30 — Automatic CPU-usage 10–90% normalization

- The user CPU spin box, stored `ResourcePolicy.cpuPercent`, and engine input
  always use the same `10–90` value.
- Preset values, GPU policy, scheduler, worker, and batch formulas are unchanged.
- There is no CPU-policy QSettings persistence path, so no migration was needed.
- Verification: CPU 80/80 PASS, GPU 81/81 PASS, `--version` 0.9.4.30.
- This is unrelated to the D3 follow-up optimization and claims no performance
  improvement.

## 0.9.4.31 — D3 Follow-up Shared-Decode Candidate Measurement

- Committed the pre-register
  `docs/implementation-briefs/I-decode-once-resize-twice.en.md` before any
  measurement code.
- Measured baseline 2-decodes vs candidate (one shared decode + two in-memory
  Fant derivations) with `tests/shared_decode_probe.cpp` (measurement-only, no
  production changes) across 849 files × 5 resolutions × 5 runs.
- The candidate costs 24–46 % less than baseline but byte reproduction fails
  and 2 verdict flips occur at R≥384 (same TIFF near-duplicate pairs,
  reproduced 5×) — candidate DEFERRED, not adopted.
- Verification: CPU 80/80 PASS, GPU 81/81 PASS, `--version` 0.9.4.31,
  probe `--selfcheck` 14 checks.
- **Superseded in part by 0.9.4.32**: the "2 verdict flips" and the "delta grows
  with R (2.63→9.19)" trend above were probe artifacts (mixed baseline/candidate
  buffers), not candidate behaviour. The performance figures stand, and are
  measurement-only probe values, not product performance.

## 0.9.4.32 — D3 Follow-up Candidate Stability / Resampling Cause

- Committed the pre-register
  `docs/implementation-briefs/I-shared-decode-stability.en.md` first.
- Found the measurement defect: the full-run baseline score received the
  current file's baseline buffers together with the previous file's
  **candidate** buffers, so its "baseline" was neither world.
- Re-measured with pure pairing (both sides baseline vs both sides candidate):
  **0 verdict flips at all 8 investigated R** (128·192·256·288·320·352·384·512),
  max abs delta **1.972205–2.557407** (not monotonic in R).
- Cause identified: two-step Fant chain + intermediate 8-bit GrayImage
  quantization + differing resampling chains. The scoring-stage
  `centerCropResize` (integer nearest-neighbor) only carries that difference
  into the crops; it is not the origin.
- Within the investigated R range (R128–512) byte parity was not restored.
  Impossibility beyond that range is not proven and is not claimed.
- Verdict comparison was performed on **1691 sampled pairs** per resolution; a
  **full-scan groups comparison was not performed.** No EXIF-orientation or PGM
  fallback fixture exists, so those paths are unverified.
- Candidate stays **DEFERRED**, production adoption NO. Revisit priority:
  ① full-scan groups comparison ② near-threshold fixtures ③ EXIF fixtures
  ④ PGM fixtures ⑤ accuracy verification without changing the production path.
  Correction record in `docs/build-history/0.9.4.32.en.md` §2.
- Verification: CPU 80/80 PASS, GPU 81/81 PASS, `--version` 0.9.4.32,
  probe `--selfcheck` 14 checks, `decomp_mismatch=0`.
- Commit 16e1240 and tag v0.9.4.32 contain version-string updates only; no
  production algorithm change.

## 0.9.4.33 — I-2: Shared WIC Source + Two Independent Scalers Candidate

- Committed the pre-register
  `docs/implementation-briefs/I-shared-wic-source.en.md` before the probe.
- Candidate structure: the factory, decoder, frame and EXIF-orientation source
  are created once, while the scaler/converter/`CopyPixels` triples are created
  **independently** for f and for a. There is **no intermediate GrayImage**, so
  I-1's two-step Fant chain is structurally absent.
- **Exactness (whole dataset, identical across CPU 5 runs + GPU 5 runs)**
  - f geometry 849/849, f byte 849/849
  - a geometry 849/849, a byte 849/849
  - **Zero** geometry or pixel mismatches (I-1 had 93 geometry mismatches at
    R128 alone)
  - baseline and candidate fail on the **same 5** files
    (`WINCODEC_ERR_FRAMEMISSING`). **0 files fail only in the candidate.**
- **Cost (measurement-only probe)**: CPU 40.8–42.2 %, GPU 41.5–42.6 % lower. The
  second `CopyPixels` drops from 3.88 to 1.09 ms mean, so WIC does share the
  real decode.
- **Two real defects found during verification** (neither caught by
  self-consistency): (1) `CoUninitialize` was called while WIC objects were
  still alive, producing an access violation — exactly the failure the product
  source warns about; fixed by making a helper own every WIC object so the
  caller uninitializes only after it returns. (2) The loop's
  `if (!okCand) continue` masked baseline failures, producing a misleading
  `baseFail=0`; fixed by recording both sides independently plus a
  `candOnlyFail` counter.
- **EXIF end-to-end verification is `not_measured`**: the 8 synthesized fixtures
  are byte identical, but the product applied orientation 0 times, so the EXIF
  branch never ran. WIC returned `PROPERTYNOTFOUND` for all four query paths,
  which is not a candidate-only problem — it also suggests the **product's own
  EXIF path may never have executed** against this dataset. Registered as a
  separate task.
- No full-scan groups comparison was performed; it is deferred to the separate
  production implementation brief given the EXIF gap.
- **Verdict: exactness PASS / production adoption NO.** No threshold was tuned
  and no score tolerance widened; neither was needed.
- Verification: CPU 80/80 PASS, GPU 81/81 PASS, `--version`/`--smoke` exit 0 on
  both trees, new probe `--selfcheck` 11 checks, existing probe selfcheck 14
  checks as a regression check. **No production code change.** Details:
  `docs/build-history/0.9.4.33.en.md`

## 0.9.4.34 — I-2 Verification Completion (Full Corpus · EXIF Correction · Full-Scan Groups)

- **Correction 1 — "whole dataset" wording error.** The 849 files in
  v0.9.4.33 were not the 3,347-file standard dataset but a **probe corpus**
  of `images/format` plus a BMP stride sample. Re-measured over the full
  standard dataset: `both_success=3341, baseline_only_fail=0,
  candidate_only_fail=0, both_fail=6` (the extra one is
  `images/format/SOURCES.md`, a text file). f/a geometry and pixel are both
  **3,341/3,341**.
- **Correction 2 — two EXIF diagnostic defects.** (1) Query paths were kept as
  `const char*` and passed with an `LPCWSTR` cast, producing **false
  negatives**; re-measured with correct wide literals. (2) The printed applied
  counter was a variable that was never incremented, so it always read 0. After
  the fixes the **EXIF fixtures are 8/8 valid** with values matching 1–8.
- **Incidental finding (product defect).** The path the product uses,
  `/app1/ifd/exif/{ushort=274}`, is rejected by WIC with
  `WINCODEC_ERR_BADPROPERTYKEY` (8/8). The working path is
  `/app1/ifd/{ushort=274}`. This is a **product EXIF defect unrelated to I-2**;
  production code was not changed here, so it remained unfixed at this point.
  v0.9.4.33's "the product EXIF path does not work" was not an observation and
  had no basis.
  → **fixed in 0.9.4.35** (see the next section).
- **The I-2 structure passes under rotation.** With the fixture's transform
  forced so rotation genuinely happens, the shared-source candidate and two
  fully independent pipelines are **7/7 byte identical** for f and a.
- **Exhaustive full-scan groups.** Both sides use the product deciding function
  `verifyScorePlan`, enumerated exhaustively with no prefilter:
  `pairs_compared=5,579,470`, `baseline_groups=candidate_groups=457,126`,
  `verdict_diffs=0`, `max_abs_score_diff=0.000000000`.
- **Cost (full corpus 3,341; not comparable with the probe corpus)**
  CPU raw 47.6–48.2 % / adjusted 32.0–32.6 %, GPU raw 47.2–48.9 % /
  adjusted 31.5–32.2 %. The second `CopyPixels` dropping from 0.8354 to
  0.2834 ms is an **observation in this environment and dataset**, not a
  generalization.
- **Verdict: `READY FOR PRODUCTION IMPLEMENTATION REVIEW`, implementation NOT
  performed.** I-1 stays `DEFERRED`.
- Verification: CPU 80/80 PASS, GPU 81/81 PASS, `--version`/`--smoke` exit 0 on
  both trees, new probe selfcheck 11, existing probe 14, telemetry 25/29/71,
  `git diff --check` clean. **No production code change.** Details:
  `docs/build-history/0.9.4.34.en.md`

## 0.9.4.35 — EXIF Orientation Query Path Correction and Post-Fix Regression

- Committed the pre-register
  `docs/implementation-briefs/I-exif-orientation-path-fix.en.md` before the
  production change.
- **Survey result:** the bad literal was in exactly three places in
  `src/image_decoder.cpp` (fixed fingerprint, aspect fingerprint, display
  color), all copies of identical logic. Rather than three line edits it is
  consolidated into **one shared helper**.
- **Minimal correction:** try `/app1/ifd/{ushort=274}` (JPEG) then
  `/ifd/{ushort=274}` (TIFF) and **return as soon as a value appears**. No
  container branch, no XMP, no new framework. The display path is corrected too,
  because a rotated photo shown differently from its fingerprint would make the
  screen disagree with matching.
- **Gate A PASS** — fixtures orientation 1–8: metadata 8/8, values 8/8
  matching, baseline applied 7/7 (orientation 1 is the identity and is
  correctly not counted), decode success, and the source really changes
  209x248 → 248x209 on the 90/270 fixtures.
- **Gate B PASS** — f/a byte parity 8/8. Production and candidate read the same
  orientation and apply the same transform.
- **Gate C PASS** — the exhaustive group result is **identical before and
  after** the fix (5,579,470 pairs, 457,126 groups, 0 verdict diffs, max score
  difference 0.000000000).
  - Standard dataset EXIF re-measured: 8 files carry metadata, **all
    orientation 1**, 0 applicable, 0 query failures. **The dataset contains no
    rotated image**, which is why scan results do not change — and that is a
    measured result, not an assumption.
  - Those 7 TIFF files prove the `/ifd/` path genuinely works.
- **Cost:** the fix's own cost is measured separately — one_path 0.01678 ms vs
  two_paths 0.04547 ms, **0.02870 ms per file**. Because the helper returns on
  the first value, a JPEG does not pay it. Corpus cost stays at the
  v0.9.4.34 level: CPU raw 46.8–49.4 %, GPU raw 46.9–50.4 % lower.
- **Regression test added:** an EXIF regression went into the dataset-free
  `--selfcheck` and was **registered in CTest** — selfcheck 11 → 16 checks,
  CPU 80/80 → **81/81**, GPU 81/81 → **82/82**. It asserts orientation 1 is
  not applied, orientation 6 is, the rotation actually changes pixels, and
  baseline/candidate parity on a rotated file. Without it the defect could
  silently return.
- **Gate D PASS — I-2 = `READY FOR PRODUCTION IMPLEMENTATION`.** However
  **production I-2 integration was NOT performed** (adoption NO). That is the
  next step.
- **Left open:** no rotated image in the dataset (would change the
  fingerprint), and no XMP fallback.
- Dataset rules respected: fingerprint `e8f8fa6a..e2640a` kept,
  `SOURCES.md` kept, `both_fail` 6 kept.
- Details: `docs/build-history/0.9.4.35.en.md`


## 2026-09-30 — Benchmark / Console CLI Design Decision

F-1 currently forbids production NVDEC adoption, so Benchmark / Console CLI infrastructure is recorded as an independent cross-cutting track. This does not authorize F-2 NVDEC production integration.

### Fixed decisions

1. Benchmark mode is limited to AUTO / CPU-only / GPU-max.
2. Existing GUI image/video selection remains a separate axis.
3. Console exposes the same media scope:
   - --media images
   - --media videos
   - --media all
   - default: all
4. GUI retains at most the latest three mode results per source folder.
5. Console is the cumulative long-term data-mining store and is not automatically pruned.
6. Normal Search Index and benchmark index/cache state are isolated.
7. Run = one mode measurement; Suite = a group of mode measurements under the same dataset/media scope.
8. Each Run stores sourceRoot, dataset fingerprint, mode, mediaScope, CPU/GPU/scheduler settings, environment, and completion/failure/fallback state.
9. GPU-max is not GPU-only; mandatory CPU work and CPU fallback remain.
10. Benchmark Runs should prefer independent processes, while OS filesystem cache is recorded as uncontrolled.

### Build / implementation schedule

Version numbers are not pre-assigned.

~~~text
S0  design / pre-register
 ↓
S1  Console entry foundation
 ↓
S2  Run / Suite benchmark core
 ↓
S3  Benchmark storage isolation
 ↓
S4  GUI Detailed Logging
 ↓
S5  Console benchmark execution
 ↓
S6  Data-mining automation
 ↓
S7  Help / usability
 ↓
S8  Full verification / release gate
~~~

Each stage closes before the next stage starts:

source change → CPU/GPU build → CTest → execution verification → documentation → Build History when applicable → commit

This entry records the approved design and implementation order, not an implementation result or benchmark measurement. The detailed contract is maintained in docs/architecture/benchmark-telemetry-roadmap.*.

## 2026-09-30 — Benchmark Console Design Finalized

The Benchmark/Console design is finalized before implementation.

Key decisions:
- file-level AUTO -> CPU -> GPU-max execution
- immediate append-only journal persistence per completed file
- Ctrl+C cancellation with partial Suite preservation
- reuse the existing CPU Resource Policy; recommended default Balanced 55%
- Maximum is an actual measured condition, not a simple linear extrapolation
- interactive fixed header is three rows and never auto-wraps
- Target paths may use middle ellipsis on screen while JSON stores the full value
- header owns execution conditions and IMG/VID progress; the lower area owns detailed CURRENT FILE information and compact completed history
- TTY/non-interactive use different renderers but the same journal/JSON model
- existing benchmark source/schema and the v0.9.4.43 baseline remain permanent legacy references

The next implementation stage is S1 Console entry foundation; benchmark core follows in S2. This is a documentation design gate only and does not change product source.
### Benchmark Console Mockups Added

Static mockups were added for review of the finalized Console UI contract.

- HTML: `project/uimock/benchmark-console-mockup.html`
- JPG preview: `benchmark-console-mockup.jpg` (review artifact in the current working environment)
- The superseded GUI-like SVG console draft was removed from the final Console mockup set.

The mockups are not executable benchmark functionality; they are references for S1/S2/S5 implementation.

## 2026-10-01 — S5 Console benchmark execution implemented and verified

S5 was implemented across three commits (S5-1 CLI, S5-2 renderer, S5-3 execution
wiring) and verified by actually running `--benchmark`. The detailed record is in
`docs/build-history/S5-verification.ko.md` / `.en.md`.

Baseline commits:

```text
f4c3fdd  S5: add benchmark CLI parsing
fcace68  S5: add console benchmark renderer
842ba01  S5: connect console benchmark execution
```

Implemented scope:

- **S5-1**: parses `--benchmark <folder>` plus `--mode/--suite/--log-dir/--log`.
  `--mode` is a single comma list and is normalized to canonical order. Invalid
  input is never silently ignored and is always an argument error. 40 -> 95 checks.
- **S5-2**: Qt-free display-only renderer `src/benchmark_console_renderer.*`.
  The presentation model reuses S2's `GpuBackendKind` / `BenchmarkStatus` /
  `MediaKind` rather than copying them, so the renderer cannot express a state S2
  never produced. Every observable value is `std::optional` and an absent value is
  omitted rather than replaced by 0 or a dash. 100 checks.
- **S5-3**: `runConsoleBenchmark()` reuses S3 `BenchmarkSession` -> S2
  `BenchmarkRunner` -> the product scan path unchanged. No new engine. Includes
  automatic suite id generation, `--log-dir` storage root override, `--log` renderer
  sink and Ctrl+C wiring. 32 checks.

Defect found during E2E and fixed:

- `Scanner::scan_stream()` in `src/scanner.cpp` never set `FileState.kind`, so it
  stayed `Unknown` and the benchmark media filter never fired at all.
  `--media images` / `videos` / `all` all processed the same 10 files.
- Restored in one line by reusing the `isVideoPath()` rule that already existed in
  the product. No new classifier was created and `toMediaKind()` was not changed.
- Impact scope was established by tracing the code. The only reader of the
  scanner's `kind` is the `toMediaKind` call in `benchmark_core.cpp`, and
  `MediaSearchEngine` recomputes it itself via `kindOf(path)`, so there is
  **no impact on production indexing/search**. (The initial "product-wide
  image/video classification broken" estimate was wrong and is corrected here.)
- The regression test was confirmed to actually catch the defect: with the fix
  disabled it fails with exit=3, and with it restored it passes with exit=0.

Measured results:

- `--media`: images 8 cases (`Image=8`), videos 2 cases (`Video=2`), all 10 cases
  (`Image=8, Video=2`), matching the real dataset composition (Image 8, Video 2,
  Total 10). Option order independence also confirmed.
- Default 3-mode: exit 0, Cases 10, Records 42, correct journal sequence.
- Suite: an explicit id is identical across suite.json / runs.jsonl / summary.json.
  Auto generation is `YYYYMMDD-HHMM-SS` (UTC). `..\..\evil` is refused with exit 2.
- `--log-dir` does not pollute the default storage, and `--log` is a text artifact
  with no ANSI.
- On the CPU build AUTO / GPU-max are `SKIPPED`; on the GPU build they succeed with
  `effectiveMode=CUDA`.
- CPU CTest 96/96, GPU CTest 97/97, both builds exit 0.

Verifications not run (not recorded as PASS):

- Real TTY ANSI repaint: **NOT RUN** — the verification environment had no Windows
  console.
- Real Windows Ctrl+C trigger: **NOT RUN** — same reason. A process kill was not
  substituted for it.

S5 status: **feature implementation complete, automated/non-interactive E2E
verification complete.** Only the two items above are preserved as not run.
(Interpretation as of 2026-10-03: the above is historical evidence of S5
infrastructure verification. S5 infrastructure stands revalidated after the S4
semantic reset, but final product benchmark validation is DEFERRED because
image/video comparison and search/index semantics are not final. Current
benchmark numbers must not be treated as final performance evidence.)

Recorded as separate debt: S3 `benchmarkNowStamp()` formats `localtime_s` output
with a literal `Z`, so journal timestamps carry local time labelled as UTC. The S5
suite id is real UTC, so the two differ by the local offset. `benchmark_store.cpp`
was not changed in this S5.
## 2026-10-01 — S6 Data-mining Automation design investigation and brief

S6 design and its implementation brief were written. **No S6 code was written at this
stage, and S6 is not CLOSED.** Details are in
`docs/implementation-briefs/S6-data-mining-automation.ko.md` / `.en.md`.

Facts confirmed by the design investigation:

- **No analysis tool that reads the journal exists in the repository.** The only code
  that reads it is `replayJournal()` + `buildSummaryJson()`, whose output is counts
  and a `totalElapsedMs` sum only.
- **Python does not exist in this project.** Zero `.py` files, zero
  `requirements.txt` / `pyproject.toml` / `Pipfile` / `setup.py`, and both CI
  workflows use `shell: pwsh`. The scripting convention is PowerShell.
- **Journal schema measured** (full key scan over 29 journals / 862 lines). The actual
  fields of `run_started` / `mode_result` / `case_complete` / `run_finished` /
  `run_cancelled` are all recorded in brief section 6.
- **Largest constraint**: `git` / `resourcePolicy` / `cpuPercent` / `gpuPercent` /
  `gpuEnabled` / `distance` are **all at 0 occurrences**. The cause is that
  `MSF_BUILD_GIT` exists only in the generated header and is not written to the
  journal. So `buildVersion` alone cannot distinguish two commits of the same version,
  and the "comparison summary" required by roadmap §25 (storage requirement) and defined
  as S6's core in §28/§30 cannot be completed in the current state. This is recorded as
  S6's largest risk at entry condition ① in brief section 22.
- `run_cancelled` has no `completedAt` field and has **zero measured samples**.
- The journal `timestamp` appends a literal `Z` to a `localtime_s` value.
  Timestamps in this repository have split four ways (all confirmed by measurement), and
  S6 neither hides this nor silently reinterprets it: section 16-2 rules work around it
  by ordering on `suiteId` and computing duration only as
  `completedAt - startedAt`. S3 code is not changed.
- The legacy `BenchmarkRecorder` (schema 9) is completely separate from the journal
  (schema 1), writes no file, and has no reader. S6 neither reads nor writes it.

Design decisions:

- The journal parser is not rewritten; `replayJournal()` is reused, to avoid
  duplicating the S3 recovery rules.
- A **measured / derived / invalid** three-way split is enforced, and the count of
  excluded invalid entries must be printed.
- The regression verdict threshold is **not decided**. `AGENTS.md` item 9 already
  governs it, and `worklog` `E-3B-BUG` is a precedent of an arbitrary threshold
  being removed.
- Because `S2-PERF` accepted cache and load as uncontrolled, S6 provides a regression
  **candidate plus a condition warning**, not a regression **verdict**.
- Output is two layers, machine-readable plus human-readable, and both come from the
  same computed result.
- The default output contains no analysis timestamp (reproducibility). It is enabled only
  via `--provenance`.

Internal S6 decomposition (not an official roadmap node): S6-1 schema contract -> S6-2
ingestion/normalization -> S6-3 grouping -> S6-4 aggregation -> S6-5 cross-run comparison
-> S6-6 regression candidates -> S6-7 full verification.

Recorded as DEFERRED: the regression threshold, whether to add `git` / `distance` /
`resourcePolicy` to the journal, the final output format choice (JSON vs CSV),
automated suite execution, the p95 algorithm, and `run_cancelled` analysis refinement.
Fixing the S3 timestamp is a separate S3 follow-up.
## 2026-10-01 — S3 journal git provenance added (S6 entry condition 1 resolved)

The `git provenance missing` entry condition from the S6 brief has been resolved.
**This is not an S6 implementation.**

Changes:

- Added `BenchmarkRun::gitCommit` / `BenchmarkRequest::gitCommit` (S2 run metadata,
  in the same place as `buildVersion`). The runner only **copies** it and never
  invokes git.
- `run_started` records `gitCommit` as an additive field directly after
  `buildVersion`. The value reuses S5's `MSF_BUILD_GIT` as-is.
- When no provenance is available it records the literal `"unknown"`, never an empty
  string.
- Added `JournalReplay::gitCommit` and taught replay to read it. A journal without
  the field leaves it **empty and never invents a value.**

**The schema version stays 1** (it was not bumped unconditionally). Three grounds:

1. `kBenchmarkJournalSchemaVersion` is **written** at 7 sites and read nowhere. No
   code branches on it.
2. The replay parser extracts only the keys it knows, ignores missing fields, and does
   not reject unknown fields.
3. Existing repository precedent in `benchmark_schema_test`: "meta.schemaVersion is
   still 9 (**additive fields did not force a bump**)".

Measured verification:

- A real Console benchmark run wrote `run_started.gitCommit` = `fcace68` and that
  binary's generated `MSF_BUILD_GIT` = `fcace68` -> **exact match**. This compares
  the generated value with the journal value; no git command was re-executed.
- The Console header of the same run also shows `Git : fcace68`, so there is no
  duplicate logic.
- Comparing the `run_started` field sets of an old and a new journal: **1 added
  (`gitCommit`), 0 removed.** Purely additive.
- Journals with no provenance (3 checks), summary regeneration, and every regression
  test pass.
- The **29 pre-existing journals were not modified**; they have no such field and remain
  valid replay input.

Not changed: `benchmark_store.cpp` (the `benchmarkNowStamp` timestamp is untouched),
`journalSchemaVersion`, any S3 recovery semantics, S2 execution semantics, the GUI, the
Console renderer, MediaKind/Scanner, NVDEC, and the legacy benchmark schema.

Tests: `benchmark_journal_test` 51 -> **66**, `benchmark_integration_test` 137 ->
**144**. CPU build exit 0 / CTest **96/96**, GPU build exit 0 / CTest **97/97**.

Remaining entry conditions: `distance` and `resourcePolicy` are still absent from
the journal (DEFERRED), and the controlled-measurement-environment definition, repeated
run data, and a `run_cancelled` measured sample are also unresolved. S6 is still not
CLOSED.
## 2026-10-01 — S6-1 Data Ingestion / Journal Normalization implemented

Only the `S6-1` stage of the S6 brief (collection and normalization) was
implemented. **No comparison, aggregation, regression or statistical judgement was
implemented, and S6 is still not CLOSED.**

Location: `src/benchmark_data_mining.{h,cpp}` (member of `msf_core`). The test
is `tests/benchmark_data_mining_test.cpp` (69 checks).

### Core rule: recovery semantics are not reimplemented

There is **no journal parsing here at all.** `msf::replayJournal()` makes every
integrity, idempotency and anomaly decision, and S6 only calls it and projects the
result. S6 does exactly one thing itself, and it is not a judgement:

- It enumerates which runs exist by locating the `run_started` and terminal
  records with S6's own copy of the minimal flat field reader
  (`jsonFieldString`). A suite journal accumulates several runs while replay
  returns one run at a time, so the run list is needed before a per-run replay. That
  step copies four strings and nothing else: runId, datasetFingerprint, mediaScope
  and completedAt.

`completedAt` is not populated by S3's replay at all: `JournalReplay::completedAt`
stays empty even though `run_finished` writes the field, because replay only reads
`completionReason` from the terminal record. S6 therefore reads it with the same
minimal reader.

### Normalized model

- `IngestRunClass` (Complete / Cancelled / Incomplete / Corrupt / Unavailable) is an
  **S6 analysis classification, not a benchmark status.** `BenchmarkStatus` keeps
  S2's definition unchanged.
- Ten `IngestExclusion` values record why something was excluded. Nothing is
  dropped silently.
- `GitCommitState` distinguishes three states (Known / Unknown / **Legacy**). A
  Legacy journal never has the current git value filled in for it.
- Fields the journal does not carry (`distance` / `resourcePolicy` /
  `gpuBackend`) are kept as absence with `std::optional` and are never replaced
  by 0, false or `"unknown"`.
- A mode `elapsedMs` has a value only when the mode actually ran. Storing 0.0 for a
  mode that never ran would be a claim, because it reads as "instantly fast".

### Two findings from measurement

1. **Wall duration has one-second resolution.** S3 writes `startedAt` and
   `completedAt` as `%Y-%m-%dT%H:%M:%S` with no fractional part, so the derived
   duration is always a multiple of 1000 ms and **any run shorter than a second comes
   out as exactly 0**. A stored 0 means "finished within the same second", not "took
   no time". It must stay distinguishable from the absent value, and a test pins
   that.
2. **A multi-run journal exists in the real storage tree.** `suite-TEST-SUITE-001`
   holds two `run_started` records. 31 runs come out of 30 journals and each keeps
   its own datasetFingerprint and cases, which is the S3-BUG regression guard.

### Verified against the real storage tree

The test binary has a read-only diagnostic path so this could be checked against
journals the product actually wrote.

`	ext
journals=30  unreadable=0  withoutRuns=0
runsFound=31  accepted=31  excluded=0  commitless=0  anyFatal=0
provenance: legacy=30  known=1  unknown=0   withDuration=31
determinism: IDENTICAL
`

`legacy=30 / known=1` matches the real situation of 29 pre-provenance journals plus
one that carries the field.

Tests: ingestion **69 checks** (new). journal 66 / store 51 / integration 144 / core
63 / gui_store 110 / worker 35 / ui 34 / ui_e2e 40 / cli 95 / renderer 100 /
orchestrator 32 all PASS. CPU build exit 0 / CTest **97/97**, GPU build exit 0 /
CTest **98/98**.

Not changed: S2 BenchmarkRunner/Executor/Request, the S3 journal schema, recovery or
summary generation, the Console renderer, the GUI, Scanner/MediaKind, NVDEC, and the
legacy benchmark. No journal field was added, and no CLI was introduced.
## 2026-10-01 — S6-2 normalization hardening / analysis data contract fixed

The `S6-2` stage of the S6 brief (fixing the normalized data contract) was
implemented. **No grouping execution, aggregation, statistics or regression judgement
was implemented, and S6 is still not CLOSED.**

Location: existing `src/benchmark_data_mining.{h,cpp}` hardened, plus a new
`tests/benchmark_data_contract_test.cpp` (67 checks).

### Five real gaps found while auditing S6-1

1. **The run's benchmark status was not stored.** `BenchmarkStatus` existed on
   mode and case but the run only had `IngestRunClass`, which is an S6
   classification. The terminal record's status is now read and kept as
   `std::optional<BenchmarkStatus> runStatus`. S3's replay does not fill this
   field either, so it is read with the same minimal reader as completedAt.
2. **An empty fingerprint and a missing one were not distinguishable.** The writer
   always emits the field, so a blank value is a real answer meaning "the source
   could not be measured", and S6-1 collapsed it into absent. That is now fixed as
   `DatasetIdentityState{Missing,Empty,Valid}`.
3. **There was no aggregate type.** `NormalizedBenchmarkData` was introduced.
   The S6-1 name is kept as `using IngestResult = NormalizedBenchmarkData;` so the
   existing test keeps compiling unchanged.
4. **The duration resolution existed only in a comment.** It is now an executable
   contract, `TimestampResolution::OneSecond`, exposed through
   `benchmarkTimestampResolution()`. It is a single value rather than a per-run
   field because it is a property of how the journal is written and is therefore
   identical for every run.
5. **Exclusions carried no journal or run provenance.** `IngestExclusionRecord`
   adds a flat list with `sourceJournalPath`, `runId`, `suiteId` and reason.

### Measured / Derived / Missing as an executable contract

`valueOrigin(run, RunField)` returns Measured, Derived or Missing for each
run-level field. Case- and mode-level fields are Measured by construction because
they are copied straight from their record. `distance`, `resourcePolicy` and
`gpuBackend` are **structurally always Missing** and are never replaced with 0,
false or `"unknown"`. resourcePolicy is not inferred from a preset and gpuBackend
is not inferred from effectiveMode.

### Run identity and provenance are separate

gitCommit is provenance, not identity. `datasetFingerprint + gitCommit` is not
synthesised into a new run id, and the S3 run id is preserved verbatim.
`groupingKey(run)` gathers the comparable fields in one place so a later stage can
choose among them, but **which combination becomes a group key is S6-3's decision**
and is deliberately not fixed here.

### Legacy provenance is never back-filled

A test sweeps the whole dataset and asserts that no run in the
`GitCommitState::Legacy` state carries a gitCommit value, so the current HEAD can
never be written into a legacy journal.

Tests: contract **67 checks** (new), S6-1 ingestion **71 checks** (regression),
journal 66 / store 51 / integration 144 / core 63 / gui_store 110 / worker 35 / ui 34 /
ui_e2e 40 / cli 95 / renderer 100 / orchestrator 32, all PASS. CPU build exit 0 /
CTest **98/98**, GPU build exit 0 / CTest **99/99**.

Real storage re-check: journals 30 / runsFound 31 / accepted 31 / excluded 0 /
provenance legacy 30 and known 1 / determinism IDENTICAL.

Not changed: BenchmarkRunner/Executor/Request, the journal schema, recovery or summary
generation, the Console renderer, the GUI, Scanner/MediaKind, NVDEC and the legacy
benchmark. No journal field was added, no CLI was introduced, and there is no
grouping, aggregation or statistics.
## 2026-10-01 — S6-3 grouping / separation of analysis views

The S6 brief §12 contract was aligned with the implementation. Location: new
`src/benchmark_data_grouping.{h,cpp}` + `tests/benchmark_data_grouping_test.cpp`
(58 checks).

### The important decision — no single composite key

One key combining `fingerprint + mediaScope + buildVersion + gitCommit +
requestedMode + effectiveMode` puts every run in a cohort of its own and makes
**cross-build comparison structurally impossible**. It is the simplest thing to build,
and it deletes the comparison, so it was rejected. Instead there are five typed keys
(`DatasetCohortKey` / `ScopeCohortKey` / `BuildCohortKey` / `ModeCohortKey` /
`CaseCohortKey`), one per analysis axis, and combining axes is the caller's job. The
reasoning is recorded in `docs/worklog/0.9.4.*.md`.

### `caseId` is not run-scoped (measured correction)

S2 uses `caseId = IndexManager::folderId(file.path)` (canonical-path FNV-1a), so
**the same file in two runs yields the same `caseId`**. The brief's run-scoped
assumption was corrected by measurement, and no new hash was invented: the existing
id is paired with the dataset fingerprint.

### Deterministic order and state separation

The canonical mode order `AUTO → CPU → GPU-MAX` differs from the `GpuBackendKind`
declaration order, so an explicit rank is used; it orders display and group results
and does not redefine execution order. Known, Unknown and Legacy cohorts are separated
structurally, and `commitComparable` is true only for Known — a provenance-quality
statement, not a comparability verdict. An empty or missing fingerprint never
becomes a cohort and is only counted. An excluded run enters no cohort at all.

Tests: grouping **58 checks** (new), S6-2 contract 67, S6-1 ingestion 71, journal 66 /
store 51 / integration 144 / core 63, all PASS. CPU build exit 0 / CTest **99/99**,
GPU build exit 0 / CTest **100/100**. `git diff --check` clean.

Real storage (30 journals / 31 runs, read-only): dataset cohorts **2**, scope cohorts
**4** (`all` 22, `images` 5, `videos` 3), build cohorts **2** (Known 1 commit-comparable
/ Legacy 28), mode cohorts **3**, case cohorts **70** (10 recur in more than one run,
up to 26). Determinism IDENTICAL. Insufficient build diversity is recorded as normal
per brief §23; none was fabricated.

Not implemented: aggregation, mean/median/p95, delta, speedup, regression, thresholds,
anomalies, final comparability judgement, distance/resourcePolicy/gpuBackend
inference, CLI, GUI, journal schema changes.

### Capability observation confirmed in the real journals

**60 records have `requested=CUDA` with `effective=CPU`** (`status=SKIPPED`, `mode
unavailable in this environment`). Had requested and effective been merged, 60 CUDA
observations would have been disguised as CPU runs and the capability observation
would have disappeared. Brief §9's rationale is confirmed by real data.
## 2026-10-01 — S6-4 aggregation / separated populations and deterministic statistics

Location: new `src/benchmark_data_aggregation.{h,cpp}` +
`tests/benchmark_data_aggregation_test.cpp` (**97 checks**).

### Three populations, structurally separated

| Level | `MetricLevel` | `MetricResolution` | Eligible when |
| --- | --- | --- | --- |
| mode | `ModeElapsed` | `Recorded` | `Success` and an elapsed exists |
| case | `CaseElapsed` | `Recorded` | `Success` |
| run | `RunWallDuration` | `OneSecond` | a status exists, is `Success`, and a wall exists |

mode `elapsedMs` is optional, so `missingElapsed` is possible there. case `elapsedMs` is
a non-optional `double`, so it **cannot occur** at that level. A run with no terminal
record has no status, so it is excluded as `invalid` rather than assumed Success and is
counted separately as `runsWithoutStatus`. The three are never added together: run wall
duration is not corrected by summing modes or cases.

### `observed = eligible + excluded` everywhere

`SampleAccounting` preserves the identity and keeps the reason for every exclusion. A
bare excluded count would be indistinguishable from a bug. That **all 384 real
exclusions are `skipped`** is only explicable in this form. With zero eligible samples
the statistic **does not exist** (rather than being 0).

### Real storage, 30 journals / 31 runs, read-only

| Population | observed | eligible | excluded | Reason |
| --- | --- | --- | --- | --- |
| mode | 494 | 110 | 384 | skipped 384 |
| case | 324 | 110 | 214 | skipped 214 |
| run | 31 | 31 | 0 | — |

- **Scope**: `all` 22 runs / caseN 60 / case median 619.405 ms; `all` (other
  fingerprint) 1 / caseN 50 / 645.322 ms; `images` 6 / **caseN 0**; `videos` 3 /
  **caseN 0**
- **Build**: Legacy 30 runs (comparable=no), Known `fcace68` 1 run (comparable=yes),
  unknown 0
- **Mode**: `AUTO/CPU` 260 observed **eligible 0, skipped 260**; `CPU/CPU` 110 observed,
  eligible 110, median 645.322; `CUDA/CPU` 60 observed **eligible 0, skipped 60**
- **Statistics**: count/min/max/mean/median/**p95** implemented. Median is the middle
  sample or the mean of the two middle ones; p95 is nearest rank. `n = 1` and `n = 2`
  are computed and **not hidden**, with the sample count alongside. Reported values
  are rounded to 6 decimal places.
- **Determinism**: IDENTICAL in-process and **identical across processes**
- `all` was not decomposed into images/videos and no elapsed was distributed between
  them. The `caseN` of 0 for images and videos means every case in those scope runs was
  Skipped, and that was not filled in by estimation.

### Real defect found — runId is not unique

`suite-ORDER-A/B/C` share one `runId`, so **31 accepted runs carry only 29 distinct
runIds.** Keying per-file aggregation on `runId` alone kept one of the three and silently
dropped the other two runs' cases (case observations rose from **314 to 324** once
fixed). Deduplicating instead would discard measurements in the opposite direction.
The journal cannot say whether these are one measurement written three times or three
separate measurements, so **neither is applied silently**: per-file populations are keyed
on `(sourceJournalPath, runId)` and the collision is only reported through
`runsWithCollidingIdentity`. S6-1's "31 accepted" hid this, so it is recorded in the
worklog.

### Duplicate protection confirmed

Logical mode entities 494 equals the aggregated observed 494. Raw journal line counts
were not used; the logical entities recovered by S6-1/S6-2 were.

Not implemented: regression percentage, thresholds, anomaly judgement, statistical
confidence, winner ranking, resourcePolicy/distance/gpuBackend inference, CLI, GUI,
CSV/Markdown reports, journal schema changes.

### Verification

aggregation **97 checks** (new), S6-3 grouping 58, S6-2 contract 67, S6-1 ingestion 71,
journal 66 / store 51 / integration 144 / core 63, all PASS. CPU build exit 0 / CTest
**100/100**, GPU build exit 0 / CTest **101/101**. `git diff --check` clean, no stale
objects.
## 2026-10-01 — S6-5 comparison eligibility and candidate generation

Location: new `src/benchmark_data_comparison.{h,cpp}` +
`tests/benchmark_data_comparison_test.cpp` (**78 checks**).

### Comparison model

| Item | Value |
| --- | --- |
| candidate type | `ComparisonCandidate` |
| dimension | `BuildProvenance` / `ModeSemantics` (never collapsed) |
| left / right | `ComparisonSide`, a whole aggregate (population-vs-population) |
| run reference | `RunReference { sourceJournalPath, runId }` |

Left and right are fixed by typed key order, so any pair appears once with the same
orientation on every run. No pairwise run matching: it would discard the rest of the
samples and invent a correspondence the data does not contain.

### Eligibility, nine states

`Eligible` / `MismatchedDataset` / `MismatchedScope` / `MismatchedMetric` /
`MismatchedMode` / `MissingProvenance` / `SameProvenance` / `NoComparableSamples` /
`InsufficientData`

Dataset identity is checked first, because "a different dataset" is the more fundamental
reason and a caller should not have to satisfy itself about every other condition before
learning it.

### Real storage, 30 journals / 31 runs, read-only

**0 candidates / 8 opportunities / 8 rejected / accounted=yes**

| Reason | Count |
| --- | --- |
| `missing-provenance` | 2 |
| `no-comparable-samples` | 4 |
| `insufficient-data` | 2 |

**Why zero, with every rejection individually accounted**

1. `images` / build-provenance / mode-elapsed — Known (`fcace68`) vs Legacy, both eligible 0
   → **a Legacy side cannot join commit-level comparison**
2. `images` / build-provenance / run-wall — Known (n=1) vs Legacy (n=5) → **a Legacy side
   cannot join commit-level comparison**
3-4. `all` (two fingerprints) / case-elapsed → **S6-4 keeps no per-build or per-mode
   breakdown of case elapsed, so the comparison is not expressible** (`InsufficientData`)
5-8. `all` / mode-semantics — `AUTO>CPU` (eligible 0) and `CUDA>CPU` (eligible 0) against
   `CPU>CPU` (eligible 50 and 60) → **one side has no samples**

The core fact: **`CPU/CPU` is the only mode cohort with any eligible sample**, and there is
**exactly one Known-commit build**, so no build-vs-build pair exists to compare. This is
precisely what brief §20 anticipated.

**A measured result worth keeping**: the `images` scope already holds per-build run wall
samples of **1 and 5**. Had the Legacy side carried a `gitCommit`, a build-vs-build
candidate would exist today. **Missing provenance is the measured cause of the missing
data**, not missing measurements.

### Run identity

`distinctRunIds=29` / `sharedRunIdRuns=3` / `references=31`. Looking up by `runId` alone
keeps one of the three and loses the others, so everything is keyed on
`(journal, runId)`. A test asserts that no candidate lists the same `(journal, runId)` on
both sides.

### Limitations

**Nine** travel with every candidate: distance, resourcePolicy and gpuBackend unavailable;
controlled environment undefined; process isolation uncontrolled; OS filesystem cache
uncontrolled; repeated runs insufficient; run wall one-second resolution; cancellation
unobserved. A bare `comparable=true` with these dropped would be a claim the data cannot
support.

### Regression **not computed in this stage**

Regression percentage, speedup percentage, confidence interval, p-value, threshold
pass/fail, winner, better/worse and anomaly are all not implemented, and **no threshold was
chosen** — not 5%, not 10%, not 20%.

### Determinism

IDENTICAL in-process and **identical across processes**. Verified that no pair is emitted
twice.

### Verification

comparison **78 checks** (new), S6-4 aggregation 97, S6-3 grouping 58, S6-2 contract 67,
S6-1 ingestion 71, journal 66 / store 51 / integration 144 / core 63, all PASS. CPU build
exit 0 / CTest **101/101**, GPU build exit 0 / CTest **102/102**. `git diff --check` clean,
no stale objects.
## 2026-10-01 — S6-4 completion / CaseElapsed on the build and mode axes

Location: `src/benchmark_data_aggregation.{h,cpp}` +
`src/benchmark_data_comparison.cpp` (the candidate generation path) + two test binaries.
aggregation **129 checks**, comparison **123 checks**.

### Aggregation — the added axes

| Axis | Fields | Note |
| --- | --- | --- |
| Build | `AggregatedBuild.caseStatuses` / `caseAccounting` / `caseElapsed` | all cases of that build |
| Mode | `AggregatedMode.caseStatuses` / `caseAccounting` / `caseElapsed` | only cases where that mode succeeded |

The build axis was nearly free: `placeRun()` already fed every case into the dataset, scope
and build cells and the result was simply **never exposed**. Only the mode axis was new.
**No new measurement was created** — this is a projection of the `case_complete.elapsedMs`
values S6-2 had already normalized.

**The mode axis is a filter, not an allocation.** Case elapsed is by S2 definition the sum of
all of that case's modes and the journal records no split, so dividing it between modes would
mean **inventing a factor that was never measured**. Instead each mode cohort contains only
the cases where that mode semantics actually succeeded. A case where both modes succeeded
enters both, so **the two populations overlap by design and must never be added**. The overlap
is not hidden: each cohort states it in `caseAccounting.observed`.

**Skipped contributes nothing.** A `requested=CUDA / effective=CPU / SKIPPED` case is excluded
on the case axis too, so it never becomes a CPU case performance sample.

### Current actual data, 30 journals / 31 runs

```text
opportunities  8 -> 11
eligible           0        (unchanged)
rejected      8 -> 11
  missing-provenance    2 -> 3
  no-comparable-samples 4 -> 8
  insufficient-data     2 -> 0
```

**That `insufficient-data` count falling from 2 to 0 is the evidence the completion worked.**
The structural impossibility is gone and `missing-provenance` took its place: the axis exists
and **the commit is still unknown**. Every case in the `images` scope is Skipped, so per-build
CaseElapsed there still has 0 samples.

**No gitCommit was backdated onto the Legacy journals to force a candidate.** That would
invent provenance for 30 measurements that the journal never recorded. Nor was a Known commit
synthesised from `buildVersion`, nor was CaseElapsed back-computed from the dataset/scope
aggregates, nor was mode elapsed substituted for case elapsed.

The existing Run and Scope metrics are **all unchanged** (mode 494/110/384, case 324/110/214,
run 31/31/0, wall median `OneSecond`).

### Synthetic known-known

```
Dataset A / images
  Build A commitA: case 100, 120  -> count=2 mean=110 median=110 p95=120
  Build B commitB: case 150, 180  -> count=2 mean=165
```

**S6-5 now produces a real CaseElapsed candidate** — one `CaseElapsed / BuildProvenance`
candidate from the two Known builds, carrying 2 samples on each side. Passing the same pair
directly to `evaluateComparison` also returns `Eligible`. This is a **synthetic fixture**,
not a benchmark result.

### RunId collision preserved

Two runs sharing one runId across different journals, with 2 and 3 cases, give **5** on the
scope axis and **5** on the build axis. No observation is lost on either axis and the cases
stay separate rather than being merged.

### Statistics

Implemented: `count / min / max / mean / median / p95`, 6-decimal rounding, no statistic when
eligible is zero. The existing algorithm is reused and nothing new was introduced.
Not computed: **thresholds, winner, better/worse, regression percentage** — none of
5% / 10% / 20% was chosen. `RunWallDuration` keeps its one-second resolution, no correction by
mode or case sums, and no reclassifying 0 ms as missing.

### Verification

aggregation 129, comparison 123, S6-3 grouping 58, S6-2 contract 67, S6-1 ingestion 71,
journal 66 / store 51 / integration 144 / core 63, all PASS. Real journal read-only exits 0
for both; aggregation and comparison are **identical across processes**. CPU build exit 0 /
CTest **101/101**, GPU build exit 0 / CTest **102/102**. No stale objects.
## 2026-10-01 — S6-6 comparison metrics / numeric calculation and the regression input model

Location: new `src/benchmark_data_comparison_metrics.{h,cpp}` +
`tests/benchmark_data_comparison_metrics_test.cpp` (**77 checks**).

### ComparisonMetrics

```text
absoluteDeltaMs        = right - left                        (the metric's own unit, ms)
relativeDeltaPercent   = ((right - left) / left) * 100
ratio                  = right / left
```

`left` is **not a baseline.** It is a deterministic orientation only, and is not read as
`old`/`new` or `better`/`worse`. `+50%` is reported as **+50%** and is not a regression,
improvement, better, worse or winner.

### Synthetic known-known

```
Build A: case 100, 120  -> mean 110
Build B: case 150, 180  -> mean 165
```

Verified as **absoluteDelta = +55 ms, relativeDelta = +50%, ratio = 1.5**. The report does
not call this a 50% regression.

This fixture is **synthetic** and is not a benchmark result. It produces 3 candidates, one
per metric: ModeElapsed, CaseElapsed and RunWallDuration.

### Zero behaviour

| left | Result |
| --- | --- |
| 0 | absoluteDelta **present**, relative/ratio **unavailable** |
| 0 vs 0 | absoluteDelta = 0 present, relative/ratio unavailable (0/0 has no answer either) |

Nothing was replaced with 0, and no NaN or infinity string entered the output contract. The
`leftValueIsZero` flag and a `ZeroLeftReference` reason distinguish "absent because a value
was missing" from "absent because the reference was a measured zero". A `RunWallDuration` of
0 ms means the two timestamps fell in the same second; it was not reclassified as missing.

### Statistics

`Min / Max / Mean / Median / P95` are each compared independently. One statistic being
unavailable never removes another. Verified that p95 is not the mean repeated: left values
110 and 120 give a p95 of 120 and a delta of +60.

### Resolution

| metric | resolution |
| --- | --- |
| ModeElapsed | `Recorded` |
| CaseElapsed | `Recorded` |
| RunWallDuration | `OneSecond` |

`OneSecond` is preserved through a RunWallDuration comparison. A mismatched metric level or
resolution is **re-checked defensively** and produces no numbers at all.

### Provenance

Legacy and Unknown are never converted to Known. Such a candidate is never generated, so
**there are no metrics for it either**, which a test confirms.

### Limitations

The nine limitations from S6-5 are passed through unchanged. No second limitation system was
created and the count is pinned at nine.

### Current actual data, 30 journals / 31 runs, read-only

```text
candidates = 0
comparisonMetrics = 0
absolute deltas available = 0
rejections: missing-provenance=3, no-comparable-samples=8
determinism: IDENTICAL
```

Per brief §21, **0 candidates and therefore 0 metrics is the correct result** and not a code
failure. The real store has no eligible candidate, so there is nothing to compute.

### Regression was not judged

Thresholds, regression classification, winners, better/worse, confidence intervals,
p-values, statistical significance, anomalies, controlled-environment scores, CLI, GUI,
report formatters, journal schema changes and S2/S3 changes are **all not implemented**.
None of 5% / 10% / 20% was chosen.

### Verification

metrics **77 checks** (new), S6-5 comparison 123, S6-4 aggregation 129, S6-3 grouping 58,
S6-2 contract 67, S6-1 ingestion 71, journal 66 / store 51 / integration 144 / core 63, all
PASS. Real journal read-only exits 0 and is **identical across processes**. CPU build exit 0
/ CTest **102/102**, GPU build exit 0 / CTest **103/103**. No stale objects.

### A real bug found while implementing

The first `resolveSide()` refused the `BuildProvenance + ModeElapsed` combination as "not
expressible", but S6-5 generates that candidate per mode semantics, so it **carries a mode
key**. The result was that the synthetic fixture produced metrics whose every value was
absent. The nesting level is now decided by **what is addressed** rather than by the
dimension alone.
## 2026-10-01 — S6 measurement gate / controlled A/B measurement procedure

**This stage is documentation, not code.** No product code changed.
Added `docs/implementation-briefs/S6-measurement-gate.ko.md` + `.en.md`. Updated the
`STRUCTURE.md` / `llms.txt` indexes. **S6 is not CLOSED** and the roadmap was not changed.

### Started from variation that was already measured

No new baseline was invented; the measured 0.9.4.32 figures were reused.

| Target | Measured spread |
| --- | --- |
| Full execution | **3.7 – 7.5 %** |
| Short probe | **19.4 – 45.9 %** |

> **Any difference below 7.5 % sits inside measurement error** and cannot be declared an
> improvement or a regression. Short probes do not become usable by adding repeats alone —
> **the measurement unit itself must not be short.**

### Three facts fixed by investigation

**1. The current 31-run store is not a real dataset.** The case paths resolve to
`...\Temp\msf_s5_e2e`, an S5 end-to-end fixture of **10 files**. `caseMedian 619–645 ms` and
`run wall 0 ms` are **properties of a 10-file fixture** and cannot be performance evidence.

**2. The GPU exists — CUDA being unavailable is a build configuration issue.** The machine
**has an RTX 3080 Ti**, yet 60 `CUDA → CPU / SKIPPED` records exist. The cause is that the
store sits under `build-windows-cpu\Release` and that binary does not link CUDA. The earlier
"fallback on a machine without GPU support" reading is corrected, while the observed values
(60 records / SKIPPED) are kept exactly as they were.

**3. The dataset has zero video files.** The `videos` scope is impossible for this Gate, and
`all` points at the same file set as `images`. The real dataset
`C:\project\test_sample_img_vid` is **3347 files / 102,475,315 bytes / 102 directories**.
One `.md` under `format` is not media, so it must be confirmed before the Gate; removing it
changes the fingerprint and both sides must then be re-measured.

### The procedure that was fixed

Scope `images` / `--mode cpu` stated explicitly / primary metric `ModeElapsed` /
`CaseElapsed` secondary / **`RunWallDuration` excluded from primary for its one-second
resolution** / crossed order `A B B A B B A` / warm cache adopted as the standard without
claiming OS cache control / 5 repeats per build (AGENTS.md item 9).

`runId` is `run-YYYYMMDD-HHMM-SS`, **one-second resolution**, so each repeat uses its own
suite, passes `--suite` explicitly, and starts at least 2 seconds apart.
`RunReference = (sourceJournalPath, runId)` is kept.

There is no `--resource` / `--cpu-percent` on the CLI and no `resourcePolicy` in the
journal. The Gate runs on the same machine, OS session and power scheme, and the resource
policy is only stated in the document.

### Gate entry conditions

Not discovering a regression, but **generating data with 2 Known builds + identical
dataset/scope/mode + real SUCCESS samples + 5 repeats per build + a computed spread +
recorded crossed order and environment + S6 candidate > 0.** Verified mechanically with S6's
four read-only diagnostics (`known>=2`, `eligibleCandidates>0`,
`absolute deltas available>0`, `missing-provenance=0`, `no-comparable-samples=0`).
**The threshold is still undefined** and a separate decision stage remains.

### Scope

No code change. S2 execution, the S3 journal schema, all S6 layers, Scanner, GUI, renderer
and CLI were not touched. Benchmark execution automation, a measurement engine,
threshold/regression/anomaly code and a report formatter were not implemented.

---

## 2026-10-04 — 0.9.4.45 XMP review correction + color_thumb audit / pre-register

Baseline: `0.9.4.45` / `97db24f` (implementation) + `392a4c2` (this review correction) - CPU CTest 102/102 - GPU CTest 103/103
Detail: the last entry of `docs/worklog/0.9.4.en.md`

### Current XMP status (verdict corrected)

| Axis | Status |
|---|---|
| code implementation (1..8 + precedence + invalid fallback) | PASS |
| fixture (mapping 2/4/5/7 included, 38 checks) | PASS |
| real-dataset coverage | NOT_AVAILABLE (standard dataset has 0 XMP files, reported only) |
| full Search/Scan regression | DEFERRED (depends on S4/S5) |
| **production acceptance** | **CONDITIONAL** |

Implementation correctness is proven by fixtures, but without full scan regression
and real-dataset coverage the acceptance is CONDITIONAL. build-history 0.9.4.45 is
not rewritten retroactively.

### Live candidate (newly pre-registered)

`docs/implementation-briefs/I-color-thumb-no-ffmpeg-classification.ko.md` / `.en.md`
- audit only, no production correction.

Core conclusion: **classification is a single extension rule and is identical across
all three FFmpeg states.** The absence of decoder capability must not change the media
type; that is the contract candidate.

Confirmed risks:

- **R1 (high)** `color_thumb_test` always fails in a no-FFmpeg build (unconditional
  CMake registration + no skip handling + `frameAtColor` unconditionally `false`).
  This classifies the previously unclassified `exit 5` from worklog run 082.
- **R2 (medium)** `kindOf()` (extension) and DB `x.kind` are dual sources of truth,
  with no cross-check.
- **R3 (medium)** the extension list is duplicated four times (`scanner`, three places
  in `monitor`, GUI).
- **R4 (medium)** the shell thumbnail overwrites the engine color thumbnail without an
  `isNull()` guard, polluting `thumbStatEngine_`.
- **R5/R6 (low)** overstated configure message / magic-number `MediaKind` mapping.

Next step is the R1 fixture plus skip/pass handling. R2..R6 stay separate decisions.

### Boundaries

```text
color_thumb production correction = NOT PERFORMED
color_thumb fixture               = NOT PERFORMED
S4 final GUI visual/save acceptance = DEFERRED
S5 product benchmark               = DEFERRED
S6                                = DEFERRED
NVDEC production adoption          = DEFERRED
```

---

## 2026-10-04 — 0.9.4.45 CPU/GPU reproduction verification + progress baseline correction

Baseline: `0.9.4.45` / `619f74a` (product fix) - CPU CTest 102/102 - GPU CTest 103/103
Detail: the last entry of `docs/worklog/0.9.4.en.md`, `docs/build-history/0.9.4.45.en.md`

### Two product fixes (`619f74a`) that progress had never recorded

#### 1. `--version` console output predicate misdetection

Reported by the user: `MediaSimilarityFinder.exe --version` printed nothing from
PowerShell while CMD worked.

The cause was `streamIsRedirected()` in `gui/main.cpp::attachParentConsole()`.
Because the executable is GUI subsystem (`WIN32_EXECUTABLE TRUE`), a null
`GetConsoleWindow()` leads to `AttachConsole(ATTACH_PARENT_PROCESS)`, and that path
skips the `CONOUT$` reopen when it believes the streams are redirected. The
predicate **mistook "the stream cannot be written to" for "the stream is
redirected".**

| Condition | Previous verdict | What it actually meant |
|---|---|---|
| `fd < 0` | `true` (wrong) | no descriptor = unusable |
| `_get_osfhandle()` is `-1` or `0` | `true` (wrong) | no usable OS handle = unusable |
| `GetFileType()` is `FILE_TYPE_UNKNOWN` | fell through the `FILE_TYPE_DISK`/`FILE_TYPE_PIPE` test, so `false` | invalid handle = unusable |

So **two** cases were wrongly `true`; `FILE_TYPE_UNKNOWN` already resolved to
`false`, but only implicitly. The fix makes **all three explicitly `false`**, which
keeps the original design intent of preserving real redirection and removes only
the misdetection.

VERIFIED: `--version`, `--help`, an invalid option (stderr, EXIT=2), `--smoke`,
`cmd /c`, OS-level separated stdout/stderr redirection, and `Start-Process -Wait`
all correct. OS redirection records 45 bytes.

#### 2. CUDA host compiler encoding warning C4819

The `warning C4819` in the GPU build log was not a CUDA syntax or link error but an
encoding warning: code page 949 cannot represent non-ASCII characters in the CUDA
headers (`driver_types.h`, `cuda_runtime_api.h`). The cause was `/utf-8` being
applied to `CXX` only, so it never reached the CUDA host compiler.

`nvcc` does not accept `/utf-8` directly, so it is forwarded to the MSVC host
compiler through `-Xcompiler`. The first attempt, `COMPILE_LANG_AND_ID:CUDA,MSVC`,
**failed to match because CUDA reports compiler id `NVIDIA`**. The final form is
`$<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CXX_COMPILER_ID:MSVC>>:-Xcompiler=/utf-8>`.

VERIFIED: `-Xcompiler="/EHsc -Ob2 /utf-8"` present on the real nvcc command line;
forced recompile of `cuda_backend.cu` reported C4819=0 / warning=0 / error=0 and
produced `msf_cuda.lib`. CUDA architectures (`compute_75/86/89`) and runtime are
unchanged.

### Reproduction verification result (VERIFIED)

| Item | Verdict |
|---|---|
| CPU CTest (`build-windows-cpu`, Release) | VERIFIED 102/102 |
| GPU CTest (`build-windows-gpu`, Release) | VERIFIED 103/103 |
| CUDA C4819 | VERIFIED 0 (real nvcc recompile confirmed) |
| `/utf-8` forwarded via `-Xcompiler` | VERIFIED (real command line) |
| Four CLI cases + OS redirection | VERIFIED |
| PowerShell `>` 0-byte | REPORTED — not a regression |

The first GPU build did not invoke nvcc at all, because the CUDA object was
up-to-date (0 invocations), and **it was not recorded as PASS on that basis.** Only
the mtime of `src/cuda_backend.cu` was changed to force a recompile; its content was
left untouched (`hash-object` identical to HEAD).

The PowerShell `>` 0-byte case reproduces identically on the pre-fix binary and
leaves `$LASTEXITCODE` empty, because PowerShell does not wait for GUI-subsystem
executables. Console output verification therefore uses **OS-level redirection** as
the criterion from here on.

### Progress baseline correction (why this document changed)

During the review, the "Current Status" header of `development-progress.{ko,en}.md`
still pointed at `0.9.4.44 (6ada90f)` while the lower part of the same document
already documented `0.9.4.45` work — a **self-contradiction**. `AGENTS.md` clause 7
names this document as the source of truth for the current state, so the header was
brought in line with reality.

- Reference code `0.9.4.44 (6ada90f)` → `0.9.4.45 (619f74a)`
- Current version `0.9.4.44` → `0.9.4.45`
- Current node now carries XMP CONDITIONAL, the `color_thumb` pre-register, and the
  state of both fixes
- The XMP entry baseline now names its own commit `392a4c2`
- `docs/llms.txt` current-baseline references updated from `0.9.4.44` / `6ada90f`

### Boundaries

```text
product code change                  none (this commit is docs-only)
version bump                         none (stays 0.9.4.45)
engine/DB/schema/cache version       unchanged (1.5.0 / 1.0.3 / 9 / 9)
XMP semantics change                 none
color_thumb R1 implementation        none (audit only)
Search/Index/Comparison change       none
vcpkg migration                      none
dedicated console-output test        not added (recorded as a candidate)
S4 / S5 / S6 / NVDEC                kept DEFERRED
```

## 2026-10-07 — 0.9.4.61 direct GUI feedback and 0.9.4.62 work order

Direct Windows GUI verification shows that the similar-group list now scrolls normally in current testing, including scrollbar-based movement. The core 0.9.4.61 scroll regression is therefore recorded as **effectively PASS at product-use level**. Video-list equivalence is deferred because the video comparison algorithm is not yet complete.

### Immediate scope
- Add a persistent Settings option to **show/hide the Detailed Logs control**. Keep the GUI Detailed Logs semantic unchanged; this is only a user-facing visibility preference.
- Adjust the main horizontal splitter so the right group-detail pane stops growing once it has roughly the width needed for a four-image preview, and additional window width primarily expands the center similar-group pane.
- Make the filename value in the Details form text-selectable and double-click selectable in the same way as the full path value.

### Later scope
- Record a developer-oriented **Test Mode** that revives the old GUI measurement intent without reviving the old GUI Benchmark product surface: assume the index is absent/stale, execute the same processing path, and collect timing/telemetry without modifying the actual index. Keep CLI Benchmark and GUI Test Mode semantically separate.
- The current `thumbCatchUpVisible()` performs actual thumbnail decode/update only for visible items, but it still scans the full QListWidget to find those visible items on each tick. This is a contained performance risk, not the current scroll blocker; investigate only after the immediate GUI cleanup and do not add a risky workaround without measurement.

### Post-1.0 retained backlog
- Burst-shot similarity refinement stays DEFERRED until after 1.0; the planned Settings option plus dual-algorithm selectable search remains in `docs/architecture/image-burst-shot-similarity.en.md`.
- The low image-GPU duty observation is not a correctness blocker. CPU/WIC decode, crop, I/O, and GPU-hash overlap remains post-1.0 throughput work.
