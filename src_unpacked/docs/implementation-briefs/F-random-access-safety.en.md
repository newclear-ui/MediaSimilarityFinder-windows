# Implementation Brief — F-1 Random-Access Safety Contract + NVDEC Exactness Preflight (Pre-register)

Status: **PRE-REGISTERED** — this document contains the brief only; the probe/test code must not
precede it.
Version: 0.9.4.43
Base: `v0.9.4.42` / `395ae57`

---

## 1. Problem

The F preflight established that NVDEC genuinely runs on the RTX 3080 Ti
(`Runtime PASS` / `Decode PASS`). But pixel-exactness broke on a 4K H.264 fixture
(`Pixel-exact FAIL`).

What is wanted is not a production NVDEC implementation, but the **safety contract**
that must hold *before* NVDEC is used. That is, separating "GPU decode is possible"
from "GPU decode is still exact", so that a stream that is not exact falls back to
CPU automatically. Establishing that basis is the purpose of this stage.

## 2. Current Evidence (recorded facts)

Target `real_h264_3840x2160_30fps_020s.mp4`:

```text
codec            = H.264 High, level 6.0
resolution       = 3840x2160, yuv420p
r_frame_rate     = 60/1
avg_frame_rate   = 18045/301   (59.95)
has_b_frames     = 2
duration         = 20.066667 s
nb_frames        = 1203
file start       = decode-order frame 0..22 are all P-frames, key_frame=0
first IDR        = decode-order frame 23
```

CPU software ↔ NVDEC comparison:

```text
file start / mid-GOP   → 12/12 frame mismatch   (maxAbs 148)
confirmed IDR onwards  →  0/12 frame mismatch   (PIXEL-EXACT)
```

Supporting control / eliminated hypotheses:

```text
software A vs A                      → byte-identical (deterministic)
software default vs -threads 1       → byte-identical (not threading)
both aligned to nv12 then compared   → maxAbs 135 persists (not format conversion)
1080p H.264 (IDR-start)              → 30/30 pixel-exact
HEVC Main10 (IDR-start)              →  8/8  pixel-exact
```

### Expression rules that must be honoured (directive §5)

Good:

> The CPU/NVDEC pixel mismatch observed on the currently validated 4K H.264 fixture is
> **strongly associated with a mid-GOP stream start**, and pixel-exactness was confirmed
> over the range after a confirmed IDR.

To avoid:

> NVDEC mismatch occurs on all 4K H.264.
> All mid-GOP H.264 mismatch against NVDEC.

**The generalisations above are not verified by the current data.** There is no second
4K fixture, and directive §13 forbids creating one. Whether it "repeats at 4K" therefore
remains `INCONCLUSIVE`.

### Term distinction (directive §11)

```text
Confirmed-IDR decode start   = decoding started at an actually confirmed IDR
File-start IDR fixture      = the file itself begins at an IDR
```

The exactness obtained via `-ss 2.0` is the **first**, and does not imply the second. The
two states are kept separate.

## 3. Random-Access Safety Contract

The most important artefact. It must not be defined as "H.264 supported + GPU available".

```text
Video
  ↓
Codec capability
  ↓
Decoder availability
  ↓
Random-access safety
  ↓
Exactness compatibility
  ↓
NVDEC candidate
```

If any step cannot be established → `CPU software fallback`.

### 3-1. State definitions (three)

```text
RandomAccessSafe
RandomAccessUnsafe
RandomAccessUnknown
```

The concrete enum/string names follow codebase convention and are fixed in F-2. Only the
concept is pinned here.

### 3-2. Minimum conditions investigated

| Condition | Content |
|---|---|
| **A** Is the start/seek point self-contained | Are all reference pictures needed at that decode start obtainable within the current decode sequence |
| **B** Does the first decoded frame stay at or before the requested point | Adapting E-3A's `firstDecodedPts <= seekRequestPts` |
| **C** Can the production sampling predicate be reproduced | Using `ft + 0.05 >= target` **unchanged** |
| **D** Same sample identity as the CPU baseline | Timestamp proximity alone is not a PASS |

**Parser prohibition**: no new Annex B / SPS / PPS / HEVC parser is written. The
information needed for the judgement is limited to what **FFmpeg already exposes**
(`AVPacket.key_frame`, `pict_type`, index entries, `avformat_index_get_*`).

## 4. Exactness Criteria

An NVDEC production candidate must satisfy **all** of:

```text
sample count
sample order
sample timestamp / frame identity
pixel output
```

Final criteria:

```text
diffPixelCount = 0
maxAbsDelta    = 0
```

The following are **not** PASS evidence (directive §9, §23):

```text
small mean absolute difference
mean signed difference ≈ 0
high PSNR / SSIM
matching perceptual hash
looks the same visually
"close enough"
```

## 5. Fallback Policy

F-1's conservative initial policy:

```text
RandomAccessUnsafe  → CPU software fallback
RandomAccessUnknown → CPU software fallback
```

