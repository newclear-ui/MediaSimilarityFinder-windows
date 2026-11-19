# Long-Form Ad-Insertion Detection — Full-Timeline Capture Analysis and Optimal Extraction Point Experiment

- Experiment date: 2026-11-06. Build 0.9.4.173 (commit `b2c4d64`, identical to GitHub main HEAD — verified at experiment start).
- Environment: Windows x64, project-local vcpkg FFmpeg. The 5 originals use HW decode (d3d11va); re-encoded copies use SW decode (identical pixels or a clean failure under the F-2 contract).
- Nature: measurement-only, one-off investigation. Not registered as a permanent test or product feature. No product source, version, or verdict changes.

## 1. Purpose and dataset composition

Capture the full timeline of the originals and of the ad-prepended copies at 3-second intervals to obtain reference data, then find an efficient extraction strategy that reproduces the same verdicts from only a small fraction of those frames.

Only videos of 30 minutes or longer were used (all short-form f-series excluded):

| ID | Original length | Length band | Insert type |
|---|---|---|---|
| L04 | 30.6 min | 30–60 min | black 30 s + moving 30/90/180 s |
| L02 | 55.3 min | 30–60 min | black 30 s + moving 30/90/180 s |
| L03 | 55.3 min | 30–60 min | black 60 s + moving 30/90/180 s |
| L05 | 93.6 min | 90–120 min | black 45 s + moving 30/90/180 s |
| L01 | 116.4 min | 90–120 min | black 45 s + moving 30/90/180 s |

- 20 corresponding pairs = original × (black 1 + moving 3). Ad lengths are 30/60 (black) / 30/90/180 (moving) seconds.
- Real moving ads and black screens were kept separate. Moving ads are **controlled synthetic** inserts made by cutting the head segment of a different title and prepending it (because the same ad is inserted into multiple titles, this also serves as a false-positive control group bound only by the shared ad). Validation against real broadcast ad inserts is recorded as not performed.
- No 60–90 min or >120 min samples were available (length-band gap).
- Control group: 240 cross-title pairs (including pairs that share a common ad). The same GoT episode (L02 vs L03) is counted as a true positive and excluded from the tally.
- Original files were not modified. Derived videos, captures, and analyses are all kept outside the repository (`D:\Temp\OpenCodeWork\msf_video_ad*`). To avoid exposing personal paths, this report also records identifiers only.

## 2. Per-video length and ad conditions (30 min and longer)

| Original | Length (s) | black leader | moving insert (s) | Full inserted length |
|---|---|---|---|---|
| L04 | 1805.5 | 30 | 30/90/180 | 1835–1985 |
| L02 | 3320.4 | 30 | 30/90/180 | 3350–3500 |
| L03 | 3315.7 | 60 | 30/90/180 | 3345–3495 |
| L05 | 5576.9 | 45 | 30/90/180 | 5606–5756 |
| L01 | 6984.0 | 45 | 30/90/180 | 7014–7163 |

Ad lengths measured by ffprobe matched the requested values (e.g. L01_ad180 7163.97 s = 6983.98 + 180).

## 3. 3-second full-capture method

- Requested timestamps: the full 3-second grid plus dense 1-second windows (0–400 s from file start, ±200 s around the content center, the final 400 s). The 1-second windows are for start/center/ending landmark analysis; the 3-second grid is the full-timeline basis.
- Recorded items: capture timestamp (requested/actual), perceptual hash + horizontal flip, brightness/variance/low-information flag, frame delta, edge. Produced identically to `makeTemporalAnchorIndex`, so it uses the same evidence as the V2 fingerprint.
- The first 3 frames (0/1/2 s) are included in the grid and were compared separately (§3 instruction).
- Decode cost measured: 3463.7 s total across 25 files (27–710 s per file, 1670–3457 frames). Mixed HW/SW. The comparison itself is memory-bound work, so decode dominates.

## 4. Start / center / ending analysis results

### 4.1 Full correspondence (reference data)

