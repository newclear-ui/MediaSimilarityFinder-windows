# Build History / English

This directory records MediaSimilarityFinder release history and important architecture decisions.

- [Korean](README.ko.md)
- Ordinary changes are summarized briefly in their version log.
- Important architecture changes record **why the change was needed → previous structure → new structure → resolved state → validation → future impact**.
- Replaced implementations are preserved as non-build snapshots under `docs/architecture/legacy/`. Dead code is not left commented out in the active source tree.

## Version list

| Version | Summary |
|---|---|
| 0.9.1.0 | CPU baseline |
| 0.9.2.0~0.9.2.3 | NVIDIA CUDA/GPU foundation |
| 0.9.2.4~0.9.2.7 | Portable Index / SQLite / CandidateIndex foundation |
| 0.9.2.8 | Real-time Monitor foundation |
| 0.9.2.9 | Windows ReadDirectoryChangesW event monitoring |
| 0.9.2.10 | Live comparison-index synchronization |
| 0.9.2.11 | Condition-variable scheduler / exponential backoff |
| 0.9.2.12 | Resident CandidateIndex / root ownership / event resync / Windows path hardening |
| 0.9.2.13 | Foreground-workload protection load gate |
| 0.9.2.14 | MonitorStatus / telemetry |
| 0.9.2.15 | Manual Pause/Resume |
| 0.9.2.16 | Monitor Settings GUI |
| 0.9.2.17 | Mirror-aware Similarity |
| 0.9.2.18 | Mirror CandidateIndex optimization |
| 0.9.2.19~0.9.2.20 | Image Crop-Aware candidate/comparison pipeline |
| 0.9.2.21~0.9.2.22 | Video Temporal Crop-Aware candidate/comparison integration |
| 0.9.2.23~0.9.2.24 | Video decode / Monitor stability optimization |
| 0.9.2.25 | SQLite/Index performance and migration hardening |
| 0.9.2.26 | CandidateIndex 9-part Multi-Index Hash replacement |
| 0.9.2.27 | Video fingerprint persistent cache versioning/SQLite reuse hardening |
| 0.9.2.28 | CandidateIndex candidatePairs large-scale candidate generation optimization |
| 0.9.2.29 | ScanPipeline streaming candidate consumption and complete-index short-circuit |
| 0.9.2.30 | Scan match callback delivery and duplicate result-storage removal |
| 0.9.2.31 | Large-result Match streaming / report retention bounds |
| 0.9.2.32 | Compact index-based large-result Match streaming callback |
| 0.9.2.33-test | Windows CPU test interim (not a baseline, folded into 0.9.2.34) |
| 0.9.2.34 | Windows common compatibility alignment (VS18 2026 baseline, CUDA unchanged) |
| 0.9.2.35 | Windows CPU build/test hardening (36/36 PASS, GUI runtime verified) |
| 0.9.2.36 | Explorer-style UI rewrite (grid+list switch, KO/EN, marks, live streaming) |
| 0.9.2.37 | UI feedback round (in-settings language, 6 group views, keyboard mark, splitter save) |
| 0.9.2.38 | Unified 3-pane UI + video tab/ignore list/throughput (37 tests PASS) |
| 0.9.2.39 | Screenshot feedback round (toolbar cleanup, folder tab removal) |
| 0.9.2.40 | CUDA first-hardware validation (RTX 3080 Ti, 38 tests PASS, kernels untouched) |
| 0.9.2.41 | Non-ASCII (Korean) filename crash fix + unicode_path_test (39 tests PASS) |
| 0.9.2.42 | Search feel fixes (walk progress, merged buttons, state save) |
| 0.9.2.43 | Post-stop state restore + pause scope |
| 0.9.2.44 | Large-scan live verification + streaming switch |
| 0.9.2.45 | Scan activity log (0% CPU triage) |
| 0.9.2.46 | Scan pipelining (walk ‖ analyze) |
| 0.9.2.47 | Checkpointed scans (interruption keeps progress) |
| 0.9.2.48 | No console window + splitter warning fix |
| 0.9.2.49 | Selective scan + live matching + multicore |
| 0.9.2.50 | Match persistence (quick load) + preview WIC fallback |
| 0.9.2.51 | Video scene-changes search + GPU status visibility + cache v4 + duration gate |
| 0.9.2.52 | Incremental match checkpointing during scan (resume guarantee) |
| 0.9.2.53 | Preview-toggle crash fix + Explorer-style Tiles view |
| 0.9.2.54 | Folder h-scroll, XL default, UI-state restore, progress throttle, High mode, multicore |
| 0.9.2.55 | Window-layout non-restore root-cause fix (void QSettings) |
| 0.9.2.56 | Scan-time UI freeze fix (thumbnail decode budget) |
| 0.9.2.57 | Unstoppable final-analyze fix + recent-folders favorites |
| 0.9.2.58 | Blank group pane fix (placeholder icon storm) |
| 0.9.2.59 | Blank pane recurrence fix (list-rebuild livelock) |
| 0.9.2.60 | Icon/list view restore + favorites dedup + color previews |
| 0.9.2.61 | Uniform preview sizes + wider shell lane |
| 0.9.2.62 | Similarity-inspired additions (Pairs, ETR, thumbnail skip list) |
| 0.9.2.63 | Disk thumbnail cache + favorites/filename/resolution/0B fixes |
| 0.9.2.64 | Direct stop delivery + video diagnostic counters |
| 0.9.2.65 | Video L1 multi-anchor + variant dedup fix |
| 0.9.2.66 | E2E video regression + EXIF rotation + cleanups |
| 0.9.2.67 | L3 SSIM verification cells + thumb48 cache v5 |
| 0.9.2.68 | QuickLook integration resume |
| 0.9.2.69 | Toolbar cleanup + live intermediate results + summary/count fixes |
| 0.9.2.70 | Image false-positive second-stage gate |
| 0.9.2.71 | Engine version stamp + stored-match revalidation |
| 0.9.2.72 | Engine/DB version split (semver) |
| 0.9.2.73 | Accepted audit items (SSIM gate, QuickLook, docs) |
| 0.9.2.74 | Monitor toolbar cleanup (pause removed, toggle highlight, menu right) |
| 0.9.2.75 | Toolbar, detail, and summary UI round |
| 0.9.2.76 | Console-flash removal + spawn-free resolutions |
| 0.9.2.77 | Button style unification + detail/grid intrusion fixes |
| 0.9.2.78 | Fixed pre-count and selection-filter alignment |
| 0.9.2.79 | Reuse existing Explorer window + three GPU states |
| 0.9.2.80 | Reference tie-break (similarity first, then resolution/size) |
| 0.9.2.81 | Single-tree revalidation + GUI test plugin path |
| 0.9.2.82 | Portable launch fix (plugin layout + bundled CRT) |
| 0.9.2.83 | Startup self-check (platform plugin pre-flight) |
| 0.9.2.84 | Build output runs out of the box (in-build Qt plugin deploy) |
| 0.9.2.85 | Self-check path bug fix (GetModuleFileNameW) |
| 0.9.2.86 | Fast pHash, reveal fallback, ffprobe DLLs |
| 0.9.2.87 | CUDA kernel 1e-7 snap (CPU/GPU flat-image agreement) |
| 0.9.2.88 | Reveal root fix (CLSCTX_ALL) |
| 0.9.2.89 | Right-click selection (reveal misfire fix) |
| 0.9.2.90 | External report follow-ups (sampling, verify, index, 2nd stage) |
| 0.9.3.1 | Search benchmark log (JSON, renumbered from 0.9.2.91) |
| 0.9.3.2 | Benchmark toggle (temporary, remove in 1.0) |
| 0.9.3.3 | Progressive group thumbnails |
| 0.9.3.4 | Always-on benchmark log, log button, scroll fix |
| 0.9.3.5 | Engine thumbnail reuse + scroll jump fix |
| 0.9.3.6 | Thumbnail DB contention fix + stall fills |
| 0.9.3.7 | Engine lookup before budget gate + video unit fix |
| 0.9.3.8 | Thumbnail catch-up scheduler after scan completion |
| 0.9.3.9 | Search freshness, video fingerprint, benchmark, and monitor hardening |
| 0.9.3.10 | Group-list scroll anchor restoration |
| 0.9.3.11 | Video, image, and thumbnail cache quick-identity validation |
| 0.9.3.12 | Explorer reuse, cache freshness, and group-identity follow-ups |
| 0.9.3.13 | First-stage CUDA batch acceleration for video pHash |
| 0.9.3.14 | CUDA row-batch acceleration for video MSSIM |
| 0.9.3.15 | Degenerate-hash false-positive fix, report-wait UI, video GPU activity wiring |
| 0.9.3.16 | Crop-only verdict split, stop responsiveness, color previews |
| 0.9.3.17 | Single-sweep video decode, duration fallback |
| 0.9.3.18 | Benchmark disk I/O, duration direct fallback, popup readability |
| 0.9.3.19 | PDH disk counter fix, integrated prompt review |
| 0.9.4.0 | Node A foundation: GPU terminology/abstraction, GPU ON/OFF UI, build naming, benchmark instrumentation |
| 0.9.4.1 | MainWindow scan-workflow regression test (offscreen slot-path driver, modal closer, UI CPU/GPU parity) |
| 0.9.4.2 | Node B1 Minimal Adaptive Allocation (CpuGpuScheduler, proportional shares, re-evaluation cadence, scheduler telemetry) |
| 0.9.4.3 | Node B2 Runtime Throughput Feedback (ThroughputWindow, observed-ratio shares, effective capacities) |
| 0.9.4.4 | Node B3 Live Load Awareness (system-load headroom, external-load throttle, D1-reserved queue fields) |
| 0.9.4.5 | Node B4 Stability Control (SMA smoothing, kill-band hysteresis, minimum hold) |
| 0.9.4.6 | Node B5 Transfer / Workload Cost (total-cost rule, transfer term, workload via observed rates) |
| 0.9.4.7 | Node B6 Resource Mode Integration (per-mode floors/hold/kill band, Manual == Balanced) |
| 0.9.4.8 | Execution binding + Node B7 gate (fresh published reads, engine scheduler assertion, coverage audit) |
| 0.9.4.9 | Node C1 Profile Foundation (PerformanceProfile, INI store, verdicts, initial-estimate delivery) |
| 0.9.4.10 | Node C2 Initial Calibration (bounded probes, writer, telemetry, precedence) |
| 0.9.4.11 | Node C3 Opportunistic Recalibration (deviation trigger, candidate check, confidence steps) |
| 0.9.4.12 | Node C4 Calibration Gate (17-combination evidence, usability helper, gate close) |

