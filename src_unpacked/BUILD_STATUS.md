# Build Status

- Current development version: **0.9.2.68**
- Official preserved baseline: 0.9.2.32 ??Large-result Match streaming and report retention bounds
- Windows CPU: 0.9.2.35??.9.2.39 ??VS18 2026 build, UI rewrite rounds
- CUDA validation: 0.9.2.40 ??RTX 3080 Ti, Toolkit 13.4, 38/38 PASS
- Current: 0.9.2.68 — QuickLook integration resume
- Last completed Windows build/test line: **0.9.2.68**
- Last completed v0.9.2.68 build: Core + GUI Release build PASS
- Last completed v0.9.2.68 test run: **CTest 52/52 PASS**
- Last completed v0.9.2.68 validation: --version 0.9.2.68 and --smoke exit 0
- candidatePairs exhaustive correctness regression: PASS
- candidatePairs benchmark: PASS (environment-dependent timing; see build history)
- Bucket-heavy benchmark: PASS ??5k / 12,497,500 pairs; streaming ScanPipeline path avoids materializing candidate-pair vectors
- Last completed v0.9.2.63 Windows Qt6 GUI runtime: VERIFIED on Windows (MediaSimilarityFinder.exe --version exit 0 with Qt6\plugins deployed; windowed run not exercised in that validation session)
- NVIDIA CUDA runtime: VERIFIED on RTX 3080 Ti (Toolkit 13.4, sm_86; cuda_backend_test green, kernels unchanged)

See docs/build-history/0.9.2.68.ko.md and .en.md for the current development changes. Official baseline: docs/build-history/0.9.2.32.ko.md and .en.md.
