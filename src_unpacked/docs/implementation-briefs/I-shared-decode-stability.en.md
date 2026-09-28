# I-shared-decode-stability — D3 Follow-up Candidate Stability & Resampling-Divergence Cause Investigation (Pre-register)

Status: **PRE-REGISTERED** (committed before any investigation code)

```text
Base        v0.9.4.31 / a1169b8
Experiment  D3-follow-up stability (Node I)
Product path change    NO
Version change         NO (only at the v0.9.4.32 recording step, after results)
```

This document is a cause-investigation contract. It is neither a product
decode-path change nor a candidate adoption.

---

## 1. Purpose

Investigate the accuracy divergence of the "one shared decode + two resizes"
candidate DEFERRED in v0.9.4.31. The performance gain is already observed
(24–46 % below baseline); this step asks only "why do the outputs differ".

## 2. Questions to Answer

```text
Q1. Exactly where do the baseline/candidate pixel differences arise?
Q2. Is the difference caused by the resize algorithm, the shared decode
    resolution, or one-step vs two-step WIC scaling?
Q3. Why are there no sampled verdict flips at R128–R256 but 2 at R384–R512?
Q4. Are the two flip cases a structural property of specific images, or a
    specific interpolation/resampling boundary?
Q5. Does a shared resolution or resampling structure exist that can realistically
    preserve the existing semantics?
```

## 3. Established Facts (code and measurement evidence, not speculation)

- The baseline WIC scaler is Fant in all three decode paths
  (`src/image_decoder.cpp:220,294,344`).
- `centerCropResize` is integer nearest-neighbor sampling
  (`src/crop_fingerprint.cpp:12`, integer `y*ch/outSize` arithmetic).
- The `a` dimension formula is long-edge-fixed, `lround`-rounded, floored at 1
  (`src/image_decoder.cpp:293`; PGM uses an `sw>=sh` variant at `:143`).
- `ssimBuf`/`flipBuf` are file-local to `image_verify.cpp` (`:112,122`). The
  probe cannot call them directly, so it replicates equivalent logic on top of
  public `frame_ssim` (`src/video_fingerprint.h:77`) and self-validates with
  bit-exact equality against the `verifyScorePlan` total.
- Measured specs of the 2 flip cases (ffprobe + file sizes):

```text
hpredict.tiff            2,163 B   32x32 RGBA
hpredict_packbits.tiff   4,094 B   32x32 RGBA
l1.tiff                  1,558 B   100x100 1-bit bilevel
l1_xmp.tiff              4,430 B   100x100 1-bit bilevel
```

- The two pairs are near-duplicates with identical pixels but different
  containers (compression variant / XMP metadata variant). Their tiny,
  bilevel/edge-heavy nature near the 64px output scale is the structural
  condition where two-step Fant resampling diverges most from one step.
  (A starting point for investigation, not a conclusion — verified by
  measurement.)
- Verdict threshold 87.5 corresponds to scan `maxDistance 8`
  (`src/scan_pipeline.cpp:18`, `tests/dataset_baseline.cpp:256`).

## 4. Prohibitions

```text
production decode path / decode() / decodePreserveAspect() / WIC path changes
D2 Path C / CPU 10-90 policy / ResourcePolicy / Scheduler / GPU policy changes
deleting existing tests, relaxing tests or thresholds
treating verdict flips as PASS
judging accuracy guaranteed from equal groups alone
```

Never change threshold/verdict policy arbitrarily to erase flips.

## 5. Individual Analysis of the 2 Flips (reproduce first)

Reproduce the 2 flips observed at R≥384 with identical pairs and scores.
Collect for each:

```text
file name/size, native width/height/aspect, EXIF/orientation info
baseline f/a sizes, candidate f/a sizes
baseline score, candidate score, absolute delta, threshold distance
per-compare-plan scores where possible
```

## 6. Score Decomposition

Compare not the final `verify` score but the intermediate results: for each of
the original-ratio crop, 4:3, 1:1, 9:16 crops, record baseline score,
candidate score, delta. Purpose: determine whether a flip comes from one
specific crop. The replicated `ssimBuf` max must equal the `verifyScorePlan`
total bit-exactly for the decomposition to be valid.

## 7. Resampling-Path Audit (source-based)

```text
baseline:  WIC decode -> direct target size (1 Fant step)
candidate: WIC decode -> intermediate resolution (Fant) -> in-memory Fant -> final f/a
```

Verify: WIC scaler interpolation mode, existing resize interpolation,
pixel-center coordinates, rounding, integer dimension conversion, width/height
formulas. `centerCropResize` is nearest-neighbor, so it fundamentally differs
from the Fant chain — show the difference numerically.

## 8. a-Geometry Analysis First

Find the boundary where applying the dimension formula (`lround`,
`std::max<…>(1,…)`, integer division, `sw>sh` branch) at the shared
intermediate size diverges from baseline. No arbitrary `+1`/`-1`/`std::max()`
to fit outputs.

## 9. Case Split

```text
Case A: geometry differs -> pixels differ
Case B: geometry equal -> pixels differ
```

Case B likely implicates resampling-algorithm differences; trace baseline
scaling vs candidate first/second scaling.

## 10–11. R Stability + Narrow Sweep

Tabulate baseline/candidate score, delta, verdict, threshold distance per R in
{128,192,256,384,512} for the flip cases. To pin the R256→R384 boundary, a
narrow sweep {256,288,320,352,384} on the flip files is allowed. It is
documented as causal analysis, not post-hoc candidate cherry-picking, with
why/what/result all recorded.

## 12. Pixel Statistics

Beyond identical/not-identical, measure per output (f and a separately):

```text
different pixel count / max absolute delta / mean absolute delta
```

Determine whether differences are globally tiny or concentrated in regions.

## 13. Spatial Analysis (evidence only)

For the differing crops of the 2 flips, compare baseline crop, candidate crop,
pixel difference. Record only observable evidence of association with
edge/text/sharp-feature/fine-texture/compression-artifact. No speculative
verdicts on cause.

## 14. EXIF / PGM Status

v0.9.4.31 state: `orient_applied_files=0`, `pgm_fallback_files=0`. The flip
pairs themselves carry no applied EXIF orientation (0 in the aggregate), so the
default stays "unverified" on the existing dataset. No new PGM fixtures — PGM
is unrelated to the flips. If fixtures are added: record reason, kind,
conditions, results.

## 15. Cache

The probe bypasses the verify cache. If a hit/miss split is needed, call public
`verifyImagePair` twice on the same file and measure miss (1st) vs hit (2nd)
timings separately. Never count hit cost (where candidate decode does not run)
as candidate decode improvement. The core comparison remains the miss path.

## 16. Full-Groups Feasibility Study

Study whether a full comparison structure is possible without changing the
production path (reuse test structure or a separate probe driver). If
implementation cost is high, finish the flip cause analysis first and then
judge necessity. Record the conclusion (feasible/infeasible + reason).

## 17. Telemetry

Keep D3 telemetry and v0.9.4.31 baseline field meanings. New investigation
values only as additive fields.

## 18. Performance Is Secondary

Priority is cause identification, not "finding a faster candidate" but "why the
outputs differ".

## 19. No Immediate New Optimization Registration

Even if a new structure is found: finish cause analysis → define candidate →
separate optimization pre-register → separate measurement.

## 20. Verdict Rules

```text
cause found + accuracy structurally preservable -> define candidate improvement
cause found + accuracy structurally difficult   -> consider dropping the candidate
cause insufficient                              -> further investigation needed
new promising structure found                   -> separate pre-register, split from old candidate
```

The fact of 2 verdict flips at R384+ is never altered.
