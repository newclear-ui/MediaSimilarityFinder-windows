# Development Progress — 0.9.4 Development Line

## Purpose

This document records the **actual execution state** of development-roadmap.en.md.

The Roadmap is the structural direction. Progress records the actual position, problems, and recovery branches.

## Current Status

| Item | Status |
| --- | --- |
| Reference code | 0.9.3.19 |
| Official preserved baseline | 0.9.2.32 |
| Development line | 0.9.4 |
| Current node | A — Foundation / Terminology / Instrumentation |
| Current phase | Design/document baseline complete → source implementation entry |
| Current version | 0.9.3.19 |
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

The current documentation work establishes the A baseline; it does not mean the 0.9.3.19 source has already been fully migrated to the 0.9.4 architecture.

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

### Node A exit criteria

- CPU-only works
- GPU OFF works
- GPU ON can reach the existing CUDA path through backend abstraction
- manual GPU utilization UI is removed
- unmeasured=0 ambiguity is removed
- benchmark instrumentation does not change search results
- baseline regression tests pass
- build-tree / CMake naming is aligned
- code/docs/tests report matching version state

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
