# Development Progress — 0.9.4 Development Line

## Purpose

This document records the **actual execution state** of development-roadmap.en.md.

The Roadmap is the structural direction. Progress records the actual position, problems, and recovery branches.

## Current Status

| Item | Status |
| --- | --- |
| Reference code | 0.9.4.37 |
| Official preserved baseline | 0.9.2.32 |
| Development line | 0.9.4 |
| Current node | **I — COMPLETE** → proceeding to **E** |
| Current phase | Node I **closed**. D9a → D9b (NOT ACCEPTED) → D9c/D9d → D1 → D2 (Path C `DEFERRED`) → D3 → I-2 (measured 0.9.4.33, verified 0.9.4.34) → EXIF defect correction (0.9.4.35) → **I-2 production integration (0.9.4.36, `PRODUCTION ADOPTION = YES`)** → **I-3 production full-scan end-to-end validation (0.9.4.37, `PASS`)**. I-1 remains `DEFERRED`. **Next node: E — Adaptive Video Decode Planner** |
| Current version | 0.9.4.37 |
| GPU implementation baseline | NVIDIA CUDA |
| CPU fallback | retained |
| Project-local vcpkg | retained; no migration |

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
