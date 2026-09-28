# I-decode-once-resize-twice — D3 Follow-up Optimization Candidate Measurement (Pre-register)

Status: **PRE-REGISTERED** (committed before any candidate measurement code)

```text
Base        v0.9.4.30 / a5fb31f
Experiment  D3 follow-up (Node I)
Product path change    NO
Version change         NO (only at the v0.9.4.31 recording step, after results)
```

This document is the measurement implementation contract. It is not a product decode-path change.

---

## 1. Purpose

As a candidate for reducing the cost of the two image decodes confirmed in D3,

```text
one shared-resolution decode
        ↓
   ┌────┴────┐
   ↓         ↓
64×64 f     aspect a
```

is validated.

This step is the **measurement and feasibility-validation step of the
optimization candidate**; it does not replace the product's real decode path.

---

## 2. Existing Baseline

The current verification path is:

```text
decode(path, 64, 64)
    ↓
f

decodePreserveAspect(path, 64)
    ↓
a
```

Both results are 8-bit grayscale `GrayImage` (`src/image_decoder.cpp:350,371`,
WIC conversion `GUID_WICPixelFormat8bppGray`, `src/image_decoder.cpp:225,297`).

`f` must be exactly 64×64 (`src/image_verify.cpp:90`, and the dimension-match
requirement of `ssimBuf`, `src/image_verify.cpp:122-125`); `a` must keep the
aspect-preserving semantics of the existing `decodePreserveAspect()`.

The real behavior of the baseline and the telemetry meanings follow the D3
results.

---

## 3. Candidate Under Validation

Candidate structure:

```text
file
 ↓
one shared-resolution decode
 ↓
GrayImage
 ├── resize → f (64×64)
 └── resize → a (aspect-preserving)
```

The candidate removes the existing second WIC decode and tests whether both
results can be produced from an image that already exists in memory.

This step does not replace the existing `decode()` / `decodePreserveAspect()`
call path in production code (`src/image_verify.cpp:82-83` stays).

---

## 4. Shared Decode Resolution

Resolutions are not picked one-by-one arbitrarily; a pre-defined candidate set
is compared.

The initial measurement set is:

```text
128
192
256
384
512
```

Baseline and candidate are compared under identical dataset and measurement
conditions for each resolution.

No new resolution is added to the set retroactively based on measurement
results.

### Resolution selection rationale (measured, not added afterwards)

The distribution below was measured at pre-register time with `ffprobe` plus
direct header parsing.

```text
format   sample  long-edge range
BMP      2700    mostly 8, some 256
GIF      20      121–1500
ICO      12      ≤256 (mostly ≤64)
JPEG     200     201–850
PNG      200     224–1281 (IHDR read directly; the ffprobe build in use does not print PNG dims)
TIFF     14      1–601
WEBP     200     312–1920
```

- The output long edge is 64, so 128 is the smallest meaningful resolution,
  starting at 2× the output.
- 192–512 covers the lower band of real native long edges (201–1920). The
  shared decode itself downsamples within this band, so raising the resolution
  increases shared-decode cost while reducing resize information loss. Drawing
  that trade-off curve is the purpose of the measurement.
- The BMP 8px / TIFF 1px cases are upsampling corners — interpolation cost with
  no information — and are interpreted separately in the per-format results.

## 5. Dataset

The same representative dataset as D3 is used.

```text
files       = 3,347
bytes       = 102,475,315
fingerprint = e8f8fa6ab0257f1e2b7d839b73ec14b1cc726695dac2efe79b55a13a13e2640a
```

The identical dataset fingerprint guarantees the baseline and candidate
measurements use the same input set.

If the dataset is changed, the change and the new fingerprint are recorded
separately.

## 6. Accuracy Validation

For each shared decode resolution, confirm:

### f

```text
width  = 64
height = 64
pixel count = 4096
```

### a

Confirm the same aspect-preserving semantics as the existing
`decodePreserveAspect()`.

In particular compare:

```text
source aspect ratio
long-edge-based size
width × height
```

The `a` dimension formula must match `decodePgmAspect` /
`decodeWicFileAspect` (long edge fixed, short edge `lround`-rounded,
`src/image_decoder.cpp:143,293`); a rounding-boundary mismatch is recorded as
geometry-different.

## 7. Pixel Parity

Where possible, directly compare the output buffers of baseline and candidate.

Targets:

```text
f baseline ↔ f candidate
a baseline ↔ a candidate
```

Record each result as one of:

```text
byte-identical
pixel-different
geometry-different
```

A difference is never judged as merely tolerable; its cause is recorded.

### Advance notice (code evidence, not speculation)

`centerCropResize` is integer nearest-neighbor sampling
(`src/crop_fingerprint.cpp:12`, integer `y*ch/outSize` arithmetic). Its output
depends on the source resolution, so byte equality with the baseline built by
the WIC Fant scaler is not expected. Byte parity is therefore a reference
metric; the main axis of judgement is §8 groups/verdict. This expectation is
written down before the fact so the bar cannot be moved afterwards.

## 8. Verification Result Comparison

Compare baseline and candidate on:

```text
groups
verdict
```

Where possible check down to individual comparison results rather than judging
by total group count alone.

Minimum requirements:

```text
confirm groups change or not
confirm verdict change or not
```

A case with identical `groups` but a changed `verdict` is recorded as a
separate problem.

## 9. Performance Measurement

Baseline:

```text
fixed decode
+
aspect decode
```

Candidate:

```text
shared decode
+
resize for f
+
resize for a
```

