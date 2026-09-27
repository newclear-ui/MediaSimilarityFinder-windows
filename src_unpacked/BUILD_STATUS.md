# Build Status

- Current development version: **0.9.4.26**
- Official preserved baseline: 0.9.2.32 — Large-result Match streaming and report retention bounds
- Windows CPU: 0.9.2.35–0.9.2.39 — VS18 2026 build, UI rewrite rounds
- CUDA validation: 0.9.2.40 — RTX 3080 Ti, Toolkit 13.4, 38/38 PASS
- Current: **0.9.4.26 — D9c expensive verify internal cost accounting (instrumentation build). decode is 94.90 % of the 8.564 ms expensive verify; frame_ssim is 0.55 %**
- Active node: **I / D9a PASS; D9b Candidate B NOT ACCEPTED; D9c PASS (measurement). Next: Candidate D (decode), which must first decompose the 4.31 ms decode**
- Last completed Windows build/test line: **0.9.4.26**
- Last completed v0.9.4.26 build: Core + GUI Release build PASS (CPU and GPU trees)
- Last completed v0.9.4.26 test run: **CTest 78/78 PASS (GPU)**
- Last completed v0.9.4.26 validation: `verify_parity_test` 25 checks (optimized path still double-identical to the preserved pre-D9b reference) + new D9c `verify_instrumentation` assertions (stage sum <= total, instrumented == uninstrumented, stage counters match the known per-call structure); CPU 77/77, GPU 78/78; every existing counter and groups 156,152 identical to 0.9.4.24 D9a; `--smoke` exit 0; `--version` 0.9.4.26
- CPU-only validation: Release build PASS; **CTest 77/77 PASS**; CUDA disabled and CPU fallback verified
- D9c scope note: **instrumentation only, no optimization.** 8 exclusive stage timings + 7 stage counters in `AnalyzeTelemetry`; timers placed outside the calls they measure; `frame_ssim` internals and decode internals deliberately left unmeasured. SSIM, window, precision, threshold, candidate, grouping, verdict, cache, decode, resize, crop, mirror, CPU-fallback, and CUDA semantics all unchanged. Engine 1.5.0 / DB 1.0.3 / cache v9 / schema v9 unchanged

### D9c measured result — where the 8.564 ms goes

| Stage | Total ms | Share | ms / expensive verify |
|---|---:|---:|---:|
| **decode** | 111,621.7 | **94.90 %** | **8.1274** |
| key (2 stat + 64 KiB quick-hash read) | 4,421.9 | 3.76 % | 0.3220 |
| other (remainder) | 481.4 | 0.41 % | 0.0351 |
| **frame_ssim** | 645.2 | **0.55 %** | **0.0470** |
| crop / aspect (crop + resize together) | 201.9 | 0.17 % | 0.0147 |
| mirror flip | 188.7 | 0.16 % | 0.0137 |
| cache store | 42.9 | 0.04 % | 0.0031 |
| cache copy (hits) | 11.0 | 0.01 % | 0.0008 |
| **total** | **117,614.6** | 100 % | **8.564** |

`sum == verifyMs` exactly (overflow 0.0000 ms). Dataset fingerprint
`9b113848…4253c` (2,700 files, 36,007,560 bytes), cold index, 3 runs, AMD Ryzen 7
5800X3D / RTX 3080 Ti. Because this is an instrumented build, read the numbers
as a relative distribution, not a production performance claim.

This is the evidence for why D9b could not have succeeded: the whole scoring
path it optimized is 0.55 % of the cost, and decode is roughly 173x
`frame_ssim`.

Counters also expose two structural facts: each cache miss decodes the same
file **twice** (`verifyDecodes` 25,898 = 2 x 12,949, because `decode` and
`decodePreserveAspect` are both called on the same path), and cache hits still
rebuild the 64 KiB quick-hash key, which is 27,468 reads and 363.3 MB across the
run.

**D9b remains NOT ACCEPTED** and is not reclassified as a success.

