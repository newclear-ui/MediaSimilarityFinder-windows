# I-exif-orientation-path-fix — EXIF Orientation Query Path Defect Correction and Post-Fix Regression (Pre-register)

Status: **PRE-REGISTERED** (committed before the production change)

```text
Base         v0.9.4.34 / 21d0653
Version      v0.9.4.35
Experiment   production defect correction (EXIF) + post-fix regression
Product change   YES — limited strictly to the EXIF query path defect
I-2 integration  FORBIDDEN in this step (I-2 does not go into production here)
```

This document is the correction contract. It is separate from I-2 production
integration.

---

## 1. Purpose

Fix the product defect that v0.9.4.34 found. The WIC metadata path used to read
the EXIF Orientation tag was wrong, so the product has never applied a rotation.

## 2. Established Facts (measured in v0.9.4.34)

```text
product path  /app1/ifd/exif/{ushort=274}  -> 0/8 resolved, 8/8 BADPROPERTYKEY
working path  /app1/ifd/{ushort=274}        -> 8/8 resolved, values 1..8 match
fixture validity 8/8 (orientation values read exactly as written)
standard dataset orient_applied_files = 0
```

Microsoft WIC's `System.Photo.Orientation` policy:

```text
JPEG  /app1/ifd/{ushort=274}
TIFF  /ifd/{ushort=274}
```

Orientation lives in the EXIF IFD (IFD0); the `exif` segment in the product
string forms an invalid key.

## 3. Production Usage Survey

**Three** sites in `src/image_decoder.cpp`, all with the identical literal and
identical logic:

| Site | Function | Role |
|---|---|---|
| :194 | `decodeWicFile` | fingerprint fixed path |
| :267 | `decodeWicFileAspect` | fingerprint aspect path |
| :321 | `decodeWicFileAspectColor` | display (color) path |

The logic is triplicated. Rather than three line edits, it is consolidated into
one shared helper. The display path (the third site) must be corrected too: a
rotated photo displayed differently from its fingerprint would make the screen
disagree with the matching result.

## 4. Minimal-Correction Principle

- Do **not** introduce a container (JPEG/TIFF) branch. The current code has
  none, and adding more structure than necessary is out of scope.
- Instead, **try the two known paths in order** and use the first that yields a
  value:

```text
/app1/ifd/{ushort=274}   (JPEG)
/ifd/{ushort=274}       (TIFF)
```

- Even without knowing the container, one of the two always resolves, so both
  JPEG and TIFF are handled without new abstraction.
- Orientation 1 is not a transform, so it must not increment `orientApplied`
  (existing meaning preserved).
- **No XMP fallback.** The problem here is the wrong EXIF path. Whether XMP is
  a product requirement is left as a separate decision.

## 5. Regression Fixtures

Synthetic EXIF JPEGs are used. The **standard dataset is not modified.**

```text
base        one JPEG from the dataset (209x248)
insert at   after SOI, after a leading APP0/JFIF if present
orientation all of 1..8
```

For each fixture the real production baseline must show:

```text
metadata query HRESULT = success
orientation value      = matches the fixture
orientation transform  = matches the expected transform (1=identity, 3=180,
                         6=90CW, 8=270CW)
fixed decode           = success
aspect decode          = success
```

## 6. Full-Dataset EXIF Coverage Re-measurement

The pre-fix `orient_applied_files = 0` was most likely caused by the wrong path
rather than by missing coverage. After the fix, record over the standard 3,347
files:

```text
total files / orientation metadata present / distribution of values 1..8
orientation applied / orientation query failures
```

## 7. Scan Regression (mandatory)

The v0.9.4.34 group result was measured against a baseline that applied **no**
rotation. Fixing EXIF can change scan results, so the earlier numbers must not
be carried forward unchanged:

```text
pre-EXIF-fix  (v0.9.4.34)  5,579,470 pairs / 457,126 groups / verdict diff 0
post-EXIF-fix (v0.9.4.35)  re-measured, reported side by side
```

## 8. Re-run the I-2 Comparison

If the baseline changes, the I-2 candidate comparison must be re-run too. The
v0.9.4.34 values (5,579,470 / 457,126 / diff 0) are preserved as the reference
and the post-fix values are recorded next to them.

## 9. Dataset Rules

```text
fingerprint  keep e8f8fa6a..e2640a
contents     keep, including SOURCES.md
both_fail 6  keep — do not remove
```

## 10. Acceptance Gates

```text
Gate A  production EXIF corrected (JPEG / TIFF / fixtures 1..8)
Gate B  I-2 EXIF parity
Gate C  scan regression (post-fix baseline / candidate / group consistency)
Gate D  I-2 readiness
```

Gate D may only be raised when A, B and C all pass. If anything is incomplete,
`CONDITIONAL` stands. **PASS is not assumed in advance.**

## 11. Not Done in This Step

```text
integrate I-2 into ImageDecoder / ScanPipeline   forbidden
XMP fallback / new metadata framework            forbidden
dataset changes / removing SOURCES.md            forbidden
deleting or rewriting the v0.9.4.34 record       forbidden
reviving I-1                                     forbidden
```

## 12. Telemetry

`metaMs` and `orientMs` were already separated in 0.9.4.34 (before that
`orientMs` was a copy of `metaMs`). Their meaning is re-confirmed in this step
and the field names are checked against what is actually measured.
