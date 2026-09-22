# MediaSimilarityFinder 0.9.2.2 — NVIDIA CUDA backend

0.9.2.2 is the first GPU-development build after the 0.9.1.0 CPU baseline.
The CPU backend, SQLite index, FFmpeg/video pipeline, CandidateIndex, and Qt GUI architecture are retained.

## GPU path

`MediaPipeline::imageBatch()` decodes images to normalized 32x32 grayscale on the CPU, packs them into bounded batches, and sends each batch through `GpuBackend` when CUDA is available. `GpuBackend` owns the CPU/CUDA selection boundary; CUDA failure falls back to the CPU pHash reference implementation.

The CUDA kernel computes the same 64-bit 8x8 low-frequency DCT pHash used by the CPU reference implementation.

## NVIDIA target

Default CUDA SM targets: 75, 86, 89. SM 86 is included for NVIDIA Ampere GPUs such as the RTX 3080 Ti.

## Verification status

The current development container has no NVIDIA CUDA Toolkit/nvcc and no NVIDIA GPU, so actual CUDA compilation and runtime execution cannot be claimed here. The CPU configuration was rebuilt after the GPU changes and all 24 existing CTest cases passed.

On a CUDA-equipped Windows system, run `scripts/build_windows_cuda.ps1`. The CUDA-specific regression test compares GPU pHash output with the CPU reference for deterministic 32x32 images.


## 0.9.2.x GPU roadmap

The 0.9.2.x line keeps the CPU reference implementation intact while progressively moving image fingerprint work to CUDA. The current batch path is bounded by both the resource-policy GPU percentage and detected free VRAM. Each scan reports how many image fingerprints used CUDA and how many fell back to CPU.

The CUDA kernel uses precomputed DCT coefficients and a two-pass separable DCT, avoiding per-pixel trigonometric evaluation in the hot path while retaining double-precision reference arithmetic.
