# E-exact-sparse-seek — E-2B Exact Sparse Seek + Adaptive Sampling Planner (Pre-register)

Status: **PRE-REGISTERED** (committed before the probe change and the measurement)

```text
Base         v0.9.4.39 / b8a125e
Version      0.9.4.40
Experiment   E-2B
Previous     E-2A = CONDITIONAL (exactness failed)
Production change   NONE expected — the planner activates only on test/benchmark paths
Related brief E-video-decode-planner.{ko,en}.md
                 E-adaptive-sampling-strategy.{ko,en}.md
```

## 1. Work Order (Directive §1, and this order is followed)

```text
1. resolve the exactness cause
2. implement exact sparse seek
3. re-verify frame-level parity
4. measure performance per condition
5. design planner rules
6. implement a planner prototype
7. verify regression
```

**No performance threshold is created first. The production planner is not
declared complete before sample parity passes.**

## 2. The E-2A Exactness Failure — Cause Hypothesis

The product sampling predicate is at `video_decoder.cpp:127`:

```text
ft + 0.05 >= target
```

which selects the **first** frame with `pts >= target - 0.05`.

The E-2A sparse prototype sought to `target`. That lands on a keyframe
`K <= target`, and when `K > target - 0.05` the frame the product chose sits
**before** K and is unreachable. That was the cause of `tsLater`.

**Hypothesis: seeking to `target - 0.05` instead makes the landing keyframe
`K'` satisfy `K' <= target - 0.05`, so the frame the product selects always lies
at or after the landing point and is reachable.**

The predicate, the tolerance and the target are **not changed at all.** Only the
candidate's seek target changes.

## 3. Absolutely Forbidden Changes (Directive §5)

```text
changing the production predicate                  forbidden
changing the 0.05 tolerance                        forbidden
changing target timestamps                         forbidden
arbitrary frame substitution to match sample count forbidden
allowing pixel mismatch                            forbidden
judging "close enough frames are identical"        forbidden
relaxing a threshold                               forbidden
accepting PASS because only the final fingerprint matches  forbidden
```

## 4. Exactness Verdict Criteria (Directive §14)

```text
sample count     identical
sample order     identical
sample timestamp identical
frame identity   identical
pixel bytes      identical
tsLater          0
```

## 5. Handling a Remaining Failure (Directive §34)

A condition that breaks exactness does not get sparse seek; it is classified
`SequentialPreferred`. **Resolution is not forced.** The planner must be able to
detect that condition at runtime.

## 6. Decoder State After Seek (Directive §8)

The frame the product selects satisfies `pts >= target - 0.05`, so exact
reproduction **requires the first decoded frame to be at or before the requested
seek point.** This is measured:

```text
seekRequestPts
firstDecodedPts (landing)
firstSelectedPts
targetPts
is landing <= seekRequestPts ?
```

If that is violated, the file is excluded from sparse-seek candidates. This is
exactly the classification §34 requires, and it doubles as the planner's
runtime signal.

## 7. Planner / Executor Separation (Directive §28·29)

```text
VideoSamplePlan
    ↓
AdaptiveSamplingPlanner        <- only decides WHICH strategy; decodes nothing
    ├── SequentialPreferred
    ├── SparseSeekCandidate
    └── SparseSeekUnavailable
    ↓
ExactSeekExecutor / SequentialExecutor   <- only executes
```

The planner **classifies**; the executor **executes**.

## 8. Planner States (Directive §20)

Three outcomes suffice instead of a complex score:

```text
SequentialPreferred
SparseSeekCandidate
SparseSeekUnavailable
```

## 9. Hard Fallbacks (Directive §21·22)

Correctness outranks performance. All of these are evaluated **before** any cost
comparison.

```text
decode unavailable (av1)           -> SparseSeekUnavailable
GopUnavailable                     -> SparseSeekUnavailable
GopEstimated                       -> SequentialPreferred (conservative)
seek landing violation             -> SequentialPreferred
tsLater/tsBehind > 0               -> SequentialPreferred
pixel mismatch                     -> SequentialPreferred
GOP large relative to sample spacing-> SequentialPreferred
sparse expected frames >= sequential-> SequentialPreferred
otherwise, exactness held and cost favourable -> SparseSeekCandidate
```

## 10. Production Default (Directive §2)

```text
Production default = Sequential   (not changed)
```

The planner candidate activates only on explicit test/benchmark paths.

## 11. Measurement (Directive §24·25)

Interleaved repeats, minimum 3 and preferably 5, with the median as the
headline. Telemetry ON/OFF is used to separate instrumentation overhead.

## 12. Dataset Rules (Directive §37·38)

Personal real content: no commits, no public artifacts, keep the existing
gitignored directory, record only metadata in the manifest. The standard image
dataset `e8f8fa6a..e2640a` is **never modified.**

## 13. What Is Not Done (Directive §30·31·32·33)

```text
executing hardware decode          forbidden (HardwareCandidate expression only)
only CPU software decode + sparse seek is completed first
adding an AV1 decoder / dependency change  forbidden
writing a new codec parser to resolve the 4K GOP disagreement  forbidden
  -> if the cause is unknown, treat it as GopEstimated
changing the production sampling policy     forbidden
```

## 14. Success Criteria (Directive §44–47)

```text
Gate A  sample count / order / timestamp / frame identity / pixel / tsLater all PASS
Gate B  improvement in the representative winner group, and the planner choosing
        sequential in the loser group
Gate C  the three planner states decided reproducibly, with the inputs recorded
Gate D  E/F boundary maintained (E = sampling/decode strategy, F = hardware backend)
```

## 15. Artefacts

```text
tests/video_sampling_strategy_probe.cpp  (exactness + planner + selfcheck)
```

`docs/experiments` is not created.
