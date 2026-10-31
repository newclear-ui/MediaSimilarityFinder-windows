# V-video-content-matching — Video Content Duplicate Semantics Implementation Plan

Status: **PROPOSED / PRE-REGISTERED / DEFERRED**  
Workstream: **V — Video Content Matching** (separate product feature outside the A→B→C→D→I→E→F→G→H mainline)  
Product baseline: **0.9.4.155** · Build version: **Unassigned**  
Production code change: **NONE** — this document pre-registers engineering and validation gates only.

## 1. Purpose

Improve on uniform duration-only sampling and all-pairs comparison. Goals: (1) exclude a 10-second short and a 2-hour film from default whole-video candidates; (2) use a small set of duration-aware representative frames and temporal structure; (3) tolerate ad/intro/credit edits while prioritizing distinctive main content and temporal continuity; (4) refine only uncertain candidates; (5) distinguish retained frames from actual decoded frames; (6) separate whole-video duplicates from partial-clip search; and (7) expose recommended automatic defaults plus user-adjustable short/long-form probe intervals and frame budgets in a Video tab in Settings.

CPU/GPU decoder and hardware acceleration selection are out of scope.

## 2. Baseline Code and Constraints

- src/video_sampling.cpp: duration-only input. Intervals: <=10s:1s, <=60s:2s, <=240s:4s, <=960s:8s, <=3840s:16s, otherwise 32s; append end position if necessary.
- src/video_decoder.cpp::VideoDecoder::framesAt(): one backward seek to the first target, then sequential decode to the last target.
- src/video_fingerprint.cpp: low-variance filter, pHash, horizontal mirror, crop, scene changes, and cache writing.
- VideoBuildStats: separates decodedFrames, sampledFrames, and keptFrames; 0.9.4.155 corrected decodedFrames to count actual decoder output.
- video_similarity(): existing DTW/scene-anchor comparison is a reuse candidate, not proof that the new verdict semantics are already validated.

For 130 minutes (7,800 seconds), the current 32-second plan requests about 245 timestamps. If the sequential sweep outputs every frame, 25fps implies about 195,000 frames arithmetically. (245 is the requested-position count; ~195,000 is an estimate, not a measurement; 10–14 is the new kept-frame target.)

### Node E Boundary

Node E closed without production sparse-seek adoption because frame exactness failed; ExactnessPolicy::RefuseAll remains. V does not reopen or bypass it. Only isolated prototype measurement is allowed; production adoption is forbidden until a separate gate and explicit approval.

## 3. Separate Search Paths and Results

- Whole-video: default candidates with compatible duration cohorts and core structure.
- Partial-clip: a separate opt-in path/index to find a short clip in an arbitrary interval of a long video.
- Byte-identical file: may be checked first using a complete file hash.
- A 10-second/2-hour pair must not enter default whole-video comparison.

Result types: Byte-identical / Same full content / Edited full-content variant / Partial clip / Similar scenes only / Uncertain.

## 4. Duration Classes and Kept-Frame Budget

| Class | Duration | Policy | Target |
|---|---:|---|---:|
| G1 | Up to 15s | Full timeline, scene-change driven | 4–6 |
| G2 | Over 15s–60s | Full timeline, initial 5s probes + event refinement | 6–10 |
| G3 | 1–3m | 6–8 observation regions across the full timeline | 8–10 |
| G4 | 3–10m | 6–8 observation regions across the full timeline | 10–14 |
| G5 | 10–25m | Distributed beginning/middle/end | 9–13 |
| G6 | 25–60m | Down-weight recurring intros/recaps/credits; core focus | 9–12 |
| G7 | 60–150m | Opening + few middle anchors + reverse-time ending | 10–14 |
| G8 | Over 150m | Expand by structural complexity | 12–16 |

For a 130-minute film, observe 0–30 seconds at about 5-second intervals (about six probes; keep 1–2), find the main-content boundary between 30 seconds and 3 minutes (keep 1–2), retain one early-core frame, 2–3 middle anchors, and 1–3 late-core/pre-ending anchors. Reverse probes at T-15s/30s/1/2/4/8/12/20m are examples; store black/credits as interval metadata. Center target about 12. Not a proven optimum or quota.

