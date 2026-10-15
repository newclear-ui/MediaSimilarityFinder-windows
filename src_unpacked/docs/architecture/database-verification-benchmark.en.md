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

### 7.1 User-dataset measurement (0.9.4.92 GUI run, 2026-10-12)

User log `detailed-log-2026-10-12T162124.json`. Dataset `G:\Downloads\ss_twit`, 177,392 files, **cold scan after deleting the whole index**.
Baseline is the 0.9.4.91 log (177,372 files). **Note**: this run had `cudaAvailable=False` (CPU hashing), so the absolute wall is confounded. The verify metrics below are directly comparable (identical lookup count 14,806).

| Metric | 0.9.4.91 | 0.9.4.92 | Change |
|---|---|---|---|
| verifyQuickHashReads | 14,806 | **0** | -100% |
| verifyQuickHashBytes | 934.5 MB | **0** | -100% |
| verifyKeyMs | 169,877 ms | **4,582.6 ms** | **-97.3%** |
| verifyCacheHits | 1,353 | 1,960 | hit 9.1% -> 13.2% |
| verifyDecodeMisses | 13,453 | 12,846 | -4.5% |
| verifyDecodeMs | 1,095,446 ms | 1,066,381 ms | -2.7% |
| verifyMs | 540,758 ms | 471,820 ms | -12.7% |
| verifyCalls | 19,221 | 19,222 | ~identical |
| groups | 9,054 | 9,055 | +1 (dataset +20 files) |

- **V-1 confirmed (key result)**: on the high-latency G: drive, removing the cache key's 64 KiB read cut `verifyKeyMs` from **170 s to 4.6 s** (-97.3%). Much larger than the standard dataset's -37.9% (C:) — the measurement confirms the prediction that a slower drive benefits more.
- **V-2 partial**: hit rate 9.1% -> 13.2%. The user dataset reuses files across pairs less (13% even with a 1024 cache) than the standard dataset (96.3%). The cache-enlargement ceiling depends on the data's reuse rate.
- **Confound**: `cudaAvailable=False` -> CPU hashing (177,392). The CUDA-unavailable cause needs a separate check (ran the CPU build? driver?). wall (101m vs 112m) is not directly comparable because of this and the index deletion.
- dataset fingerprint: full re-hash 1,393 s (content_hash empty after the index deletion — expected cold cost).
- Status: **PASS** (user dataset, verify-key metrics). The absolute wall needs a re-measurement with CUDA available.

### 7.2 User-dataset Stable-mode rescan example (0.9.4.94 GUI run, 2026-10-13)

