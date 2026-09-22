# Media Similarity Finder v3.27.0-NVIDIA

## v3.23~v3.27 integrated work

This release continues from v3.22.0 as one five-stage development bundle.

### v3.23 — CandidateIndex batch/scalability API
- Added bulk insertion (`addAll`) and batch querying (`queryAll`).
- Candidate results are deterministically ordered by Hamming distance then index.
- Existing exact BK-tree semantics are preserved.

### v3.24 — Benchmark/regression expansion
- Retained the 6,000-fingerprint indexed-vs-linear benchmark.
- Added batch-query regression coverage.
- Benchmark output records build/query time, speedup, hit count and candidate reduction.

### v3.25 — Persistent video fingerprint cache
- Added optional SQLite-backed video fingerprint cache.
- Cache key is absolute path as supplied by the caller plus file size and modification timestamp.
- Cache payload stores duration, timestamps and 64-bit frame hashes.
- Memory cache remains available and is checked before persistent cache.
- FFmpeg is not required to read a valid persisted fingerprint cache entry.

### v3.26 — Temporal alignment quality
- Added configurable timestamp tolerance to video similarity.
- Temporal penalty compares local sampling intervals rather than absolute timestamps, so a clip beginning at a different time offset is not incorrectly penalized.
- Existing local sequence alignment remains the basis for partial-video matching.

### v3.27 — Incremental integration correctness / build preparation
- Fixed incremental scan result reconstruction: unchanged files are now restored from SQLite into the in-memory search set before candidate/group analysis.
- Added regression coverage proving that a second unchanged scan still produces similarity groups.
- CMake version updated to 3.27.0.
- CUDA and Qt6 remain optional; missing toolkits do not block CPU/core configuration.

## Validation in this environment
- GCC 14.2 / CMake 3.31.6 / SQLite 3.46.1.
- CPU/core build succeeded with CUDA and GUI disabled.
- **21/21 CTest tests passed.**
- FFmpeg real-video regression test passed.
- Candidate benchmark: N=6000, indexed query 1427.29 ms, linear query 2287.50 ms, measured speedup 1.60x, identical hit count, candidate reduction 100% for the benchmark's random workload.
- Partial-video alignment: 100%.
- Different absolute timestamp offset alignment: 100%.

## Not verified here
- Windows 11 Qt6 GUI runtime.
- Windows WIC and Windows shell/file-operation behavior.
- Actual NVIDIA CUDA compilation/runtime and NVDEC.

Those require a Windows/NVIDIA development machine with the corresponding SDK/toolkits installed.
