# V Duration-Band Candidate Selection + ECO Comparison (Proposal, Measured)

Status: **PROPOSED (measured on captures, product code untouched)**
Baseline: **0.9.4.173** · Date: **2026-11-06**
Related: `docs/field-reports/longform-ad-capture-efficiency.ko.md` (§5.3, §6)

## 1. Purpose

All-pairs comparison over ~500k pairs is infeasible, so filter candidates by
runtime band first and run ECO only on survivors. This concretizes the user
directive (band prefilter + start/center/ending triangulation + zoom-in near
similarities) into measurable rules.

## 2. Candidate Rule (Bands)

- Shorts (either side under 20 min): candidate only if `abs(d1-d2) <= 90 s`.
  Rationale: trims/short inserts stay under 60 s per user observation, plus margin.
  Design-consistency only (f originals 33–134 s + 30 s leaders all pass). Not capture-measured.
- Long-form (both 20 min or more): candidate only if `max(d1,d2)/min(d1,d2) <= 1.2`.
  Rationale: a 180 s ad on 30 min is 1.10, on 2 h is 1.03.
  1 h vs 1.5 h (1.5) is out; 20 min vs 22–23 min (1.10–1.15) is in.
- Measured (25 videos): 300 pairs -> 75 candidates (**25%**, 75% removed).
  Same-work recall **50/50 (100%)**. GoT same-episode (L02<->L03) stays in-band,
  preserving true-catch opportunity.

## 3. ECO Rule (Candidates Only)

1. Capture start 0–300 s at 3 s grid (~140 frames), tolerant 3-of-4 ordered
   search (<=1 skip, cadence 4.5 s) for the offset.
2. Check 3 fixed center points (center-120/0/+120 s) and 3 fixed ending points
   (end-6/-3/0 s) with 2-of-3 passing.
3. Run both directions; either direction confirms DUP.
4. Start+center+ending consistent -> DUP. Start only -> UNCERTAIN. No evidence -> hold (miss, never forced DIFF).

## 4. Measured Results (Existing Captures Reused, No New Decodes)

- 75 candidates: **19 DUP (all true) / FP 0 / UNCERTAIN 30 / miss 26**.
- On the 20 orig-vs-inserted pairs: DUP 13–15, UNCERTAIN 0–2, miss 5 (matches ECO fixed-rep measurements).
- UNCERTAIN is a hold, not a drop. Band recall is 100%, so ECO-missed pairs stay in the candidate pool.
- Compare cost is milliseconds per pair. Decode dominates, so the start gate (first 5 min only) skips full passes for rejects.

## 5. C++ Mapping (On Implementation)

- Band filter: at candidate generation (`CandidateIndex` tail or before
  `ScanPipeline` pendingVideo intake). `MediaFile::duration` already exists.
  Ranking/exclusion only, never verdict.
- ECO compare: new V2-family functions in `temporal_anchor.*`
  (generalized `findLeadingOffsetV2` + 3-of-4 + fixed-rep checks). V1 frozen,
  no engine/DB/schema bumps.
- Shorts path: few direct start/mid/end frames, no ad logic. Capture-based
  validation is follow-up.

## 6. Gate Criteria (Before Product Adoption)

- Band recall 100% on same-work pairs.
- ECO DUP with FP 0 on 200+ cross pairs.
- At least one of: L01-class weak-evidence rule, multi-probe, last-non-low
  ending rule — measured.
- CPU/GPU full suites green + LegacyV3 default byte-identical.