See the version-specific `.ko.md` / `.en.md` files for details.

## Architecture documents

- `../architecture/realtime-monitor.ko.md` / `.en.md`
- `../architecture/candidate-index.ko.md` / `.en.md`
- `../architecture/reference-similarity.ko.md` / `.en.md`
- `../architecture/runtime-audit-0.9.2.63.ko.md` / `.en.md`
- `../architecture/storage-design.md`

## Legacy

Previous implementation/design snapshots are preserved under `../architecture/legacy/`.

| 0.9.4.13 | Node C4.1 Calibration Lifecycle Fix — GPU OFF→ON metric-gap, 30-day stale policy, CPU/GPU identity completion — **GPT Fix** |
| 0.9.4.14 | C4.1 build recovery (kDefaultMaxAgeDays declaration site, msf:: prefix in test) |

- v0.9.4.15 — Node D1a Image-Path Observability (implementation complete; validation PASS confirmed in 0.9.4.16)
- v0.9.4.16 — D1a gate: schema v3 restoration, version-string completion, full-suite validation
- v0.9.4.17 — D1b gate: walker-queue and video-range observability, schema v4
- v0.9.4.18 — D2 barrier review: video completion-order join, maxRangeFileMs, schema v5
- v0.9.4.19 — D3-Minimal: bounded walker queue, backpressure, cancel/pause safety, schema v6
- v0.9.4.20 — D4a: CUDA backend internal timing (h2d/kernel/d2h device split + host wait), schema v7
- v0.9.4.21 — D8a: reproducible dataset + dataset fingerprint in benchmark JSON, schema v8
- v0.9.4.22 — D8b: scaled dataset (2700 files) + walker queue / stage breakdown evidence; D4b and Full D3 deferred on measurement
- v0.9.4.23 — D9a: analyze internal stage split + verify/SSIM counters, schema v9; "cache capacity 32" hypothesis refuted by measurement
- v0.9.4.24 — QSettings organization renamed to `MediaSimilarityFinder-ui` with a safe one-time legacy settings migration
- v0.9.4.25 — D9b Candidate B: **REJECTED.** centerCropResize 8→6 + aspect buffer reuse. 25 parity checks double-identical and every counter identical to D9a, but no cost reduction (frame_ssim dominates)
- v0.9.4.26 — D9c: expensive verify internal cost accounting (instrumentation). **PASS.** decode 94.90% / frame_ssim 0.55% of the 8.564 ms verify; parity held, groups 156,152
- v0.9.4.27 — D1: open / factory root-cause measurement. **PASS.** Factory2 succeeded on all 25,898 calls (0 fallbacks), OS `CreateFileW` 0.0576 ms, and 97.8% of `open` is WIC-internal
- v0.9.4.28 — D2: comparison of the three WIC decoder entry paths (measurement-only). **PASS.** `CreateDecoderFromStream` is the shortest at the decoder step for all 7 formats (16–30% below A). Not adopted into the product — **Path C = `DEFERRED`.** Three real bugs found and recorded during implementation (handle lifetime / wrong stream connection / COM lifetime). Added TIFF 14 + ICO 12 to the dataset (3,347 files, `e8f8fa6a…e2640a`)
- v0.9.4.29 — D3: duplicate decode / decodePreserveAspect cost measurement (measurement-only). **PASS.** 12,962 calls each per verify miss, and the second decode is **49.60 %** of verifyDecodeMs, 39.19 % of verifyMs and 38.47 % of analyzeMs. But `f` (fixed 32x32) and `a` (aspect-preserving) are different geometries and the scoring plan consumes both, so "remove the second decode" would be an accuracy regression. The follow-up candidate is **producing both from a single decode**. Split identity `decodeSplitOverMs = 0.000000`
- v0.9.4.30 — Automatic CPU-usage 10–90% normalization. The displayed value and `ResourcePolicy.cpuPercent` always match; presets, GPU policy, scheduler, and worker calculations are unchanged. CPU 80/80, GPU 81/81 PASS.
- v0.9.4.31 — D3 follow-up shared-decode candidate measurement (measurement-only). **Measurement PASS.** At R∈{128,192,256,384,512} the probe candidate cost 23.8–45.9 % below baseline (not a product-performance figure), but byte reproduction fails and 2 verdict flips were reported at R≥384 (same TIFF near-duplicate pairs, reproduced 5×) — **candidate `DEFERRED`**, not adopted. (→ **corrected in 0.9.4.32**: those 2 flips and the "delta grows with R (2.63→9.19)" trend are void as measurement artifacts of the probe's baseline/candidate buffer mixing. The original record is preserved, not deleted.)
- v0.9.4.32 — D3 follow-up candidate stability / resampling-divergence cause investigation. **investigation PASS / candidate stays `DEFERRED` / production adoption NO.** Re-measurement with pure baseline/candidate pairing gives **0 verdict flips at all 8 investigated R** (128·192·256·288·320·352·384·512), max abs delta **1.972205–2.557407** (not monotonic in R). The f/a pixel divergence originates in the two-step Fant chain + intermediate 8-bit GrayImage quantization + differing resampling chains; the scoring-stage `centerCropResize` (integer nearest-neighbor) only carries that difference into the crops and is not its origin. DEFERRED reasons are byte parity not maintained, geometry differences, real pixel divergence, no full-scan groups measurement, and no EXIF/PGM fixtures — not the flip. Verdict comparison was performed on 1691 sampled pairs per resolution; a full-scan groups comparison was **not** performed. Correction record: `docs/build-history/0.9.4.32.en.md` §2.
- v0.9.4.33 — I-2 candidate: **shared WIC source/frame + two independent scalers** (measurement-only). **exactness PASS / production adoption NO.** No intermediate `GrayImage` is created, so each output's resampling chain is identical to the baseline's. Whole dataset: **f geometry 849/849, f byte 849/849, a geometry 849/849, a byte 849/849** — **zero** geometry or pixel mismatches, the decisive difference from I-1. Baseline and candidate fail on the same 5 files (`WINCODEC_ERR_FRAMEMISSING`); **0 files fail only in the candidate**. Probe cost **40.8–42.6 % lower** (CPU 5 runs + GPU 5 runs, identical parity in every run). The second `CopyPixels` drops from 3.88 to 1.09 ms on average, confirming WIC does share the real decode. **EXIF end-to-end verification is `not_measured`** and no full-scan groups comparison was performed. Details: `docs/build-history/0.9.4.33.en.md`
- v0.9.4.34 — I-2 verification completion. **Candidate = `READY FOR PRODUCTION IMPLEMENTATION REVIEW` / implementation NOT performed.** (1) The 849 files above were a **probe corpus**, not the standard dataset; corrected by re-measuring over the **entire 3,347-file standard dataset** — `both_success=3341`, `baseline_only_fail=0`, `candidate_only_fail=0`, `both_fail=6`, f/a geometry and pixel **3,341/3,341**. (2) Two EXIF diagnostic defects corrected (a `char*`→`LPCWSTR` cast producing false negatives, and a counter that never incremented); afterwards the **EXIF fixtures are 8/8 valid** with values matching 1–8. (3) **Incidental finding: the product query path `/app1/ifd/exif/{ushort=274}` is rejected by WIC with `BADPROPERTYKEY`** — a product defect unrelated to I-2, left unfixed. (4) **Exhaustive full-scan groups: 5,579,470 pairs, 0 verdict diffs**, max score difference 0.000000000. (5) With rotation forced, shared source vs fully independent pipelines are **7/7 byte identical**. (6) Telemetry `metaMs`/`orientMs` split. Details: `docs/build-history/0.9.4.34.en.md`
- v0.9.4.35 — EXIF Orientation query path correction (production). **Gate A/B/C/D all PASS.** The product used `/app1/ifd/exif/{ushort=274}`, which WIC rejects with `BADPROPERTYKEY`, so rotation was never applied anywhere. Fixed with one shared helper across all three call sites (fixed fingerprint, aspect fingerprint, display color): try `/app1/ifd/` (JPEG) then `/ifd/` (TIFF) and return on the first value. No container branch, no XMP fallback, no new framework. Fixtures 1–8 correct, EXIF parity f/a 8/8, **exhaustive 5,579,470-pair group result unchanged**. The dataset's 8 EXIF files are all orientation 1, so it holds no rotated image; those 7 TIFF files prove the `/ifd/` path works. Cost of the fix 0.02870 ms per file, not paid by JPEG. **EXIF regression test selfcheck 11→16, CTest 80/80→81/81 and 81/81→82/82.** **I-2 = `READY FOR PRODUCTION IMPLEMENTATION`; integration is the next step.** Details: `docs/build-history/0.9.4.35.en.md`
- v0.9.4.36 — I-2 Shared WIC Source **production integration**. **`PRODUCTION ADOPTION = YES`.** The two duplicated bodies were unified into a single `decodeWicBranches` source of truth, and the only production path that requests both results, `verifyBuffersFor` (`image_verify.cpp:82-83`), now uses the new `decodeBoth()`. Factory, decoder, frame, metadata, orientation source and `GetSize` went from twice per file to once, with **no intermediate `GrayImage`** (each output keeps the same one-step Fant chain). `decode()`/`decodePreserveAspect()` keep their signatures and semantics, so the `media_pipeline` and `monitor` single-output callers are unchanged. On failure the existing PGM fallbacks are called unchanged, so the failure path stays byte-identical. Telemetry assigns each bucket once, preserving the D3 invariants (`calls=1`, `aspectCalls=1`, the two `totalMs` summing to the call time) with no benchmark JSON schema change. **A new `--production` mode compares the real production entry point over the whole dataset**: `both_success=3341`, `base_only_fail=0`, `cand_only_fail=0`, `both_fail=6`, fixed/aspect geometry and pixel **3,341/3,341 diff_px=0**, zero telemetry violations. **`--production-groups` makes the exhaustive sweep use the production path as its candidate**: 5,579,470 pairs, both at 457,126 groups, **verdict diffs 0, max score diff 0.000000000, group parity identical**. EXIF fixtures 8/8, and 7/7 byte-identical shared-vs-independent under rotation. **CPU CTest 81/81, GPU 82/82, selfcheck 16 checks.** **Production measurement improved in all 5 runs — baseline 0.481800 ms → decodeBoth 0.304900 ms, a 36.72 % reduction** (the actual product path, not the probe). Details: `docs/build-history/0.9.4.36.en.md`
- v0.9.4.37 — **I-3 Production Full-Scan End-to-End Validation** (measurement-only, no production code change). **`I-3 END-TO-END VALIDATION = PASS` / `I NODE = COMPLETE` / `I-1 = DEFERRED` / `I-2 = PRODUCTION` / `NEXT = E`.** The driver was the already-existing `msf_dataset_baseline` → `MediaSearchEngine::scan()` (the same `scan_pipeline` + `image_verify` path as the GUI). All required telemetry already existed, so **no telemetry and no code change was needed**. The baseline was built in a separate `git worktree` reusing the existing `vcpkg_installed` (working tree untouched, no 2.2 GB dependency rebuild). Dataset unchanged (fingerprint `e8f8fa6a..e2640a`). **Condition A (cold process = decode-active, BCBCBCBCBC interleaved)**: CPU 10 paired → full scan **-10.17 %** (9/10), GPU 5 paired → **-11.13 %** (5/5, baseline and candidate ranges completely non-overlapping), decode stage **-22.3…22.4 %**. **Condition B (warm / repeat scan)**: GPU 12 rows -26.73 % (faster in 24/24), but an unexplained process-level slowdown makes both versions 30–60 % slower from the second scan onward, so it is not used as the headline. **Consistency**: decode share 50.7 % × 22.3 % = 11.3 % ≈ observed 11.13 %. **Decomposition: 28–30 % of the saving is removal of the duplicated, measurement-only D1 reference probe** (not product work); the genuine gain is WIC decoder creation 25,924→12,962 calls, while `copy` (actual pixel decode) moved only -95 ms. Correctness: all 39 executions identical at `groups=156211`, `misses=12962`, `hits=14524`; exactness 3,341/3,341 `diff_px=0`; EXIF 8/8; exhaustive 5,579,470 pairs unchanged; CPU 81/81, GPU 82/82. Details: `docs/build-history/0.9.4.37.en.md`
- v0.9.4.38 — **E-1 Adaptive Video Decode Planner research / measurement / design**. **Gates A–F all PASS. No production code changed.** Code survey: FFmpeg 9.0.1; every FFmpeg symbol sits in one 182-line `video_decoder.cpp`; hwaccel (dxva2/d3d11va/d3d12va) is available yet there are **zero** hardware symbols; sampling is a **duration-only** 6-bucket ladder; there is **one seek then a sequential sweep**; there is **no GOP/keyframe awareness**; `VideoInfo::fps` is read nowhere; and the telemetry has **no fields for requested sample frames, seek count/latency, GOP cost or decode throughput**, while `videos.decodedFrames` is not a measured count. A measurement-only probe was added and verified **byte identical to the real `framesAt96Plus32` on 8/8 files**. **Headline: 12,573 frames decoded to produce 140 samples = 89.81× waste** (192.3× for a single 300 s 25 fps file), with decode at **97.57 %** of the sweep. **H1**: the ratio is not an independent variable but `fps × interval(duration)` (6.48 % error). **H2**: per-frame cost is set by codec × resolution (19.2 % spread across the same combination despite 5× fps and 60× duration; 640x360→1080p is 9.0× pixels → 9.2× cost, linear). **Planner inputs reduced 18 → 5** (duration, fps, resolution, codec, GOP), the other 13 excluded as derived, unevaluable, degenerate, or forbidden to guess. **Most important reframing: this waste is a sampling-strategy problem rather than a hardware-decode problem, so E-2 has value without hardware.** Unresolved — a trustworthy GOP source (`AV_PKT_FLAG_KEY` is inaccurate for ffv1, only 2 data points), HEVC/AV1/VP9 are ungeneratable because the bundled FFmpeg has no software encoder (recorded as a constraint), and the 42–97 % sparse-seek saving is **arithmetic, not measurement**. CPU CTest 82/82, GPU 83/83, probe selfcheck 24 checks. Details: `docs/build-history/0.9.4.38.en.md`
- v0.9.4.39 — **E-2A Adaptive Sampling Strategy measurement** (measurement-only, no production code change). **Verdict `CONDITIONAL` — the measurement succeeded but exactness is broken, so no production integration.** 8 real-content files (including HEVC, 4K, portrait and a 270.5 s feature, trimmed with `-c copy`) + 5 controlled-GOP synthetics (gop5/15/60/250 + intra-only) + **a correction to the E-1 record: HEVC/AV1 real content does exist, and AV1 turns out to be not merely ungeneratable but entirely not decodable** in this build (`Function not implemented`). **Performance: sparse seek wins in most cases** (decoded 17,164 → 6,203 = 2.77x, elapsed **-41.7 %**, instrumentation overhead of 0.2–0.4 % separated and confirmed negligible). Best case is the 270.5 s real h264 file at **30.1 s → 0.31 s (99.0 %)**, waste 231.9x → 1.40x. **Adversarial conditions were also found: GOP250 is -136.8 % and a real GOP225 file -132.2 %, where sparse is slower than baseline and the waste ratio actually worsens** (46.9x → 109.6x). The E-1 arithmetic model `GOP/2 < wasteRatio` **matches 11 of 13**. **Exactness: pixel parity 4/14 FAIL** with maxAbs 31–188 of 255, i.e. genuinely different frames. The cause is that the product predicate `ft+0.05>=target` permits a frame **earlier** than the target (0.05 s = 1.5 frame periods at 30 fps), so the sequential sweep can pick one frame early while sparse seek cannot reach back before its seek landing point — **a structural difference of the strategy, not a decoder defect**. **GOP is now measured twice** (packet key flag plus decoded `pict_type` I-frames), giving Known 12 / Estimated 2 / Unavailable 0 and surfacing a real 11-packet disagreement on the 4K file. CPU CTest 83/83, GPU 84/84, probe selfcheck 20 checks. Details: `docs/build-history/0.9.4.39.en.md`
- v0.9.4.40 — **E-2B Exact Sparse Seek + Adaptive Sampling Planner**. **Verdict `CONDITIONAL` / `PRODUCTION ADOPTION = NO` / production default remains Sequential. No production code changed.** It follows the §1 order, exactness first. **Key fix: the seek target moves from `target` to `target - 0.05`**, so the landing keyframe satisfies `K' <= target-0.05` and the frame the product picks (`pts >= target-0.05`) always lies at or after the landing point, letting the identical predicate select the identical frame. `pixel parity 4/14 → 13/14`, files with tsLater 10 → 1. The production predicate, tolerance and target are unchanged (selfcheck asserts 0.05). The cost of exactness is also measured: sparse decoded 6,203→8,612 (+38.8 %), reduction 41.7 %→37.0 %. **The one remaining failure** (HEVC 1080p, tsLater 2) is explained by the §8 decoder-state-after-seek check, whose necessary condition is `firstDecodedPts <= seekRequestPts`, and **the violation fires on exactly the file whose pixel parity failed** — **detected, not resolved**. **The Adaptive Sampling Planner is implemented** (classify-only / execute-only separation, hard fallbacks evaluated before any cost comparison): SequentialPreferred 8 / SparseSeekCandidate 5 / SparseSeekUnavailable 1, **avoiding all 7 unfavourable or inexact cases** while capturing 5 of 8 favourable ones and conservatively missing 3. AV1→Unavailable, 4K(GopEstimated)→Sequential, HEVC(landing violation)→Sequential. The 4K GOP mismatch of 11 is suggestive of VFR plus an edit list but undetermined, so it is handled conservatively as `GopEstimated` with no new parser. CPU CTest 83/83, GPU 84/84, probe selfcheck 29 checks automating every mandatory fallback. Details: `docs/build-history/0.9.4.40.en.md`
- v0.9.4.41 — **E-3A HEVC Seek Landing Characterization**. **Verdict `PASS` / `HEVC_EXACT_SPARSE_SEEK = NOT VERIFIED` (exit 3) / `HEVC = SequentialPreferred` confirmed final. No production code changed.** **Order followed**: brief committed alone (`f7b7f3b`, no code) → probe change → measurement. All six strategies (A `av_seek_frame` BACKWARD, B `avformat_seek_file` at three windows, C `AVSEEK_FLAG_ANY`, D `avformat_flush`) were **NOT EXACT on HEVC 1080p**, and the two that genuinely seek keyframes, A and D, show the same viol=2 as v0.9.4.40. **`AVSEEK_FLAG_ANY` does not fix the problem, it worsens it** (HEVC 2→14, H.264 270 s 0→33), validating the directive's §7 diagnostic-only framing. **`avformat_flush` is identical to A across all 8 files**, so it is not the cause. The only safe primitive is `av_seek_frame(..., BACKWARD)`. HEVC 1360x808 is EXACT, so neither "always impossible" nor "always possible" holds. The narrow B windows and C also **break H.264 exactness** and are rejected. **This is not an E failure but a safe fallback choice.** Two real defects were fixed: ① a **verdict logic bug** that aggregated all files and wrongly reported `VERIFIED`, letting H.264 smoke mask the HEVC failure ② a use-after-free crash (0xC0000005). No planner code changed. CPU CTest 83/83, GPU 84/84, selfcheck 39 checks (29 preserved + 10 added). Details: `docs/build-history/0.9.4.41.en.md`
 - v0.9.4.42 — **E-3B Adaptive Sampling Planner Calibration + End-to-End Validation**. **Verdict `NOT ACCEPTED` / `SPARSE_SEEK_PRODUCTION_ADOPTION = NO` / `EXACTNESS = DISPROVEN` / production default remains Sequential. The sparse path is now unreachable in production.** **Order followed**: brief committed alone (`b3ee35e`) → implementation → measurement. **Central finding: the E-2A/E-2B "exact" verdicts were self-referential.** Both experiments compared one seek-based implementation against *another* seek-based implementation, and both call `av_seek_frame` + `avcodec_flush_buffers`, so they **shared the same decoder reference-state loss** and agreed for the wrong reason. E-3B is the **first measurement that included the production from-zero sweep**, and there one 4K H.264 file **genuinely diverged** (with `reference count overflow` / `no frame!` / `concealing` in the log): the seek path reconstructs frames the from-zero sweep does not. In the same runs sparse was also **16.77 % slower end to end** (4K decode dominates), so **the performance argument is gone too**. **Three corrections made**: ① the executor **returned a truncated result as success** (`!out32.empty()`) → added a sample-count contract that discards it ② container-index GOP was reported as `Known` (measured index ≈109 vs decoded I-frames ≈120) → downgraded to `Estimated` ③ a `0.5 × framesPerSample` threshold I had added is a brief-forbidden simple threshold and contradicts the calibration data → removed, leaving only the `sampleCount × GOP/2 < totalFrames` comparison. **Outcome**: `ExactnessPolicy::RefuseAll` as the default — with no codec holding a production-parity proof, every file is Sequential. `AllowVerified` is a one-line change reserved for revisiting. A/B/C **13/13 bit-identical**, adaptive **-0.02 %** (neutral). CPU CTest 84/84, selfcheck 31 checks. Details: `docs/build-history/0.9.4.42.en.md`

---

## Node E current status (after v0.9.4.42 — distinct from the experiment record)

The v0.9.4.42 entry above is the **experimental record as of that point** and is preserved
as-is. What follows is the **current Node state**.

- **Node E: closed.** It was closed on the basis of E-3B.
- **Production sequential decode is the production baseline.**
- **Sparse production adoption is not performed.**
- **`ExactnessPolicy::RefuseAll` is retained.** The sparse production path is unreachable.
- **E-4** is **not carried out as a separate sparse integration stage and is absorbed into
  Node E closure**, because the sparse production path does not exist. E-4 must not be
  interpreted as reintroducing sparse.
- **E-3C** is not a separate roadmap Stage and is not migrated to F as a work item. It is
  included in Node E closure. Its possible relevance to F architecture is kept as a note
  only.
- **Further sparse adoption does not proceed until new production-parity evidence is
  obtained.**
- E-1-GOP / E-2B-4K **keep their `INCONCLUSIVE` experimental result** and are treated as
  `DEFERRED`, because the production-adoption judgement target has ended. Neither is
  promoted to PASS.

Related: `docs/development-roadmap.*` (Node E completion/closure conditions),
`docs/development-progress.*`, `docs/build-history/0.9.4.42.*`,
`docs/implementation-briefs/E-planner-calibration.*`,
`docs/implementation-briefs/F-hardware-video-decode-backend.*`
 - v0.9.4.43 — **F-1 Random-Access Safety Contract + NVDEC Exactness Preflight**. **Verdict `CONDITIONAL` / `PRODUCTION ADOPTION = NO` / F-2 production integration forbidden. Gate A PASS · B FAIL · C PASS · D PASS (measured) · E PASS.** **Core reversal: "starting at an IDR is safe" is false.** **13 of 14 dataset files are IDR-start** (key=1 at index 0), yet **1360x0808 mismatched 20/20** and **still mismatched 20/20 when restarted from a confirmed IDR (ss 2.0)**. 1080x1920 also mismatched 1/20. So **mid-GOP is only one failure mode**, and NVDEC exactness holds neither at 4K nor merely because a stream is IDR-start. The contract therefore makes **`exactnessVerified` a necessary condition for `Safe`**: structure is necessary but not sufficient. `Unsafe`/`Unknown` are collapsed onto CPU with no optimistic branch. **1360x0808 characterisation (INCIDENT-F1-1)**: 98.6 % bytes differ, maxAbs 185, persists with both paths at nv12 (so not format conversion), **chroma worse than luma**, height 808 is **not 16-aligned** (chroma height 404). Root cause is **not confirmed (INCONCLUSIVE)**; the parser prohibition rules out direct SPS/PPS parsing. **Performance preflight on a confirmed exact-condition file**: CPU software 0.112 s/100f · **4.438 s**/870f vs NVDEC+transfer 0.337 s/100f · **9.397 s**/870f → **NVDEC is 2.1× slower per frame** (marginal CPU 5.62 vs NVDEC 11.77 ms/frame), so this is not a fixed-overhead story. Production samples only ~12 frames so real transfer may be cheaper, but with sparse decode unavailable the full-decode structure leaves **no performance case for NVDEC**. **Additional fixture required**: no 4K IDR-start H.264 exists, and it cannot be told whether 1360x808 is a structural or a size effect — not generated here. Changes are limited to `tests/f_random_access_safety_test.cpp` (probe only, not wired to production) plus a version bump. CPU CTest 85/85, GPU CTest 86/86, F-1 selfcheck 20 checks. Details: `docs/build-history/0.9.4.43.en.md`
  - v0.9.4.44 — **S4 follow-up GUI cleanup integration regression baseline**. Integrates the S4 semantic reset (`0a7955a`), GUI benchmark plumbing removal, strategy dropdown and toolbar cleanup (`2db7aca`, `c0c5ad6`-`cf126c4`), and status doc corrections (`028c5ef`, `cd62ea5`). The only source change in this version is the 1-line version bump. CPU CTest 100/100, GPU CTest 101/101, `--version` shows 0.9.4.44, CPU/GPU `--smoke` exit=0. S4 verification in progress (not CLOSED), S5 product benchmark DEFERRED. Details: `docs/build-history/0.9.4.44.en.md`
  - v0.9.4.45 — **XMP Orientation Fallback production path**. Standalone contract (`I-xmp-orientation-fallback.*`); EXIF contract untouched. EXIF VT_UI2 1..8 wins, otherwise XMP `tiff:Orientation` tried (measured as VT_LPWSTR). Fixtures A-G pass (14 checks). CPU CTest 102/102, GPU CTest 103/103. Standard-dataset XMP coverage 0 (recorded separately). Details: `docs/build-history/0.9.4.45.en.md`
  - v0.9.4.45 (follow-up) — **`--version` redirection misdetection + CUDA C4819 removal**. `attachParentConsole()`'s `streamIsRedirected()` reported unusable streams (`fd<0`, invalid handle, `FILE_TYPE_UNKNOWN`) as redirected, which removed the `AttachConsole` path and made `--version` print nothing from PowerShell (`gui/main.cpp`). Forwarded `/utf-8` to the CUDA host compiler (`-Xcompiler=/utf-8`), so C4819 is now 0. No version bump. CPU CTest 102/102, GPU CTest 103/103. Details: `docs/build-history/0.9.4.45.en.md`
  - v0.9.4.46 — **DEFECT-A + DEFECT-B fix (analysis-failure state model introduced)**. `fingerprint==0` expressed both "analysis not completed (retry needed)" and "analysis failed (no retry needed)", so a decode-failing file was reported as a successful index entry and then repeated as `modified` on every later scan. Both defects were fixed as **one state transition**. Added the `files.analysis_failed` column (DB 1.0.3 → 1.0.4), moved `added`/`modified` to analysis outcome time, added a `failed` counter, and corrected telemetry `pending`. Regression test `analysis_failure_state_test` 28 checks (CPU and GPU). Unchanged measured `candidates`/`groups` prove normal Search/Index/Comparison semantics are preserved. Engine verdict version stays 1.5.0. CPU CTest 103/103, GPU CTest 104/104. Details: `docs/build-history/0.9.4.46.en.md`
