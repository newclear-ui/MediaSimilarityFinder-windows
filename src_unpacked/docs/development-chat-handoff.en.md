# Development Session Handoff — 2026-09-26

This document is the continuation handoff for a new chat or OpenCode session.

## 1. Two rules to verify first

### Project-local vcpkg must remain

Keep the project-local `vcpkg_installed` as-is.

Do not migrate to a shared/public vcpkg installation until Windows CPU build, run, and all verification are fully complete.

When migration becomes appropriate, do not immediately delete the old installation. First tell the user: “이제 vcpkg 정리/이동 단계입니다” and proceed step by step.

Current local path:
`C:\MediaSimilarityFinder-v0.9.2.33-Windows-CPU-Test-source-ready\vcpkg_installed\x64-windows`

### Track all Windows CPU build/compatibility problems

Continue tracking every Windows CPU build and compatibility fix. After complete CPU build/run/verification, write the full KO/EN build-history record.

## 2. Project baseline

Repository:
`newclear-ui/MediaSimilarityFinder-windows`

Official preserved baseline:
**0.9.2.32**

Current reference code:
**0.9.3.19**

Current development line:
**0.9.4**

The current 0.9.3.19 source still contains the legacy fixed CPU/GPU percentage policy and GPU percentage UI. The 0.9.4 implementation is not complete.

## 3. Development-stage vs version rule

Never map roadmap stages directly to version numbers.

Use:

```
Roadmap: A -> B -> C -> D -> E -> F -> G -> H
Version: 0.9.4.0 -> 0.9.4.1 -> 0.9.4.2 -> ...
```

A1/B1/C1 are recovery substeps inside the current node.

Version numbers advance only when a validated code state is established.

## 4. Current roadmap

A Foundation / Terminology / Instrumentation
B Adaptive Scheduler
C Calibration / INI Performance Profile
D Pipeline / Queue Optimization
E Adaptive Video Decode Planner
F Hardware Video Decode Backend
G Additional GPU Backends
H Regression / Stability / Performance Validation

Each node must pass its exit criteria before the next node is started.

## 5. Current progress

Current Node:
**A — Foundation / Terminology / Instrumentation**

Current phase:
**design/document baseline complete → source implementation entry**

Current version:
**0.9.3.19**

Current GPU implementation:
**NVIDIA CUDA**

CPU fallback:
**retained**

## 6. Document model

```
Development Roadmap
        |
        v
Development Progress
        |
        v
Build / Test
        |
        v
Build History
```

Main documents:

- `src_unpacked/docs/development-roadmap.ko.md`
- `src_unpacked/docs/development-roadmap.en.md`
- `src_unpacked/docs/development-progress.ko.md`
- `src_unpacked/docs/development-progress.en.md`
- `src_unpacked/docs/architecture/`
- `src_unpacked/docs/build-history/`
- `src_unpacked/AGENTS.md`

Recommended future implementation briefs:
`src_unpacked/docs/implementation-briefs/`

## 7. OpenCode operating model

Do not ask OpenCode to implement all of 0.9.4 at once.

OpenCode reads:

1. AGENTS.md
2. Development Roadmap
3. Development Progress
4. current-node architecture documents
5. relevant source/tests

Then it implements only the current node.

Problems become A1/B1/C1-style recovery substeps.

Do not start the next node before the current node gate passes.

## 8. Node A scope

Node A establishes:

- vendor-neutral high-level GPU terminology
- GPU ON/OFF user model
- preserved CPU Resource Modes: Maximum / High / Balanced / Gaming / Manual
- `MSF_ENABLE_GPU`
- `MSF_GPU_BACKEND`
- `build-windows-cpu`
- `build-windows-gpu`
- existing CUDA as concrete backend
- CPU fallback
- benchmark schemaVersion
- measurement states
- scheduler/queue/transfer/decoder instrumentation
- decodedFrames vs sampledFrames
- cancellation/partial state

Do not implement the full Adaptive Scheduler or future hardware backends in Node A.

## 9. Core architecture constraints

No fixed 50:50 CPU/GPU split.

GPU utilization itself is not the optimization target.

GPU ON means AUTO scheduling.

GPU OFF means CPU path.

Manual controls CPU only.

Low-end/inefficient GPU may receive little or no work.

Software FFmpeg remains the video decode reference/fallback.

NVDEC is a future concrete hardware decode backend.

Do not claim Vulkan/HIP/Level Zero support without actual validation.

## 10. Current important technical bottlenecks

From 0.9.3.19:

- `video_decoder.cpp::framesAt()` decodes every frame up to target rather than only the requested sample frames.
- `sws_getContext` may be created/destroyed per sample.
- video range/future/barrier behavior can create straggler/worker-starvation effects.
- current GPU hash path has a global `hashMutex_`.
- crop processing may duplicate decode/open work.
- benchmark detail-off can report video/resource measurements as zero even when work occurred.
- decodedFrames and sampledFrames are not properly separated.

Adaptive Video Decode should therefore address unnecessary decode work, not merely add NVDEC.

## 11. Build environment

- Windows 10 Pro 2009 build 26200
- Visual Studio Community 2026 / 18.10.12201.205
- MSVC 19.51.36257.0
- Windows SDK 10.0.26100.0
- CMake 4.4.2
- Git 2.55.0.windows.5

Keep project-local vcpkg.

Current old GPU build tree:
`build-windows-cuda`

Target:
`build-windows-gpu`

Migration should use a clean configure; do not rename/reuse the old cache in place.

## 12. Next action

Create and save the Node A implementation brief:

```
src_unpacked/docs/implementation-briefs/
├─ A-foundation.ko.md
└─ A-foundation.en.md
```

The brief should contain only the detailed implementation work for Node A. Do not duplicate the entire Roadmap or all architecture documents.

## 13. New-chat starter

Use:

```
Continue MediaSimilarityFinder from the previous session.

First read:
src_unpacked/docs/development-chat-handoff.ko.md
AGENTS.md

Then read the Development Roadmap and Development Progress.
Identify the current node and active substep.

Work only on the current node.
Do not enter the next node before the current node gate passes.
```