The median time offset obtained from exhaustive 3-second-grid correspondence matched the actual ad length within ±0.02 s for all 20/20 pairs and was consistent. **A 3-second interval is sufficient for timeline correspondence (ground truth).**

### 4.2 Main-feature start search

Probe = the original's leading texture run (3 consecutive non-low-information frames), order-preserving search, W = search cap, N = number of consecutive matches:

| W (s) | 15 | 30 | 60 | 120 | 180 | 240 | 300 | 420 |
|---|---|---|---|---|---|---|---|---|
| black 5 pairs | 0 | 0 | 2 | 2 | 2 | 2 | 2 | 2 |
| moving 15 pairs | 0 | 0 | 2 | 5 | 6 | 8 | 8 | 8 |

- No growth beyond 300 s. **The observed cap of 300 s (= max ad 180 s + 120 s margin) is the saturation point.**
- Consecutive-frame sweep (W=300, strict): N=2 and N=3 both 10/20, N=4 → 7/20, N=5 → 4/20. Increasing N does not help.
- The first 3 frames (0/1/2 s) alone are insufficient: mismatches against the ad region are 0 (good), but exact-position 3/3 matches occur only for L03 3/4 and L05 4/4, while L01/L02/L04 are 0 (re-encoding drift in the first 0–2 s). The first 3 frames cannot be used to pin down the main-feature start.
- Tolerant run (**3-of-4**: 3 of the first 4 frames in order, allowing 1 skip) measured **15/20** (W=300; +5 over the strict 10/20: L05 4 pairs + L03_ad30 all at the exact position).

### 4.3 Main-feature center

Content-center (ad-length corrected) basis:

| Window | ±1 min | ±2 min | ±3 min | ±5 min | ±8 min |
|---|---|---|---|---|---|
| Hit rate | 2927/3188 (92%) | — | 8601/9560 (90%) | 10781/11948 (90%) | 13069/14336 (91%) |

Representative frame counts: 1 frame 19/20, 3 frames 56/60 (93%), 5 frames 92/100, 7 frames 129/140. **3 frames at least 60 s apart is optimal** (diminishing returns beyond that).

### 4.4 Just before the ending

| Range | last 30 s | 1 min | 2 min | 3 min | 5 min | 8 min | 10 min | 15 min |
|---|---|---|---|---|---|---|---|---|
| Hit rate | 672/724 (93%) | 1340/1460 | 2659/2860 | 4069/4368 | 6869/7428 | 9734/10548 | 10494/11348 | 12997/14140 (92%) |

Representative frames: 1 frame 17/20, 3 frames 57/60 (95%), 5 frames 95/100. 3 tail frames within the last 15 minutes is optimal. No case in this experiment set matched on shared credits only (same file + leader means an identical ending); by the verdict rules, credit-only agreement is excluded from the evidence.

## 5. Optimal capture windows and extraction counts

| Point | Capture interval | Observed range | Extraction count | Basis |
|---|---|---|---|---|
| Start | 1 s (or 2 s under the generalized rule) | 0–300 s | probe run 4 frames (3-of-4) | 15/20; 300 s saturation |
| Center | 3 s | content center ±8 min | 3 frames (≥60 s apart) | 56/60; window 91% |
| Ending | 3 s | last 15 min | tail 3 frames | 57/60; window 92% |

Keeping the 1-second interval at the start is necessary because the order-search cadence (2.5 s) collides with a sparse grid (see §5.1).

## 5.1 Which capture interval is efficient (interval sweep, generalization rule)

The fixed constants (probe span 3.5 s, cadence 2.5 s, pool 5 s) are tailored to 1-second sampling and collapse on sparse grids (start 0/20 for I≥2). Generalizing them as interval-proportional (`span/cadence/pool = 1.5I/1.5I/2I+1` seconds) gave:

