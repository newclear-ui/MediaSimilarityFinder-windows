# Build Status — v3.22.0-NVIDIA

Date: 2026-09-02

## Build
- CMake configuration: SUCCESS
- C++ compiler: GCC 14.2.0
- SQLite3: 3.46.1
- FFmpeg: 7.1.5
- CUDA Toolkit / nvcc: NOT AVAILABLE; CMake correctly selected CPU fallback
- Qt6: NOT AVAILABLE; GUI target skipped by CMake

## Tests
- CTest: **18/18 PASS**
- Real FFmpeg video decode: PASS
- CandidateIndex benchmark: PASS
- Parallel similarity regression: PASS
- Partial-video alignment regression: PASS
- Resource policy regression: PASS

## CandidateIndex benchmark
Dataset: 6,000 random 64-bit fingerprints, Hamming distance <= 8.

- BK-tree build: 4.36 ms
- Indexed queries: 1,427.33 ms
- Linear reference: 2,289.36 ms
- Measured speedup: 1.60x
- Candidate pairs: 0
- Candidate reduction: 100%
- Indexed and linear hit counts: identical (6,000 self hits)

This is a development-container benchmark, not a Windows production benchmark.

## Not verified
- Windows 11 Qt6 GUI runtime
- Windows WIC runtime
- Actual NVIDIA CUDA compilation and execution
- NVDEC

These require a Windows/NVIDIA validation machine with Qt6 and CUDA Toolkit installed.
