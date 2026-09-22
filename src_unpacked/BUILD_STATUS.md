# Build Status

- Current development version: **0.9.2.34**
- Official preserved baseline: 0.9.2.32 — Large-result Match streaming and report retention bounds
- Windows CPU test interim: 0.9.2.33-test (not a baseline, folded into 0.9.2.34)
- Current: 0.9.2.34 — Windows common compatibility alignment (VS18 2026 baseline, CUDA unchanged)
- Core Release build: PASS
- CTest: **36/36 PASS**
- candidatePairs exhaustive correctness regression: PASS
- candidatePairs benchmark: PASS (environment-dependent timing; see build history)
- Bucket-heavy benchmark: PASS — 5k / 12,497,500 pairs; streaming ScanPipeline path avoids materializing candidate-pair vectors
- Windows Qt6 GUI runtime: not verified in current Linux environment
- NVIDIA CUDA runtime: not verified on physical NVIDIA hardware in current environment

See `docs/build-history/0.9.2.34.ko.md` and `.en.md` for details. Official baseline: `docs/build-history/0.9.2.32.ko.md` and `.en.md`.
