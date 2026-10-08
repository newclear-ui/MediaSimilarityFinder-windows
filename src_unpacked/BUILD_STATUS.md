# Build Status

- Current development version: **0.9.4.74** — merged audit, telemetry-exit fix, progress and scan auto-resume
- Official preserved baseline: 0.9.2.32 — Large-result Match streaming and report retention bounds
- Windows CPU: 0.9.2.35–0.9.2.39 — VS18 2026 build, UI rewrite rounds
- CUDA validation: 0.9.2.40 — RTX 3080 Ti, Toolkit 13.4, 38/38 PASS
- Current (0.9.4.74): **merged audit and telemetry-exit fix.** A deterministic walker-exception test reproduced the user's `0xC0000409`; its symbolized test stack showed `TelemetryRecorder::~TelemetryRecorder` destroying a joinable sampler thread after scan unwind. RAII stop/join plus `ScanWorker::abortTelemetry()` in both catch handlers fixes that abort path. Also: walker/consumer exception-safe join, count-phase progress/logging, producer-side `readProgress` separated from consumer `walked`, stale listing reset, restart scheduled before failure UI, and one-shot replay of saved `BackendScanConfig` after READY. The user's original .72 dump has no matching PDB, so exact caller identity remains unproven; .74 emits PDBs. Engine 1.5.0 / DB 1.0.4 / benchmark schema 9 / cache format 9 unchanged. Existing quickHash I/O policy and stdout backpressure risk were not changed. See `docs/build-history/0.9.4.74.*`
- Historical (0.9.4.29): **D3 duplicate `decode` / `decodePreserveAspect` cost measurement (measurement-only, PASS). The verify-miss path decodes the same file twice, 12,962 calls each per scan, and the second call costs 0.8568 ms/call = 49.60 % of `verifyDecodeMs`, 39.19 % of `verifyMs`, 38.47 % of `analyzeMs`; the split identity holds exactly (`decodeSplitOverMs = 0.000000`). The second decode is NOT redundant work: `f` is fixed 32x32 and `a` is aspect-preserving and the scoring plan consumes both, so removal would be an accuracy regression. D2 conclusions unchanged**
- Current: **0.9.4.30 — Automatic CPU-usage 10–90% normalization. The user-visible CPU control, stored `ResourcePolicy.cpuPercent`, and engine input always match; presets, GPU policy, scheduler, worker, and GPU-batch formulas are unchanged**
- Current: **0.9.4.35 - EXIF Orientation query path correction (production defect fix). The product queried /app1/ifd/exif/{ushort=274}, which WIC rejects with WINCODEC_ERR_BADPROPERTYKEY, so rotation was never applied anywhere. Fixed with one shared helper used by all three call sites (fixed fingerprint, aspect fingerprint, display color): tries /app1/ifd/{ushort=274} for JPEG then /ifd/{ushort=274} for TIFF and returns on the first VT_UI2 value. No container branch, no XMP fallback, no new framework. Gate A PASS: fixtures 1-8 read, values match, baseline applied 7/7 (orientation 1 is the identity), 90/270 fixtures really change source size 209x248 to 248x209. Gate B PASS: f/a byte parity 8/8. Gate C PASS: exhaustive 5,579,470 pairs unchanged (457,126 groups, 0 verdict diffs). Dataset EXIF re-measured: 8 files carry metadata, all orientation 1, so the corpus holds no rotated image; those 7 TIFF files prove the /ifd/ path works. Cost of the fix measured at 0.02870 ms per file, and a JPEG does not pay it because the helper returns early. EXIF regression added to --selfcheck and registered in CTest: 11 to 16 checks, CPU 80/80 to 81/81, GPU 81/81 to 82/82. **Gate D PASS: I-2 = READY FOR PRODUCTION IMPLEMENTATION, but production integration is NOT performed and adoption remains NO.**
- Active node: **process split P1–P4 complete; 0.9.4.74 is the merged scan-progress/crash-response correction on top of 0.9.4.73.** Next is real-Windows acceptance (new-file progress, auto-resume after restart, preview/selection, Tiles/ListMode, large-dataset traversal), then final Search/Index/Comparison acceptance. GPU MAX share boost remains a documented gap. Sparse production remains blocked by `ExactnessPolicy::RefuseAll`; F-1 CONDITIONAL with NVDEC production adoption NO. Historical Node I/D line: D9a PASS; D9b NOT ACCEPTED; D9c PASS; D9d PASS; D1 PASS (root cause); D2 PASS (Path C DEFERRED); D3 PASS; I-1 shared GrayImage candidate DEFERRED; I-2 shared WIC source candidate passed all gates after the EXIF correction (production adopted in 0.9.4.36)
- Performance experiment records: every tuning / profiling / benchmark / bottleneck investigation is preserved **regardless of outcome**, including refuted hypotheses, measurements, run conditions, refutation reasons and future revisit conditions. Detailed numbers go in the version Build History; `docs/worklog/0.9.4.*.md` carries the **Performance / Tuning Experiment Index** linking the lineage (D9a BASELINE → D9b NOT ACCEPTED → D9c PASS → D9d PASS → D1 PASS → D2 PASS with Path C DEFERRED → D3 PASS) with every surviving, deferred and refuted candidate. `NOT ACCEPTED` / `REJECTED` / `DEFERRED` / `LOW PRIORITY` are current-condition verdicts, not permanent retirements, and are never deleted. Refuted by measurement so far: cache capacity 32, scoring-path duplication, `WICBitmapInterpolationModeFant`, EXIF skip, cache mutex contention, the Factory2 fallback, and raw OS file-open handling. Deferred with explicit revisit conditions: candidate A, and candidate C (`CreateDecoderFromStream` + `HandleStream`). Open follow-up from D3: produce `f` and `a` from a single decode. See `src_unpacked/AGENTS.md` item 9 and `docs/document-naming.*.md` section 2-1.
- D3 scope note: **measurement-only, no optimization, no production behavior change.** `verifyBuffersFor` now hands `decode()` and `decodePreserveAspect()` one telemetry accumulator each and folds them back into the combined D9d `decode` member via `mergeDecodeTelemetry`, so every D9d key keeps its exact previous meaning. `schemaVersion` stays 9. Engine 1.5.0 / DB 1.0.3 / cache v9 unchanged. SSIM, threshold, candidate, grouping, verdict, CPU-fallback and CUDA semantics all unchanged. Groups 156,211 across 5 measured runs (the D9a 156,152 differs only because D2 added TIFF 14 + ICO 12 to the dataset)
- D3 two defects recorded permanently: the pre-register's claim that `calls`/`aspectCalls` were also merged was wrong (only the timers were), and a missing member-by-member telemetry transfer in `ScanPipeline::analyze` made the first run read a silent 0 until the two new fields were transferred. See `docs/build-history/0.9.4.29.*.md` sections 5.1.1 and 11
- Last completed Windows build/test line: **0.9.4.74**
- Last completed v0.9.4.74 build: Core + GUI + Backend Release build PASS on both CPU and GPU trees
- Last completed v0.9.4.74 test run: **CTest 116/116 PASS (CPU)**, **117/117 PASS (GPU)**
- Last completed v0.9.4.74 validation: all GUI/Backend exes `--version` 0.9.4.74; GUI/Backend PDBs present on both trees; `scan_streaming_test` walker-exception seam PASS; `backend_e2e_test` automatic resume after forced kill PASS; portable `--smoke` PASS; source zip 780 files byte-identical to GitHub (0 mismatches); portable zip 86 entries with GUI + Backend exe + turbojpeg
- Historical v0.9.4.32 validation: `--version` 0.9.4.32 both trees exit 0; `--smoke` both trees exit 0; `verify_parity_test` 25 checks; `benchmark_schema_test` 29 checks `additive-read-confirmed`; `analyze_telemetry_test` 71 checks; `msf_shared_decode_probe --selfcheck` 14 checks; `--flips` forensic mode, decomp_mismatch=0
- v0.9.4.30 scope note: **user-policy consistency only, no scheduler or performance change.** `normalize_user_cpu_percent()` is the single 10–90 rule for the user-visible CPU value and Custom `ResourcePolicy.cpuPercent`; presets, `gpuPercent`, GPU ON/OFF, Adaptive Scheduler, `recommended_worker_count()`, and `recommended_gpu_batch_size()` are unchanged. The CPU setting has no QSettings persistence path, so no settings migration was needed
- Last completed v0.9.4.28 validation: `--version` 0.9.4.28; `verify_parity_test` 25 checks; `ui_translation_keys_test` 21 keys; `benchmark_schema_test` 29 checks `additive-read-confirmed`; `analyze_telemetry_test` 41 checks; D2 per-format A/B/C probe reporting HRESULT plus identical success/failure across all three paths
- D2 scope note: **measurement-only, no optimization, no production change.** `ImageDecoder::probeDecoderPaths` is a static helper reachable only from `msf_dataset_baseline`; the production `CreateDecoderFromFilename` call sites are untouched and `HandleStream` is wired into no product path. `schemaVersion` stays 9. Engine 1.5.0 / DB 1.0.3 / cache v9 unchanged. SSIM, threshold, candidate, grouping, verdict, cache, decode, resize, crop, mirror, CPU-fallback and CUDA semantics all unchanged
- D2 dataset note: TIFF 14 and ICO 12 were added to `images/format` from the MIT-licensed `image-rs/image` test fixtures, with source URLs recorded in `test_sample_img_vid/images/format/SOURCES.md`. Result: 3,347 files / 102,475,315 bytes, fingerprint `e8f8fa6ab0257f1e2b7d839b73ec14b1cc726695dac2efe79b55a13a13e2640a`. Because the dataset changed, D2 absolute totals are **not** directly comparable to D1 — only paired per-call and per-format results are
- D1 and D2 must not be compared directly: D1's `CreateDecoderFromFilename ≈ 2.6257 ms/call` includes the one-time codec DLL load, while D2 measured a warm state (~0.07–0.12 ms). The gap is a measurement-condition difference, **not** a performance improvement
- CPU-only validation: Release build PASS; **CTest 80/80 PASS**; CUDA disabled and CPU fallback verified
- D1 scope note: **root-cause measurement only, no optimization.** `createWicFactory` now records the previously discarded Factory2 HRESULT and counts attempts/successes/fallbacks; `probeOsFileOpen` times a plain `CreateFileW`+`CloseHandle` on the same path as a reference, excluded from `openMs`; open HRESULT failures recorded. The 3 `CoCreateInstance` sites are one helper with identical activation order and fallback condition. `schemaVersion` stays 9. Engine 1.5.0 / DB 1.0.3 / cache v9 unchanged. SSIM, threshold, candidate, grouping, verdict, cache, decode, resize, crop, mirror, CPU-fallback, CUDA semantics all unchanged

