# Media Similarity Finder v3.32.0-NVIDIA

## v3.28~v3.32 integrated five-stage bundle

### v3.28 — Candidate pair generation
- CandidateIndex now exposes `candidatePairs()` so downstream search can consume the exact BK-tree candidate set directly.
- ScanPipeline no longer performs the final all-files O(N²) similarity loop; it evaluates only indexed candidate pairs.
- Candidate reduction statistics remain available.

### v3.29 — Core result propagation
- Added `MediaMatch` / `SearchMatch` result structures.
- MediaSearchEngine now returns concrete candidate matches with paths and similarity percentages.
- Qt GUI consumes core search results instead of reconstructing every pair with an O(N²) loop.

### v3.30 — Persistent video cache integration
- MediaSearchEngine automatically opens a sidecar SQLite video fingerprint cache next to the main index.
- Existing v3.25 cache format is reused.
- Cached video fingerprints can therefore survive application restarts and avoid FFmpeg re-analysis when file size/mtime are unchanged.

### v3.31 — NVIDIA pHash correctness and image batch path
- CUDA batch hashing now implements the same 32x32, 8x8 low-frequency DCT/median pHash algorithm used by the CPU implementation instead of the old average-hash kernel.
- CPU fallback was aligned with the same pHash algorithm.
- Added `MediaPipeline::imageBatch()` as the common batch boundary for future scheduler/GPU batching.
- CUDA remains optional and CPU configuration works without nvcc.

### v3.32 — GUI large-result path preparation
- GUI result reconstruction was moved to the Core `SearchReport` match list.
- This removes the previous GUI-side all-pairs comparison and makes large result sets substantially more suitable for later virtualization/thumbnail-cache work.
- Window title/version updated to 3.32.

## Validation in this environment
- CPU/core CMake configure and build completed with CUDA and Qt GUI disabled.
- **24/24 CTest tests passed.**
- FFmpeg real-video regression passed.
- Candidate pair regression passed.
- Image batch CPU fallback regression passed.
- Search report regression passed.

## Not verified here
- Windows 11 Qt6 GUI runtime and compilation (Qt6 is unavailable in this environment).
- Windows WIC/shell/file-operation behavior.
- Actual NVIDIA CUDA compilation/runtime and NVDEC (nvcc/CUDA toolkit unavailable).
- The CUDA pHash kernel was implemented to match the CPU algorithm, but it has not been executed on an NVIDIA GPU in this environment.
