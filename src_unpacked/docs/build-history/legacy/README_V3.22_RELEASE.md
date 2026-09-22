# Media Similarity Finder v3.22.0-NVIDIA

v3.18~v3.22 integrated release. This release keeps the v3.17 core/GUI architecture and adds five areas of work in one bundle.

## v3.18 — Large-scale CandidateIndex
- Replaced the previous linear CandidateIndex scan with an exact Hamming-distance BK-tree.
- `query()` remains exact for distances 0..64.
- Candidate reduction statistics are exposed by `ScanStats` / `SearchReport`.

## v3.19 — Benchmark
- Added `candidate_index_benchmark_test`.
- Benchmark compares indexed queries against a linear reference and verifies identical hit counts.
- Candidate reduction percentage is calculated against all possible pairs.

## v3.20 — Video cache / parallel comparison
- `VideoFingerprintEngine` now caches fingerprints by absolute path + file size + modification timestamp during an engine lifetime.
- Cache access is mutex-protected.
- Changed-file analysis is executed in bounded parallel batches according to the CPU resource policy.
- Added `best_match_parallel()` for multi-frame fingerprint comparisons.

## v3.21 — Similarity quality
- Added configurable image/hash similarity threshold APIs.
- Video similarity now uses local sequence alignment rather than only fixed-position sliding windows.
- Partial clips and different start/end positions can match without requiring equal sample counts.
- Existing sampling policy is unchanged:
  - <=10s: 1s
  - <=1m: 2s
  - <=5m: 5s
  - <=30m: 10s
  - <=1h: 15s
  - >1h: 30s

## v3.22 — Integration / build preparation
- Resource presets are now exactly:
  - Maximum: 90/90
  - Balanced: 55/60
  - Gaming: 25/25
  - Custom: user supplied values
- CPU percentage is connected to the scan worker concurrency budget.
- GPU percentage is retained as the GPU batch budget API for the optional CUDA backend.
- Added regression tests for policy, partial-video alignment and parallel similarity.
- CMake remains optional for CUDA and Qt6: when unavailable, CPU/core build continues.

## Validation in this development environment
- GCC 14.2 / CMake 3.31.6 / SQLite 3.46.1: build succeeded.
- FFmpeg 7.1.5: real video decode test succeeded.
- 18/18 CTest tests passed.
- Candidate benchmark (6,000 random 64-bit fingerprints, Hamming distance <=8): indexed query 1427 ms vs linear 2289 ms, 1.60x measured speedup, with identical hit counts.
- Partial-video alignment test: 100% for an exact embedded 8-sample clip. Parallel similarity and resource-policy regression tests also passed.

## Not verified here
- Qt6 Windows GUI runtime was not executed because Qt6 is unavailable in this environment.
- Actual NVIDIA CUDA compilation/runtime was not executed because `nvcc` / CUDA Toolkit is unavailable.
- Windows-specific WIC and Windows shell/file-operation behavior therefore still requires Windows validation.