### D1 measured result — the two questions answered

```text
Factory2 attempts      25,898
Factory2 successes     25,898   100.00 %
Factory2 fallbacks          0     0.00 %
factory2 attemptMs    17,769.0 ms  0.6861 ms/attempt
fallback attemptMs          0.0 ms
split == factoryMs -> over 0.0000
```

`CLSID_WICImagingFactory2` succeeds on every call, so the fallback is dead code
in practice and the measured factory cost is one activation, not two. The
"wasted first attempt" hypothesis is refuted. The fallback stays as a portability
guard.

```text
CreateDecoderFromFilename  68,000.8 ms  2.6257 ms/call
OS CreateFileW reference    1,490.5 ms  0.0576 ms/call
WIC-specific remainder     66,510.3 ms  2.5682 ms/call  = 97.8 % of open
open HRESULT failures              0
```

The OS cost of opening the file is 2.2 % of `open`; 97.8 % is WIC's own decoder
construction. "file open" was a misleading name for this cost.

Candidates **deleted** by measurement: removing the Factory2 fallback (0
occurrences) and file handle/stream management (OS 2.2 %).

5 runs: mean 119,608.3 / median 120,346.2 / min 116,839.9 / max 121,273.4 /
range 4,433.5 ms (3.68 % of median). The `factory` difference vs D9d
(31,674.5 -> 17,769.0 ms) is a measurement difference from a double-counting bug
that this build fixed, recorded as neither regression nor improvement.

