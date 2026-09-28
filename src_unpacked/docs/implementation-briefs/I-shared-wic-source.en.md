# I-shared-wic-source — Shared WIC Source/Frame + Two Independent Scalers Candidate (Pre-register)

Status: **PRE-REGISTERED** (committed before any candidate probe implementation)

```text
Base         v0.9.4.32 / 69d0350
Experiment   D3 follow-up (Node I) — I-2
Product path change    NO
Version change         NO (only at the v0.9.4.33 recording step, after real results)
Predecessor  I-1 (shared GrayImage + resize twice) = DEFERRED, not replaced by this one
D2 Path C    not mixed in (CreateDecoderFromStream / HandleStream are not examined)
```

This document is the measurement implementation contract. It is not a product
decode-path change.

---

## 1. Purpose

The duplication D3 identified is two **complete WIC pipelines**. I-1 tried to
reduce it by sharing an **intermediate GrayImage**, but the two-step Fant chain
through an 8-bit intermediate broke f/a byte parity and the candidate went
`DEFERRED` (`docs/build-history/0.9.4.32.en.md` §8, §16).

This candidate creates **no intermediate GrayImage at all.** Only the factory,
decoder, frame, metadata and orientation source are shared; the final
scaler/converter/`CopyPixels` pairs stay independent. Each output's resampling
chain can therefore be identical to baseline.

The core question is **exact output parity, not speed.**

## 2. Hypotheses (not facts)

- H1: one `IWICBitmapSource` can be referenced safely by two
  `IWICBitmapScaler` instances.
- H2: with two scalers reading the same source, f/a results are **byte
  identical** to baseline.
- H3: WIC shares the actual decode workload, so the candidate total cost drops.
- H4 (opposing hypothesis): only factory/decoder/frame creation is saved while
  `CopyPixels` stays equally expensive, so the total gain is negligible or zero.

**None of H1–H4 is used as an assumption.** All are confirmed by measurement.

## 3. Baseline

The current product path (unchanged).

```text
ImageDecoder::decode(path, 64, 64)                 -> f
ImageDecoder::decodePreserveAspect(path, 64)        -> a
```

Each independently performs the same six steps
(`src/image_decoder.cpp:154` `decodeWicFile`, `:238` `decodeWicFileAspect`).

```text
CoInitializeEx
CreateWICFactory2 (fallback: CreateWICFactory)
CreateDecoderFromFilename
GetFrame(0)
GetMetadataQueryReader -> EXIF orientation -> CreateBitmapFlipRotator if needed
CreateBitmapScaler(Fant) -> CreateFormatConverter(8bppGray) -> CopyPixels
CoUninitialize
```

## 4. Candidate Structure

```text
file
 ↓
CoInitializeEx                      once
 ↓
WIC factory                         once
 ↓
CreateDecoderFromFilename           once
 ↓
GetFrame(0)                         once
 ↓
EXIF orientation (FlipRotator)      once
 ↓
shared src (IWICBitmapSource)
   ├── scaler A -> 64x64    -> converter A -> CopyPixels A -> f
   └── scaler B -> aspect64 -> converter B -> CopyPixels B -> a
 ↓
release all COM objects             (strictly before CoUninitialize)
 ↓
CoUninitialize                      once
```

Key point: the **scaler and converter for f and for a are created separately.**
Only the source is shared. The decisive difference from I-1 — intermediate
GrayImage followed by a second Fant — does not exist here.

## 5. What Must Stay Identical to Baseline

At byte level:

```text
f  : source -> Fant -> 64x64
a  : oriented source -> Fant -> aspectDims(sw,sh,64)
```

What may differ: the number of factory/decoder/frame creations and the number of
CoInitialize calls.

## 6. Geometry Parity Rule

The `a` target size **must** be computed from the same oriented source size the
baseline uses. It is never recomputed from an intermediate resolution R.

```text
baseline : src->GetSize() -> (sw,sh) -> aspectDims(sw,sh,64) -> scaler
candidate: same src->GetSize() -> same (sw,sh) -> same aspectDims -> different scaler
```

Under this structure the geometry mismatches seen in I-1 (R128 93, R256 54,
R384 39, R512 34) must not occur. If they do, the cause is investigated.

## 7. Exactness Criteria (priority 1)

Minimum conditions for adoption. If any breaks, `DEFERRED` or a cause-specific
`NOT ACCEPTED` is reviewed immediately.

```text
1. f geometry identical   (same as baseline 64x64)
2. a geometry identical   (same as baseline)
3. f pixel byte identical (whole dataset)
4. a pixel byte identical (whole dataset)
```

- **No PASS on a sample.** The whole dataset is checked.
- Baseline and candidate are always compared per file, from the same run.
- The mixed-pairing defect from I-1 must not recur:

```text
baseline A + baseline B
candidate A + candidate B
```

`baseline A + candidate B` is never mixed in.

## 8. Dataset

```text
path        C:\project\test_sample_img_vid
files       3,347
bytes       102,475,315
fingerprint e8f8fa6ab0257f1e2b7d839b73ec14b1cc726695dac2efe79b55a13a13e2640a
formats     BMP / GIF / JPEG / PNG / WEBP / TIFF / ICO
```

If the fingerprint differs, stop before measuring and find out why.

## 9. EXIF Fixture Requirement

The current dataset reports `orient_applied_files = 0`, so **the EXIF path is
not exercised.** This candidate must verify EXIF.

```text
Minimum orientations required: 1, 3, 6, 8 (2, 4, 5, 7 if available)
```

