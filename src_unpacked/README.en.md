# MediaSimilarityFinder

## Current development version: 0.9.4.2 (engine 1.5.0, DB 1.0.3; official baseline 0.9.2.32; development line 0.9.4, Node B1 validated)

Windows 11 x64 media duplicate/similarity search engine under active CPU/CUDA development.

### Current capabilities
- Incremental SQLite index with program-owned Index storage.
- Image/video fingerprinting and CandidateIndex acceleration.
- Fast CPU pHash (cached cosine tables, separable DCT, one-DCT normal+mirror pair) with NVIDIA CUDA backend and CPU fallback; the CUDA kernel carries the same 1e-7 near-zero snap, so CPU and GPU agree bit-exactly.
- Resident real-time folder monitor with foreground-workload protection.
- Horizontally mirrored image/video similarity detection.
- Persistent video fingerprint cache (v9: base frames plus per-frame crop hashes, 64-entry memory LRU).
- Image transformation-aware second-stage matching for center crops (4:3, 1:1, 9:16), including mirror variants, with SSIM grey-zone verification.
- Video temporal second-stage crop-aware comparison with parallel verification, dyadic sampling grids, and compare-time grid thinning.
- Large-scale exact CandidateIndex acceleration using a 4-part 16-bit multi-index with radius-2 enumeration for the normal D<=8 search range.
- Streaming Match delivery with optional full-result retention and configurable report-side match bounds.
- Search benchmark log: per-stage timings, per-file decode/hash costs, video time per play-minute and per GB, and 250 ms CPU/memory/GPU-duty sampling saved as a single JSON on completion or cancellation, with a summary popup.
- Portable Windows deployment: Qt platform plugins, FFmpeg tools, and matching VC++ runtime bundled beside the executable.
- Reveal in Explorer: reuses an already-open folder window when possible, otherwise opens a new one; group reference files keep highest similarity first and break ties by resolution, then size.

### CPU + GPU cooperative execution

MediaSimilarityFinder is explicitly designed to use CPU and GPU together. The GPU is an accelerator, not a replacement for the CPU path. CPU execution remains the stable reference/fallback path.

The approved resource-management direction is:

- Resource Mode remains Maximum / High / Balanced / Gaming / Manual.
- Maximum/High/Balanced/Gaming define CPU resource policy.
- Manual lets the user define the CPU resource limit.
- GPU is controlled only by ON/OFF.
- When GPU is ON, the user does not choose a GPU utilization percentage; the Adaptive GPU Scheduler determines GPU workload automatically.
- CPU/GPU work is not fixed at 50:50. Allocation follows measured capability, current system load, queue pressure, and data-transfer cost.
- CPU/GPU load caused by other applications is monitored at runtime and reflected in workload allocation.
- Initial hardware calibration/performance profiles are stored in INI for the next scan's starting estimate, while live runtime conditions take precedence.
- Low-end or inefficient GPUs can automatically converge toward CPU-heavy or CPU-only execution.
- Hardware video decode/NVDEC is an optional backend; failures must fall back safely to Software FFmpeg.

See [CPU/GPU Adaptive Resource Scheduling](docs/architecture/resource-scheduling.en.md) and [GPU Backend and Build Naming Roadmap](docs/architecture/gpu-backend-roadmap.en.md) for the detailed design and implementation order.

> Note: 0.9.4.0 implements the Node A foundation (GPU terminology/abstraction, GPU ON/OFF UI, build naming, benchmark instrumentation). The internal GPU cap is kept deprecated until the Node B Adaptive Scheduler replaces it; the INI performance profile arrives with Node C.

### Development numbering
- 0.9.1.x: CPU baseline
- 0.9.2.x: GPU and advanced search development
- 0.9.3.x: benchmark, packaging, and accuracy follow-ups
- 0.9.4.x: Node A foundation/instrumentation (current development line)
- 1.0.0: CPU + GPU complete target

Build history is maintained under docs/build-history/ in Korean and English (65 GPU / 64 CPU CTest tests). Architecture documents are under docs/architecture/.


### Benchmark / Telemetry direction

0.9.4.x will redesign benchmarking together with the Adaptive Scheduler. The benchmark will record end-to-end time plus CPU/GPU/decoder throughput, scheduler decisions, calibration, backend selection, fallbacks, queue/transfer costs, and video-decode bottlenecks. Unmeasured values will not be encoded as zero.

See [Benchmark and Runtime Telemetry Roadmap](docs/architecture/benchmark-telemetry-roadmap.en.md).