| I (s) | Start hit | Center ±8 min | Ending 15 min | Mean frames per file |
|---|---|---|---|---|
| 1 | 11/20 | 90% | 91% | 2183 |
| 2 | 13/20 | 82% | 85% | 1592 |
| 3 | 15/20 | 89% | 88% | 1345 |
| 5 | 14/20 | 54% | 54% | 736 |
| 6 | 15/20 | 68% | 71% | 673 |
| 10 | 13/20 | 23% | 30% | 368 |
| 15 | 7/20 | 51% | 57% | 270 |
| 30 | 7/20 | 45% | 47% | 136 |

- The start search is equal or better at I=2–6 than at 1 s (landing on strong probe frames is actually advantageous). It collapses at I≥10.
- Center/ending stay robust (88–89%) up to I=3, drop sharply at I=5, and collapse at I≥10. **The 23% at I=10 is not mere sparsity but an absolute shortage of frames inside the window** (widening the window may recover it; not measured).
- Conclusion: **start 1–2 s, center/ending 3 s (up to 6 s)**. 3 s is enough both for the correspondence map and for center/ending evidence. Only the start order search needs ~1-second density.

## 5.2 How long should observation run

Start observation-cap sweep (N=3): 15/30 s 0 → 60 s 4/20 → 120 s 7/20 → 180 s 8/20 → **10/20 from 240 s, then saturated (300/420 s identical)**. Recommendation: observation = expected max ad + 120 s (180 s ad → 300 s).

## 5.3 Fixed-representative ECO composition (~150 frames per title, all measured)

An evidence-only composition used for the verdict: a 3-second grid over the first 0–300 s (~140 frames, tolerant 3-of-4 search) + 3 fixed center points (center −120/0/+120 s) + 3 fixed ending points (end −6/−3/0 s) = **about 146 frames**:

- ECO fixed representatives: **DUP 13 / UNCERTAIN 2 / miss 5, cross 216 pairs FP 0** (tolerant rule).
- RND (100 random frames from the first 300 s, order preserved): 2/1/17 → **rejected**. Order search needs structural spacing; randomness breaks it.
- Lesson from fixed representatives: the 2 L01 cases remain UNCERTAIN. The center +120 s point lands on a scene change (68.8) and the last 2 frames land on a flat tail (low information). Fixed positions are weak at boundaries, while selecting non-low-information frames within a window (§5) is robust. With fixed representatives, the endpoint rule should change to "last 3 non-low-information frames" (not measured; follow-up).

Timing measurements (same algorithm, 20 pairs + 216 cross pairs):
- Same 20 pairs: DUP 13 / UNCERTAIN 2 / miss 5. Comparison total 0.1 s (mean 4 ms per pair, 3–385 comparisons).
- Cross 216 pairs: FP 0. Comparison total 0.4 s (mean 2 ms per pair).
- Decode (reference, measured during capture): 27–710 s per file. Comparison work is about 1/100,000 of decode and negligible. Total per pair = decoding both sides; the start gate (first 5 minutes only) finishes in tens of seconds.

Band candidate selection + ECO (separate measurement, `V-durationband-ECO-candidate` spec):
25 titles → 300 pairs; band candidates 75 pairs (25%). Same-title 50 pairs, recall 50/50.
ECO within candidates: **DUP 19 (all true positives) / FP 0 / UNCERTAIN 30 / miss 26**.
UNCERTAIN is deferred, not rejected.

## 5.4 Test of the algorithm exactly as instructed (ending-first ladder + short-form)

Tested exactly as implemented per the user's instruction (no variation):

- Long-form: band gate → wide start+end+center → **backwards from the ending similarity** 1/3/10 min 9-second comparison → **second confirmation from the center** → DUP if both match.
- Short-form: no ad logic; direct comparison at 3 points each (start/middle/end).

Measurements (L03/L04/L05 12 pairs + synthetic short-form 3 pairs):

- Long-form: **DUP 10 / UNCERTAIN 2 / miss 0**. L03 4/4 all DUP (ending pool expansion + offset refinement resolved L03ad30 and the L03 black structure).
  L05ad90/ad180 are UNCERTAIN (ending found, but part of the backward ladder failed).
