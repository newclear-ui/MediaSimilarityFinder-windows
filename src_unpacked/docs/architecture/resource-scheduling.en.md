# CPU/GPU Adaptive Resource Scheduling

## Document status

- Status: **Approved target architecture**
- Reference code line: 0.9.3.19
- Official preserved baseline: 0.9.2.32
- This document defines the development direction rather than claiming that every item is already implemented.
- The 0.9.3.19 implementation still contains fixed CPU/GPU percentage policies and GPU percentage UI. Those parts are to be migrated incrementally to this target architecture.

## 1. Core principles

MediaSimilarityFinder is designed as a CPU+GPU media-search application.

The goal is not to split work into a fixed CPU/GPU ratio on every machine.

The goals are:

1. Keep user-facing CPU resource policies for system stability.
2. Let the user control GPU only as ON/OFF; never require a manual GPU utilization percentage.
3. When GPU is ON, let an Adaptive GPU Scheduler decide GPU workload from hardware capability, current load, measured throughput, and queue pressure.
4. Do not assume a fixed 50:50 CPU/GPU split. Allocate work according to effective measured capacity.
5. React to CPU/GPU load caused by other applications.
6. Allow low-end or inefficient GPUs to converge to CPU-heavy or CPU-only execution.
7. Keep CPU execution as the correctness and recovery path whenever GPU acceleration is unavailable or fails.

## 2. User modes

The five Resource Modes remain:

| Mode | CPU policy | GPU policy |
|---|---|---|
| **Maximum** | Most aggressive CPU use | AUTO when ON |
| **High** | High CPU use | AUTO when ON |
| **Balanced** | Balanced CPU use | AUTO when ON |
| **Gaming** | Conservative CPU use for coexistence with games/heavy foreground work | AUTO, with stronger external-load response |
| **Manual** | User-defined CPU resource limit | AUTO when ON |

The existing CPU preset targets remain:

- Maximum: 90%
- High: 75%
- Balanced: 55%
- Gaming: 25%
- Manual: user-selected CPU limit

These percentages describe a CPU resource policy/target, not a literal rule that the total workload must be split at that ratio.

### GPU setting

No manual GPU-utilization slider is part of the target UI.

- GPU ON: Adaptive GPU Scheduler controls workload automatically.
- GPU OFF: use the CPU path without GPU acceleration.

Therefore manual GPU settings such as 25% / 50% / 75% / 100% are intentionally removed from the final design.

## 3. Adaptive Scheduler

The scheduler does not force a static CPU/GPU ratio.

It estimates **effective capacity** from:

- CPU baseline performance profile
- GPU baseline performance profile
- decoder/backend capability
- current CPU load
- current GPU load
- memory/VRAM availability
- CPU queue state
- GPU queue state
- recent measured throughput
- CPU↔GPU transfer cost
- workload-specific cost

If CPU throughput is 100 units/s and GPU throughput is 500 units/s, the scheduler should not use a 50:50 split. It should send more work to the GPU.

If a low-end iGPU is slower than CPU after transfer overhead is included, the scheduler may converge to CPU-heavy or effectively GPU-zero execution.

## 4. First-run calibration and performance profile

At scan start the application inspects hardware capability.

### Capability detection

- CPU model/threads/SIMD
- GPU vendor/model/generation/VRAM
- CUDA and other available GPU backends
- available hardware video decoders
- codec/profile/pixel-format capability

### Lightweight calibration

Do not force a long standalone benchmark. Use short probes and early real scan work where practical to measure:

- CPU fingerprint throughput
- GPU fingerprint throughput
- CPU/GPU resize and conversion cost
- CPU decode throughput
- GPU video decode throughput
- CPU↔GPU transfer cost
- queue wait time

The scheduler can begin from these estimates and refine them from actual work.

## 5. INI performance profile

Relatively stable hardware characteristics are stored in INI and reused as the starting point for the next scan.

The profile may include:

- CPU identity and baseline performance
- GPU identity and baseline performance
- backend/decoder throughput
- application/FFmpeg/driver profile version
- last calibration time
- profile confidence

The INI profile is an **initial estimate**. It never overrides current runtime load.

The profile should be revalidated or recalibrated after changes such as:

- CPU/GPU replacement
- GPU driver change
- FFmpeg/backend change
- major algorithm change
- scheduler/backend architecture change

## 6. Real-time load adaptation

The stored baseline is separate from live runtime state.

Conceptually:

Baseline capacity + Current resource availability + Queue pressure + Measured throughput

is used to estimate effective capacity.

When another application consumes CPU:

MediaSimilarityFinder CPU workers ↓

When another application consumes GPU:

MediaSimilarityFinder GPU workload ↓

When the system becomes idle, workload can increase gradually within the selected policy.

