# Review of GPU Expansion for 48x48 Video MSSIM

## Current bottleneck

`video_similarity()` runs `frame_ssim()` on the CPU for frame pairs that pass the Hamming gate. `thumb48` is already stored as contiguous per-frame arrays, so this stage can become CPU-bound when candidate counts reach tens of thousands.

## Recommended architecture

- Add `GpuBackend::ssimBatch()` accepting paired 48x48 grayscale arrays and an output array.
- The CUDA kernel should calculate per-pair 8x8-window MSSIM using the same C1/C2 constants and averaging rule as the CPU implementation.
- Preserve DTW row dependencies: do not materialize the full matrix on the GPU; pack only Hamming-passing pairs from each DTW row.
- Fall back to `frame_ssim()` on batch failure, unsupported CUDA, or small batches.
- Pack normal and mirrored directions into one batch to reduce GPU call and transfer overhead.

## Validation requirements

- Add tolerance and threshold-boundary regressions comparing GPU and CPU results.
- Cover identical, re-encoded, low-contrast/flat, mirrored, and no-thumb legacy-cache frames.
- Measure CPU/GPU wall time by candidate count, host/device transfer time, and VRAM usage.

## Assessment

The design is feasible, but `10x or more` cannot be guaranteed yet. Per-row DTW calls and transfer overhead may erase gains for small candidate sets, so activation should follow a large real-corpus benchmark.