Unverified codec/stream structures are not guessed into NVDEC. After fallback the result
must be **identical** to the existing CPU baseline (sample count / order / frame identity
/ pixel / fingerprint). **The fallback must not change the result.**

## 6. Boundary between E and F (directive §26, §27, §28)

Do not mix the E and F policies.

```text
E = sampling strategy        (Sequential / Sparse)
F = decode backend capability & safety
```

So E choosing `Sparse` does not oblige F to use NVDEC. F re-evaluates `NVDEC safe?`.

Target structure (defence in depth):

```text
E Planner
    ↓
Sparse / Sequential
    ↓
F Backend Selector
    ↓
┌──────────────────────┐
│ Random-Access Safety │
└──────────┬───────────┘
           │
      ┌────┴────┐
      │         │
    SAFE      UNSAFE/UNKNOWN
      │         │
    NVDEC      CPU
      │         │
      └────┬────┘
           ↓
    Exact sample output
```

## 7. Separating HardwareCapability from RandomAccessSafety (directive §29)

```text
HardwareCapability   = can this GPU/FFmpeg decode the codec in hardware
RandomAccessSafety   = does processing this specific stream that way match the CPU result
```

For example:

```text
H.264
HardwareCapability  = YES
RandomAccessSafety  = NO  (mid-GOP start)
→ NVDEC forbidden
→ CPU fallback
```

This distinction must remain applicable to HEVC/AV1 and other backends.

## 8. Validation Matrix (directive §10)

```text
| Input condition        | Software | NVDEC | Exact | Policy        |
|------------------------|----------|-------|-------|---------------|
| Mid-GOP start          | baseline | differ| NO    | CPU fallback  |
| Confirmed IDR onwards  | baseline | same  | YES   | NVDEC candidate|
| File-start IDR fixture | baseline | ?     | ?     | NVDEC candidate|
| Unknown safety         | baseline | n/a   | n/a   | CPU fallback  |
```

The `File-start IDR fixture` row **must be filled in** (directive §12: use only already
available material, no new fixture generation). Investigate which of the 13 dataset files
actually start at a self-contained random-access point.

## 9. Performance Preflight (directive §18, §20, §21)

Measure **only once a normal random-access condition is confirmed**. The mid-GOP mismatch
condition is not evidence of normal NVDEC performance.

```text
CPU software decode + required frame output
        vs
NVDEC + GPU→CPU transfer + required frame output
```

Where possible, measure separately:

```text
decode elapsed
frame output
GPU→CPU transfer
end-to-end
```

Do not fabricate telemetry that does not exist. A fast GPU decode can be cancelled out by
a large transfer, so record both.

## 10. Selfcheck (directive §30)

Minimum automated items:

```text
H264 mid-GOP            → RandomAccessUnsafe → CPU fallback
Confirmed-IDR decode    → RandomAccessSafe   → NVDEC candidate
pixel exact             → PASS
pixel mismatch          → FAIL
unknown safety          → CPU fallback
```

Existing E selfchecks are not removed. The production decode decision is not changed
(only `tests/`, `probe/`, `scratch/`, `documentation/` are permitted).

## 11. Non-Goals (directive §2)

Not done in this F-1:

- production NVDEC backend implementation, production video decoder change
- E planner production default / sampling predicate / `ft + 0.05` / target change
- adding a pixel tolerance, accepting "close enough" as PASS
- new H.264 / Annex B / SPS / PPS / HEVC parser
- FFmpeg source modification, version upgrade, dependency change
- CUDA kernel, GPU→CPU copy optimisation, new GPU backend
- arbitrary 4K fixture generation, personal media commit, standard image dataset change
- **PRODUCTION ADOPTION = NO**

## 12. Exit Criteria (directive §39)

| Gate | Content |
|---|---|
| **A** Safety Contract | `Safe` / `Unsafe` / `Unknown` three states defined |
| **B** Exactness | CPU==NVDEC pixel exact **reproduced** at Confirmed-IDR, mismatch **reproduced** at mid-GOP |
| **C** Fallback | mid-GOP denies NVDEC → CPU fallback → CPU baseline exact |
| **D** Performance | CPU vs NVDEC benchmark under a normal exact condition |
| **E** Regression | CPU/GPU CTest + existing E selfcheck PASS |

## 13. Next

```text
F-1 PASS/CONDITIONAL  → F-2 NVDEC backend architecture / implementation brief
F-1 fails exactness even under normal conditions → F-2 production integration forbidden,
                                                          investigate the cause first
```

## 14. Related

- `docs/build-history/0.9.4.42.*` — E-3B, sparse rejection, exactness baseline principle
- `docs/worklog/0.9.4.*` — `E-3B-REF`, `E-CLOSE`
- `docs/implementation-briefs/F-hardware-video-decode-backend.*` — F preflight (NVDEC-only scope)
- `docs/development-roadmap.*` — Node E/F scope and the defence-in-depth structure