Before adding anything, confirm: the existing fixture generation method, the
source/licence record, and the effect on the dataset fingerprint. The standard
dataset is not changed arbitrarily. If needed, EXIF fixtures go into a
**separate accuracy fixture dataset**, whose fingerprint is also recorded.

## 10. PGM Fixture Requirement

`pgm_fallback_files = 0` today, so the PGM path is also unverified.

The PGM analogue of this candidate is examined as a structure (not wired into
production at this stage):

```text
readPgmFile once -> share the original byte buffer
                            ├-> scaleGray -> fixed
                            └-> scaleGray -> aspect
```

Current `readPgmFile` is P5-only with no `maxv` normalization
(`src/image_decoder.cpp:84-92`). That behaviour is not changed.

## 11. Measurement Items

Baseline

```text
decode()                 = f_ms
decodePreserveAspect()   = a_ms
baseline total           = f_ms + a_ms
```

Candidate

```text
shared setup (COM/factory/decoder/frame/orientation) = shared_ms
fixed branch   (scaler+converter+CopyPixels)        = fbranch_ms
aspect branch  (scaler+converter+CopyPixels)        = abranch_ms
candidate total                                    = shared_ms + fbranch + abranch
```

Additionally recorded separately: `CreateDecoderFromFilename`, `GetFrame`,
metadata/orientation, and `CopyPixels` A/B.

The key observation: **does branch B reuse WIC's internal decode?** If
`CopyPixels B` costs about the same as `CopyPixels A`, the source is not
sharing the decode work, and that is recorded as-is (H4).

## 12. Timing Method · Run Rule

```text
warm-up   1 run (excluded from results)
measured  5 runs
record    mean / median / min / max / range
judge     by median; only differences beyond run-to-run variation count
```

A 1–2 % difference inside the measured variation is not claimed as an
improvement.

## 13. No Production Change

During the pre-register and probe stages the production behaviour of the
following files is not changed.

```text
src/image_decoder.cpp
src/image_verify.cpp
src/media_pipeline.cpp
src/scan_pipeline.cpp
src/crop_fingerprint.cpp
```

A measurement-only probe is added. The probe implements its own WIC pipeline
directly; it does not replace a product decode function or reorder calls.

## 14. D2 Path C Separation

This candidate keeps `CreateDecoderFromFilename` and only examines
decoder/frame/source sharing. `CreateDecoderFromStream`, `HandleStream` and any
Path C production adoption are out of scope. D2 Path C stays `DEFERRED`.

## 15. API / COM Lifetime Verification Items (no guessing)

Before implementation, the following are confirmed **by actual compile and
execution**, and the results recorded.

```text
A. factory / decoder / frame creation
B. one IWICBitmapSource referenced by two scalers (H1)
C. an orientation FlipRotator source shared by two scalers
D. does the source outlive scaler Initialize (lifetime order)
E. initializing two converters, each on its own scaler
F. do two CopyPixels calls reuse the first one's decode
G. are all ComPtrs released before CoUninitialize
H. f/a output geometry
I. byte compare
J. EXIF branch / PGM branch
```

A constraint the current source already states: running `CoUninitialize()`
before live WIC objects fault (`src/image_decoder.cpp:150-153`). The probe
follows that order strictly.

## 16. Acceptance / Defer Criteria

Adoption (`ACCEPT`) requires **all** of:

```text
A1. f geometry identical over the whole dataset
A2. a geometry identical over the whole dataset
A3. f byte identical over the whole dataset
A4. a byte identical over the whole dataset
A5. EXIF fixture parity PASS
A6. PGM fixture parity PASS
A7. cost reduction clearly beyond run-to-run variation
A8. identical results on both CPU and GPU trees
```

If any fails:

```text
- A1..A4 broken  -> DEFERRED or cause-specific NOT ACCEPTED
- A5/A6 unverified -> DEFERRED until fixtures exist
- A7 fails -> NO MEASURABLE GAIN (exactness may still have passed; recorded
              separately)
```

**Thresholds are never tuned to fit, and score tolerances are never widened.**

## 17. Measurement Order

```text
1. API structure verification (§15)
2. probe implementation + selfcheck
3. f/a geometry parity
4. f/a byte parity
5. EXIF / PGM parity
6. cost measurement
7. if parity PASS -> consider full-scan groups comparison
8. only then a separate production implementation brief
```

Full-scan is not attempted while pixel divergence is present. That ordering is
not repeated from I-1.

## 18. Full-Scan Groups

I-1 listed full-scan as a revisit condition even though its pixel divergence was
already settled. The order is changed here. **While parity (§7) is broken,
full-scan is not run.** I-1's full-scan record is preserved as `DEFERRED` but is
not executed immediately, because it would not answer the question the
remaining problem actually poses.

## 19. Future Revisit Condition

```text
- if the cause of an A1..A4 failure is identified, redesign the candidate
- when EXIF/PGM fixtures are added to the dataset, re-measure immediately
- when the Windows/WIC runtime changes, re-measure
- if WIC turns out not to cache the whole frame, the candidate's value itself
  must be reconsidered (H4 confirmed)
```

## 20. Related

```text
I-1  docs/implementation-briefs/I-decode-once-resize-twice.*   DEFERRED
I-1S docs/implementation-briefs/I-shared-decode-stability.*   cause found
D2   docs/build-history/0.9.4.28.*                            Path C DEFERRED
D3   docs/build-history/0.9.4.29.*                            PASS
```
