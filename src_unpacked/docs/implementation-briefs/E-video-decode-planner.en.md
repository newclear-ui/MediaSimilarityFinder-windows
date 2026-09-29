# E-video-decode-planner — E-1 Adaptive Video Decode Planner Research / Design (Pre-register)

Status: **PRE-REGISTERED** (committed before the production implementation and the probe)

```text
Base         v0.9.4.37 / 01b4ed1
Version      v0.9.4.38
Experiment   E-1 (Adaptive Video Decode Planner — research / baseline / design)
Product change   NONE expected — measurement + design only
Previous node    I = COMPLETE (I-1 DEFERRED, I-2 PRODUCTION, I-3 END-TO-END PASS)
```

## 1. Problem

The stated goal of node E (roadmap) is:

> Reduce workload-specific cost from decoding more frames than necessary.

That is, the cost of **decoding far more frames than the requested sample frames**
has to be reduced. The first step is to measure how strongly the current code
supports that premise.

## 2. Current pipeline — from the actual code

Survey result (all line numbers against `v0.9.4.37`):

```text
Scanner::isVideoPath            src/scanner.cpp:16   extension only, no sniffing
  ↓  (.mp4 .mkv .avi .mov .webm .m4v .wmv — no .ts)
MediaSearchEngine::processOne   src/media_search_engine.cpp:569
  ↓
processVideoRange               src/media_search_engine.cpp:518
  ↓  std::async file-level fan-out (:531)  ← not decode threads
VideoFingerprintEngine::build   src/video_fingerprint.cpp:225
  ├─ memoryLookup / loadPersistent   SQLite/LRU cache
  ├─ VideoDecoder::open              src/video_decoder.cpp:18   ← sole FFmpeg entry
  ├─ make_sample_plan                src/video_sampling.cpp:2
  ├─ framesAt96Plus32 → framesAt     src/video_decoder.cpp:94   ← the real decode loop
  │    ├─ av_seek_frame(AVSEEK_FLAG_BACKWARD)  ×1  (:108)
  │    ├─ av_read_frame / send_packet / receive_frame (:123-125)
  │    └─ sws_scale → AV_PIX_FMT_GRAY8 96x96 (:116-118)
  ├─ software 96→32 3x3 box mean    src/video_decoder.cpp:165-169
  └─ processVideoFrames              src/video_fingerprint.cpp:167
       low-variance filter → GPU pHash batch (optional) → CPU pHash
       → 96→48 thumb48 → scene change → cropFingerprints
```

**Structural facts**

| Fact | Evidence |
|---|---|
| All 34 FFmpeg symbols live in one 182-line file | `src/video_decoder.cpp`, repo-wide grep |
| That one file is also the only FFmpeg header include. `video_decoder.h` hides everything behind `void*` | `src/video_decoder.h:27-30` |
| Linking is PUBLIC on `msf_core`, propagating `MSF_HAS_FFMPEG=1` | `CMakeLists.txt:331-370` |
| **FFmpeg 9.0.1** — libavcodec 63.1.101 / libavformat 63.1.101 / libavutil 61.1.101 / libswscale 10.1.101 | `vcpkg_installed/.../version.h`, CMakeCache |
| Hardware acceleration available: `dxva2` `d3d11va` `d3d12va` | `ffmpeg -hwaccels` |
| Yet **zero** hardware symbols: no `av_hwdevice_*`, no `avcodec_get_hw_config`, no `hw_device_ctx`, no `hw_frames_ctx` | repo-wide grep, 0 hits |
| No decode threads — `thread_count`/`thread_type` unset, `avcodec_open2` options are nullptr | `src/video_decoder.cpp:30` |
| GPU is involved **only after decode**, in pHash and MSSIM batches. All of `avformat_*`/`avcodec_*`/`sws_scale` stays on CPU | `src/video_fingerprint.cpp:177-204`, `:454-476` |

## 3. Current bottleneck evidence — already visible in the code

This section is **code evidence**; Gate B confirms it by measurement.

### 3.1 Sampling looks at duration only

The entirety of `src/video_sampling.cpp:2` is:

```cpp
if(d<=10)      interval=1;  else if(d<=60)  interval=2;
else if(d<=240) interval=4;  else if(d<=960) interval=8;
else if(d<=3840)interval=16; else             interval=32;
for(double t=0;t<d;t+=interval) timestamps.push_back(t);
```

