# I-shared-wic-source-production — I-2 Shared WIC Source Production Integration (Pre-register)

Status: **PRE-REGISTERED** (committed before the production integration)

```text
Base         v0.9.4.35 / 6c981f7
Version      v0.9.4.36
Experiment   I-2 production integration
Product change   YES — limited to the I-2 structure
Previous step     I-2 = READY FOR PRODUCTION IMPLEMENTATION (v0.9.4.35 gates A–D PASS)
```

## 1. Purpose

Move the I-2 structure — validated in the probe and past every gate in
v0.9.4.35 — into the real production `ImageDecoder`, and confirm complete
parity with existing behaviour.

**This is not an EXIF feature version.** It preserves the v0.9.4.35 EXIF
semantics exactly and promotes only the structural optimization.

## 2. Survey — Where the Duplication Actually Lives

```text
src/image_verify.cpp:82-83  verifyBuffersFor()
  dec.decode(path, kDim, kDim, f, &tel->decodeFull)          <- one WIC pipeline
  dec.decodePreserveAspect(path, kDim, a, &tel->decodeAspect) <- one WIC pipeline
```

Per file, the factory, decoder, frame, metadata and orientation source are
built **twice**. That is the duplication D3 measured.

No other caller needs both outputs, so no gain is available there:

```text
src/media_pipeline.cpp:33,45   decode() only           (32x32 perceptual hash)
src/media_pipeline.cpp:110     decodePreserveAspect() only (128 crop fingerprint)
src/monitor.cpp:307            decodePreserveAspect() only
```

`verifyBuffersFor` is therefore **the only production path that requests both**,
and the entire integration gain lands there.

## 3. Production Structure

One single source of truth, called by all three public entry points:

```text
decodeWicBranches(path, fw, fh, maxDim, wantFixed, wantAspect, f, a, telFixed, telAspect)
  CoInitializeEx                once
  WIC factory                   once
  CreateDecoderFromFilename     once
  GetFrame(0)                   once
  EXIF orientation source       once   (reuses exifOrientationToTransform)
  shared IWICBitmapSource
    ├─ scaler -> converter -> CopyPixels -> f
    └─ scaler -> converter -> CopyPixels -> a
  release all COM objects -> CoUninitialize

decode()                 -> wantFixed=true,  wantAspect=false
decodePreserveAspect()   -> wantFixed=false, wantAspect=true
decodeBoth()             -> wantFixed=true,  wantAspect=true    <- new
```

- `decode()` and `decodePreserveAspect()` keep their **signatures, return
  values and semantics**; only their internals move behind the shared helper.
  Single-output callers are unaffected.
- `decodeBoth()` exists for the path that needs both results at once
  (`verifyBuffersFor`).
- **No intermediate GrayImage.** I-1's two-step resample is not reintroduced.

## 4. Telemetry Allocation (meaning preserved)

Invariants held since D3 / v0.9.4.29:

```text
decodeFull.calls        == 1 per file
decodeAspect.aspectCalls == 1 per file
decodeFullTotalMs + decodeAspectOnlyTotalMs == whole call time (split identity)
```

They are preserved after integration:

```text
shared cost (COM/factory/open/metadata/orient) -> telFixed (else telAspect)
fixed branch (scaler/convert/copy)             -> telFixed
aspect branch (scaler/convert/copy)            -> telAspect
telFixed.totalMs  = shared + fixed branch
telAspect.totalMs = aspect branch
```

Every D9d/D3 key still reconstructs from the merge. The benchmark JSON schema
is unchanged. D3's **observed** "second decode = 49.60 %" is expected to fall
after integration; that is the point, not a loss of meaning.

`metaMs` and `orientMs` keep the separation established in v0.9.4.34/35.

## 5. EXIF Preserved

`exifOrientationToTransform` is reused. The query paths are not redesigned:

```text
/app1/ifd/{ushort=274}  (JPEG)
/ifd/{ushort=274}      (TIFF)
```

The orientation 1–8 fixture results must stay exactly as they are.

## 6. Display (Color) Path

`decodeWicFileAspectColor` produces color output, so it cannot share the gray
source chain. It already shares the same **`exifOrientationToTransform`**, so
the analysis lane and the display lane cannot diverge in orientation semantics.
The UI is not refactored.

## 7. Failure Behaviour Preserved

- Absent orientation metadata is a normal image, not a decode failure.
- On WIC failure the **existing fallbacks are called unchanged**
  (`decodePgm` / `decodePgmAspect`). No second fallback is invented, so the
  failure path stays byte-identical.
- `decodeBoth()` fails exactly when the two separate calls would.

## 8. Exactness Criteria

```text
dataset fingerprint e8f8fa6a..e2640a kept (must not change)
both_success        = 3341
candidate_only_fail = 0
both_fail           = 6 kept
fixed geometry/pixel  = 3341/3341
aspect geometry/pixel = 3341/3341
```

## 9. Scan Regression Criteria

```text
pairs_compared = 5,579,470
baseline_groups = candidate_groups = 457,126
verdict_diffs = 0
max_abs_score_diff = 0
group_parity = identical
```

Score, verdict, grouping and failure classification must all match.

## 10. Performance Measurement

From this version, **production end-to-end** is measured, not the probe.

```text
A. decoder itself   v0.9.4.35 production vs v0.9.4.36 I-2 production
B. full scan elapsed
```

Warm-up 1 run + 5 measured runs, median. Decoder microbenchmark, full scan,
filesystem/cache state and process startup are recorded separately and never
mixed. If performance does not improve while exactness holds, that is recorded
as-is. **No expected figure is written in advance.**

## 11. Rollback Criteria

If any of the following occurs, the integration is not declared complete; the
cause, blast radius, reproduction conditions, old vs new behaviour and whether
rollback is needed are recorded.

```text
pixel mismatch / geometry mismatch / score mismatch / grouping mismatch
EXIF regression / failure classification change / CPU or GPU build failure
CTest failure / caller regression / telemetry meaning broken
```

Code is not arbitrarily relaxed and no test is deleted.

## 12. Not In Scope

```text
XMP fallback / new EXIF framework / new container branch
new formats / GPU backend / Node E Video Decode Planner / NVDEC
Node D changes / I-1 reintroduction / dataset or fingerprint changes
similarity threshold / grouping policy / verify policy / UI behaviour
performance claims justified by relaxing exactness
```

## 13. Success Gates

```text
Gate A  production structure (shared source, duplication removed, no intermediate)
Gate B  exactness  (3341/3341, failure parity)
Gate C  scan regression (exhaustive 5,579,470 pairs unchanged)
Gate D  EXIF       (orientations 1–8 PASS)
Gate E  CPU/GPU    (build + full CTest + smoke)
Gate F  performance (production-based measurement complete)
```

All satisfied means `PRODUCTION ADOPTION = YES`. Otherwise `NO`, with a
specific blocking reason.
