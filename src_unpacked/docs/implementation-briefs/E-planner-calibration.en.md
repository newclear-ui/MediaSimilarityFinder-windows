# E-planner-calibration — E-3B Adaptive Sampling Planner Calibration + End-to-End Validation (Pre-register)

Status: **PRE-REGISTERED** — this commit contains the brief **only** and precedes any code change.

```text
Base         v0.9.4.41 / 9d5dc26
Version      0.9.4.42
Experiment   E-3B
Previous     E-3A = PASS (HEVC_EXACT_SPARSE_SEEK = NOT VERIFIED)
```

## 0. Process — the v0.9.4.41 Order Is Maintained

```text
brief → brief commit → code → build → test → benchmark
```

**This commit contains the brief alone.** The v0.9.4.40 deviation is not repeated.

## 1. Purpose

"Is sparse seek possible" is no longer being researched. E-3A answered it. The
question becomes:

> **Is sparse safe for this file, and is it actually a gain?**

## 2. Planner Decision Order (correctness always before performance)

```text
1. correctness / capability gate
2. confidence gate
3. estimated cost comparison
4. strategy selection
```

**Whether exactness is possible always comes before a performance score.**

## 3. Confirmed Policies Reflected (E-3A result)

```text
HEVC  = SequentialPreferred  (all six seek strategies failed, no alternative API)
4K    = SequentialPreferred  (GopEstimated)
AV1   = SparseSeekUnavailable (not decodable)
```

These are **reflected as the planner's initial policy.** HEVC is not promoted back
to a sparse candidate. Any later HEVC seek research starts a new brief and
experiment.

## 4. Planner Structure — Separation Maintained (§5)

```text
VideoSamplePlan
        ↓
AdaptiveSamplingPlanner        ← classification only
        ↓
SamplingDecision
        ├── SequentialPreferred
        ├── SparseSeekCandidate
        └── SparseSeekUnavailable
        ↓
SamplingExecutor              ← execution only
        ├── Sequential
        └── ExactSparseSeek
```

The planner and the executor are not merged again.

## 5. Planner Inputs (§6)

```text
duration, fps, resolution, codec, GOP / GOP confidence, sampling density
```

Derived values such as `estimated frame count` and `sample count` are **not
reintroduced as independent inputs.** E-1's reduction conclusion is kept.

## 6. Derived Costs (§7) — Distinguished From Raw Inputs

```text
estimated_total_frames
estimated_sample_count
estimated_sequential_decode_work
estimated_sparse_decode_work
estimated_seek_count
estimated_preroll_work
```

## 7. GOP Confidence (§8)

```text
GopKnown        → sparse candidate possible
GopEstimated    → SequentialPreferred by default (E-3B initial implementation)
GopUnavailable  → SparseSeekUnavailable or SequentialPreferred
```

## 8. Capability Gate (§9·10·11)

Evaluated first:

```text
CanExactSparseSeek == false  →  SequentialPreferred   (before any cost math)
```

**An unverified codec or condition is never assumed `Verified`.** In particular
`HEVC 1080p` is false.

## 9. Cost Model (§12·13)

```text
SequentialCost ≈ sequential decode work
SparseSeekCost ≈ seek + preroll + post-seek decode + sample decode
```

Simple threshold rules are not used:

```text
forbidden:  duration > 30s → sparse
           GOP > 50 → sequential
           fps > 30 → sparse
           resolution > 1080p → sparse
```

## 10. Calibration Data (§14·15)

The E-2A/E-2B/E-3A results are organised as:

```text
codec, duration, fps, resolution, GOP, GOP confidence, sampling density,
sequential decoded frames, sparse decoded frames,
sequential elapsed, sparse elapsed, exactness
```

Four states are distinguished:

```text
Fast + Exact      → Sparse        (the only goal)
Slow + Exact      → Sequential    (cost unfavourable)
Fast + Not Exact  → Sequential    (rejected outright)
Slow + Not Exact  → Sequential
```

## 11. Overfitting Forbidden (§17)

Benchmark filenames are not put into rules:

```text
forbidden: if filename == "real_h264_270s.mp4"
forbidden: if path contains "gop225"
allowed:   codec / duration / fps / resolution / GOP confidence /
           sampling density / estimated cost
```

## 12. Insufficient Evidence (§18)

```text
EvidenceStrong / EvidenceLimited / EvidenceMissing
```

Without sufficient evidence, `SequentialPreferred`. No aggressive extrapolation
up front.

## 13. Decision Reason (§19)

```text
ExactSparseVerified / GOPUnknown / HEVCFallback /
CostNotAdvantageous / SparseUnsupported / ExactnessUnverified
```

For debugging and benchmarking; no UI exposure required.

## 14. Production Integration (§20·21·22)

