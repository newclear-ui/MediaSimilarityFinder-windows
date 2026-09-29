# E-hevc-seek-landing — E-3A HEVC Seek Landing Characterization (Pre-register)

Status: **PRE-REGISTERED** — this commit contains the brief **only** and precedes any probe change.

```text
Base         v0.9.4.40 / 57d447b
Version      0.9.4.41
Experiment   E-3A
Previous     E-2B = CONDITIONAL (pixel parity 13/14, HEVC 1080p remaining failure)
Production change   NONE
```

## 0. Process Principle — the Order Is Followed This Time

v0.9.4.40 put the pre-register brief and the probe change in the same commit.
That procedural deviation is not repeated here. The order is enforced:

```text
1. write the brief
2. commit the brief        ← this document only
3. modify the probe
4. measure
```

**This commit is step 2 and contains the brief alone.**

## 1. Purpose

Answer exactly one question:

> **Is the seek landing violation seen on HEVC solvable with the current seek
> API and decoder interaction?**

If it is, that is the evidence needed to promote exact sparse seek to HEVC. If
it is not, **HEVC = SequentialPreferred is confirmed as the final policy.**

**The goal is not to force HEVC onto sparse seek.** The goal is to prove whether
it is technically safe, and if it is not, to draw a clear boundary.

## 2. Facts Observed So Far (v0.9.4.40)

```text
pixel parity = 13/14        one failing file
files with tsLater = 1
failing file = real_hevc_1920x1080_030s.mp4
necessary condition violated = firstDecodedPts > seekRequestPts
the planner detects this and classifies SequentialPreferred (detected, not resolved)
```

## 3. Hypothesis (a measurement target, not a conclusion)

```text
HEVC CRA / random-access point / decoder state
+ av_seek_frame landing semantics
```

## 4. APIs to Investigate

Availability is confirmed against the current FFmpeg 9.0.1 dependency, then
measured.

```text
av_seek_frame()            keyframe-based seek
avformat_seek_file()       decides the seek point with min_ts / ts / max_ts
AVSEEK_FLAG_BACKWARD       backward direction
AVSEEK_FLAG_ANY            requests treating a non-keyframe as a keyframe (diagnostic only)
avformat_flush()           discards demuxer internal buffers
avcodec_flush_buffers()    discards decoder internal buffers
```

## 5. Candidate Definitions

### Candidate A — current approach (baseline)

```text
av_seek_frame(stream, pts(target - 0.05), AVSEEK_FLAG_BACKWARD)
+ avcodec_flush_buffers(cc)     (identical to current production)
```

### Candidate B — avformat_seek_file

```text
avformat_seek_file(stream, min_ts, target, max_ts, flags)
```

No complex timestamp window is assumed up front. At minimum the three cases
`target - ε`, `target`, `target + ε` are compared, with ε chosen against the
**actual time_base**.

### Candidate C — AVSEEK_FLAG_ANY (diagnostic only, not a production candidate)

It has exactly one purpose:

> determine whether a non-keyframe seek removes the landing violation

If exactness breaks or decoder state becomes unstable it is **excluded from
production candidates immediately**.

### Candidate D — post-seek flush / state reset

Compares use of `avformat_flush()`. Compared in the probe only; **never placed
into production arbitrarily.**

## 6. Measured Values (Directive §11, the most important part)

For every seek, all of the following is recorded:

```text
seekRequestPts
firstDecodedPts        ← landing
firstEligiblePts       ← first frame satisfying the predicate
selectedPts            ← the frame actually chosen
targetPts
```

Necessary condition:

```text
firstDecodedPts <= seekRequestPts
```

And by the production predicate:

```text
selectedPts >= target - 0.05
```

the same frame must be selectable.

## 7. HEVC CRA Collection Items (Directive §9)

```text
packet key flag
decoded picture type
PTS
DTS
first decoded after seek
first output after seek
seek landing packet timestamp
```

How CRA / IDR / I-picture actually appear in these files is recorded.

## 8. FFmpeg Keyframe Flag Is Not Absolute Ground Truth (Directive §10)

```text
packet KEY  ≠  unconditionally a random-access-safe decode point
```

That possibility is kept open. **But it does not by itself justify concluding
"HEVC is broken."** The actual decoded picture, PTS and landing results of these
files are checked.

## 9. Exactness Criteria (Directive §12)

For each candidate:

```text
sample count
sample order
sample PTS
frame identity
pixel parity
tsLater
```

Target:

```text
pixel parity = 14/14
tsLater      = 0
```

**If solving HEVC one breaks exactness for another codec, that candidate is
rejected.**

## 10. Scope — HEVC Limited (Directive §15)

HEVC is the primary target. A minimum smoke test is kept for H.264 and FFV1 as
regression. The scope is not widened to other codecs.

## 11. No Production Planner Change (Directive §16)

The current `HEVC → SequentialPreferred` is **kept as is**. Even if candidates
B/C/D look good they are not wired into the planner immediately. **The results
are recorded first.**

## 12. Forbidden Production Changes (Directive §2·23)

```text
changing the production sampling policy   forbidden
changing the production default           forbidden
changing tolerance for HEVC exactness     forbidden
changing target timestamps                forbidden
adding pixel tolerance                    forbidden
frame substitution                        forbidden
new codec parser / HEVC bitstream parser  forbidden
NVDEC / CUDA video decode / HW decoder    forbidden
FFmpeg dependency upgrade / patch         forbidden
swapping the decoder library              forbidden
rewriting the sampling algorithm          forbidden
```

Only test/probe, telemetry and documentation are permitted.

## 13. Termination Condition (Directive §19, no infinite optimisation)

It **must** end in one of:

```text
A. HEVC Exact Sparse Seek = VERIFIED
   → promoted as an E-3B planner calibration candidate

B. HEVC Exact Sparse Seek = NOT VERIFIED
   → HEVC = SequentialPreferred confirmed as final
```

If no safe seek API guarantees identical landing, or decoder state makes
exactness impossible to guarantee stably, **the problem is not forced.** B is
not an E failure. It is the result of weighing correctness against performance
and choosing the safe fallback.

## 14. Not Done in This Version (Directive §20·21·22)

- **The 4K GOP mismatch of 11 is not the subject.** The
  `GopEstimated → SequentialPreferred` policy is kept, and no GOP parser is
  written while the cause is undetermined.
- **AV1** keeps `SparseSeekUnavailable`. No dependency change.
- **Planner calibration is not done.** No duration/fps/GOP/resolution threshold
  is fixed. The HEVC landing problem is closed first; calibration is E-3B.

## 15. Selfcheck (Directive §24)

Added without **deleting the existing 29 checks**:

```text
Candidate A landing
Candidate B landing
Candidate C landing
Candidate D landing
HEVC exactness
HEVC tsLater
HEVC pixel parity
```

## 16. Artefacts

```text
tests/video_sampling_strategy_probe.cpp  (candidates A–D added)
docs/build-history/0.9.4.41.{ko,en}.md
docs/worklog/0.9.4.{ko,en}.md
```

`docs/experiments` is not created.