Each stage is timed separately.

Minimum items:

```text
baseline fixed decode
baseline aspect decode
baseline total

candidate shared decode
candidate f resize
candidate a resize
candidate total
```

Total baseline and candidate costs are compared under identical measurement
conditions.

## 10. Measurement Conditions

Keep the same conditions as D3.

```text
1 warm-up excluded + 5 measured runs (same as D3)
mean / median / min / max / range
```

State in the results:

```text
warm-up count
measured repetition count
mean or median
measured file count
dataset fingerprint
```

Reuse the existing benchmark framework
(`msf_dataset_baseline <root> <app-dir> [runs]`, `docs/STRUCTURE.md`
§build/test).

## 11. Format Coverage

Use the formats in the existing D2/D3 validation scope.

```text
BMP
GIF
JPEG
PNG
WEBP
TIFF
ICO
```

If the PGM fallback is on the tested path, record it separately
(`readPgmFile` is P5-only and does not normalize `maxv`,
`src/image_decoder.cpp:88-91`; it also has no orientation handling).

## 12. EXIF Orientation

The existing `decodePreserveAspect()` EXIF orientation handling must also hold
for the candidate.

Both decode paths read EXIF orientation tag 274
(`src/image_decoder.cpp:194,267`). The candidate's shared decode must apply the
same mapping; because the application point (one shared decode) differs from
the baseline (two calls), the attribution of orientation cost changes and must
be reflected in telemetry.

Compare baseline and candidate `a` results on inputs carrying rotation info.

If an orientation difference appears, analyze the cause before performance.

## 13. Cache

The existing verification cache behavior is unchanged.

The cache key is `{path, size, mtime, quickHash}`, LRU-capped at 32, storing
the `{f, a}` pair on miss
(`src/image_verify.cpp:22-28,60-66,101-105`).

On a cache hit the existing `f` / `a` pair keeps being used as-is.

Candidate measurement neither removes nor bypasses the cache in a way that
changes product behavior.

If needed for candidate-effect analysis, record results split by:

```text
cache hit
cache miss
```

## 14. Telemetry

The existing D3 telemetry field meanings are unchanged.

Actually-verified existing baseline keys (`src/benchmark.cpp:725-799`):

```text
decodeTotalMs / decodeCalls / decodeAspectCalls
decodeFullCalls / decodeFullTotalMs / decodeFullState
decodeAspectOnlyCalls / decodeAspectOnlyTotalMs / decodeAspectOnlyState
decodeFullOpenMs / decodeAspectOpenMs
decodeFullCopyMs / decodeAspectCopyMs
decodeFullFactoryMs / decodeAspectFactoryMs
decodeSplitOverMs / decodeSplitState
verifyDecodeMs
```

This project's naming convention attaches a `*State` (`measured` /
`not_measured`) to every `*Ms` key. Candidate measurements are recorded as
separate telemetry items following that convention. Candidate field names are
finalized at implementation time after checking the convention; this
pre-register fixes the **rule** (separate recording + state attached + existing
meanings intact), not the names.

The combined meaning of `mergeDecodeTelemetry` kept by D3 is not damaged.

## 15. No Product Code Application

This step changes none of:

```text
production decode path
decode()
decodePreserveAspect()
existing verification cache
D2 Path C production path
Adaptive Scheduler
resource_policy
CPU 10–90 policy
GPU policy
```

The candidate is measured on a benchmark- or test-only path.

## 16. Judgement Purpose

This pre-register exists to answer three questions.

### Question 1

Can the existing `f` and `a` semantics be reproduced from a single shared
decode?

### Question 2

Can `groups` and `verdict` be kept in the process?

### Question 3

Is the real total cost lower than the existing two decodes?

The answers are recorded as actual measurement results.

## 17. No Advance Decision on Product Optimization Adoption

The next step differs with the results.

```text
accuracy kept + performance improved
→ review as a production-application candidate

accuracy kept + negligible performance difference
→ re-examine whether adoption is needed

performance improved + groups/verdict changed
→ further accuracy-cause investigation

byte parity or geometry difference
→ analyze the cause, then judge further

no performance improvement
→ hold or drop the candidate
```

This pre-register itself does not finalize product adoption.

## 18. Change Prohibition Principle

Unrelated areas are not modified:

```text
D2 Path C
CPU 10–90 user policy
ResourcePolicy formulas
Scheduler
GPU Adaptive policy
existing benchmark meanings
existing D3 telemetry meanings
```

Existing tests are not deleted or relaxed to pass candidate results.

## 19. Pre-register Basis

This document is committed first; candidate measurement implementation starts
afterwards.

After the pre-register, none of the following is done to fit the measurement
results:

```text
adding resolution candidates
changing measurement conditions
changing judgement criteria
excluding unfavorable results
```

If a change is truly necessary, its reason and time are recorded separately.

## 20. Next Steps

After the pre-register commit, the measurement build measures, under identical
dataset and conditions:

```text
baseline
vs
128
vs
192
vs
256
vs
384
vs
512
```

The final results include:

```text
shared decode cost
f resize cost
a resize cost
total cost
f parity
a parity
groups
verdict
per-format results
EXIF results
PGM fallback results
f byte parity
a byte parity
groups comparison
verdict comparison
cache hit/miss effects
CPU CTest results
GPU CTest results
verify_parity results
telemetry/schema validation results
production path unchanged confirmation
problems found during measurement
next-step proposal
```

Production-application is decided separately from the measurement results.
