# Development Progress — 0.9.4 Development Line

## Purpose

This document records the **actual execution state** of development-roadmap.en.md.

The Roadmap is the structural direction. Progress records the actual position, problems, and recovery branches.

## Current Status

| Item | Status |
| --- | --- |
| Reference code | 0.9.4.22 |
| Official preserved baseline | 0.9.2.32 |
| Development line | 0.9.4 |
| Current node | D — measured and closed on evidence → **D9 (Analyze / Matching) opened** |
| Current phase | **D9a implementation pending.** D8b showed `analyze` is 98.62 % of engine wall with no internal decomposition, so D9a adds per-stage measurement first; no optimization is attempted while unmeasured |
| Current version | 0.9.4.22 |
| GPU implementation baseline | NVIDIA CUDA |
| CPU fallback | retained |
| Project-local vcpkg | retained; no migration |

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

### C — Calibration / INI Performance Profile (C1 done, C2 pending)

- The C brief is now concretized into C1–C4 stages.
- C1 covers Profile Foundation only; actual calibration execution is excluded.
- C2 covers short initial calibration and Profile → Scheduler initial-estimate integration.
- C3 covers repeated runtime deviation, opportunistic recalibration, and confidence updates.
- C4 validates the full Profile lifecycle, live precedence, CPU/GPU parity, failure/partial state, and persistence.
- Portable Profile location is defined as Index/PerformanceProfile.ini.
- Profile is an initial estimate; live runtime state always has precedence.
- D queue/worker topology and F hardware decode are not pulled into C.
- Metrics not yet measurable are recorded with explicit measurement states.

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
