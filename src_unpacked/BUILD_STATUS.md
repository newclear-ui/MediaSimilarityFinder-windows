# Build Status

- Current development version: **0.9.4.0**
- Official preserved baseline: 0.9.2.32 — Large-result Match streaming and report retention bounds
- Windows CPU: 0.9.2.35–0.9.2.39 — VS18 2026 build, UI rewrite rounds
- CUDA validation: 0.9.2.40 — RTX 3080 Ti, Toolkit 13.4, 38/38 PASS
- Current: 0.9.4.0 — Node A foundation: GPU terminology/abstraction, GPU ON/OFF UI, build naming, benchmark instrumentation (engine 1.5.0, video cache v9)
- Last completed Windows build/test line: **0.9.4.0**
- Last completed v0.9.4.0 build: Core + GUI Release build PASS (CPU and GPU trees)
- Last completed v0.9.4.0 test run: **CTest 63/63 PASS (GPU)**
- Last completed v0.9.4.0 validation: Node A exit gate PASS; CUDA backend discovery through the GPU abstraction; Portable deferred to release packaging
- CPU-only validation: Release build PASS; **CTest 62/62 PASS**; CUDA disabled and CPU fallback verified
- Windows build scripts: default `-BuildParallelism 1` avoids vcpkg `z-applocal` output-copy races; higher parallelism remains opt-in
- Build entry points: `scripts/build_windows_gpu.ps1` (canonical, clean `build-windows-gpu` tree) and `scripts/build_windows_cpu.ps1`; `scripts/build_windows_cuda.ps1` remains a deprecated alias path
- candidatePairs exhaustive correctness regression: PASS
- candidatePairs benchmark: PASS (environment-dependent timing; see build history)
- Bucket-heavy benchmark: PASS — 5k / 12,497,500 pairs; streaming ScanPipeline path avoids materializing candidate-pair vectors
- Last completed v0.9.2.63 Windows Qt6 GUI runtime: VERIFIED on Windows (MediaSimilarityFinder.exe --version exit 0 with Qt6\plugins deployed; windowed run not exercised in that validation session)
- NVIDIA CUDA runtime: VERIFIED on RTX 3080 Ti (Toolkit 13.4, sm_86; cuda_backend_test green, kernels unchanged)

See docs/build-history/0.9.4.0.ko.md and .en.md for the current development changes. Official baseline: docs/build-history/0.9.2.32.ko.md and .en.md.
