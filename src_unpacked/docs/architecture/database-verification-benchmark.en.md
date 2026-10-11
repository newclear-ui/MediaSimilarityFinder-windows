# Database / Pair-Verification Benchmark (measurement record)

> This document is the measurement record for **pair-verification speedups (V-1..V-7)**. Plan, conditions, raw values and verdicts live here;
> the DB structure and the planned improvements live in `docs/architecture/database-structure.en.md`.
> Experiment lineage and decisions connect through the **Performance / Tuning Experiment Index** in `docs/worklog/0.9.4.ko.md`.
> Values that were not measured are written as `Not measured`; never filled by estimate.

## 0. Status

- Current status: **DBV-1 / DBV-2 measured (0.9.4.92) / DBV-3..7 pending**
- Target improvements: V-1..V-7 (`database-structure` section 7)
- Baseline: the 0.9.4.91 user-run log (section 2) + the 0.9.4.92 standard-dataset before/after (section 7)

## 1. Experiment definition

| Field | Content |
|---|---|
| Experiment ID | DBV-1 (V-1: remove the quickHash read from the verify cache key) |
| Version | (recorded at measurement) |
| Date | (recorded at measurement) |
| Purpose | Measure the effect of removing the 64 KiB file read (verifyKeyMs) from the verify cache key |
| Hypothesis | Building the key from the DB-stored quickHash (or just (size,modified)) removes verifyKeyMs and keeps verdicts unchanged |
| Baseline | 0.9.4.91: verifyKeyMs = 170 s (summed), ≈13% of verify |
| Target | verifyKeyMs → 0 (zero file reads), verdict parity 100% |
| Implementation | (recorded at implementation) |
| Dataset | user dataset `G:\Downloads\ss_twit` (or the standard dataset) |
| Dataset fingerprint | (recorded at measurement) |
| Hardware | RTX 3080 Ti / (CPU, RAM recorded) |
| OS | (recorded at measurement) |
| Build configuration | CPU Release + GPU(CUDA) Release |
| Run count | ≥3 (record median/min/max) |
| Raw measured values | Not measured |
| Mean / Median / Min / Max / Range | Not measured |
| Derived ratios | Not measured |
| Accuracy result | Not measured |
| Verdict parity (CPU/GPU) | Not measured |
| Compatibility impact | Not measured |
| Performance result | Not measured |
| Status | **PLANNED** |
| Reason | — |
| Known limitation | — |
| Future revisit condition | — |
| Related experiments | DBV-2..7, D9c/D9d (decode breakdown) |

## 2. Baseline (0.9.4.91 log, 177,372 files / 154,041 images analyzed)

Worker-summed values (note: `src/scan_pipeline.cpp:477` — parallel verify times are summed across workers).

| Item | Value |
|---|---|
| verifyMs | 540,758 ms |
| verifyCalls | 19,221 |
| verifyDecodeMisses / verifyCacheHits | 13,453 / 1,353 (hit rate 9.1%) |
| verifyKeyMs | 169,877 ms |
| verifyDecodeMs | 1,095,446 ms (full 756,290 + aspect 339,114) |
| verifyFrameSsimMs | 476.6 ms |
| verifyFlipMs / verifyCropMs | 187.1 / 151.7 ms |
| verifySumMs / verifyBreakdownOverMs | 1,266,314 / 725,556 ms |
| msPerVerifyCall | 28.134 ms |
| meanDecodeMs (index) | 83.18 ms |

Ratios (summed): decode 86.5% · key 13.4% · SSIM 0.04% · flip/crop 0.03%.

## 3. Experiment plan (per V)

Duplicate the section-1 table for each V. Common rules:
- Verdict parity: the changed build must produce identical pair/group counts and per-pair percent as before (`image_verify_test`, `verify_parity_test`).
- If GPU SSIM (V-7) is included, verify CPU/GPU numeric parity.
- Regression: full CTest CPU/GPU, user-dataset rescan.

### DBV-1 (V-1) remove the quickHash read from the cache key
- Status: **PASS (0.9.4.92)** — key reduced to `(path,size,modified)` (zero file reads).
- Result: `verifyQuickHashReads`/`Bytes` 27,486 / 364.3 MB → 0 / 0, `key` time 5,453.8 → 3,386.6 ms (-37.9%).
- Verdict parity: groups 156,211 identical.

