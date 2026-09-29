# E-adaptive-sampling-strategy — E-2A Adaptive Sampling Strategy Measurement (Pre-register)

Status: **PRE-REGISTERED** (committed before the probe and any measurement)

```text
Base         v0.9.4.38 / 425def6
Version      v0.9.4.39
Experiment   E-2A
Previous     E-1 = PASS (0.9.4.38)
Production change   NONE — measurement + prototype only
Related brief E-video-decode-planner.{ko,en}.md
```

## 1. Problem

E-1 measured it:

```text
decoded 12,573 frames for 140 samples = 89.81x waste
decode share of sweep = 97.57 %
```

The cause is structural. `make_sample_plan` picks samples from duration alone,
but `framesAt` performs **one backward seek to the first target and then sweeps
sequentially to the last** (`src/video_decoder.cpp:107-108`). The number of
frames needed and the number decoded are structurally decoupled.

E-1 estimated the potential sparse-seek gain arithmetically and **explicitly
recorded that this was arithmetic, not measurement.** E-2A's job is to measure
it.

## 2. What Is Not Done Here (ordering principle)

```text
measurement → prototype → exactness → performance → production decision
```

The production sampling policy is **not replaced.** The candidate calls the real
decoder but lives on an experiment path fully separated from production.

## 3. Dataset — correcting an E-1 record

E-1 recorded HEVC/AV1/VP9 as `NOT_AVAILABLE_IN_CURRENT_ENVIRONMENT`. **That
statement was about generation.** The survey found real encoded HEVC and AV1
content on this machine, so E-2A uses it.

Per the AGENTS rule, the original record is not overwritten; the correction
history is kept:

```text
original (E-1): HEVC/AV1/VP9 = NOT_AVAILABLE_IN_CURRENT_ENVIRONMENT
cause         : the bundled FFmpeg 9.0.1 has no software ENCODER for them.
                only h264_mf (MediaFoundation) actually works.
correction    : generation is impossible, but real encoded content exists on
                this machine. E-2A uses it.
```

A **newly discovered** constraint also exists:

```text
AV1 = NOT DECODABLE IN THIS BUILD
  "Your platform doesn't support hardware accelerated AV1 decoding"
  "Error submitting packet to decoder: Function not implemented"
```

`-decoders` lists `av1`, but the native implementation does not work. This is a
**decoding** constraint rather than a generation one, and E-1 could not have
known it.

## 4. Dataset Composition

`C:\project\validation\video_dataset_real` (gitignored, never committed).
The standard image dataset (`e8f8fa6a..e2640a`) is **never modified**.

### 4.1 Real content

Only short, technically representative **segments** are cut with `-c copy` from
the user's media library. Stream copy preserves the native GOP, bitrate and
pixels exactly. No full personal file is duplicated and nothing is committed.
Sources are selected **by property**, never by (encoding-fragile) filename.

| file | codec | resolution | fps | duration |
|---|---|---|---|---|
| real_h264_1920x1080_030s | h264 | 1920x1080 | 30 | 29.0 s |
| real_h264_1920x1080_300s | h264 | 1920x1080 | 30 | 270.5 s |
| real_h264_1080x1920_030s | h264 | 1080x1920 | 30 | 29.9 s |
| real_h264_3840x2160_020s | h264 | 3840x2160 | 60 | 20.1 s |
| real_h264_0648x1080_030s | h264 | 648x1080 | 30 | 30.0 s |
| real_h264_1360x0808_030s | h264 | 1360x808 | 30 | 8.9 s |
| real_hevc_1920x1080_030s | hevc | 1920x1080 | 30 | 30.1 s |
| real_hevc_1360x0808_030s | hevc | 1360x808 | 30 | 8.9 s |
| real_av1_1920x1080_030s | av1 | 1920x1080 | 30 | 30.0 s → **not decodable** |

### 4.2 Controlled-GOP synthetic set (extends the E-1 set)

Real content alone cannot **control** GOP length. Directive §6 asks for
short/medium/long GOP and intra-only, so controlled points are created with `-g`:

```text
synth_mpeg4_640x360_25fps_030s_gop5 / gop15 / gop60 / gop250
synth_ffv1_640x360_25fps_030s_intra   (-g 1, every frame a key frame)
```

### 4.3 The dataset is reported in two layers

```text
Synthetic dataset   : algorithm sanity check, controlled GOP points
Real-content dataset: what sparse seek actually does on real files
```