## 5. Planned Implementation Stages

### V0 — Dataset / Ground Truth / Baseline

Preserve current production results. Classify samples by duration/codec/resolution/fps/structure and define pairs for byte-identical, re-encode, intro/ad/credit edits, different episodes, horizontal mirror, rapid cuts, and post-credit scenes. Personal clips remain local and are not committed. Public VCDB/VCSL/FiGVCL-family data is used only after terms are checked.

**Gate:** Do not proceed without long-form samples and repeated intro/credit cases with ground truth.

### V1 — Metadata Candidate Separation

Separate byte hash from content similarity. Build whole-video candidates in the same/adjacent cohort; exclude extreme duration pairs from the default path. Total-duration ratio is a ranking/filter signal, not a verdict. Use core-duration ratio only when main-content boundaries are valid.

**Gate:** 10s vs 2h never enters default candidates; positive controls around cohort boundaries remain discoverable.

### V2 — Structural Probe (Measurement Only)

Observe brightness/dark ratio, low-resolution color/histogram change, edge density, scene/fade candidates during sequential decode. OCR is not a first-stage requirement. Record introInterval, mainContentInterval, creditsInterval, postCreditsInterval and confidence in scratch results.

**Gate:** Measure false-boundary rate for black screens vs dark main scenes, fades, rapid cuts, credits, and post-credit scenes.

### V3 — Representative-Frame Selection

Separate Probe from Kept. Cover the full timeline for short-form; for films focus on opening/ending plus 1–3 middle-core anchors. Use normal + horizontal mirror only; never implement vertical flip. Do not count derived features from one frame as independent observations.

**Gate:** Record mean/p95 kept frames and probe count by class. Evaluate about 10–14 retained frames for 130-minute films without sacrificing accuracy to meet a quota.

### V4 — Temporal-Continuity Verification

Enforce monotonic timestamp correspondences. Check affine offset/scale first; use piecewise monotonic alignment/DTW for inserted/deleted intervals. Down-weight common logos/intros/recaps/credits by library frequency. Reject long-form candidates matching only opening+ending; require independent core evidence from separated intervals. Refine only the relevant core region when uncertain.

**Gate:** Detect re-encodes and intro/ad/credit edits while separating different episodes; measure candidate recall separately from final precision/recall.

### V5 — GUI Video Settings Tab

After V0–V4 pass their offline accuracy and cost gates, add a **Video** tab to the Settings dialog so users can adjust the validated policies. Present Short-form / Mid-length / Long-form groups while preserving internal G1–G8 cohorts and structural classification.

- Default mode: **Automatic (recommended)**. Select the existing cohort policy from duration and structure. Do not require users to force-classify each file.
- Custom mode: expose per-profile Probe intervals, representative-frame targets/budgets, and additional refinement allowance for uncertain candidates.
- Short-form (up to 60s): recommend a **5-second full-timeline frame-check interval**. Allow a user override in the proposed 1–60s range; changing 5s to 3s makes observation positions denser across the short clip's complete timeline. Representative-frame target is usually 6–10; up to 15s uses 4–6.
- Mid-length (over 60s–25m): keep structure-aware automatic policy by default. Do not recommend a user mode that uniformly samples every point in the entire video at one fixed interval.
- Long-form (over 25m): recommend a **5-second structural-boundary detail interval** and a **5-second main-content refinement interval**. Apply them only inside automatically narrowed opening/main-content-start/ending boundary windows and selected uncertain core windows. This is not a mode that scans the entire film every 5/3 seconds. Changing 5s to 3s densifies observations only inside those selected windows.
- Keep 1–3 distributed main-content anchors automatic. Representative-frame targets remain 9–12 for 25–60m, 10–14 for 60–150m, and 12–16 over 150m. Add 1–2 frames only in distinctive main-content regions for uncertain candidates.
- Partial-clip search remains a separate feature and OFF by default.