A pre-register stop condition earned its place here: the first implementation
reported `factoryMs = 235,023,895 ms` because the shared helper accumulates into
running totals and the call site re-added them each call. The
"split must reconstruct factoryMs" check caught it immediately.


### D9d measured result — what the decode region is made of

25,898 decode calls (12,949 `decode` + 12,949 `decodePreserveAspect`), 4.4654 ms
per call, `decodeTotalMs` 115,628.4:

| Sub-stage | Total ms | Share | ms/call |
|---|---:|---:|---:|
| **open** (`CreateDecoderFromFilename`) | 71,865.2 | **62.15 %** | 2.7749 |
| **factory** (WIC factory `CoCreateInstance`) | 31,674.5 | **27.39 %** | 1.2230 |
| copy (`CopyPixels`, contains the real decode) | 5,956.9 | 5.15 % | 0.2300 |
| comInit | 492.7 | 0.43 % | 0.0190 |
| resize (Fant) | 369.0 | 0.32 % | 0.0142 |
| convert | 311.7 | 0.27 % | 0.0120 |
| metadata (EXIF) | 194.0 | 0.17 % | 0.0075 |
| orient | 15.4 | 0.01 % | 0.0006 |
| **total** | **115,628.4** | 100 % | **4.4654** |

`open + factory` = 89.55 % of decode = **84.9 % of the whole expensive verify**.
WIC succeeded 25,898 times, 0 PGM fallbacks, 0 failures, 0 orientations applied.

