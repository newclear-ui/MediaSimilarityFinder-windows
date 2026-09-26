# Build Status

- Current development version: **0.9.4.10**
- Official preserved baseline: 0.9.2.32 — Large-result Match streaming and report retention bounds
- Windows CPU: 0.9.2.35–0.9.2.39 — VS18 2026 build, UI rewrite rounds
- CUDA validation: 0.9.2.40 — RTX 3080 Ti, Toolkit 13.4, 38/38 PASS
- Current: 0.9.4.10 — Node C2 Initial Calibration (engine 1.5.0, video cache v9)
- Active Node C substep: C2 (C3 not started)
- Last completed Windows build/test line: **0.9.4.10**
- Last completed v0.9.4.10 build: Core + GUI Release build PASS (CPU and GPU trees)
- Last completed v0.9.4.10 test run: **CTest 67/67 PASS (GPU)**
- Last completed v0.9.4.10 validation: calibration_test PASS on both trees (CPU-only completion, reload Exact, partial, GPU policy/off, telemetry, real GPU measurement); scheduler precedence live > profile; `--version` 0.9.4.10
- CPU-only validation: Release build PASS; **CTest 64/64 PASS**; CUDA disabled and CPU fallback verified
- GUI execution evidence split (automation vs interactive): offscreen `--smoke` PASS on both trees; real windowed launch PASS (OS window handle + version title observed, process terminated cleanly); slot-path workflow automated PASS (`scan_workflow_test`); automated validation of human operation NOT_VALIDATED (environment limit) — development lead user-reports direct confirmation of run → search → report display in a real Windows session
- Windows build scripts: default `-BuildParallelism 1` avoids vcpkg `z-applocal` output-copy races; higher parallelism remains opt-in
- Build entry points: `scripts/build_windows_gpu.ps1` (canonical, clean `build-windows-gpu` tree) and `scripts/build_windows_cpu.ps1`; `scripts/build_windows_cuda.ps1` remains a deprecated alias path
- candidatePairs exhaustive correctness regression: PASS
- candidatePairs benchmark: PASS (environment-dependent timing; see build history)
- Bucket-heavy benchmark: PASS — 5k / 12,497,500 pairs; streaming ScanPipeline path avoids materializing candidate-pair vectors
- Last completed v0.9.2.63 Windows Qt6 GUI runtime: VERIFIED on Windows (MediaSimilarityFinder.exe --version exit 0 with Qt6\plugins deployed; windowed run not exercised in that validation session)
- NVIDIA CUDA runtime: VERIFIED on RTX 3080 Ti (Toolkit 13.4, sm_86; cuda_backend_test green, kernels unchanged)

See docs/build-history/0.9.4.8.ko.md and .en.md for the current development changes. Official baseline: docs/build-history/0.9.2.32.ko.md and .en.md.
