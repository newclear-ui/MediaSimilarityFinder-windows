# MediaSimilarityFinder

## Current development version: 0.9.3.2 (engine 1.1.0, DB 1.0.2; official baseline 0.9.2.32)

Windows 11 x64 media duplicate/similarity search engine under active CPU/CUDA development.

### Current capabilities
- Incremental SQLite index with program-owned `Index` storage.
- Image/video fingerprinting and CandidateIndex acceleration.
- Fast CPU pHash (cached cosine tables, separable DCT, one-DCT normal+mirror pair) with NVIDIA CUDA backend and CPU fallback; the CUDA kernel carries the same 1e-7 near-zero snap, so CPU and GPU agree bit-exactly.
- Resident real-time folder monitor with foreground-workload protection.
- Horizontally mirrored image/video similarity detection.
- Persistent video fingerprint cache (v7: base frames plus per-frame crop hashes, 64-entry memory LRU).
- Image transformation-aware second-stage matching for center crops (4:3, 1:1, 9:16), including mirror variants, with SSIM grey-zone verification.
- Video temporal second-stage crop-aware comparison with parallel verification, dyadic sampling grids, and compare-time grid thinning.
- Large-scale exact CandidateIndex acceleration using a 4-part 16-bit multi-index with radius-2 enumeration for the normal D<=8 search range.
- Streaming Match delivery with optional full-result retention and configurable report-side match bounds.
- Search benchmark log: per-stage timings, per-file decode/hash costs, video time per play-minute and per GB, and 250 ms CPU/memory/GPU-duty sampling saved as a single JSON on completion or cancellation, with a summary popup.
- Portable Windows deployment: Qt platform plugins, FFmpeg tools, and matching VC++ runtime bundled beside the executable.
- Reveal in Explorer: reuses an already-open folder window when possible, otherwise opens a new one; group reference files keep highest similarity first and break ties by resolution, then size.

### Development numbering
- `0.9.1.x`: CPU baseline
- `0.9.2.x`: GPU and advanced search development
- `0.9.3.x`: benchmark, packaging, and accuracy follow-ups (current minor line)
- `1.0.0`: CPU + GPU complete target

Build history is maintained under `docs/build-history/` in Korean and English (59 CTest tests). Architecture documents are under `docs/architecture/`.
