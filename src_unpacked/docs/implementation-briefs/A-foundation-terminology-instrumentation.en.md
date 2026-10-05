# A — Node A: Foundation / Terminology / Instrumentation

**Status: CLOSED** (a reference contract for a completed node). The actual
implementation and measurement evidence is `docs/build-history/0.9.4.0.en.md`.

> This document collects the contract of a finished node in one place as a
> **reference**. Its origin is build-history 0.9.4.0 plus the Node A section of
> `docs/development-roadmap.en.md`. Node A is already closed, so **do not start
> new design work from this document.** If the Node A area has to be touched
> again, read build-history 0.9.4.0 first and diff it against the current code.

## 1. Purpose

Organize the upper GPU abstraction into vendor-neutral terminology, expose GPU to
the user as only ON/OFF, and establish build naming and benchmark instrumentation.
Search verdict logic is not touched.

## 2. Settled terms / structures (as implemented)

| Item | Settled form |
|---|---|
| GPU backend | `GpuBackendKind { Auto, Cuda, Cpu }` + `backendName()`. Auto by default, CUDA when present, otherwise CPU fallback |
| CMake options | `MSF_ENABLE_GPU` + `MSF_GPU_BACKEND` (AUTO/CUDA/CPU). `MSF_ENABLE_CUDA` is a deprecated compatibility alias |
| Source macros | `MSF_HAS_GPU` and `MSF_HAS_CUDA` are **both** defined. `MSF_HAS_GPU` is canonical |
| Build entry points | `build-windows-cpu` / `build-windows-gpu` + `scripts/build_windows_cpu.ps1` / `scripts/build_windows_gpu.ps1` |
| Measurement state | `MeasureState` = measured / not_measured / not_available / partial / failed / fallback |
| Frame instrumentation | `decodedFrames` and `sampledFrames` kept **separate**. `not_measured` when the sampler did not run |
| CPU control | only `cpuPercent` is user-editable. `gpuPercent` is a deprecated internal value |
| CPU fallback | always retained |

## 3. Closure conditions (all met)

- Legacy GPU UI (GPU usage slider, numeric spin, monitor entries) removed; GPU is an ON/OFF checkbox only
- GPU ON/OFF structure complete, with a GUI error message when the backend is unavailable
- The benchmark "unmeasured = 0" problem removed
- CPU fallback behavior has no defect and is covered by a regression test
- Code that participates in search verdicts is unchanged (fingerprint, similarity, thresholds, cache, timestamps)
- Build / CMake cleaned up and versions aligned

## 4. Invariants that must be maintained

- **Instrumentation must not change search results.** What was added is read-only
  accumulation plus JSON serialization only.
- **Never record an unmeasured value as 0.** It must be distinguished by `MeasureState`.
- **CPU fallback is always retained.**
- Benchmark JSON is an additive extension. Keep existing keys, add new ones, and let
  `schemaVersion` start at 1 independently of the engine/DB versions.

## 5. Items still in a deprecated state (verified in code 2026-10-04)

The following **still exist** and diverge from what Node A settled, so they are
cleaned up together when next touched.

| Item | Current state | Note |
|---|---|---|
| `ResourcePolicy::gpuPercent` | **present (25 references)** | the comment at `resource_policy.h:6` states "deprecated internal". Not user-editable. `resource_policy.cpp` still sets per-mode defaults |
| `MSF_ENABLE_CUDA` CMake option | present (CMakeLists deprecated alias) | 0 references in src/gui. Kept only as an alias |
| `MSF_HAS_CUDA` | defined (12 references) | coexists with `MSF_HAS_GPU`, which is canonical |
| `windows-cuda` / `windows-cuda-release` in `CMakePresets.json` | present | `binaryDir` is `build-windows-cuda` |
| `scripts/build_windows_cuda.ps1` | present | the canonical script is `build_windows_gpu.ps1` |
| `build-windows-cuda/` tree | **absent** | the old tree is not preserved and is not renamed and reused in place |

> Removing `gpuPercent` was originally deferred to the Node B scheduler, but it is
> still present now that Node B is CLOSED. Whether to clean it up is left as a
> separate decision.

## 6. Verification (measured when the node landed)

- CPU Release: CTest **62/62 PASS**, `--version` 0.9.4.0, `--smoke` PASS
- GPU Release (`build-windows-gpu`, clean configure): CTest **63/63 PASS**
- `benchmark_test`: schemaVersion, runId, completionReason, gpuBackend, stage/scheduler/calibration/files states,
  decoded/sampled separation, `not_measured` when the sampler did not run, `not_available`
  for a root with no drive letter
- `gpu_backend_policy_test`: Auto default, `backendName()` consistency (CUDA/CPU),
  unavailable when pinned to Cpu

> These figures are **the values at 0.9.4.0**. The current baseline is CPU 102/102
> and GPU 103/103; see `docs/node-status-gate-matrix.en.md`.

## 7. Related documents

- `docs/build-history/0.9.4.0.{ko,en}.md` — the origin of the actual change and verification
- `docs/architecture/resource-scheduling.{ko,en}.md`
- `docs/architecture/gpu-backend-roadmap.{ko,en}.md`
- `docs/development-roadmap.{ko,en}.md` Node A section
- Following nodes: `B-adaptive-scheduler.*` (scheduler), `C-calibration-profile.*` (calibration)