### DBV-2 (V-2) enlarge the verify buffer cache
- Status: **PASS (0.9.4.92)** — `kVerifyCacheMax` 32 → 1024.
- Result: hit rate 50.4% → 96.3%, `verifyDecodes` 27,240 → 1,028 (-96.2%), `decode` 13,596.4 → 931.6 ms (-93.1%).
- Metrics: hit rate, decode miss, verifyDecodeMs.

### DBV-3 (V-3) reduce decode cost
- Status: PLANNED (CONDITIONAL)
- Precondition: adopt only after verdict parity passes.

### DBV-4 (V-4) reuse the index decode
- Status: PLANNED (under review)

### DBV-5 (V-5) fewer candidate pairs
- Status: PLANNED (CONDITIONAL)

### DBV-6 (V-6) overlap decode I/O
- Status: PLANNED (under review)

### DBV-7 (V-7) image GPU SSIM
- Status: PLANNED (LOW PRIORITY)

## 4. Results summary (updated at measurement)

| ID | Status | Key result | Basis |
|---|---|---|---|
| DBV-1 | **PASS** (0.9.4.92) | reads/bytes → 0; key -37.9% | standard-dataset before/after (section 7) |
| DBV-2 | **PASS** (0.9.4.92) | hit 50.4%→96.3%; decode -93.1% | standard-dataset before/after (section 7) |
| DBV-3 | PLANNED | Not measured | — |
| DBV-4 | PLANNED | Not measured | — |
| DBV-5 | PLANNED | Not measured | — |
| DBV-6 | PLANNED | Not measured | — |
| DBV-7 | PLANNED | Not measured | — |

## 5. Refuted / deferred candidates (preserved)

- (Record refuted/deferred candidates during measurement with reasons and revisit conditions. Never delete.)

## 6. Related documents

- `docs/architecture/database-structure.ko.md` — DB structure + planned improvements.
- `docs/worklog/0.9.4.ko.md` — Performance / Tuning Experiment Index.
- `docs/build-history/0.9.4.92.*` — DBV-1/DBV-2 implementation and verification.
- `docs/build-history/0.9.4.29.*` (D3 decode duplication), `0.9.4.26.*` (D9c/D9d verify breakdown).

## 7. DBV-1/DBV-2 measurement (0.9.4.92, standard dataset)

Build: 0.9.4.92 (engine 1.5.1). Dataset: `test_sample_img_vid` (3,347 files). Temp appdir. run=1.
**Note**: the standard dataset is on C: (low latency), so V-1's absolute effect is smaller than on the user's G: (high latency). This measures the mechanism and the relative effect.

| Metric | before (0.9.4.91 logic) | after (0.9.4.92) | Change |
|---|---|---|---|
| wall_ms | 14,847.3 | 6,974.4 | -53.0% |
| verifyMs | 10,492.8 | 3,043.3 | -71.0% |
| key (stat+64KB) | 5,453.8 | 3,386.6 | -37.9% |
| decode | 13,596.4 | 931.6 | -93.1% |
| verifyQuickHashReads | 27,486 | 0 | -100% |
| verifyQuickHashBytes | 364.3 MB | 0 | -100% |
| verifyDecodes | 27,240 | 1,028 | -96.2% |
| verifyCacheCopies | 13,866 | 26,972 | hit 50.4%→96.3% |
| groups | 156,211 | 156,211 | identical (parity) |
| scanned / analyzed | 3,334 / 3,333 | 3,334 / 3,333 | identical |

- Attribution: **V-1** = removes the 64 KiB read from the key (reads/bytes → 0, key -2,067 ms). **V-2** = larger cache (hit↑, decode -12,665 ms).
- Verification: CPU CTest 122/122, GPU CTest 123/123 (including `image_verify_test`, `verify_parity_test`). Identical groups → verdicts unchanged.
- **Not measured**: absolute values on the user dataset (G:); on a high-latency drive V-1's key effect is larger. DBV-3..7.
- Status: **PASS** (standard dataset). User-dataset re-measurement pending.