- The only input is `i.duration` (`src/video_fingerprint.cpp:244,264`).
- fps, frame count, resolution, bitrate and codec are **not inputs**.
- There is no upper bound — at a 32 s interval a 10-hour video yields ~1125
  samples, a 24-hour video ~2700.
- The bounds 10/60/240/960/3840 are exactly 2×/4×/8×/16× steps.

### 3.2 One seek, then a sequential sweep

`src/video_decoder.cpp:107-108` seeks **exactly once**, to the first target, and
then advances with `while(next<targets.size() && av_read_frame(...)>=0)`. A
seek-per-timestamp mode does not exist. The code comment says so explicitly
(`src/video_fingerprint.cpp:246-248`):

> Single-sweep decode … each sweep is a single sequential decode, never one
> seek per timestamp.

So **the number of samples and the number of actually decoded frames are
structurally decoupled.** When the interval exceeds the frame duration far fewer
frames are needed, yet the implementation decodes every consecutive frame from
the first sample to the last.

### 3.3 No keyframe / GOP awareness whatsoever

`AV_PKT_FLAG_KEY`, `keyframe`, `GOP`, `has_b_frames`, `AVDiscard`, `skip_frame`
— **zero occurrences** repo-wide. `AVSEEK_FLAG_BACKWARD` is used only to land
on-or-before the target; its keyframe-alignment cost is neither measured nor
exploited. **The keyframe/GOP cost that the roadmap lists as required node E
telemetry currently has no measurement at all.**

### 3.4 fps is computed and then never read

`src/video_decoder.cpp:33-34` computes it with `av_guess_frame_rate`, but no code
anywhere reads `VideoInfo::fps`. The sampler is entirely fps-blind.

### 3.5 No pixel format or bit depth handling

`fr->format` is handed raw to swscale (`:74`, `:116`). No `av_get_pix_fmt`, no
capability probe, no fallback pixel format. 10-bit/HDR is neither rejected nor
validated.

### 3.6 Telemetry gaps (the direct blocker for Gate B)

| Exists | Where | Problem |
|---|---|---|
| `stages.videoAnalysis.ms` | `benchmark.cpp:223` | whole `processVideoRange` — includes task spawn/join, lumped |
| `videos.buildMs` | `benchmark.cpp:332` | the entire `build()`: cache lookup, open, decode, `sws_scale`, 96→32, pHash, thumb48, crop, **cache write** |
| `videos.decodedFrames` | `benchmark.cpp:325` | **misnamed.** `stats->decodedFrames=frames32.size()` (`video_fingerprint.cpp:251`) = frames *emitted at sample points*. The real `av_read_frame` iteration count is never counted |
| `videos.sampledFrames` | `benchmark.cpp:329` | one cache hit makes it `null` + `not_measured` via a sentinel |
| roadmap's `requested sample frames` | — | **no such field** |
| seek count / latency | — | **no such field** |
| decode throughput | — | **no such field** |
| keyframe/GOP cost | — | **no such field** |
| open / demux / decode / seek / convert split | — | **the split does not exist** |

The D9d stage breakdown (`decodeOpenMs`, `decodeCopyMs`, …) is WIC/`ImageDecoder`
only; there is no FFmpeg analogue whatsoever. Building that is E-1's first
instrumentation job.

### 3.7 There is not a single video fixture in the project

```text
Recursive search of all of C:\project for .mp4 .mkv .mov .avi .webm .ts .m4v  →  0 files
Standard dataset test_sample_img_vid (3347 files)  →  0 videos
```

The deliberate exclusion is documented (`docs/test_sample_img_vid.md:116-129`):
ffmpeg output is **not byte-reproducible**, so content hashes shift between runs.
Every existing test fixture is **≤8 s, ≤256x192, 2–10 fps, mpeg4 or libx264,
synthetic lavfi** (`tests/video_scan_e2e_test.cpp:29-45`, `video_real_test.cpp:8`,
`video_reencode_test.cpp:12-14`, `video_decode_parity_test.cpp:20-22`,
`color_thumb_test.cpp:42`, `unicode_video_path_test.cpp:26-28`). There is no
long-GOP, high-resolution, high-fps, 10-bit, B-frame-heavy, VFR, multi-stream or
corrupt content.

