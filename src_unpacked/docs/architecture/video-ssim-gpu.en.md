# 48x48 Video MSSIM GPU Batch Architecture

## Current structure

`video_similarity()` selects only Hamming-passing frame pairs per DTW row and scores them through `GpuBackend::ssimBatch()` on 48x48 MSSIM. The full DTW matrix is never materialized on the GPU, so device memory scales with one row batch.

## Execution flow

1. Hamming similarity is computed first for each DTW row.
2. Normal and mirrored `thumb48` pairs passing the gate are packed into contiguous batch arrays.
3. Row batches of 8 or more go to the CUDA kernel, which computes 36 8x8-window MSSIM values per pair and averages them.
4. Batch failure, unsupported CUDA, or rows smaller than 8 use the CPU `frame_ssim()` path.
5. SSIM never promotes a Hamming reject; the existing `0.4*Hamming + 0.6*SSIM` blend rule is unchanged.

## Concurrency and semantic preservation

- `ssimBatch()` calls are serialized by the `GpuBackend` mutex so concurrent temporal workers share one backend.
- Identical C1/C2 constants and window-averaging rules keep threshold semantics unchanged.
- The `ScanPipeline` temporal stage and the `compareFingerprint()` live path share the same GPU backend.

## Limits

- Per-row DTW calls plus host/device transfer bound the gain on small candidate sets.
- The engine verdict version is unchanged; this is an additive acceleration with no stored-pair revalidation.
