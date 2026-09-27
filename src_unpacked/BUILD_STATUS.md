# Build Status

- Current development version: **0.9.4.22**
- Official preserved baseline: 0.9.2.32 — Large-result Match streaming and report retention bounds
- Windows CPU: 0.9.2.35–0.9.2.39 — VS18 2026 build, UI rewrite rounds
- CUDA validation: 0.9.2.40 — RTX 3080 Ti, Toolkit 13.4, 38/38 PASS
- Current: **0.9.4.22 — D8b Scaled Dataset / Walker Queue Evidence (measurement only, no product change)**
- Active node: **D measured and closed on evidence → Node I (Analyze / Matching) opened; D9a pre-register committed (`0fc3344`), implementation PENDING**
- Planned next version: **0.9.4.23 — D9a Analyze Internal Observability (measurement only)**
- Current version stays 0.9.4.22 because a version represents a *validated* code state, and no code has changed yet
- Last completed Windows build/test line: **0.9.4.22**
- Last completed v0.9.4.22 build: Core + GUI Release build PASS (CPU and GPU trees)
- Last completed v0.9.4.22 test run: **CTest 71/71 PASS (GPU)**
- CPU-only validation: Release build PASS; **CTest 70/70 PASS**; CUDA disabled and CPU fallback verified
- D8b scope note: **no product code change** — generator scale (`-Scale full`) + stage-breakdown reporting in the review probe only
- Standard dataset: `test_sample_img_vid/` (generated, not committed) — fingerprint `9b1138489827804b24bdfb645e4b72c5a3ab3fa79b8b8ba1b2d5cd051614253c`, 2700 files, 36,007,560 bytes, 95 dirs, fingerprintVersion 1

### D8b measured findings (RTX 3080 Ti, 3 runs, cold index each)

| Item | D8a (60 files) | D8b (2700 files) |
| --- | --- | --- |
| walker maxDepth | 1 | **964 mean / 1076 max** (23.5 % of capacity) |
| walker blocked_ticks | 0 | **0** (capacity never bound) |
| walker starved_ticks | 0 | 0 |
| gpu_batch share of engine wall | 0.34 % | **0.026 %** |

Stage breakdown of engine wall (109,737 ms): `analyze` **98.62 %**,
walk 1.40 %, image stage 0.59 %, gpu batch 0.04 %.
**D3+D4 addressable ceiling: 0.044 %.** The real bottleneck is the final
matching/grouping stage, which Node D does not own.

- GUI execution evidence split (automation vs interactive): offscreen `--smoke` PASS on both trees; real windowed launch PASS (OS window handle + version title observed, process terminated cleanly); slot-path workflow automated PASS (`scan_workflow_test`); automated validation of human operation NOT_VALIDATED (environment limit) — development lead user-reports direct confirmation of run → search → report display in a real Windows session
- Windows build scripts: default `-BuildParallelism 1` avoids vcpkg `z-applocal` output-copy races; higher parallelism remains opt-in
- Build entry points: `scripts/build_windows_gpu.ps1` (canonical, clean `build-windows-gpu` tree) and `scripts/build_windows_cpu.ps1`; `scripts/build_windows_cuda.ps1` remains a deprecated alias path
- candidatePairs exhaustive correctness regression: PASS
- candidatePairs benchmark: PASS (environment-dependent timing; see build history)
- Bucket-heavy benchmark: PASS — 5k / 12,497,500 pairs; streaming ScanPipeline path avoids materializing candidate-pair vectors
- Last completed v0.9.2.63 Windows Qt6 GUI runtime: VERIFIED on Windows (MediaSimilarityFinder.exe --version exit 0 with Qt6\plugins deployed; windowed run not exercised in that validation session)
- NVIDIA CUDA runtime: VERIFIED on RTX 3080 Ti (Toolkit 13.4, sm_86; cuda_backend_test green, kernels unchanged)

See docs/build-history/0.9.4.14.ko.md and .en.md for the current development changes. Official baseline: docs/build-history/0.9.2.32.ko.md and .en.md.

- **GPT Fix (v0.9.4.13):** source changes committed with validation honestly marked pending (no PASS claimed); 0.9.4.14 establishes the missing validation.


## Node D0 / D1a status

- D0 baseline frozen at v0.9.4.14.
- v0.9.2.32 remains untouched.
- v0.9.4.15 is the first Node D1a implementation line.
- D1a is limited to the existing imageBatch synchronous boundary and existing BenchmarkRecorder telemetry.
- Scheduler share policy, Profile/Calibration, CUDA backend, video, similarity, DB/cache, and queue topology are unchanged.
- Transfer timing remains not_measured until backend-internal hooks exist; hashBatch elapsed time is not labeled as transfer time.
- v0.9.4.15 build/test validation is PENDING.
