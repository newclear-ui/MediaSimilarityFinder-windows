# GPU Backend and Build Naming Roadmap

## Purpose

The MediaSimilarityFinder top-level architecture and user documentation must not use one vendor or API name as the generic name for all GPUs.

- User/product level: CPU / GPU
- Common engine layer: GPU backend
- Concrete implementation layer: CUDA / Vulkan / HIP (ROCm) / Level Zero, etc.

The 0.9.3.19 implementation currently uses NVIDIA CUDA, but the architecture must remain ready for Intel/AMD integrated and discrete GPUs.

## 1. GPU backend layer

The target is a common GpuBackend interface with concrete backends connected independently.

- CudaGpuBackend — NVIDIA CUDA, current implementation
- VulkanGpuBackend — vendor-neutral Vulkan compute candidate
- HipGpuBackend — AMD HIP/ROCm candidate
- LevelZeroGpuBackend — Intel oneAPI Level Zero candidate
- CPU reference/fallback remains a separate baseline path

Vulkan is a candidate cross-platform/cross-vendor graphics and compute layer. It must not be assumed to provide identical features or performance to CUDA on every GPU. Device capability, driver, shader features, subgroup support, memory behavior, and transfer cost must be measured.

For Intel GPUs, Vulkan and oneAPI Level Zero are the primary candidate layers. Intel describes Level Zero as a low-level interface for accelerator devices that provides explicit control to higher-level runtimes.

For AMD GPUs, Vulkan is the broad candidate path and HIP/ROCm is an optional vendor-specific compute backend where the actual Windows GPU/driver combination is supported. Runtime capability must still be detected on each system.

## 2. Backend selection

Users do not choose NVIDIA/AMD/Intel backends directly.

When GPU is ON:
1. Discover GPU devices.
2. Discover available backends.
3. Query backend capabilities.
4. Run lightweight calibration / observe early real workload.
5. Inspect current system load.
6. Evaluate workload-specific efficiency.
7. Select the most appropriate backend/work allocation.

Record backend capability independently, for example:
- CUDA: available
- Vulkan: available
- HIP: unavailable
- LevelZero: available
- NVDEC: available

This becomes part of the reusable hardware profile.

## 3. Vulkan's role

0.9.4.x does not mean rewriting the entire search pipeline in Vulkan immediately.

The first goals are:
- establish vendor-neutral GPU interfaces
- establish device discovery/capability layers
- establish backend registration
- retain CPU fallback
- preserve the current CUDA backend behavior
- make Vulkan connectable as a future backend without changing the search engine

A Vulkan compute pilot follows separately.

Do not treat Vulkan as a complete CUDA replacement until correctness parity and end-to-end throughput have been verified.

## 4. Intel iGPU / dGPU

Both Intel integrated and discrete GPUs use the same GPU abstraction.

Candidate backends:
1. Vulkan
2. Level Zero
3. Other oneAPI layers only when justified

A weak Intel iGPU may still be beneficial for some stages such as decode, resize, or hashing. Backend usefulness is therefore judged per workload rather than from one overall GPU score.

## 5. AMD iGPU / dGPU

Both AMD integrated and discrete Radeon GPUs use the same abstraction.

Candidate backends:
1. Vulkan
2. HIP/ROCm

If the actual AMD GPU/backend combination is not beneficial or unavailable, the scheduler may converge to CPU execution.

## 6. NVIDIA

NVIDIA keeps CUDA as the current compute backend.

Future NVIDIA structure:
- CUDA compute backend
- NVDEC video decode backend
- optional Vulkan compute backend

Therefore GPU != CUDA; CUDA is one NVIDIA GPU backend.

## 7. CPU/GPU naming

Product, documentation, and build entry points use CPU/GPU.

Planned renames:
- build-windows-cuda → build-windows-gpu
- build_windows_cuda.ps1 → build_windows_gpu.ps1
- windows-cuda CMake preset → windows-gpu
- windows-cuda-release → windows-gpu-release
- user-facing CPU/CUDA wording → CPU/GPU, except when specifically referring to NVIDIA CUDA

The top-level CMake option should evolve toward:
- MSF_ENABLE_CUDA → MSF_ENABLE_GPU
- backend selection separated into a backend selector

Example:
- MSF_ENABLE_GPU=ON
- MSF_GPU_BACKEND=AUTO
- development/diagnostic override: MSF_GPU_BACKEND=CUDA

### Concrete implementation naming

A file implementing CUDA should still be named after CUDA because that is technically accurate.

Examples:
- gpu_backend_cuda.cu
- gpu_backend_vulkan.cpp
- gpu_backend_hip.cpp
- gpu_backend_level_zero.cpp

The rule is: generic above, technology-specific below.

## 8. Build-directory migration

Migrating from build-windows-cuda to build-windows-gpu is appropriate at the 0.9.4.x transition.

Do not reuse the old CMake build tree by simply renaming the directory.

Recommended:
1. Keep build-windows-cuda temporarily.
2. Create a clean build-windows-gpu.
3. Configure/build/test both CPU and GPU lines.
4. Remove or archive the old directory only after successful verification.

Update all script/preset/document/CI references together.

## 9. Version line

The next development line is 0.9.4.x.

Recommended first build:
0.9.4.0

This marks the start of:
- CPU/GPU terminology generalization
- stronger GPU backend abstraction
- Adaptive CPU/GPU Scheduler
- INI performance profile
- multi-vendor GPU connection points
- build naming cleanup

This is an internal development minor-line transition, not the semantic-versioning 1.x major release.

## 10. Implementation stages

### 0.9.4.0
- clean up GPU backend naming/abstraction
- finalize CPU/GPU Resource Modes
- remove manual GPU utilization control
- implement the basic Adaptive Scheduler layer
- define INI performance-profile schema
- rename GPU build entry points
- reconnect the existing CUDA backend without changing its algorithm
- CPU/GPU regression coverage

### Later 0.9.4.x
- richer runtime telemetry
- pipeline scheduling
- adaptive video-decode planner
- Vulkan capability/discovery pilot
- NVDEC backend separation/integration
- HIP/ROCm prototype where justified
- Level Zero prototype where justified

Every backend must be independently validated. Adding one backend must not break CPU fallback or other GPU backends.

## 11. Prohibitions

- Do not use CUDA as the generic name for the entire GPU system.
- Do not assume Vulkan is automatically optimal on every GPU.
- Do not claim Intel/AMD backend support is complete without real hardware validation.
- Do not let one GPU backend failure terminate the whole scan.
- Do not spread CUDA API calls through the high-level search engine.
- Do not delete or weaken CPU fallback.