## 5. E-1 Pre-measurement (probe reuse, baseline established)

The E-1 probe was reused as-is to establish the real-content baseline first:

```text
files_measured                  = 14
probe_matches_production_frames = NO (13/14 byte identical)
  unfaithful: av1 (production framesAt96Plus32 failed — not decodable)
decoded / emitted / ratio       = 17,164 / 208 / 82.52x
decode share of open+decode     = 99.90 %
```

### 5.1 Real per-codec GOP (E-1 had only two data points)

| file | decoded | key packets | GOP estimate |
|---|---|---|---|
| real_h264_1080x1920_030s | 902 | **4** | **≈ 225** ← long GOP |
| real_h264_3840x2160_020s | 1199 | 11 | ≈ 109 |
| real_h264_1920x1080_300s | 8115 | 271 | ≈ 30 |
| real_h264_0648x1080_030s | 900 | 30 | = 30 |
| real_hevc_1920x1080_030s | 900 | 33 | ≈ 27 |
| real_h264_1920x1080_030s | 868 | 37 | ≈ 23 |
| real_h264_1360x0808_030s | 265 | 9 | ≈ 29 |

Real GOP spans **roughly 23 to 225**, a 10x range. E-1 had only mpeg4 11.9 and
h264 50.0. The **GOP 225 file is the worst-case candidate for sparse seek.**

### 5.2 Real per-file waste ratio

```text
29.4x (1360x808 8.9s)  ~  53–58x (30s class)  ~  109x (4K)  ~  231.9x (270.5s)
```

The 270.5 s real h264 file decodes **8,115 frames to obtain 35 samples**.

### 5.3 Correction to E-1's H2 — content complexity also affects per-frame cost

```text
h264 1920x1080 30s : 3036 ms / 868 frames =  3.50 ms/frame
hevc 1920x1080 30s : 10050 ms / 900 frames = 11.17 ms/frame   (3.2x h264)
h264 1080x1920 30s  : 9057 ms / 902 frames = 10.04 ms/frame   (2.9x, SAME pixel count!)
h264 3840x2160 20s  : 17661 ms / 1199 frames = 14.73 ms/frame
```

`1920x1080` and `1080x1920` have the **same pixel count** yet differ 2.9x in
per-frame cost. E-1's H2 ("per-frame cost = f(codec, resolution)") is
**incomplete**: bitrate, GOP and content complexity are additional factors.
E-2A records this but does not add them as planner inputs (§10 reduction rule —
no measurement basis).

## 6. GOP Measurement — Independent Dual Measurement (Directive §9)

GOP is **not** defined by `AV_PKT_FLAG_KEY` alone. The starting point is that
E-1 measured ffv1, which is intra-only, at 63/750 — the flag's standalone
reliability is already broken.

All three values are collected and the disagreement is **not hidden**:

```text
A. packet key flag    : AV_PKT_FLAG_KEY positions → key distance
B. decoded I-picture  : AVFrame::pict_type == AV_PICTURE_TYPE_I positions → I distance
C. mismatch           : number of positions where A and B disagree
```

### 6.1 Confidence states (Directive §10)

```text
GopKnown        A and B agree and the distribution is regular
GopEstimated    only one of A/B is available; cross-validation impossible
GopUnavailable  neither can be trusted
```

**Under `GopUnavailable` the planner must not compute a seek cost that depends
on GOP.** No sparse-seek candidate is generated in that state.

### 6.2 No fabricated estimates (Directive §11)

Values like `mpeg4 ≈ 12` or `h264 ≈ 50` are not turned into per-codec constants.
Where evidence is thin it is recorded as `insufficient evidence`.

## 7. Sampling Baseline Fixed (Directive §12)

```text
plan  = msf::make_sample_plan(duration)     ← real production function
frame = msf::VideoDecoder::framesAt96Plus32 ← real production function
```

The probe does not reimplement the sample plan. As with the E-1 probe, fidelity
is confirmed by byte comparison.

## 8. Sparse-Seek Candidate (Directive §13·14)

**Exactly two strategies exist.** No planner threshold is created.

```text
Baseline Sequential  : production framesAt96Plus32 (unchanged)
Sparse Seek Candidate: prototype, fully separated from production
```

Candidate concept:

```text
for each target:
  seek to the nearest keyframe at or before the target (AVSEEK_FLAG_BACKWARD)
  avcodec_flush_buffers, then decode only up to the target
  emit the sample, move to the next target
```

