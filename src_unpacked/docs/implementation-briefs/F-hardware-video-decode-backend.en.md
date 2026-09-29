# Implementation Brief — Node F Hardware Video Decode Backend (Pre-register)

Status: **PRE-REGISTERED** — this document contains the brief only; the probe/implementation
code must not precede it.
Version reference: v0.9.4.42 (`395ae57`)

---

## 1. Purpose

Node E (Adaptive Video Decode Planner) was closed at 0.9.4.42. E set out to "reduce
decoding more frames than necessary", but sparse seek could not be proven exact, so it
was **refused for production adoption**, and the outcome was "sequential is confirmed".

Node F works on a **different axis**. It is not about *which* frames to sample, but about
*how* the selected frames are decoded: replacing software FFmpeg decode with **GPU
(NVDEC) hardware decode**. This is the axis E never addressed.

## 2. Entry condition

- Node E closure confirmed (`docs/development-progress.*`, `docs/build-history/0.9.4.42.*`)
- The earlier E brief's condition "F does not start before E is fully finished" is met
- `ExactnessPolicy::RefuseAll` and the production sequential decode path are **not changed**

## 3. Scope

### 3-1. Actual scope of this F — a single backend

```text
Primary backend candidate: NVIDIA NVDEC
Actual investigation / experiment / implementation scope of this F: NVDEC
```

- Actual investigation, experiment, implementation and verification of **other** hardware
  decode backends are **not included in this F**.
- This reflects the roadmap's NVDEC-first priority (Node F).

### 3-2. The architecture is not fixed to NVIDIA

The backend abstraction is expected to **accommodate additional hardware decode backends
such as Intel/AMD in future**. Their implementation and verification are, however,
**out of scope for this F**.

The distinction matters:
- **This F**: actually investigate, verify and implement NVDEC, alone.
- **G and later**: design so that adding other backends is **low cost**.
- The abstraction target is "a form where the backend is replaceable", but
  **premature generalization is not the goal.** Abstracting an interface that has not
  been verified is itself a risk.

## 4. Out of scope for this F

- Reintroducing or reimplementing sparse seek
- Relaxing `ExactnessPolicy::RefuseAll`
- Activating `AllowVerified`
- Changing the production predicate (`ft + 0.05 >= target`) or the tolerance
- Reversing the E-3B conclusion
- Actual implementation/verification of Intel/AMD/other hardware decode backends
- Changing Node E's sampling logic

## 5. Verification principles inherited from E-3B (mandatory)

These are **entry conditions** for Node F, not options.

### 5-1. The baseline must be the production path

```text
production sequential decode (software FFmpeg)
        vs
experimental optimization
```

**Forbidden**: comparisons of the form `seek implementation A vs seek implementation B`.
E-2A/E-2B exactness was **self-referential** in exactly that form, and those results were
invalidated (`docs/build-history/0.9.4.42.*`, worklog `E-3B-REF`).
Two implementations of the same family **cannot validate each other, because they can
share a defect.**

### 5-2. A vs A control

Experimental results should be judged alongside an `A vs A control` (the same condition
run twice) wherever possible. If the control is not 0, differences between runs cannot be
attributed to the experiment itself.

### 5-3. Verify the following separately

For video decode related changes, confirm each of these **separately**:

- sample count
- sample position (PTS)
- decoded result (pixel)
- fingerprint
- failure / recovery state
- differences by codec / container

Never conclude exactness on the grounds that "the tests passed".

### 5-4. Not admissible as exactness evidence

- container index values
- measurements taken inside a probe
- seek landing consistency
- `SparseSeekStats`-style instrumentation

Nothing is exactness evidence until a production-parity proof is recorded.

## 6. Investigation targets (pre-register before experiments)

Investigate and record the following before writing code. Commit the investigation
first.

- The NVDEC path in FFmpeg 9.0.1: `avcodec_get_hw_config`,
  `AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX`
- The FFmpeg configuration in the current build: whether the FFmpeg in
  `vcpkg_installed` was built with `cuda` / `nvdec` / `cuvid`
- Where the GPU abstraction (`src/gpu_backend.*`) governs decode, if at all
- How deeply `VideoDecoder` (`src/video_decoder.*`) is bound to the software path
- The separation point between the GPU hash (96x96/32x32) pipeline and decode
- Behavioural differences of the video path between `build-windows-gpu` and
  `build-windows-cpu`
- The GPU / driver / CUDA versions in this environment

**Open question (must be answered explicitly before experimenting)**:
- Is hardware decode output **pixel-identical** to software decode? And what prevents
  this work from repeating the E-3B trap of comparing implementations of the same family?

## 7. Measurement plan (pre-register)

- Dataset: first confirm the state of the video dataset. If absent, document the
  preparation procedure first
- Run count: 5 or more recommended; record median / min / max / range
- Metrics: decode wall time, frames/s, GPU/CPU split, fallback count, VRAM
- Exactness: **pixel and fingerprint** comparison against production sequential decode
- Record unmeasured values as `Not measured`. Do not estimate.
- Do not declare an improvement that falls within the measurement error range.

## 8. Fallback principles

- Software FFmpeg remains the **reference/fallback** path
- Initialization / seek / frame mapping / decode failure falls back **per file**
- Fallback must never be silent. Record backend selection and fallback reason in the
  benchmark
- If NVDEC brings no benefit for a file, CPU decode or another path must remain
  selectable
- Do not spread low-level vendor APIs through the high-level engine

## 9. Expected artefacts

- `docs/build-history/<version>.*`
- Updated `docs/development-progress.*`
- On implementation: backend separation structure, benchmark/telemetry fields,
  regression tests

## 10. Completion criteria (draft — to be fixed at kickoff)

- CPU path PASS (software decode results unchanged)
- GPU build PASS
- **Video exactness PASS against the production baseline** (per §5.1)
- Fallback PASS
- No unexplained mismatch
- Per-file fallback is **demonstrated by measurement** for codec / profile / pixel-format /
  bit-depth that NVDEC does not support

## 11. Risks to recognise before kickoff

- **Premise-collapse risk**: the FFmpeg build in this environment may not include NVDEC.
  The existence of `build-windows-gpu` means the GPU **hash** is computed on the GPU; it
  does **not** mean **decode** runs on the GPU. This must be checked first in §6.
- **Exactness risk**: hardware decode and software decode may differ. Which differences
  are acceptable must be stated **together with the basis for that judgement**, and must
  not be settled by an arbitrary threshold.
- **Scope drift**: abstracting other backends up front crosses the §3-2 boundary and
  leaves an unverified interface behind.
- **Performance premise**: at E-3B, sparse showed an exactness violation together with a
  17 % regression. NVDEC must likewise **not be adopted on a performance argument before
  exactness is verified.**

## 12. Related documents

- `docs/build-history/0.9.4.42.*` — E-3B, sparse rejection and the exactness lesson
- `docs/worklog/0.9.4.*` — `E-3B-REF`, `E-CLOSE`
- `docs/implementation-briefs/E-planner-calibration.*` — E COMPLETE conditions, closed
- `docs/development-roadmap.*` — Node F scope
- `docs/architecture/benchmark-telemetry-roadmap.*` — telemetry design
