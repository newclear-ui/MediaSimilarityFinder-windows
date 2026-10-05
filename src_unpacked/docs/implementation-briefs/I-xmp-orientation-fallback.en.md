# Implementation Brief — I-XMP Orientation Fallback (Pre-register)

Status: **IMPLEMENTED / PRODUCTION ACCEPTANCE CONDITIONAL** — original pre-registration contract is retained; implementation is complete, while real-dataset/full-scan acceptance remains deferred.

```text
Implementation baseline  v0.9.4.44 / 1564b87
Current product baseline  v0.9.4.45 / 619f74a
Experiment   I-XMP orientation fallback
Product change   YES — XMP fallback added to the ImageDecoder orientation source
```

`I-exif-orientation-path-fix.*` explicitly forbade an XMP fallback, and that
contract is not retroactively changed. This document is the standalone contract
for XMP fallback as a separately promoted requirement.

## 1. Purpose

Define the production path that uses XMP Orientation as a fallback for files
whose EXIF Orientation is absent or unresolvable through the EXIF path. The
existing EXIF path stays as is.

## 2. Precedence

```text
EXIF query → VT_UI2 value found
  ├─ value 1..8 (valid) → use EXIF, XMP query forbidden
  └─ out-of-range value (invalid) → treated as unresolved → try XMP
EXIF absent / query failed / not VT_UI2 → unresolved → try XMP
XMP valid → use XMP
XMP invalid / absent → no transform (Rotate0)
```

EXIF wins when both carry usable values. EXIF query failure and EXIF value
invalid are treated identically as "no usable value".

## 3. Supported representation (exactly one)

Only the XMP `tiff:Orientation` integer (`http://ns.adobe.com/tiff/1.0/`),
read through the WIC metadata query path. VT_UI2/VT_UI4/VT_I4 integers or a
parseable integer string are normalized. Any other XMP representation is
`unsupported`; never claim "full XMP support".

Measured (Windows WIC): the `/xmp/tiff:Orientation` path resolves with the
value as a `VT_LPWSTR` string (e.g. `"6"`). `/xmp/exif:Orientation` and
similar return `WINCODEC_ERR_PROPERTYNOTFOUND` and are not supported.

## 4. Value normalization

Same meaning as the EXIF path:

```text
1 = identity (no transform, orientApplied not incremented)
3 = 180 degrees
6 = 90 degrees CW
8 = 270 degrees CW
2/4/5/7 = same mapping as EXIF (flip family)
```

Out-of-range or unparseable values are not guessed; they stay unresolved.

## 5. Telemetry

No new fields. Existing bucket meanings kept:

- XMP is queried only when EXIF is unresolved, so corpus-wide cost shape is
  unchanged. The extra query cost lands in `metadataMs`.
- `orientApplied` increments only when a transform is actually applied
  (regardless of source).
- `orientMs` stays the FlipRotator create+initialize cost.

## 6. Fixture contract (standard dataset untouched, standalone synthetic)

| Case | EXIF | XMP | Expected |
|------|------|-----|----------|
| A | absent | 1 | identity, orientApplied not incremented |
| B | absent | 6 | 90° CW applied |
| C | absent | 3 | 180° applied |
| D | absent | 8 | 270° CW applied |
| E | 6 | 8 | EXIF 6 applied (EXIF wins) |
| F | absent | invalid | no transform |
| G | absent | absent | existing default semantics |

## 7. Scan regression contract

Record a pre-XMP baseline and a post-XMP candidate separately on the standard
dataset. Compare: scanned/analyzed, orientation applied count, groups,
match/pair counts, verdict differences. With near-zero XMP coverage, record
implementation correctness separately from real dataset coverage instead of
concluding "no effect".

## 8. Forbidden

- Retroactive change of the EXIF correction contract
- Preferring XMP over EXIF
- Guessing invalid XMP into an orientation
- Orientation/threshold/SSIM/CandidateIndex/grouping semantics changes
- SearchReport/DB schema changes