## 4. FFmpeg capability — actually checked

`avcodec_get_hw_config()` and friends certainly exist in FFmpeg 9.0.1 (API since
FFmpeg 4.0) and are usable. No hardware decode path is **wired up** in this
version, but for assessing F viability the following was verified:

```text
ffmpeg -hwaccels   →  dxva2  d3d11va  d3d12va
```

So **hardware decode is technically possible in this build.** That is a viability
fact for F, not an implementation approval.

### 4.1 Constraint — there is almost no software encoder (directly affects dataset design)

```text
software encoders available : mpeg4, ffv1 (+ rawvideo)
hardware encoders available : h264_mf, h264_d3d12va*, hevc_mf (failed), av1_* (all failed)
absent                       : libx264, libx265, libvpx-vp9, libaom-av1  ← not in this build
(* h264_d3d12va needs an hwupload path and failed on this machine)
```

Verified: **only `h264_mf` actually succeeded** (320x180, 2 s, 50 frames, h264
confirmed). `hevc_mf` did not even create a file, and AV1, VP9, libx264 and
libx265 are all unavailable.

Directive §8 says to not force unsupported codecs into the benchmark. HEVC, AV1
and VP9 are therefore **excluded from this dataset**, and their absence is
recorded as a **constraint, not a result**. Resolving it requires a dependency
change, which §31 forbids.

## 5. Representative video dataset — design

The standard dataset is never modified (§4). A separate reference dataset is
created.

```text
location   C:\project\validation\video_dataset   (gitignored)
nature     a separate reference, distinct from the standard dataset
reproducible  generated with -fflags +bitexact -map_metadata -1
             (directly mitigating the non-reproducibility the docs call out)
```

### 5.1 Composition (only generatable codecs)

| # | codec | How | Purpose |
|---|---|---|---|
| 1 | `mpeg4` | software | baseline identical to the existing fixtures |
| 2 | `h264` (H.264/AVC) | `h264_mf` | modern inter-frame; the actual codec F targets |
| 3 | `ffv1` | software, intra-only | **every frame is a keyframe** — the extreme contrast group for the GOP hypothesis |

The three codecs make "does GOP structure change cost?" measurable. mpeg4 and
h264 are inter-frame; ffv1 is intra-only.

### 5.2 Varying axes

Per §9, the planner must not be built from the codec name alone. These axes are
crossed:

```text
duration   short / medium / long   (covers all 7 sampling ladder buckets)
resolution low / medium / high
fps        low / medium / high     (changes decoded:sampled ratio)
container  mp4 / mkv
```

### 5.3 Before expanding the axes

§10 requires keeping only the variables that actually explain decode time or
end-to-end time. So **the full cross matrix is not built first**: a small
representative set confirms whether the sampled-vs-decoded gap really appears,
and only then are the axes fixed. If the gap does not appear, node E's premise
itself comes into question.

## 6. Planner goal

```text
"Under the current conditions, how should this video be decoded?"
```

E owns **decision**; F owns **implementation**. E-1 designs the inputs, cost model
and fallback policy for that decision and establishes measurement-based evidence.

## 7. Planner inputs — candidates and the reduction rule

Per §10, not everything goes in. The full candidate list (§9) and what survives
measurement are both recorded:

```text
candidates  codec, container, width, height, fps, duration, estimated frame count,
            pixel format, bit depth, file size, sampling count, seek distance,
            keyframe interval, CPU mode, CPU live load, GPU ON/OFF,
            GPU capability, expected transfer cost
```

Reduction rule: **does it explain decode time or end-to-end time?** Variables
that do not are dropped. In particular `CPU live load`, `GPU capability` and
`expected transfer cost` have no measurement basis in E-1, so they are **not
added by guesswork**; they join when they become measurable at F implementation
time.

## 8. Planner outputs — design (not implemented)

Per §15, because no hardware backend exists, the planner must not return an
immediately executable `HardwareDecode`. It is expressed as a planning state:

```text
VideoDecodePlan
{
    samplingMode    SequentialSweep | SparseSeek | Hybrid   (only sequential exists today)
    backend         Software | HardwareCandidate
    expectedCost    estimated ms
    fallbackPolicy  FallbackToSoftware
    reason          enum (below)
}
```

