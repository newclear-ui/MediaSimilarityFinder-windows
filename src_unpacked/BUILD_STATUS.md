# Build Status

- Current development version: **0.9.4.25**
- Official preserved baseline: 0.9.2.32 — Large-result Match streaming and report retention bounds
- Windows CPU: 0.9.2.35–0.9.2.39 — VS18 2026 build, UI rewrite rounds
- CUDA validation: 0.9.2.40 — RTX 3080 Ti, Toolkit 13.4, 38/38 PASS
- Current: **0.9.4.25 — D9b Candidate B (expensive verify cost reduction): NOT ACCEPTED, parity held but no cost reduction**
- Active node: **I / D9a PASS; D9b Candidate B failed on evidence; Candidate A still deferred**
- Last completed Windows build/test line: **0.9.4.25**
- Last completed v0.9.4.25 build: Core + GUI Release build PASS (CPU and GPU trees)
- Last completed v0.9.4.25 test run: **CTest 78/78 PASS (GPU)**
- Last completed v0.9.4.25 validation: `verify_parity_test` 25 checks (optimized path double-identical to the preserved pre-D9b reference); CPU 77/77, GPU 78/78; counters all unchanged vs 0.9.4.24 D9a
- CPU-only validation: Release build PASS; **CTest 77/77 PASS**; CUDA disabled and CPU fallback verified
- D9b scope note: **no optimization accepted.** `centerCropResize` 8→6 calls and aspect-buffer reuse only; verdict semantics, candidate semantics, thresholds, SSIM formula, crop semantics, DB/cache/schema all unchanged
- Source backup zip: **3 kept, max 3, oldest recycled.** `backup/MediaSimilarityFinder-v<version>-src.zip`, built by `scripts/backup_src.ps1` with `git archive` only so the contents are byte-identical to GitHub. Current: `MediaSimilarityFinder-v0.9.4.25-src.zip` (0.95 MB, commit `cff1aae`, 514 files verified byte-identical). The rule is a session- and agent-independent long-term item in AGENTS.md (root item 2, `src_unpacked` item 4)

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
