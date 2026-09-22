# Media Similarity Finder v3.17.0

## Bundle v3.13-v3.17

This release advances the analysis engine rather than adding another GUI mock.

### v3.13 — perceptual image hash
The CPU image/video fingerprint path now uses a compact 64-bit DCT perceptual hash (pHash-style low-frequency coefficients) on normalized 32x32 grayscale media instead of a raw average hash.

### v3.14 — temporal video alignment
Video comparison uses a sliding contiguous temporal window. Two otherwise identical clips can therefore match even when their starting offsets differ.

### v3.15 — CUDA batch execution
The CUDA backend now contains a real device allocation, host/device transfer, kernel launch, synchronization and result-copy path for the batch hashing interface, with deterministic CPU fallback.

### v3.16 — GPU abstraction hardening
GPU discovery and the batch accelerator remain behind `GpuBackend`, preserving the architecture for a future CUDA pHash kernel, NVDEC decoding, and AMD/Intel backends without changing the search engine API.

### v3.17 — regression/release
All existing 14 CTest targets pass in the available Linux verification environment. FFmpeg-enabled configuration also passes. Qt6 GUI and NVIDIA CUDA execution cannot be exercised in this environment because those toolkits/devices are unavailable.

## Verification

- C++20 core build: PASS
- CTest: 14/14 PASS
- FFmpeg-enabled core build: PASS
- GUI: source retained; requires Qt6 on Windows
- CUDA: source retained; requires CUDA Toolkit + NVIDIA GPU on Windows