- Source + compiled backup zips: **kept in the repository-root `backup/` folder, max 3 per kind, oldest recycled.** Source: `MediaSimilarityFinder-v0.9.4.25-src.zip` (0.96 MB, 529 files verified byte-identical to GitHub). Compiled: `MediaSimilarityFinder-v0.9.4.19-Portable-Windows-x64.zip` and `-v0.9.4.20-...` (42.54 MB each, 85 entries, exe present) relocated here from src_unpacked/. `package_portable.ps1` now writes portable zips straight to `backup/`; both kinds rotate independently at 3. Session- and agent-independent long-term rule in AGENTS.md (root item 2, `src_unpacked` item 4)

### D9b measured result — Candidate B NOT ACCEPTED

| Metric | 0.9.4.24 D9a | 0.9.4.25 D9b | Verdict |
|---|---:|---:|---|
| verifyCalls | 158,020 | 158,020 | identical |
| expensive verifies | 13,734 | 13,734 | identical |
| verifyDecodeMisses | 12,949 | 12,949 | identical |
| verifyCacheHits | 14,519 | 14,519 | identical |
| verifyHitRate | 0.528579 | 0.528579 | identical |
| ssimEvals | 137,340 | 137,340 | identical |
| frameSsimEvals | 274,680 | 274,680 | identical |
| groups | 156,152 | 156,152 | identical |
| total verify ms | 109,142 / 117,291 | 110,717 | no reduction |
| ms / verify call | 0.69 ~ 0.74 | 0.7007 | unchanged |

**Not one counter changed** — verdict, grouping, and parity are all preserved,
and simultaneously this shows the optimized target was not the dominant cost.

Cause: `frame_ssim` runs 4,096 inner-loop iterations per call (20 calls =
81,920 ops) and that is essentially the whole cost. D9b cut
`centerCropResize` from 8,192 to 6,144 pixels, i.e. **2.3 % of the work**.
The duplication the hypothesis pointed at did not exist — the original
already computed each aspect buffer once outside the loop. D9a's own run-to-run
variation (7.5 %) is the same magnitude as the D9b observation, so it sits
inside measurement noise.

**Candidate A (candidate arrival reduction) remains deferred.** This failure
does not raise its relative priority, because what A touches is verdict semantics.


### D9a measured findings (RTX 3080 Ti, D8b dataset `9b113848…4253c`, cold index)

Analyze internal split (of 109,501 ms analyze):

| Stage | ms | Share | State |
| --- | --- | --- | --- |
| index | 3.2 | 0.00 % | measured |
| scan | 350.8 | 0.32 % | measured |
| **verify** | **109,142.3** | **99.67 %** | measured |
| video | 0.0 | 0.00 % | **not_measured** (no video in dataset) |

- `verifyCalls` 158,020 · `verifyDecodeMisses` 12,949 · `verifyCacheHits` 14,519
- **`verifyHitRate` 0.5286 — the "cache capacity 32 is the cause" hypothesis is REFUTED**
- `msPerVerifyCall` 0.69–0.74 ms · `ssimEvals` 137,340 · `frameSsimEvals` 274,680
- Internal consistency: `ssimEvals/10 == (misses+hits)/2 == 13,734` exactly
- Actual dominant structure: 158,020 calls reach the gate; ~91.3 % short-circuit
  at kFast, ~8.7 % (13,734) run decode+SSIM at ~8.5 ms each → 117 s. **The
  bottleneck is candidate-pair volume reaching the gate, not per-call cost.**
- Parity: `groups` 156,152 identical to 0.9.4.22; instrumented vs uninstrumented
  `verifyImagePair` returns a double-identical value
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

See docs/build-history/0.9.4.24.ko.md and .en.md for the current 0.9.4.24 implementation changes. Official baseline: docs/build-history/0.9.2.32.ko.md and .en.md.

- Historical note: v0.9.4.13 was a GPT Fix and v0.9.4.14 established the missing validation.


## Node D0 / D1a status

- D0 baseline frozen at v0.9.4.14.
- v0.9.2.32 remains untouched.
- v0.9.4.15 is the first Node D1a implementation line.
- D1a is limited to the existing imageBatch synchronous boundary and existing BenchmarkRecorder telemetry.
- Scheduler share policy, Profile/Calibration, CUDA backend, video, similarity, DB/cache, and queue topology are unchanged.
- Transfer timing remains not_measured until backend-internal hooks exist; hashBatch elapsed time is not labeled as transfer time.
- v0.9.4.15 build/test validation is PENDING.