Accuracy is the priority here. Sparse seek locates the "nearest preceding
keyframe" via `AVSEEK_FLAG_BACKWARD`, so it **may emit a different frame** than
the baseline's forward sweep selects. When that happens pixel parity breaks and
it is reported.

## 9. Exactness Criteria (Directive §16·17·18)

Not only the final fingerprint: the sample frames themselves are compared.

```text
sample count parity
sample order parity
sample timestamp parity   (against time_base; no invented tolerance)
sample pixel parity      (32x32 gray, byte level)
```

### 9.1 Floating Timestamps (Directive §18)

Because of PTS/time_base, `target_pts` and `actual_pts` need not be the same
integer. **The meaningful tolerance range in time_base units is investigated
first, and only then decided.**

```text
measured: record target_pts, actual_pts and |target_pts - actual_pts| in
          time_base ticks
verdict :  state the 0.05 s tolerance already used at video_decoder.cpp:127
          alongside its value in time_base ticks, so which one applies is
          unambiguous. No new tolerance is invented.
```

## 10. Measured Items (Directive §19–25)

```text
seekCalls  seekTime  preTargetDecodedFrames  sampleDecodedFrames
totalDecodedFrames  sampleEmittedFrames
```

Metrics:

```text
decoded / emitted ratio                    (E-1: 89.81x → measure the real reduction)
extra_decoded_per_sample
  = (candidate decoded - emitted) / emitted
```

The §22 sampling spacings (96/128/192/256/384/512) are **not** values the
production plan uses, so they are labelled an explicit exploratory benchmark.
§23 duration (short/medium/long — 270.5 s must be included), §24 fps, §25
resolution and §26 codec are each measured.

## 11. Adversarial Conditions Must Be Found (Directive §27)

The candidate is not assumed to always be faster. A condition where it is
actually slower must be found, otherwise the planner means nothing. Targets:

```text
very short sample interval / very short GOP / large seek overhead /
targets almost always adjacent to a keyframe / long GOP such as 225
```

**No threshold is created before these measurements** (Directive §28).

## 12. Performance Measurement (Directive §33·34)

```text
baseline → candidate → baseline → candidate ...  (interleaved)
at least 3 runs per condition, median recorded
```

Recorded separately (Directive §34):

```text
seek elapsed / decode elapsed / frame decode count / sample conversion / total elapsed
```

It must be shown that seek being fast does not imply the whole run being fast
when the decode after it is large.

## 13. Instrumentation Overhead Separation (Directive §35)

v0.9.4.37 found `probeOsFileOpen` instrumentation overhead leaking into the
benchmark. To avoid repeating that:

```text
measure a path with and without telemetry separately, so instrumentation cost
is isolated; the headline uses the value least affected by instrumentation
```

## 14. E / F Boundary (Directive §36·37)

```text
E = which frames are approached in which manner   ← this work
F = how the chosen decode backend runs on CPU/GPU hardware   ← not implemented
```

- Node I is COMPLETE. **I-2 code is not modified again.**
- Sparse seek is meaningful with a software decoder, so it is validated in E.
- No NVDEC, CUDA video decode, D3D11VA, QSV, Vulkan or hardware frame path.
- `av_hwdevice_*` is not called.

## 15. Success Criteria (Directive §44)

```text
Gate A  real-content dataset + manifest + fingerprint + codec/resolution/duration classes
Gate B  packet key flag / decoded I-picture / mismatch / confidence state
Gate C  real seek + identical sample plan + count/order/timestamp/pixel parity
Gate D  decoded count / ratio / seek count / seek elapsed / decode elapsed / total
Gate E  CPU CTest PASS / GPU CTest PASS / existing probes PASS / no production regression
```

## 16. Non-goals (Directive §45, §48)

```text
replacing the production sampling policy   forbidden (E-2B)
fixing a planner threshold                 forbidden
implementing an adaptive algorithm         forbidden (only two strategies compared)
changing the production decode path        forbidden
NVDEC / CUDA video decode / D3D11VA / QSV / Vulkan / hw frame   forbidden
dependency upgrade                         forbidden
changing the standard image dataset        forbidden
```

E-2A may PASS **without** production integration (Directive §45). If sparse
seek is not consistently faster across codecs and conditions, E-2B needs a
further model.

## 17. Artefacts

```text
tests/video_sampling_strategy_probe.cpp   (baseline / sparse / compare / manifest / selfcheck)
scripts/validation/e2a_make_real_dataset.ps1
```

`docs/experiments` is not created.