The proposed initial UI range for custom intervals is 1–60 seconds. Finalize the ceiling and per-file Probe/Kept budgets using real-data measurements; never allow unbounded extraction. A frame-check interval is Probe timestamp spacing, not the number of retained frames or actual decodedFrames.

Persist settings through the existing user-preferences layer. Freeze a settings snapshot at search start; edits during a run apply to the next search. Reflect fingerprint-affecting policy changes in cache-reuse eligibility so fingerprints from different policies cannot be mixed. Do not introduce a DB/schema/cache migration before inspecting the current cache contract.

**Gate:** Verify the Video tab, Automatic (recommended) first-run defaults, save/restart round-trip, 5s-to-3s scope, invalid-value rejection/clamping, next-run-only application, safe stale-cache/reuse behavior, and candidate recall/precision plus Probe/Kept/Decoded/time telemetry on short-form, episode, and film datasets. The UI must not present interval changes as a reduction in actual decoded frames.

### V6 — Partial Clip (Separate Proposal)

Use a timeline index/query route separate from default whole-video search. Do not assume 10–14 representative frames can find short clips from arbitrary long-video positions. Return source/query intervals and keep disabled from default search until separately approved.

### V7 — Production Adoption

Validate V0–V4 offline/shadow against ground truth and current production. Propose only parts passing both accuracy and cost gates. Review engine version only if verdict semantics change; review cache/DB migration only if persistent structural payloads require it. GUI acceptance follows the core algorithm separately.

## 6. Evaluation and Telemetry

Evaluation pairs: byte-identical, re-encode/resolution/fps changes, horizontal mirror, leading ad/intro, credit changes, mid-roll, head/tail trim, different episodes sharing intro, videos sharing only logos/music, rapid montage, and partial short clips from long-form (V6).

Record duration/structural profile/boundary confidence, probeCount, requested samples, keptFrames, actual decodedFrames, seek/landing (experimental only), decode/convert/fingerprint/compare/alignment time, candidate counts, candidate recall, precision/recall, false positives/negatives, core coverage, cache hits/bytes, kept-frame p95 and index size. Unmeasured values are Not measured/Not available, never zero.

## 7. Production Gates and Stop Conditions

- Extreme duration pairs are excluded from default whole-video search.
- Shared intros/credits alone never classify different episodes as duplicates.
- Leading ad/intro and credit edits are tolerated when independent core regions match.
- Horizontal mirror supported; no vertical-flip path.
- Cohort-level candidate recall and final precision/recall pass.
- Fewer retained frames/comparison work without unacceptable accuracy loss.
- Claim decode improvement only with measured decodedFrames and end-to-end reduction.
- Cancellation/failure does not persist partial fingerprints as complete.
- CPU/GPU/fallback paths preserve the same verdict semantics.

Stop adoption for a class if shared intro/credit false positives, dark-scene boundary errors, or reduced candidate recall exceed the accepted range.

## 8. Non-Goals

Hardware decoder implementation, production sparse seek reintroduction, vertical flip, all-pairs comparison across radically different lengths, mixing partial clips into default duplicate groups, unsupported engine/DB/cache version bumps, and declaring optimal frame counts/thresholds before measurement.

## 9. References

- Authoritative design: architecture/video-similarity-search.{ko,en}.md
- Node E decode planner: implementation-briefs/E-video-decode-planner.{ko,en}.md
- Sparse exactness rejection: implementation-briefs/E-adaptive-sampling-strategy.{ko,en}.md, build-history/0.9.4.42.*
- GPU hash/SSIM: architecture/video-gpu-hash.{ko,en}.md, architecture/video-ssim-gpu.{ko,en}.md
- IEEE Access 2026 PVCD review, DOI 10.1109/ACCESS.2026.3707321; IEEE TPAMI 2025 FiGVCL, DOI 10.1109/TPAMI.2025.3595625.

## 10. Current Status

This is a plan/pre-registration and is not added to the Active Build Queue. The Video settings tab is planned as V5 after the V0–V4 accuracy/cost gates and is not implemented today. It does not replace XMP acceptance or the S5 real-dataset benchmark. No build version, schedule, or production adoption is assigned. Until the user separately directs implementation, do not change source/tests/DB/engine.