- L01/L02 were not included in this round (prior measurement: missed via start failure; honest deferral).
- Short-form: 2 same pairs UNCERTAIN (per-zone offset mismatch — multiple locks in the repeating testsrc pattern; forced confirmation was refused, real short-form validation is follow-up),
  1 cross-title pair miss (correct rejection).
- Unlike the previous structure where start-search failure blocked DUP, **ending-first rescues pairs whose start failed** (if the end is intact, it is solved from the back).

Deferred-rescue measurement (improved variant: offset refinement ±10, last 3 non-low-information frames, short-form 5-zone majority, 20 pairs + 216 cross pairs):

- Long-form 20 pairs: **DUP 15 / UNCERTAIN 0 / miss 5, cross FP 0**.
  +2 DUP versus ECO fixed representatives (13/2/5) (L01 90 s and 180 s rescued), UNCERTAIN resolved.
  The 5 misses are the same (L01 black and 30 s, L02 black, L03 black and 30 s — pairs where neither start nor end is caught).
- RND (100 random frames): 2/1/17 → rejected. Order search needs structural spacing.
- Short-form 3 pairs: 2 same pairs remain deferred, 1 cross-title correctly rejected. A limitation of synthetic repeating patterns; real short-form validation is a follow-up task.

## 5.5 Pair list — same video or different video

Summarized so the relationship of each tested pair is visible at a glance.

**Same video, 20 pairs** (original vs ad-inserted — same content with only an ad prepended):

| # | Pair | Relation | ECO verdict |
|---|---|---|---|
| 1–4 | L01 original vs black leader 45 s / moving ad 30·90·180 s | same video L01 | miss 2 (leader, 30 s), deferred 2 (90·180 s) |
| 5–8 | L02 original vs black leader 30 s / moving ad 30·90·180 s | same video L02 | black leader miss, moving 3 DUP |
| 9–12 | L03 original vs black leader 60 s / moving ad 30·90·180 s | same video L03 | black leader and 30 s miss, 90·180 s DUP |
| 13–16 | L04 original vs black leader 30 s / moving ad 30·90·180 s | same video L04 | all 4 DUP |
| 17–20 | L05 original vs black leader 45 s / moving ad 30·90·180 s | same video L05 | all 4 DUP |

**Different videos, 240 pairs** (L01↔L02, L01↔L03, … across titles. The same GoT episode L02↔L03 is excluded from true positives because it is the same episode):
- ECO verdict: **0 false positives. All correctly rejected.**

**User-specified algorithm, 12 pairs** (L03/L04/L05 original vs each insert — all same video):
- DUP 10 (L03 4, L04 4, L05 2), deferred 2 (L05 90 s and 180 s), miss 0.

**Short-form 3 pairs**: sh60 original vs 30 s leader (same video) → deferred. sh600 original vs 30 s leader (same video) → deferred.
sh60 vs another title (different video) → correctly rejected.

## 6. Savings versus full capture

- Full: 1670–3457 frames per file.
- Recommended extraction: start 300 + center 320 (±8 min × 3 s) + ending 300 (15 min × 3 s) + probe margin ≈ 1000 frames → **about 60–70% fewer captures**. Decode dominates, so wall-clock savings are estimated at the same level (comparison work is dominated by the start search: tens to hundreds per pair, tolerant run about 2×).
- Hit rate per strategy (20 pairs, rules stated. UNCERTAIN = start found but center/ending insufficient):
  - S0 full correspondence map: offset 20/20 (not a verdict, reference).
  - S1 start only: DUP 10 / UNCERTAIN 10.
  - S2 start+center: 10 / 0 / miss 10.
  - S3 start+center+ending (strict): 9 / 1 / 10.
  - S4 minimal representatives (start + center1 + ending1): 9 / 1 / 10.
  - **S3T tolerant start + strict representatives: 12 / 3 / 5. Allowing 2-of-3 representatives: 15 / 0 / 5.**
  - Current V2 reference (black 5 pairs, 0.9.4.173): DUP 2 (L01/L04) + UNCERTAIN 3. This experiment's S3T+2of3: black 3/5 DUP (L03/L04/L05).

## 7. Causes by success and failure case