The scheduler should use moving averages, hysteresis, and minimum hold times so that worker/workload allocation does not oscillate on every short utilization fluctuation.

## 7. Gaming mode

Gaming mode is not simply a lower fixed CPU percentage.

It assumes that games or other heavy foreground workloads may be active:

- keep CPU work conservative
- start GPU work conservatively under AUTO
- reduce MediaSimilarityFinder load when external CPU/GPU pressure increases
- recover gradually when external pressure decreases

## 8. Manual mode

Manual mode exposes direct CPU resource control only.

Example:

CPU limit = 40%, GPU = ON

means:

- CPU: respect the user-defined policy
- GPU: still managed automatically

Manual mode does not disable the Adaptive Scheduler. The user's CPU limit is a scheduler constraint; worker counts and workload allocation can still change dynamically inside that constraint.

## 9. CPU/GPU parallelism goal

Using CPU and GPU together is about **pipeline parallelism**, not a 50:50 split.

Where useful, stages should overlap, for example:

CPU decode/analysis → GPU hashing/verification → CPU result/DB

or:

NVDEC → GPU resize/hash → CPU result

High-end systems can place more work on the GPU; low-end systems can place most work on the CPU.

Therefore maximizing GPU utilization is not itself an optimization goal.

The optimization target is **end-to-end scan throughput plus system stability**.

## 10. Relationship to video decode / NVDEC

Hardware video decode belongs behind a separate VideoDecodeBackend abstraction.

Target structure:

- Software FFmpeg decoder: reference/fallback path
- NVIDIA NVDEC: optional acceleration backend
- Future hardware backends: same abstraction

Per-file codec/profile/pixel-format/driver/backend capability is checked before attempting hardware decode.

The first hardware-decode implementation should reuse the existing FFmpeg abstraction as much as practical. Low-level NVDEC APIs should not spread through the core search engine.

If hardware decode initialization, seek, frame mapping, or decode fails, the affected file/work can safely fall back to Software FFmpeg.

## 11. Correctness principles

CPU and GPU are not separate algorithms with intentionally different answers.

Where possible, the CPU/reference path is the correctness oracle and the GPU path accelerates equivalent computation.

Validation should include at least:

- fingerprint parity
- timestamp/sample alignment
- similarity-verdict parity
- CPU fallback after hardware decode failure
- regression coverage across codec/profile/pixel-format combinations

Higher GPU utilization or throughput must not change search decisions.

## 12. Development flow and detailed design

This document defines the detailed scheduling design. The implementation order follows the top-level flow in `docs/development-roadmap.en.md`.

A Foundation / Instrumentation
        |
        v
B Adaptive Scheduler
        |
        v
C Calibration / INI Profile
        |
        v
D Pipeline / Queue
        |
        v
E Adaptive Video Decode
        |
        v
F Hardware Decode Backend
        |
        v
G Additional GPU Backends
        |
        v
H Regression / Validation

When a problem occurs, use A1/B1/C1-style recovery branches inside the current node. Diagnose, fix, regression-test, then return to the same node gate.

Benchmark / Telemetry evolves with every node. Detailed schema and measurement states are defined by the benchmark-telemetry-roadmap document.

## 13. Simplifications explicitly rejected



The target architecture does not adopt:

- fixed 50:50 CPU/GPU allocation
- user-selected GPU utilization such as 25/50/75%
- scheduling from GPU utilization alone
- forced GPU decode for every file
- forced GPU use on low-end hardware
- treating NVDEC failure as a whole-scan failure
- solving the problem only by increasing CPU worker count
- maximizing GPU utilization as the performance objective

## 14. Baseline preservation

This architecture does not modify or overwrite the official GPU baseline 0.9.2.32.

Implementation proceeds incrementally on the 0.9.3.x development line.

CPU fallback remains mandatory, and hardware acceleration is treated as an optional backend.

 
## 15. Benchmark / Telemetry integration

The Adaptive Scheduler cannot be validated adequately without detailed benchmark telemetry, so benchmark redesign is a first-class development stage.

In 0.9.4.x, benchmark output must record scheduler CPU/GPU allocation, calibration results, backend selection, queue state, fallback reasons, and measured throughput.

Measurements that are disabled or unavailable must not be encoded as numeric zero.

See [Benchmark and Runtime Telemetry Roadmap](benchmark-telemetry-roadmap.en.md) for the detailed benchmark design.

### Additional 0.9.4.0 requirements

- benchmark schemaVersion
- measured / not_measured / not_available / partial / failed / fallback states
- scheduler decision telemetry
- calibration telemetry
- GPU backend capability and selected backend
- video decoder/backend/fallback reason
- separate decodedFrames and sampledFrames
- queue wait / transfer time
- cancellation / partial-result state
- separate human-readable summary and machine-readable JSON
