# Build Status

- Current development version: **0.9.3.14**
- Official preserved baseline: 0.9.2.32 ??Large-result Match streaming and report retention bounds
- Windows CPU: 0.9.2.35??.9.2.39 ??VS18 2026 build, UI rewrite rounds
- CUDA validation: 0.9.2.40 ??RTX 3080 Ti, Toolkit 13.4, 38/38 PASS
- Current: 0.9.3.14 — CUDA row-batch acceleration for video MSSIM
- Last completed Windows build/test line: **0.9.3.14**
- Last completed v0.9.3.14 build: Core + GUI Release build PASS
- Last completed v0.9.3.14 test run: **CTest 61/61 PASS**
- Last completed v0.9.3.14 validation: RTX 3080 Ti SSIM GPU equivalence PASS; corpus-scale benchmark PASS (pHash ~4.1x, MSSIM ~3.0x); Portable v0.9.3.14 smoke exit 0; `--version` 0.9.3.14
- CPU-only validation: Release build PASS; **CTest 60/60 PASS**; CUDA disabled and CPU fallback verified
- Windows build scripts: default `-BuildParallelism 1` avoids vcpkg `z-applocal` output-copy races; higher parallelism remains opt-in
- candidatePairs exhaustive correctness regression: PASS
- candidatePairs benchmark: PASS (environment-dependent timing; see build history)
- Bucket-heavy benchmark: PASS ??5k / 12,497,500 pairs; streaming ScanPipeline path avoids materializing candidate-pair vectors
- Last completed v0.9.2.63 Windows Qt6 GUI runtime: VERIFIED on Windows (MediaSimilarityFinder.exe --version exit 0 with Qt6\plugins deployed; windowed run not exercised in that validation session)
- NVIDIA CUDA runtime: VERIFIED on RTX 3080 Ti (Toolkit 13.4, sm_86; cuda_backend_test green, kernels unchanged)

See docs/build-history/0.9.3.14.ko.md and .en.md for the current development changes. Official baseline: docs/build-history/0.9.2.32.ko.md and .en.md.
