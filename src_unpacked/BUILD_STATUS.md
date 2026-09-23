# Build Status

- Current development version: **0.9.2.36**
- Official preserved baseline: 0.9.2.32 — Large-result Match streaming and report retention bounds
- Windows common alignment: 0.9.2.34 — VS18 2026 baseline, CUDA unchanged
- Windows CPU hardening: 0.9.2.35 — VS18 2026 build, 36/36 PASS, GUI runtime verified
- Current: 0.9.2.36 — Explorer-style UI rewrite (grid+list, KO/EN, live streaming)
- Core Release build: PASS
- CTest: **36/36 PASS**
- candidatePairs exhaustive correctness regression: PASS
- candidatePairs benchmark: PASS (environment-dependent timing; see build history)
- Bucket-heavy benchmark: PASS — 5k / 12,497,500 pairs; streaming ScanPipeline path avoids materializing candidate-pair vectors
- Windows Qt6 GUI runtime: VERIFIED on Windows (MediaSimilarityFinder.exe --version exit 0 with Qt6\plugins deployed; windowed run not exercised in this session)
- NVIDIA CUDA runtime: not verified (no CUDA toolkit/GPU in current environment)

See `docs/build-history/0.9.2.35.ko.md` and `.en.md` for details. Official baseline: `docs/build-history/0.9.2.32.ko.md` and `.en.md`.