Refuted with measurements, not left as opinions: `WICBitmapInterpolationModeFant`
0.32 %, EXIF skip 0.17 %, and the actual decompression itself 5.15 %.

### D9d cache mutex — not a bottleneck

```text
acquires 40,417   (27,468 lookups + 12,949 stores)
waitMs   8.4      0.0002 ms/acquire   wait share 10.38 %
holdMs  72.4      0.0018 ms/acquire   hold share 89.62 %
```

Total lock time is 80.8 ms, i.e. **0.07 % of decode**. Wait is never summed with
hold, because the verdict depends on their ratio. This closes the question D9c
could not answer and confirms D9c's `other` bucket (0.42 %) contains no hidden
contention.

`decodeSubSumMs` 110,879.4 vs `decodeTotalMs` 115,628.4 — the 4.1 % gap is
unmeasured code between the instrumented calls (allocation, `GetSize`, HRESULT
checks), reported rather than attributed to a stage. Read the stage shares, not
the absolute milliseconds: this is an instrumented build.


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

- Source + compiled backup zips: **kept in the repository-root `backup/` folder, max 3 per kind, oldest recycled.** Current set (0.9.4.74): Source `MediaSimilarityFinder-v0.9.4.74-src.zip` (2.37 MB, 780 files verified byte-identical to GitHub, 0 mismatches); Compiled `MediaSimilarityFinder-v0.9.4.74-Portable-Windows-x64.zip` (43.84 MB, 86 entries, GUI + Backend exe + turbojpeg present, `--smoke` PASS). Previous kind members: `.72`/`.73` src and portable; `.71` src and portable recycled to the Recycle Bin on 0.9.4.74. `package_portable.ps1` writes portable zips straight to `backup/`; both kinds rotate independently at 3. Session- and agent-independent long-term rule in AGENTS.md (root item 2, `src_unpacked` item 4)

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
