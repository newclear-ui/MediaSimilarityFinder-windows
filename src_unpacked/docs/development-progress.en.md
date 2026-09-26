# Development Progress — 0.9.4 Development Line

## Purpose

This document records the **actual execution state** of development-roadmap.en.md.

The Roadmap is the structural direction. Progress records the actual position, problems, and recovery branches.

## Current Status

| Item | Status |
| --- | --- |
| Reference code | 0.9.4.1 |
| Official preserved baseline | 0.9.2.32 |
| Development line | 0.9.4 |
| Current node | A — Foundation / Terminology / Instrumentation (exit gate PASS → next gate B) |
| Current phase | Node A implementation and validation complete → B entry ready |
| Current version | 0.9.4.1 |
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

## Node B/C/D detailed-design state

Node B and D are **new design work**. Node C is **partial reuse and extension of the existing profile/benchmark concepts**.

Detailed implementation contracts live under `docs/implementation-briefs/`. The Roadmap keeps direction and boundaries; implementation stages are not duplicated there.

Current B/C/D briefs:
- `docs/implementation-briefs/B-adaptive-scheduler.ko.md / .en.md`
- `docs/implementation-briefs/C-calibration-profile.ko.md / .en.md`
- `docs/implementation-briefs/D-pipeline-queue.ko.md / .en.md`

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
