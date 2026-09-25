# MediaSimilarityFinder

Windows x64 media duplicate and visual-similarity search application.

MediaSimilarityFinder is designed as a **CPU + GPU cooperative application**. CPU remains the stable reference/fallback path; GPU accelerates suitable workloads.

## Resource Management

User-facing modes:

- Maximum
- High
- Balanced
- Gaming
- Manual

CPU policy remains user-selectable. GPU is ON/OFF only.

- GPU ON → Adaptive GPU Scheduler chooses GPU workload automatically
- GPU OFF → CPU path
- CPU/GPU allocation is dynamic rather than fixed 50:50
- Hardware performance profiles are stored in INI as the next-run starting estimate
- Live CPU/GPU load from other applications is incorporated at runtime
- Low-end iGPU/dGPU systems may converge toward CPU-heavy or CPU-only execution

## Multi-vendor GPU Direction

The current concrete GPU compute backend is **NVIDIA CUDA**.

The architecture does not use CUDA as the generic name for the GPU system. The planned backend connection points are:

- NVIDIA CUDA
- NVIDIA NVDEC
- Vulkan
- AMD HIP/ROCm
- Intel Level Zero

Intel and AMD integrated/discrete GPUs use the same common GPU abstraction. Actual availability and performance are determined by capability and workload measurement.

## Benchmark / Telemetry

The 0.9.4.x redesign updates benchmarking as a first-class architecture layer. It records scheduler decisions, calibration, backend/decoder selection, fallback reasons, queue/transfer costs, detailed video decode stages, and explicit measurement states. Unmeasured values are not written as zero.

## Current / Next Development Line

Current validated code line: **0.9.3.19**

Next structural development line: **0.9.4.x**

Recommended first transition build: **0.9.4.0**

## Detailed Architecture

- [CPU/GPU Adaptive Resource Scheduling](src_unpacked/docs/architecture/resource-scheduling.en.md)
- [GPU Backend and Build Naming Roadmap](src_unpacked/docs/architecture/gpu-backend-roadmap.en.md)
- [Benchmark and Runtime Telemetry Roadmap](src_unpacked/docs/architecture/benchmark-telemetry-roadmap.en.md)
- [Source Structure](src_unpacked/docs/STRUCTURE.md)
- [Build History](src_unpacked/docs/build-history/)

## Build Naming

Top-level build entry points use CPU/GPU terminology:

- CPU build: build-windows-cpu
- GPU build: build-windows-gpu

The old 0.9.3.19 build-windows-cuda tree remains during migration. A clean build-windows-gpu tree is created for the 0.9.4.x transition.

Concrete NVIDIA implementation code continues to use the CUDA name. Generic layers use GPU terminology; concrete layers use their real backend technology name.

## Baseline

Official preserved baseline: 0.9.2.32.

CPU fallback remains mandatory.