User log `detailed-log-2026-10-13T200015.json` (original kept by the user, reviewed from `D:\Temp\OpenCodeWork\`). Dataset `G:\Downloads\ss_twit`, 177,581 files with **only 80 new/changed and 177,501 unchanged** — an incremental rescan. `analyze.mode = B` (Stable, the default) — Fast (A) / Auto (AB) measurements will follow the same format from §7.3 on.

| Metric | Value | Note |
|---|---|---|
| wall / walk / analyze | 980.6 s / 347.0 s / 591.9 s | walk 35% + verify 60% closes the account (~976 s) |
| scanned / analyzed / unchanged | 177,581 / 80 / 177,501 | incremental verdicts correct |
| verifyCalls (= candidates) | 19,234 | Stable global re-verify: all pairs re-verified regardless of the 80 changed files |
| verifyDecodeMisses / hits | 12,887 / 1,935 | hit 13.1% (matches §7.1's 13.2%; reuse-rate ceiling) |
| verifyQuickHashReads/Bytes | 0 / 0 | DBV-1 holds |
| verifyKeyMs | 6.6 s | DBV-1 holds (same class as §7.1's 4.6 s) |
| verifyMs / msPerVerifyCall | 590.3 s / 30.7 ms | G: decode tax (3.6x the standard 8.5 ms; `decodeCopy` 87%) |
| dataset fingerprint | 34.4 s (69.1 GB logical) | incremental `content_hash` hit — cold 1,393 s -> 34 s |
| revalidateMs | 0 | skipped, engine versions unchanged — correct |
| pairs / groups | 15,193 / 9,060 | +5 groups reasonable for the added files |
| GPU | 80 hashed, duty 0.0% | 3.7 ms of batching over 980 s — no contradiction; matching is CPU work |
| decodeOrientApplied | 2 | slow files (`hkdrh69*` 0.4–1.2 s) are size/G: latency, not a rotation-runaway recurrence |
| procIoRead | 46.4 GB | consistent with walk quickHash + verify decodes + decoder re-reads |

- **Stable-mode cost confirmed**: even a 177,501-unchanged rescan re-verifies every pair under Stable, costing the full 592 s. Switch to Fast (A) for changed-files-only rescans (identical verdicts).
- **Reading note**: `verifyBreakdownOverMs = 651 s` — the part-sums are thread-summed (since parallel analyze in 0.9.4.80) and exceed wall. The D9c-era "sum == verifyMs" invariant does not hold on multi-worker runs; read shares.
- Status: **PASS** (measurement-record example — not a defect; Fast/Auto measurements pending).

### 7.3 User-dataset Fast(A) / Stable(B) / Auto(AB) full-refresh comparison (0.9.4.106 GUI runs, 2026-10-15/16)

User logs: A `detailed-log-2026-10-15T234751.json`, B `detailed-log-2026-10-16T002443.json`, AB `detailed-log-2026-10-16T013202.json` (originals kept by the user, reviewed from `D:\Temp\OpenCodeWork\`). Dataset `G:\Downloads\ss_twit`, **full search update with the index kept**. A/B/AB are directly compared on the same build (0.9.4.106); §7.2 (B, 0.9.4.94) is kept as the historical reference.

| Metric | §7.2 B (0.9.4.94) | A (0.9.4.106) | **B (0.9.4.106)** | **AB (0.9.4.106)** | Note |
|---|---|---|---|---|---|
| wall | 980.6 s | 632.3 s | 1012.3 s | **521.2 s** | walk variance is the main confounder |
| walk | 347.0 s | 399.7 s | 359.1 s | 306.5 s | drive/session variance |
| analyze | 591.9 s | 185.8 s | 605.7 s | **170.2 s** | |
| ├ index | (not recorded) | 1.4 s | 1.7 s | 1.2 s | |
| ├ verify | 590.3 s (cap caveat) | **0.0 s / 0 calls** | 604.0 s (capped/thread-summed; wall ~= 420 s) | **0.25 s / 1 call** | AB verifies only exactly-one-changed pairs |
| └ scan (remainder) | (converges to 0 via cap) | 184.4 s | 0.0 s (cap artifact) | 168.8 s | the measured enumeration fixed cost (A/AB) |
| candidates | 19,234 | 19,175 | 19,160 | 19,152 | same dataset family |
| verifyCalls | 19,234 | 0 | 19,160 | **1** | AB: both-changed pairs are the live pass's job (skipped in the final pass) |
| verifyDecodeMisses / hit | 12,887 / 13.1% | 0 | 12,859 / 13.1% | 2 / 0% | AB's single pair = 2 file decodes (254.8 ms) |
| msPerVerifyCall | 30.7 ms | — | 31.5 ms | 254.8 ms | the G: decode tax (slow hkdrh69 files) |
| pairs / groups (report) | 15,193 / 9,060 | 0 / 0 | 15,118 / 9,050 | 0 / 0 | A/AB report = final-pass new verdicts only |
| dataset fingerprint | 34.4 s | 42.2 s | 43.3 s | 38.2 s | incremental `content_hash` hit holds |
| procIoRead | 46.4 GB | 25.0 GB | 43.1 GB | 25.3 GB | the verify-decode difference |
| peak memory | (not recorded) | 1025 MB | 1548 MB | 1030 MB | B's verify buffer cache |
| CPU avg / max | (not recorded) | 8.8% / 91.2% | 10.6% / 71.1% | 9.8% / 95.1% | |
| changed files | 80 | 2 | 0 | **64** | AB needs changes to be meaningful |
| image stage | 80 GPU | 0.6 s (2 GPU) | 0 | 2.9 s (64 GPU, cropMs 15.6 s) | 64 changed files analyzed (5 slow ones 0.84-0.96 s) |
| GPU duty | 0.0% | 0.0% | 0.0% | 0.0% (idle 413.8 s) | matching is CPU work |

- **Three-mode cost model (same-build measurements)**: A = "verify pairs touching changed files (both sides included)", B = "re-verify every pair regardless of the change count", AB = "**verify only exactly-one-changed pairs in the final pass** (both-changed pairs are already emitted by the live pass)". In this AB run (64 changed) the final pass verified **exactly 1 pair (0.25 s)** — AB's verification cost scales with the number of one-changed candidate pairs, not the dataset size.
- **0.9.4.107 targeted-enumeration field validation (additional run)**: `detailed-log-2026-10-16T041623.json`, 177,660 files / 33 changed, wall 363.6 s (walk 316.0). `analyze.mode` = **A** (requested as AB — the record and the screen disagreed with no way to check, which triggered the 0.9.4.108 summary-header display). analyze **1.9 s** = index 1.36 + **scan 0.19** + verify 0.33 (1 call, 325.9 ms — one new match: candidates 1, pairs 1, groups 1). The previous same-workflow A/AB scan bucket (168.8-184.4 s) is **gone (0.19 s)** — fixed-cost removal confirmed (implementation `P-0.9.4.107-A-targeted-enum`).
- **AB report semantics**: `pairs/groups 0` counts final-pass verdicts only. Matches from both-changed pairs travel to the GUI/persisted set via the live pass and never appear in the final report numbers (by design — not a defect). Here none of the 64 changed files produced a match (the single verified pair stayed below threshold), so everything is 0.
- **Cap caveat**: B's `verifyMs 604.0 s` hit `verifyCap` (~= totalMs - index), pressing the scan bucket to 0.0 s. A/AB's 0-0.25 s verify runs measured the enumeration fixed cost (184.4/168.8 s), which exists in B as well, so read **B's real verify wall as ~= 420 s**.
- **B-only structural overhead**: Sequential (B) calls `analyzeCheckpoint` (= `persistMatchesSnapshot`) every 1000-file slice. In the zero-change run this repeated a ~15k-pair snapshot once per slice (~177 times) — absent in A/AB (sliceGroups=0).
- **Dataset/confounder note**: file count A 177,572 -> B 177,567 -> AB 177,627 (+60, 64 changed). Walk oscillated between 306.5 and 399.7 s, so read wall comparisons with the walk delta removed (analyze: A/AB ~= 170-186 s, B ~= 606 s).
- **New finding (fixed cost)**: of A/AB's analyze, verify is 0-0.25 s and **scan (enumeration) is 168.8-184.4 s** — the full index seed + crop (4:3/1:1/9:16) enumeration runs regardless of the change count (`consumeWindowed` is sequential). Restricting the enumeration to changed-file queries in A/AB would cut this fixed cost proportionally to the change count (candidate, not implemented — worklog index `P-0.9.4.106-A-targeted-enum`).
- Status: **PASS** (measurement record). All three modes recorded once each on the same build — repeat counts and walk-variance control are follow-ups.