```text
planner enabled + decision telemetry + actual executor + fallback
```

Controlled by a compile-time/test-time switch, and **the production default stays
Sequential.**

Fallback is a normal part of a planner decision:

```text
SparseSeek → failure → SequentialDecode     (recorded in telemetry)
```

**But a result that produced a pixel mismatch must never be accepted as a
candidate and then reported as having "fallen back".** Correctness is guaranteed
by the capability gate and the executor contract **before** any sample output.

## 15. End-to-End Validation (§24·25·26·36)

Three conditions are compared:

```text
A. Sequential baseline
B. Forced Sparse executor   (diagnostic only)
C. Adaptive planner
```

**§36 is the most important part: the real `MediaSearchEngine::scan()` production
path is used. Results are not produced by a separate probe algorithm.** The
I-2 lesson of `probe ≠ product` is applied unchanged.

## 16. Planner Confusion Matrix (§27)

```text
| Condition | Actual best | Planner | Exact | Result |
```

```text
Best + Exact        → PASS
Safe but slower     → acceptable
Unsafe selected     → FAIL
Unsafe avoided      → PASS
Unsupported avoided → PASS
```

## 17. Evaluation Basis (§28)

Planner success is not measured by GPU utilisation or sparse selection count.

> **Reduce total end-to-end video analysis time while maintaining exactness.**

## 18. False Positive / Negative (§29·30)

```text
False positive (sparse chosen → actually slower)  : add to calibration data, revise the rule on repetition
False negative (sequential chosen → sparse was faster) : do not relax immediately, accumulate evidence
```

The base philosophy is `correctness > unnecessary optimization`.

## 19. Preserving E-2B Results (§34·35)

```text
production predicate unchanged
target unchanged
tolerance unchanged
the exact sparse seek concept using target - 0.05 is kept
ft + 0.05 >= target remains the basis
```

## 20. Forbidden (§4)

```text
HEVC parser / HEVC CRA parser      forbidden
FFmpeg source change / version upgrade forbidden
hardware decode / NVDEC / CUDA video decode / D3D11VA / QSV / Vulkan forbidden
codec-specific decoder swap         forbidden
production sampling tolerance change forbidden
ft + 0.05 >= target change           forbidden
target timestamp definition change  forbidden
"similar frame" relaxation          forbidden
filename-specific workaround        forbidden
hardcoding a personal media path     forbidden
```

## 21. Conditions for Changing the Production Default (§44)

```text
exactness PASS + planner regression PASS + end-to-end PASS + fallback PASS
```

If any fails, `PRODUCTION DEFAULT = Sequential` is maintained.

## 22. Selfcheck (§52)

```text
HEVC → Sequential
AV1 → Unavailable
GopEstimated → Sequential
GopKnown + exact sparse + cost beneficial → Sparse
exactness unsafe → Sequential
fallback → Sequential
```

All existing E-2B/E-3A tests are preserved.

## 23. E COMPLETE Conditions (§45) — **CLOSED (user decision after 0.9.4.42)**

```text
E-1 pipeline understanding + baseline
E-2 adaptive sampling candidate implemented and verified
E-3 planner calibration              E-3B complete / NOT ACCEPTED
E-3C                                closed within Node E's scope (no separate Stage
                                    promotion, no migration to F)
E-4-equivalent production integration + end-to-end validation
                                    closed within Node E's scope
```

Plus CPU path PASS, GPU build PASS, video exactness PASS, fallback PASS, and no
unexplained mismatch.

### 23-1. Reason for closure

E-3B resulted in sparse seek being **refused for production adoption**
(`EXACTNESS = DISPROVEN`, see `docs/build-history/0.9.4.42.*`). Because
`ExactnessPolicy::RefuseAll` makes the sparse production path unreachable, no
"qualitative" path is left for E-4 to integrate.

Therefore **E-4 must not be read as grounds for reintroducing sparse.**

- **E-3C** is closed within Node E's scope. It is not promoted to a separate roadmap
  Stage and not migrated to F. Only the fact that it may inform F's architecture in
  future is recorded as a note; **it is not migrated as a work item.**
- **E-4** is closed within Node E's scope.

The `ExactnessPolicy::RefuseAll` default and the production sequential decode path are
retained.

**F starts once Node E closure is confirmed.** (Original wording: "F does not start
before E is fully finished." — E closure is now confirmed, so the condition is met.)

## 24. Artefacts

```text
src/video_sampling_planner.{h,cpp}   planner (classification only)
src/video_decoder.{h,cpp}            exact sparse seek executor added
benchmark JSON                       decision telemetry
docs/build-history/0.9.4.42.{ko,en}.md
docs/worklog/0.9.4.{ko,en}.md
```

`docs/experiments` is not created.
