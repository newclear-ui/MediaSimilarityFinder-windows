# Image Burst-Shot Similarity Verdicts — Algorithm Limits and Post-1.0 Plan

Status: **DEFERRED (post-1.0)**. Current behavior stays until v1.0 is complete.
No code changes. This document is the reference for the analysis and the plan.

## 1. Observed case

Two burst shots of the same person in the same background grouped together at
95.8% similarity. The two files differ byte-wise, so they are distinct files,
but the product similarity verdict puts them in one group.

## 2. Current algorithm limits

Image verdicts run in two stages (`src/similarity.cpp`, `src/image_verify.cpp`).

- L1: 64-bit pHash Hamming similarity `100*(64-popcount)/64` plus a distance gate.
- L2/L3: when Hamming is below 97.0, blend the max SSIM over 10 windows
  (64x64 gray plus 4:3, 1:1, 9:16 crops): `0.5*Hamming + 0.5*(100*SSIM)`.

95.8% is not a multiple of 1.5625, so it came from the verify path, not the
fast path: pHash differed by about 3-5 bits while SSIM sat around 0.96-0.99.
A slight pose shift flips a few DCT coefficients, and an identical background
keeps SSIM near 1. Both metrics measure "same overall scene", so **a pose
difference within the same scene attenuates in neither metric**. That is the
structural limit.

A stricter distance threshold separates such pairs, but it can also split
genuine near-duplicates (re-encodes, resizes). The current structure has no
burst discriminator besides the threshold.

## 3. What needs handling (post-1.0)

Same-burst different-pose shots must be separable from re-encoded copies of
the same photo. "Same burst, different pose" and "re-encoded identical photo"
overlap heavily in pixel statistics, so threshold tuning alone cannot split
them cleanly.

## 4. Plan: settings option plus dual-algorithm selectable search

Implement after v1.0 (not implemented at the time of writing).

1. **Program Settings option**: when searching similar images, do not judge
   burst shots as similar (on/off). Default keeps current behavior
   (backward compatible).
2. **Dual algorithms, selectable search**: keep the current pHash+SSIM path,
   plus a strict path with stronger burst separation (e.g. full-full SSIM
   focus, lower bounds across all windows — exact design fixed at
   implementation time), selectable per the option.
3. Model-based verdicts such as face landmarks stay candidates only. The repo
   has no ML model dependencies, so that path is expensive; the first
   improvement attempt stays within recombining and re-gating existing metrics.

## 5. Record locations

- This document: reference for the analysis and the plan
  (`docs/architecture/image-burst-shot-similarity.ko.md` / `.en.md`).
- Brief entry plus link: `docs/development-progress.{ko,en}.md` post-1.0
  backlog ("post-1.0 image-verdict refinement backlog").