Common: every exhaustive correspondence offset was exact, and the 240 cross pairs had 0 false positives (including pairs sharing a common ad; the same GoT episode is excluded from true positives).

- **All 4 L01 pairs fail at the start (even with unlimited search)**: 4 of 5 probe frames score 81.2 at the correct position (6.5 points short of the 87.5 threshold, a 12-bit difference). Center/ending are 3/3. Cause: re-encoding instability of the opening pHash. Post-analysis: 4 consecutive weak 81.2 evidence + order + center/ending completion could yield DUP → **a weak-evidence (margin) rule is the next implementation task** (not measured; proposed in §9).
- **L05 4 pairs fail strict → all succeed after tolerance**: the first frame is 81.2 and the rest are 100. Cause: one unstable leading frame blocks the entire run (even though the triple is 3/3). Resolved by the tolerant run.
- **L03_ad30 fails strict → succeeds after tolerance**: leading 81.2 (rate-control effect at the 30 s concat boundary; the 90/180 s versions are 96.9+). e3 is also 2/3 (one of three dropped) → resolved by 2-of-3 representatives.
- **L02 black fails (start+center)**: leading 56.2 completely rejected + center 0/3. Cause: dark HDR. Kept as a limitation consistent with V2's UNCERTAIN-first policy.
- **L03 black / L05 black drop 1 representative** (e3 and c3 2/3): single-frame drop. Resolved by the 2-of-3 rule.
- The length-band difference (30–60: 11/12 tolerant, 90–120: 4/8) is attributed to content (the L01 opening) rather than length.

## 8. Distinguishing real ads from black screens

| Type | Pairs | Strict start | Tolerant start (+2of3 final) |
|---|---|---|---|
| moving (controlled synthetic, other-title heads) | 15 | 8 | 12 (final DUP 12) |
| black (black leader) | 5 | 2 | 3 (final DUP 3) |

Real broadcast ad inserts could not be validated. The moving results are numbers under controlled synthetic conditions only and must not be mixed with real-ad results.
User-specified algorithm round (L03/L04/L05 12 pairs, ending-first ladder): DUP 10 / UNCERTAIN 2 / miss 0.
Synthetic short-form 3 pairs (1 min, 10 min): 2 same pairs deferred (multiple locks in the repeating testsrc pattern; confirmation refused), 1 cross-title correctly rejected.

## 9. Recommended processing order for long-form detection

Composed only from measured results (unmeasured items are in §10):

1. Capture the start window at 1-second intervals up to 300 s (expected max ad + 120 s; 2 s is equivalent under the generalization rule, but 1 s is the default).
2. Use the leading texture run (3 consecutive non-low-information frames, span ≤3.5 s) as the probe. If absent, fall back to searching up to 60 s.
3. Take 3-of-4 ordered matches among the first 4 probe frames (1 skip allowed, cadence 2.5 s, mirror allowed) as main-feature start candidates. Run multiple candidates and the default (0,0) hypothesis in parallel.
4. For each start-offset candidate, estimate the content center (ad corrected) and compare 3 frames at least 60 s apart on a ±8 min 3-second grid (2-of-3 pass).
5. Compare the 3 tail frames on a 3-second grid over the last 15 minutes (2-of-3 pass). Credit-only agreement is excluded from the evidence.
6. Consistent start+center+ending offset → DUP. Start only or partial → UNCERTAIN. No evidence → DIFF. Low information without contrary evidence (e.g. darkness) → UNCERTAIN (never DIFF).
7. Capture budget: about 1000 frames per pair (7 representatives + windows). About 60–70% savings overall.
8. Interval policy: start 1–2 s, center/ending 3 s (up to 6 s), full map 3 s.