**Actual enum and struct names are minimised against the real code at
implementation time.** The above is design direction, not an adoption mandate.

## 9. Reason codes (§14)

So that a wrong choice can be debugged later:

```text
GPU_OFF              user turned GPU off
NO_HW_CAPABILITY     no capability
SHORT_VIDEO          too short for hardware setup to pay off
LOW_SAMPLING_DENSITY samples are sparse, seeking may cost more
SEQUENTIAL_SUFFICIENT a sequential sweep already suffices
HIGH_TRANSFER_COST   transfer cancels the gain
CPU_SOFTWARE_PREFERRED
HARDWARE_CANDIDATE
```

No UI exposure is required; development telemetry level is enough.

## 10. Cost model (E-1 design)

Per §12, hardware is not assumed to be unconditionally faster:

```text
SoftwareDecodeCost
  = open + demux + seek + Σ(decodePerFrame) + convert + fingerprint
HardwareEstimatedCost
  = setup + Σ(hwDecodePerFrame) + transferPerFrame×frames + surface management
FallbackCost
  = wasted setup + software re-run
```

Hardware capability, device and frame configuration each require their own
decision. Because FFmpeg's official API requires consulting
`avcodec_get_hw_config()` results before deciding, **no "GPU available = choose
GPU" rule is created.**

## 11. Fallback model (§15, §23)

```text
HardwareCandidate
  ↓  no real hardware backend in current production
FallbackRequired
  ↓
SoftwareDecode          ← must always remain possible
```

The CPU fallback is never removed under any condition, and F keeps the same
contract.

## 12. E / F boundary (§13, §4)

```text
E = decision / planning     which video, under which conditions, decoded how
F = implementation          how the chosen hardware decode is actually wired up
```

E-1 does **not** call `av_hwdevice_*`. *Querying* hardware capability for F's
design is not *using* hardware decode; the former is a required fact, the latter
is F's work.

## 13. Measurement plan (Gate B)

A measurement-only probe is added under `tests/`. It does not change production
behaviour and `docs/experiments/` is not created. Per-sample collection (§18):

```text
file, codec, container, width, height, fps, duration,
requested sample count, emitted/sampled frames,
ACTUALLY DECODED frame count (av_read_frame iterations — currently absent),
seek count, seek latency,
open time, decode time, convert time, total analysis time
```

**Hardware values are not fabricated**, since F has not happened. Only the
software decode baseline is recorded.

The key measurement:

```text
decodedFrames / sampledFrames gap
```

This is the measured basis of node E's premise. If the gap does not exist, the
planner design has no foundation.

## 14. Success criteria (§33)

```text
Gate A  the real production video decode path can be explained from code;
        sampling policy and decode/seek/conversion structure confirmed
Gate B  per-sample time composition measurable + software baseline established
Gate C  list of actually measurable planner inputs fixed, unnecessary variables removed
Gate D  explainable initial cost model + software/hardware candidate decision
        structure + fallback definition
Gate E  E = decision / F = implementation boundary is explicit
Gate F  no image-path, CPU/GPU build or CTest regression
```

## 15. Non-goals (§2, §20, §34)

```text
NVDEC / CUDA video decode / D3D11VA / QSV / Vulkan / AMD / Intel implementation   forbidden
AVHWDeviceContext / hw_frames_ctx production wiring                               forbidden
GPU frame surface production path / GPU→CPU transfer optimization                 forbidden
decoder library swap / FFmpeg version change / codec coverage expansion           forbidden
changing the sampling policy itself / similarity / UI / resource mode
changing the user-facing GPU ON/OFF semantics                                      forbidden
introducing hard-coded rules without evidence (duration>30s→GPU style)            forbidden
```

Conclusions like "hardware decode is always faster", "HEVC always goes to GPU"
or "4K always goes to GPU" are **only** reached after F and a real hardware
benchmark.

## 16. Absolute rules (AGENTS compliance)

- Keep the vcpkg/dependency structure. No FFmpeg version change.
- Never round-trip a UTF-8 file through `Get-Content | Set-Content`. Verify with
  `git diff --check` / `git diff --numstat` after editing.
- Deletions only via `scripts/safe_remove.ps1` (Recycle Bin).
- Backups: max 3 each of src and portable.
