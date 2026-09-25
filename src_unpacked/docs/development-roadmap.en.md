# Development Roadmap — 0.9.4 Development Line

## Purpose

This document defines the high-level development direction and dependency order for the MediaSimilarityFinder 0.9.4 development line.

The key rule is to **separate development stages from build numbers**.

- A, B, C, ... are development nodes.
- B1, B2, B3, ... are recovery and verification substeps inside a node.
- Build numbers are never pre-assigned to roadmap nodes.
- A version advances when a reproducible, validated code state is established.
- One node may therefore produce several versions such as 0.9.4.0 → 0.9.4.1 → 0.9.4.2 → ...
- Failed experiments or one-off debug states do not need to become meaningful release versions.
- Actual location and per-version evidence are tracked by development-progress.* and build-history/*.

This is the development-direction anchor. Implementation must read it together with the current Progress document before selecting the next task.

## Development Flow

text flow:
START
  |
  v
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
  +--> NVIDIA NVDEC
  |
  v
[G] Additional GPU Backends
  |
  +--> Vulkan
  +--> AMD HIP/ROCm
  +--> Intel Level Zero
  |
  v
[H] Regression / Stability / Performance Validation
  |
  v
NEXT DEVELOPMENT LINE

Each node must pass its exit criteria before the next node becomes active.

### Recovery branches

Example when a scheduler problem appears in B:

[B]
 |
 +--> [B1] Reproduce / Diagnose
 |       |
 |       v
 |     [B2] Fix / Refine
 |       |
 |       v
 |     [B3] Regression / Verification
 |       |
 |       +---- fail ----> [B1]
 |       |
 |       +---- pass ----> [B]
 |
 v
[C]

Common pattern:
A -> A1 -> A2 -> A3 -> A
B -> B1 -> B2 -> B3 -> B
C -> C1 -> C2 -> C3 -> C

A problem does not automatically redesign the whole direction. First reproduce, diagnose, fix, and regression-test inside the current node, then return to that node's gate.

If evidence requires a structural direction change, update Roadmap and Progress together and record why.

## Node A — Foundation / Terminology / Instrumentation

Goal:
- Make GPU terminology vendor-neutral at higher layers.
- Expose GPU ON/OFF only and remove manual GPU utilization control.
- Preserve CPU Resource Mode semantics.
- Establish MSF_ENABLE_GPU / MSF_GPU_BACKEND structure.
- Establish build-windows-cpu / build-windows-gpu.
- Keep current CUDA as a concrete backend.
- Preserve CPU fallback.
- Promote Benchmark / Telemetry to first-class instrumentation.

Core telemetry:
- measured / not_measured / not_available / partial / failed / fallback
- separate decodedFrames and sampledFrames
- scheduler / queue / transfer / backend / decoder observations
- cancellation / partial state

Exit criteria:
- Generic GPU terminology and concrete backend names are separated.
- GPU ON/OFF policy is explicit.
- Benchmark cannot misrepresent unmeasured values as zero.
- CPU fallback remains intact.
- Basic CPU/GPU regression tests pass.
- Vendor-specific APIs do not spread through the high-level search engine.

## Node B — Adaptive Scheduler

Goal:
- Dynamic allocation based on effective capacity rather than fixed 50:50 sharing.

Inputs:
- CPU/GPU baseline capacity
- decoder/backend capability
- live CPU/GPU load
- memory/VRAM pressure
- CPU/GPU queue depth
- recent throughput
- CPU↔GPU transfer cost
- workload-specific cost
- external load

Required:
- GPU utilization itself is not the optimization target.
- Inefficient GPU configurations may converge toward CPU-centric or CPU-only work.
- Gaming is conservative adaptive policy.
- Manual constrains CPU only; GPU remains AUTO.
- moving average / hysteresis / minimum hold time
- Record scheduler decisions in benchmark.

Exit criteria:
The scheduler must control work without repeated oscillation on real workloads and CPU-only / GPU OFF / GPU ON-AUTO runs must be comparable.

## Node C — Calibration / INI Performance Profile

Goal:
- Separate relatively stable baseline from current-run measurements.

Measure:
- CPU fingerprint throughput
- GPU fingerprint throughput
- resize / conversion
- CPU decode / hardware-decode feasibility
- CPU↔GPU transfer
- queue latency

Store profile id / version / confidence / timestamp in INI.

Exit criteria:
The INI profile is used only as the next-run initial estimate, never above live runtime state, and can be revalidated after driver/backend/algorithm changes.

## Node D — Pipeline / Queue Optimization

Goal:
Increase CPU/GPU overlap and reduce unnecessary barriers and worker starvation.

Basic direction:
CPU decode / analysis -> GPU hashing / verification -> CPU result / DB

Where appropriate:
Hardware Decode -> GPU resize / hash -> CPU result / DB

Do not solve the problem only by increasing worker count. Measure queue depth, wait time, batching, transfer cost, and stage overlap.

Exit criteria:
End-to-end throughput improves and queue wait / barrier / starvation become observable.

## Node E — Adaptive Video Decode Planner

Goal:
Reduce workload-specific cost from decoding more frames than necessary.

Keep planner and backend separate:
- planner: sequential / hybrid / sparse seek
- backend: Software FFmpeg / hardware decoder

Required telemetry:
- requested sample frames
- sampled frames
- decoded frames
- seek count / latency
- decode throughput
- keyframe/GOP cost
- conversion / resize
- fallback

Exit criteria:
The benchmark clearly exposes the sampled-vs-decoded gap and planner choices can be validated against real throughput.

## Node F — Hardware Video Decode Backend

Connect NVIDIA NVDEC as the first real hardware-decoder backend candidate.

Rules:
- Software FFmpeg is the reference/fallback.
- Inspect codec/profile/pixel-format/bit-depth/capability per file.
- Fall back on initialization / seek / frame-mapping / decode failure.
- Do not spread low-level vendor APIs through the high-level engine.
- Record backend selection and fallback reason in benchmark.

If NVDEC is not beneficial for a file, CPU decode or another path must remain selectable.

## Node G — Additional GPU Backends

Review independently after the GPU abstraction is stable:
- Vulkan
- AMD HIP/ROCm
- Intel Level Zero

Do not claim support completion without real hardware validation.

## Node H — Regression / Stability / Performance Validation

Validate:
- CPU-only
- GPU OFF
- GPU ON / AUTO
- low-end GPU simulation
- acceleration not beneficial
- high-end GPU
- external CPU load
- external GPU load
- hardware decode success
- hardware decode fallback
- mixed image/video
- cancellation / partial scan
- accuracy parity
- system stability
- cache compatibility
- existing CUDA behavior

Acceptance is based on correctness + end-to-end throughput + fallback correctness + stability + observability, not a single GPU-utilization number.

## Version number policy

Version numbers are not roadmap node numbers.

Roadmap: A -> B -> C -> D -> E -> F -> G -> H
Version: 0.9.4.0 -> 0.9.4.1 -> 0.9.4.2 -> 0.9.4.3 -> ...

Example:
0.9.4.0
  B
  |
  +-- B1 scheduler oscillation discovered
  +-- B2 hysteresis fixed
  +-- B3 regression
  |
  +--> 0.9.4.1
       B gate passed
       |
       v
       C

This is only an example. Several versions may occur inside one node, and one version may contain several documentation/code tasks.

The version represents the result; the roadmap node represents the direction.

## Document roles

| Document | Role |
| --- | --- |
| docs/development-roadmap.ko.md / .en.md | Overall development direction, flow, nodes, recovery rules |
| docs/development-progress.ko.md / .en.md | Actual node, status, blockers, and B1/B2/B3 recovery history |
| docs/build-history/<version>.ko.md / .en.md | Actual code changes, reasons, fixes, and validation evidence |
| docs/architecture/*.ko.md / .en.md | Detailed technical design |
| AGENTS.md | OpenCode/developer work rules |

OpenCode reads Roadmap and Progress first, determines the current node and next gate, and then uses the node-specific detailed prompt.

## Change management

Do not skip roadmap nodes arbitrarily.

A roadmap change is justified when there is a structural blocker, hardware/codec/backend evidence invalidates an assumption, correctness or CPU fallback is threatened, or end-to-end throughput is harmed.

When that happens:
1. Record the problem in Progress.
2. State the original path and root cause.
3. Update the Roadmap.
4. Record the reason in both KO and EN.
5. Continue along the new path.

This document is not a complete implementation prompt; it is the anchor that prevents loss of development direction.
