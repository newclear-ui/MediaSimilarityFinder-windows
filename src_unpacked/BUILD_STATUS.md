# Build Status

- Current development version: **0.9.2.41**
- Official preserved baseline: 0.9.2.32 — Large-result Match streaming and report retention bounds
- Windows CPU: 0.9.2.35–0.9.2.39 — VS18 2026 build, UI rewrite rounds
- CUDA validation: 0.9.2.40 — RTX 3080 Ti, Toolkit 13.4, 38/38 PASS
- Current: 0.9.2.62 — Similarity-inspired additions (Pairs column, ETR, thumbnail skip list)
- Core Release build: PASS
- CTest: **48/48 PASS** (CUDA build incl. `cuda_backend_test`, `unicode_path_test`, `match_store_test`, `video_reencode_test`, `color_thumb_test`, `scan_streaming_test`, `view_mode_probe`, `thumb_layout_test`, `ui_settings_write/verify`)
- candidatePairs exhaustive correctness regression: PASS
- candidatePairs benchmark: PASS (environment-dependent timing; see build history)
- Bucket-heavy benchmark: PASS — 5k / 12,497,500 pairs; streaming ScanPipeline path avoids materializing candidate-pair vectors
- Windows Qt6 GUI runtime: VERIFIED on Windows (MediaSimilarityFinder.exe --version exit 0 with Qt6\plugins deployed; windowed run not exercised in this session)
- NVIDIA CUDA runtime: VERIFIED on RTX 3080 Ti (Toolkit 13.4, sm_86; `cuda_backend_test` green, kernels unchanged)

See `docs/build-history/0.9.2.35.ko.md` and `.en.md` for details. Official baseline: `docs/build-history/0.9.2.32.ko.md` and `.en.md`.