## 10. Sample shortfalls, measurement limits, unvalidated conditions
- Samples: 5 titles, 20 pairs (controlled synthetic). Not validated: 60–90 min band, over 120 min, real broadcast ads, HDR same-content (except the L02 family), more than 2 insert regions, real-file PreCredits, threshold calibration.
- Probe position sensitivity: even for the same pair, the probe lands on strong or weak frames depending on the grid (L05 fails at 1 s and succeeds at 2 s or more). Multiple probes (reserve positions) or a weak-evidence rule are follow-up tasks.
- L01-type (4 consecutive 81.2) rescue rules and dark-video rules were not measured in this experiment and are not included in the §9 order.
- Center/ending representative selection (stride) is unstable on sparse grids (I=15 better than I=10, and other zig-zags). Use window hit rates (§4.3/4.4) as the primary metric and choose representative counts within them.
- Comparison counts (comps) are relative values from the Python analyzer. Only decode wall time is measured; comparison wall time was not separately instrumented.
- This report is not a product implementation completion or an accuracy Gate PASS. Product integration awaits a separate instruction.

## 11. Follow-up Q&A record (2026-11-06, discussion after the experiment)

- **What capture interval**: start 1–2 s, center/ending 3 s (up to 6 s). It was measured that the fixed constants (3.5/2.5/5 s) are 1-second-only and collapse on sparse grids, and this was resolved by interval-proportional generalization (`span/cadence/pool` scaling). Without generalization, the start search collapses to 0/20 at I≥2.
- **Operating at 1,000-title scale**: exhaustive comparison (~500k pairs) is infeasible, so the existing two-stage structure is used. Stage 1 L1Cheap hash candidate selection (already product behavior, seconds per file) → stage 2 applies this method only to a few hundred candidates (seconds per pair). One index per video (cached) plus minutes of candidate verification = hours. The gate (start only, tens of seconds) and confirmation (3 points) are separated.
- **Additional shortening (measured basis)**: center ±8 min → ±1 min (92% ≈ 91%), ending 15 min → last 1 min (93% ≈ 92%), start 1 s → 2 s (13/20 vs 11/20), early exit, start-first gate. About 400 frames per title is possible (more than 80% savings).
- **Start-point search coding plan**: parameterize the `probeStartIndex` span, `findLeadingOffsetV2` 3-of-4 (≤1 skip) variant, observation-cap constant (`expected max ad + 120 s`, default 300 s), ScanPipeline flush worker report-only wiring, +30/+60 s reserve probes. V1 frozen, engine/DB unchanged. Implementation on separate instruction.

## 12. Local artifact list (outside the repository, kept for re-inspection)

Location: local working directories `msf_video_ad/` · `msf_video_adcap/`. Not committed to Git. Do not delete.

- 15 inserts, about 22 GB total: L01_ad{30,90,180} about 2.2 GB each, L02_ad* about 1.0 GB, L03_ad* about 1.5 GB, L04_ad* about 0.5–0.6 GB, L05_ad* about 2.3 GB, plus `ad_manifest.csv` (ad length and bank record).
- Capture `adcap.tsv` 6.7 MB (25 files, full 3-second grid + 1-second dense windows, per-frame hash/brightness/variance/delta/edge/low-info).
- Analysis `adeff.tsv` 0.2 MB (2704 rows per pair + 240 cross rows).
- Tools: `adcap.cpp` (capture harness source) · `analyze_adcap.py` (correspondence/search/sweep analysis) · `summ_ad.py` · `mat_ad.py` · `nsw_ad.py` (aggregation) · `gen_ads.ps1` · `run_adexp.ps1` · `run_adcap.ps1` (single orchestrator) · `userspec_ad.py` (user-specified algorithm) · `rev_ad.py` · `diag_spec.py` · `ultra_ad.py` · `time_eco.py` · `imp_ad.py` (improved-variant measurement).
- The 5 originals and 5 black leaders reused the existing field dataset (`msf_video_field/`) unchanged.
- The 5 synthetic short-form clips (`sh60/sh600` original, leader, other title; 1 min and 10 min), captures (`adcap_s.tsv`), and user-specified algorithm files (`userspec_ad.py`, `rev_ad.py`, diagnostics `diag_spec.py`, aggregation `ultra_ad.py`, timing `time_eco.py`) are also kept in the same external working directory. Not committed to Git.
