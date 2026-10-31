# Video Similarity and Duplicate Search Architecture

> **Design status: PROPOSED / NOT IMPLEMENTED / NOT VALIDATED**
>
> Baseline date: 2026-10-31  
> Purpose: Design a duration- and structure-aware algorithm that selects a small set of representative frames and timeline features, while separating whole-video duplicates from partial-clip search.  
> This document does not choose CPU/GPU decoder implementations or hardware-acceleration paths.

> 2026-10-31 refinement: §16 is the current controlling detail for Probe/Kept/Decoded counts, the 130-minute budget, main-content weighting, and the Node E decoder boundary.

## 1. Role and Goals

This document defines the **video-content duplicate-detection semantics** for MediaSimilarityFinder. It is separate from how a decoder obtains frames through CPU, CUDA, NVDEC, D3D11VA, or another backend.

The goal is not to store dense screenshots across the entire timeline. It is to identify the same content using a small set of representative frames plus temporal and structural evidence.

Design principles:

1. Do not treat video frames like independent photographs with exhaustive orientation variants. **Temporal order, continuity, and scene changes** are primary evidence.
2. Separate whole-video duplicate search from partial-clip search, including their candidate populations, indexes, and verdicts.
3. Use duration as the first coarse grouping axis without assuming duration determines genre.
4. Inspect the opening and ending intelligently. Long-form content uses both forward opening analysis and reverse-time ending analysis.
5. Represent black screens, logos, intros, and credits as structural intervals when an image would add little value.
6. Add dense analysis only around promising candidate matches.
7. Frame counts below are initial experimental targets, not validated optima or hard quotas.

## 2. Boundary Between Current Implementation and Proposed Design

The repository already contains these video-processing elements:

- `video_sampling.*`: duration-based sample-time planning.
- `video_fingerprint.*`: timestamps and frame hashes, mirror/crop features, low-variance filtering, and scene-change handling.
- `video_similarity`: temporal comparison using DTW, scene bonus, and dyadic-grid alignment.
- `video-gpu-hash.{ko,en}.md`: CUDA batching for 32×32 pHash with CPU fallback.
- `video-ssim-gpu.{ko,en}.md`: GPU processing and CPU fallback for temporal MSSIM.

Those documents describe existing code paths. This document defines a **target search policy and verdict model above those paths**. It does not claim that the duration cohorts, structural classification, sparse sampling, or candidate routing described here are already implemented in production.

Node E's adaptive video decode planner work is a separate investigation of decoder sparse-seek exactness and cost. Node E was closed without adopting production sparse seek; the sequential path remains. This document does not reverse that decision or authorize random-access decoding.

## 3. Separate the Candidate Populations First

### 3.1 Global File Identity

Byte-identical files can be found with a file hash regardless of duration cohort. This check can precede video decoding and visual fingerprint comparison.

### 3.2 Whole-Video Duplicate Search

Search for the same full content, including versions with different leading/trailing ads, intros, or credits, among candidates with compatible duration cohorts and structural characteristics.

A 10-second clip and a 2-hour movie should not be compared as ordinary **whole-video duplicate candidates**. Pairs with radically different durations leave the whole-video path and enter a separate partial-clip path only when that feature is requested or enabled.

Duration cohorts reduce candidates; they do not assert genre or provide absolute exclusion. Consider the same and adjacent cohorts to avoid missing boundary cases, then re-check duration ratio and structure before expensive comparison.

\[
R = \frac{\min(T_A,T_B)}{\max(T_A,T_B)}
\]

`R` is a coarse duration-compatibility filter. Values such as 0.95, 0.80, and 0.70 are initial experimental hypotheses only. They must not become production thresholds before the tolerated amount of inserted/removed ads, intros, and credits is measured. Duration ratio alone must never confirm or reject identity.

### 3.3 Partial-Clip Search

Finding that a 10–60-second short-form clip came from a particular interval in a 2-hour movie is a different problem from whole-video duplicate search.

- The default whole-video path excludes radically different duration pairs.
- Partial-clip search is a separate optional feature with a separate candidate index.
- It may require a lightweight timeline index spanning the long video, but it does not require storing screenshots of every frame.
- Do not assume that 10–14 representative frames from a feature film can find every short clip extracted from an arbitrary time position.

## 4. Duration Cohorts and Initial Frame Budgets

The groups below are **duration cohorts** used to select a search policy. Actual structure is inferred from low-cost features such as repeated intros, scene-cut density, and main-content/credit intervals.

| Cohort | Duration | Typical examples | Representative-frame target |
|---|---:|---|---:|
| G1 | Up to 15 seconds | Very short social clips | 4–6 |
| G2 | Over 15 to 60 seconds | X/social short-form | 6–10 |
| G3 | 1–3 minutes | Short clips | 8–10 |
| G4 | 3–10 minutes | Trailers, short-form edits, online videos | 10–14 |
| G5 | 10–25 minutes | Mid-length content | 9–13 |
| G6 | 25–60 minutes | Drama/TV-like content | 9–12 |
| G7 | 60–150 minutes | Feature-film-like content | 10–14 |
| G8 | Over 150 minutes | Long films, concerts, long broadcasts | 12–16 |

These are initial targets for retained representative-frame fingerprints. Do not save low-information or duplicate frames merely to fill a quota. A simple video may need fewer; add frames only when rapid cuts or ambiguous structure justify them.

### 4.1 Short-Form at or Below 60 Seconds

- Compare the complete timeline. Film-style reverse ending search is not the default.
- Divide the timeline into intervals, but do not persist every regularly spaced observation.
- Select representative frames at meaningful scene changes and include their order and time spacing in the fingerprint.
- Target 6–10 frames. A single-shot clip may need fewer; rapid-cut content may need more.
- Do not stop after checking only the first few frames.

### 4.2 Short and Mid-Length Videos from 1 to 10 Minutes

- Cover the whole duration rather than focusing only on the beginning and end.
- Observe roughly 6–8 time regions cheaply and select a representative scene from each useful region.
- Reduce the sample budget for an interview or single continuous shot.
- Trailers, rapid montages, and multi-clip edits may need additional scene diversity.
- Start with an 8–14-frame target, adjusted to structure.

### 4.3 Videos from 10 to 25 Minutes

- Preserve coverage across the complete duration. Do not apply the same aggressive edge focus as to feature films.
- Select distributed samples from the beginning, multiple middle regions, and ending.
- Initial target: 9–13 frames.
- Adjust weights for repeated intro/outro structure when present.

### 4.4 Drama/TV-Like Videos from 25 to 60 Minutes

Duration alone does not prove that a video is a drama episode. Apply an episode-like profile when repeated openings, title sequences, recaps, or closing credits are detected.

- Reduce the identity weight of openings, recaps, shared title sequences, and recurring credits when they repeat across files.
- Give the episode-specific main content the highest identity weight.
- Select representative frames from the beginning of the main content, early/middle/late portions, and just before the ending.
- Initial target: 9–12 frames.
- Do not classify different episodes as duplicates merely because they share opening and closing sequences.

### 4.5 Feature Films from 60 to 150 Minutes

- Use both forward opening analysis and reverse-time ending analysis.
- Infer the start of the main content rather than assuming it begins exactly at one minute.
- Use only 1–3 middle-content anchors as supplemental evidence instead of dense uniform persistence.
- Find the final main-content scene before credits and optionally one preceding scene.
- A 130-minute film starts with a 10–14 representative-frame target (center target: about 12). This is not a measured optimum.

### 4.6 Videos Longer Than 150 Minutes

Keep the feature-film policy but distribute middle-content anchors according to timeline length and structural complexity. Start with 12–16 representative frames. Do not scale image count linearly with duration.

## 5. Opening Analysis: Infer the Intro-to-Main-Content Boundary

### 5.1 Low-Cost Structural Probes

Do not immediately turn every observed frame into a permanent fingerprint. First run low-resolution, non-persisted probes and record:

- Average brightness and the proportion of dark pixels
- Color distribution and entropy
- Edge density and visual complexity
- Abrupt image changes and candidate scene cuts
- Gradual transitions such as fades
- Whether a visual state persists for a meaningful interval

For example, a 5-second observation interval during the first 30 seconds can be an initial probe policy, but those observations are not all saved as comparison images. Add observations and refine only around a suspected boundary when the intro is long or ambiguous.

### 5.2 Main-Content Start Estimate

- 30, 60, 90, 120, and 180 seconds are example coarse checkpoints, not fixed rules.
- A darkening followed by a sustained normal scene, or a sequence of scene transitions, can indicate the start of main content.
- Do not infer the end of an intro from one black frame or one cut. Check whether normal scene content continues after the transition.
- Once the likely boundary is found, inspect its neighborhood more closely and retain 1–2 distinctive frames.
- Give low weight to distributor/studio logos and titles that recur across many files.

## 6. Ending Analysis: Search Backward for Credits and the Final Main Scene

For long-form content, do not compare only the final frame. Move from the end toward the past to estimate the credits interval and the position of the final main-content scene.

### 6.1 Progressive Reverse-Time Probes

Let the video end at time \(T\). The following offsets are examples of initial probe points:

| Offset before end | Purpose |
|---:|---|
| 15 seconds | Final screen, black interval, or credits |
| 30 seconds | Structure immediately before the end |
| 1 minute | Credits and post-credit content |
| 2 minutes | Whether credits continue |
| 4 minutes | Candidate main-content boundary |
| 8 minutes | Candidate final main-content scene |
| 12 minutes | Long-credit handling |
| 20 minutes | Very long credits or a different ending edit |

These are search probes, not screenshot-storage positions. Once credits are recognized, refine only around the likely boundary. If main content is still present, expand farther into the past.

### 6.2 Credits and Main-Content Boundary

Useful credit signals include:

- Density and vertical movement of small on-screen text
- Presence and motion of background footage
- A long-lasting dark background
- The transition from the final main scene into credits
- Changes in music or speech intervals, when audio is available

These are probabilistic structural cues. Main content can include black screens or subtitles, and credits can contain moving footage or post-credit scenes. Never classify a segment from one cue alone.

Represent a credits interval with start/end timestamps and structural features instead of persisting low-value screenshots. Retain the final main-content scene immediately before credits and, when useful, one earlier scene. Continue searching after credits for post-credit or follow-up scenes.

Actual decoding need not play frames backward. The algorithm selects reverse-time timestamps; the decoder may retrieve frames using its supported seek/decode method and the results can be ordered by timestamp in reverse. This does not prove random-access exactness or approve production sparse seek.

## 7. Store Black Screens, Intros, and Credits as Structural Fingerprints

Do not repeatedly persist images from intervals with almost no visual information. Record interval metadata such as:

- Interval start/end timestamps and duration
- Full black or low-brightness state
- Logo/title candidate
- Fade-in/fade-out state
- Scene-transition locations
- Credit candidate and scroll characteristics
- Estimated main-content start/end
- Presence of a separate post-credit scene

Structural fingerprints help rank candidates and decide where additional frames should be sampled. Similar black-screen durations, logos, or credit structures must never establish video identity on their own.

## 8. Representative-Frame Features: Temporal Continuity Before Spatial Augmentation

### 8.1 Supported Orientation Variants

- Normal orientation is the baseline.
- Horizontal mirroring is a realistic transformation and may have a secondary fingerprint.
- **Do not compare vertically flipped frames.**
- Do not create exhaustive photo-style orientation combinations.
- Retain crop features only as supporting evidence compatible with the existing fingerprint path. The primary comparison is temporal order and continuity, not a combinatorial crop set.

Derive the horizontal-mirror fingerprint from the already decoded frame where possible; it should not require another screenshot or decode.

### 8.2 Per-Frame Features

A representative frame may produce a whole-frame pHash, horizontal-mirror pHash, and necessary supporting regional features. Multiple derived features from one frame are one evidence group, not several independent matching observations.

Black/low-variance intervals, common logos, and credits should be skipped or down-weighted. Distinctive main-content scenes should carry more identity weight.

### 8.3 Temporal Continuity

The primary evidence is not whether isolated frames happen to look alike, but whether:

- Similar frames appear in the same temporal order
- Inter-frame time gaps correspond plausibly
- Scene transitions and motion changes align as a sequence
- Matching scenes are distributed across multiple timeline regions
- The order remains consistent after inserted intros/ads or removed head/tail segments

## 9. Timeline Alignment and Verdicts

Record timestamp pairs for frames matched across two videos. Their correspondence must be monotonic. If the same scene occurs repeatedly, do not choose a match from one frame alone; use its neighboring sequence.

A simple leading offset can be approximated by:

\[
t_B \approx a t_A + b
\]

- \(b\): start-time difference, including a leading ad or intro
- \(a\): approximate playback-speed or timeline-scale change

For same-speed re-encodes, \(a\) is generally close to 1. Inserted or removed intervals cannot be represented by one global line; use piecewise alignment or monotonic sequence alignment such as a DTW-family method.

Do not stop at one mismatching frame. Also do not infer whole-video identity from a short shared logo or an accidentally similar scene. Evaluate the length of matching runs, distinctiveness of scenes, temporal order, timeline distribution, and overall content coverage.

Recurring studio logos, program openings, episode recaps, and credits receive lower weight as they recur across more files. Rare main-content scenes have higher retrieval value.

## 10. Distinguish Retained Images from Actual Decode Work

The representative-frame target for a 130-minute film is about 10–14 images (center target: about 12). This is not the number of all structural probes or the number of frames actually produced by the decoder.

Measure these quantities separately:

- **Probe count**: low-cost observations used to find structural boundaries, brightness changes, or scene cuts
- **Sampled frames**: frames requested by the sample plan
- **Kept/representative frames**: frames retained as comparison fingerprints after filtering and deduplication
- **Decoded frames**: frames actually produced successfully by the decoder, including intermediate frames later discarded

Preserve the semantics of existing `decodedFrames`, `sampledFrames`, and `keptFrames` instrumentation. Selecting 12 representative images does not mean only 12 frames were decoded. The current sequential decode path can process intermediate frames even when only sparse timestamps are requested.

## 11. Candidate Search Flow

1. Check the file hash and basic metadata.
2. Assign a duration cohort and run low-cost structural probes.
3. Build compatible candidate sets for whole-video search. Route radically different durations away from the partial-clip path.
4. Generate sparse representative frames and structural fingerprints using the matching duration/structure profile.
5. Use distinctive, rare main-content features to narrow candidates.
6. Align frame correspondences and temporal order for candidate pairs.
7. Extract additional probes/frames only around necessary intervals.
8. Classify as whole-video duplicate, edited full-content variant, partial clip, scene-level similarity, or uncertain.

## 12. Final Result Types

| Result | Meaning |
|---|---|
| Exact file duplicate | File-byte hash matches |
| Same full content | Main-content temporal sequence broadly matches despite encoding/container differences |
| Edited full-content variant | Ads/intros/credits/head-tail edits differ, but most main content matches |
| Partial clip | Most of a short video matches a specific interval in a longer video |
| Some similar scenes | Some scenes match, but evidence for whole-content duplication is insufficient |
| Uncertain | Sampling, alignment, continuity, or other evidence is insufficient or conflicting |

Do not place short-form-to-feature-film partial matches into whole-video duplicate groups.

## 13. Validation Plan and Adoption Gates

Do not declare the optimal frame count or thresholds by estimation. Build evaluation pairs for each duration cohort, including:

- Byte-identical copies and re-encodes
- Resolution, bitrate, and frame-rate changes
- Horizontal mirror variants
- Crop, subtitle, and watermark changes
- Different leading ads/intros, endings, and credit lengths
- Head/tail trimming and mid-roll insertion
- Different episodes sharing the same series opening/ending
- Unrelated videos sharing logos, music, or credit layouts
- Short clips extracted from long-form content
- Rapid-cut trailers and multi-clip edits

Record candidate-generation recall, final precision/recall, false positives and misses, mean/upper frame counts, probe counts, actual decodedFrames, index size, and elapsed time by cohort. Compare the initial 6–10 / 8–14 frame targets with denser baselines to determine whether accuracy loss is acceptable.

Adoption sequence:

1. Prepare offline evaluation data and ground truth.
2. Compare the proposed policy in parallel with the current production path.
3. Measure accuracy and cost by cohort.
4. Analyze misses and tune sampling, weighting, and temporal alignment.
5. Propose only the portions that pass accuracy and regression gates for production.

This document records no implementation or measurement result. Any eventual implementation needs its own brief, build-history entry, and worklog update. If search-verdict semantics change, review engine-version and cache-compatibility impact separately.

## 14. Out of Scope

- CPU/GPU decoder selection and CUDA/NVDEC/D3D11VA/Vulkan implementation
- Production adoption or exactness claims for sparse seek
- Vertical-flip comparison
- Persisting screenshots at every timeline position
- Comparing every radically different duration pair in whole-video search
- Unconditionally enabling partial-clip search in the default search path

## 15. Related Documents

- `implementation-briefs/E-adaptive-sampling-strategy.{ko,en}.md`: Node E sparse-seek exactness and cost investigation. It is a different layer from content-sampling policy.
- `implementation-briefs/E-video-decode-planner.{ko,en}.md`: decoder sample planning and execution contract.
- `architecture/video-gpu-hash.{ko,en}.md`: 32×32 pHash CUDA batch path.
- `architecture/video-ssim-gpu.{ko,en}.md`: temporal MSSIM GPU path.
- `architecture/database-structure.{ko,en}.md`: current persistent DB and video fingerprint cache structure.
- `development-progress.{ko,en}.md`: Video comparison semantics / GUI video acceptance remains DEFERRED; this document is a future design reference.


## 16. 2026-10-31 Refinement — Separate Probe / Kept / Decoded and Verify Adaptively

This section turns the earlier design into an implementation contract. Where interpretation differs, this section controls the count split, main-content weighting, adaptive refinement, and decoder boundary.

### 16.1 Three Counts Are Different

| Quantity | Meaning | Persisted |
|---|---|---|
| Probe observation | A time position observed for brightness, color, visual change, or transition structure | Usually not as an image |
| Kept representative frame | A distinctive frame retained as a comparison fingerprint | Yes |
| Decoded frame | A frame actually output by the decoder, including decoded intermediates later discarded | Measurement |

**Retaining 12 representative frames does not mean the decoder processed only 12 frames.**

The current make_sample_plan() uses a 32-second interval above 3,840 seconds. A 130-minute (7,800-second) video requests about 245 timestamps including the end. But VideoDecoder::framesAt() seeks once to the first target and sequentially decodes to the final target. At 25-fps CFR this can be on the order of 195,000 decoded frames. This is an **arithmetic illustration, not a measured value**; measure the actual number through 0.9.4.155's decodedFrames.

The first objective is therefore **fewer retained frames and less comparison work**. Do not claim reduced decode work until a separate decoder approach passes correctness and performance gates.

### 16.2 Three Observation Stages

1. **Probe:** Where possible, compute average brightness/dark ratio, low-resolution color changes, edge density, scene changes, fades, and low-information intervals in one sequential stream. Probes are not retained images, but the current sequential decoder may still decode intermediate frames to compute them.
2. **Keep:** Retain only the most distinctive frame from a useful interval. Exclude/reduce the ordinary quota for repeated near-identical frames, black screens, common logos/titles, and credits.
3. **Refine:** Add only 1–2 frames in a discriminative main-content region when the initial candidate remains uncertain. Do not tighten the sampling interval uniformly for all videos.

### 16.3 Initial Frame Budget for a 130-Minute Film

| Role | Probe plan | Retained target |
|---|---|---:|
| Opening | 0–30 seconds at about 5-second intervals (about 6 observations) | 1–2 |
| Main-content boundary | Adaptive search between 30 seconds and 3 minutes, starting around 30/45/60/90/120/180 seconds | 1–2 |
| Early main content | A distinctive scene after the main-content start | 1 |
| Middle main content | 1–3 distributed intervals | 2–3 |
| Late core / pre-ending | Reverse-time search from the end to locate the credits boundary | 1–3 |
| Ending / credits | T-15s, T-30s, T-1/2/4/8/12/20m; de-duplicate with final-30-second observations | Usually 0 |

The center target is about 12; the initial range is 10–14. Long black/credit intervals reduce the retained count. Do not stop mechanically at T-20m; expand earlier if credits continue. Twelve is not a quota.

### 16.4 Duration Operation

- Up to 60 seconds: cover the full timeline, not just the first 30 seconds; begin with observations around every 5 seconds and refine on scene changes. Target 6–10 frames.
- 1–10 minutes: divide the complete timeline into about 6–8 observation regions. Target 8–14.
- 10–25 minutes: distribute across beginning/middle/end. Target 9–13.
- 25–60 minutes: apply episode-like weighting only when repeated intros/recaps/credits are detected; focus on main content. Target 9–12.
- 60–150 minutes: target 10–14.
- Over 150 minutes: start with 12–16 and adjust to structural complexity.

Duration cohorts control candidate/sample policy; genre is inferred separately from structure.

### 16.5 Main-Content Candidate Filtering and Weighting

Distinguish introInterval, mainContentInterval, creditsInterval, and postCreditsInterval. If the main-content boundary is uncertain, do not pretend total duration is core duration; include boundary uncertainty in the score.

R_total is a coarse candidate filter. When main-content boundaries are valid, use R_core=min(coreA,coreB)/max(coreA,coreB) as a secondary ranking feature. Neither becomes a fixed threshold before validation.

Down-weight broadcaster/studio logos, program openings, recaps, and credits according to their frequency in the library. Give more weight to rare main-content scenes. Long-form candidates cannot be accepted from opening+ending alone; they need independent evidence in separated core regions.

### 16.6 Temporal Continuity and Adaptive Stopping

Matched timestamps must be monotonic. Check affine offset/scale first; use piecewise monotonic alignment or DTW when inserted/deleted intervals require it. Evaluate time gaps, main-content coverage, distinctive-scene count, and whether matches cluster only in opening/credits alongside frame scores.

Stop when a candidate is clear. If only a common intro matches or scores are close, add 1–2 frames only from relevant core regions. Return Uncertain when the verdict remains unstable within budget; never extract indefinitely.

### 16.7 Node E / Decoder Boundary

Node E did not adopt production sparse seek due to exactness failures and retains ExactnessPolicy::RefuseAll. Choosing reverse-time timestamps does not guarantee reverse playback or random-seek exactness.

- The first prototype uses the current sequential decoder as its baseline.
- Timestamp-neighborhood seeks, keyframe jumps, and window decode are tested only in separate prototype/measurement paths.
- Never describe fewer Probe/Kept frames as fewer decodedFrames.
- Claim actual decoder savings only when decodedFrames, sampledFrames, keptFrames, seek/landing, decode time, and end-to-end time are all measured.
- Production random access remains prohibited until a separate codec/profile/exactness/fallback gate and explicit roadmap change.

### 16.8 Cache / Engine Version Boundary

Validate early stages in scratch results. Do not bump cache format, DB schema, or engine version before representative frames, interval payloads, and verdict semantics are settled. Analyze re-generation/re-validation and compatibility cost only when production verdict semantics actually change.

### 16.9 Research References

- IEEE Access 2026, A Review of Partial Video Copy Detection: Technical Advances, Evaluation Frameworks, and Future Trends, DOI 10.1109/ACCESS.2026.3707321: distinguishes whole-video near-duplicate judgement from partial-copy localization and reviews coarse-to-fine verification through temporal consistency/sequence alignment after keyframe retrieval.
- Luo et al., IEEE TPAMI 2025, FiGVCL: Fine-Grained Benchmark and Method for Video Copy Localization, DOI 10.1109/TPAMI.2025.3595625: addresses evaluation and localization that preserve temporal correspondence in partial copies.

These references inform design/evaluation only; they do not approve importing deep models, datasets, or dependencies.

## 17. GUI Settings — Video Tab and User-Adjustable Profiles

> **Design status: PROPOSED / NOT IMPLEMENTED / NOT VALIDATED.** This section adds a GUI settings contract to Workstream V; it does not imply that a Video tab already exists in the product.

### 17.1 Settings layout

Add a **Video** tab to the Settings dialog. The default selection is **Automatic (recommended)** and retains the existing internal G1–G8 duration cohorts and structural classification. In Automatic mode, the application chooses the sampling policy from duration, scene changes, and intro/credit structure. Do not make manual forced classification as “short” or “movie” the default.

A **Custom** mode exposes per-profile overrides. Present three understandable groups in the UI (Short-form / Mid-length / Long-form), while preserving the existing fine-grained duration cohorts internally.

### 17.2 Recommended defaults and editable controls

| UI profile / setting | Recommended default | Scope and meaning |
|---|---:|---|
| Short-form (up to 60s) — full-timeline frame-check interval | **5 seconds** | Probe positions across the complete timeline. Scene-change events may trigger additional observations. |
| Short-form — representative-frame target | Automatic: G1 **5 frames** (4–6), G2 **8 frames** (6–10) | Custom mode selects an integer target within the proposed range for that cohort. Exclude redundant/low-information frames. |
| Mid-length (over 60s–25m) — comparison policy | Automatic (recommended) | Keep 6–8 observation regions for 1–10m and beginning/middle/end distribution for 10–25m. Prefer structure-aware probes over one fixed uniform cadence. |
| Mid-length — representative-frame target | Automatic: G3 **9 frames** (8–10), G4 **12 frames** (10–14), G5 **11 frames** (9–13) | Custom mode selects an integer target within the proposed cohort range. Actual retained count depends on information content and redundancy. |
| Long-form (over 25m) — structural-boundary detail interval | **5 seconds** | Applies only inside narrowed opening/main-content-start/ending boundary windows. It does not scan the whole film every five seconds. |
| Long-form — main-content refinement interval | **5 seconds** | Applies only inside a selected core region when an initial candidate is uncertain. A user can change 5s to 3s to inspect that region more densely. |
| Long-form — initial core anchors | Automatic, **1–3 distributed regions** | Do not increase all regions uniformly with duration. Seek independent evidence in separated main-content regions. |
| Long-form — representative-frame target | Automatic: G6 **10 frames** (9–12), G7 **12 frames** (10–14), G8 **14 frames** (12–16) | A 130-minute film has a recommended center target of 12. Custom mode selects within the proposed cohort range. |
| Uncertain-candidate refinement | Enabled (recommended), **1–2 additional frames per region** | Refine only distinctive core regions when only a common intro matches or scores are close. |
| Partial-clip search | Off by default | Keep it separate from whole-video duplicate search and its index; run only when explicitly enabled. |


Custom representative-frame targets default to the recommended cohort centers above; users may choose an integer target within the proposed range for that cohort. This is a **soft target**, not a quota. The actual retained count may be lower due to redundancy/low information, and the separate refinement allowance is used only for ambiguous candidates. These centers are initial UI recommendations, not measured optima.

Custom intervals are positive values in seconds. The initial UI proposal is **1–60 seconds**; finalize the input ceiling and total Probe budget using real accuracy/throughput measurements. Even with a dense interval, enforce separate per-file Probe and retained-frame budgets; never allow unbounded expansion.

### 17.3 Interpretation of recommended defaults

- **Short-form:** for a 60-second clip, a 5-second interval means about 12 baseline observation positions across the full timeline. It does not mean all 12 frames will be retained; the final representative-frame target is usually 6–10, selected by information content and redundancy.
- **Long-form:** do not uniformly sample an entire film every 5 or 3 seconds. First locate opening/main-content/ending structure adaptively and distribute a few core anchors. A user override from 5s to 3s applies only to automatically selected **local detail windows**.
- **Precision and cost:** shorter intervals can increase Probe positions and feature-computation work. Raising the retained-frame target can increase fingerprint/comparison/cache cost. Do not guarantee accuracy gains or linear accuracy improvement from more samples.
- Automatic mode preserves all existing G1–G8 cohorts. Do not collapse the 60s–25m range into either Short-form or Long-form. Apply episode-like weighting to 25–60m content only when repeated opening/recap/credit structure is detected.

### 17.4 Settings persistence and run lifecycle

1. Persist Automatic/Custom selection and profile overrides through the existing user-preferences layer. Do not introduce a new settings file, DB, or schema prematurely.
2. Freeze the effective settings as a **run snapshot** when a search begins. UI changes during a run apply only to the next search.
3. Separate fingerprint-affecting settings (sampling interval/frame budget) from verdict-only thresholds. Fingerprint-affecting settings change cache reuse eligibility and therefore require a policy ID/hash or equivalent cache-freshness check.
4. Never silently mix old cached fingerprints with results produced under a different policy. Design preservation of old results, stale marking, and explicit regeneration/revalidation; inspect the current cache contract before deciding whether migration is required.
5. Do not bump engine version, cache format, or DB schema for the initial UI alone. Decide through a separate gate only if persistent payloads or production verdict semantics actually change and compatibility evidence warrants it.

### 17.5 Honest UI wording and telemetry

Use **Frame-check interval** as the user-facing label. Its help text must say: “Spacing between candidate Probe timestamps; this is not the number of retained frames or the number of frames output by the decoder.”

With the current VideoDecoder::framesAt() single-seek-then-sequential-decode path, intermediate frames may still be decoded even when target timestamps are farther apart. Do not claim fewer decoded frames or shorter end-to-end time merely because the user changed 5s to 3s. Record the effective profile/interval, probeCount, sampledFrames, keptFrames, decodedFrames, fingerprint/compare time, and final verdict evidence separately in run telemetry.

### 17.6 GUI settings acceptance gate

- Confirm that the Settings dialog contains a Video tab and first-run defaults to Automatic (recommended).
- Confirm Automatic mode uses the G1–G8 duration/structure policy and correctly handles cohort boundaries without requiring forced media-type selection.
- Verify that a user override from 5s to 3s is confined to the short-form full timeline or the selected local detail windows for long-form.
- Verify settings save/restart round-trip, invalid-value rejection/clamping, and next-run-only application when changed during an active search.
- Verify that profile changes do not incorrectly reuse stale video fingerprints or mix results produced by different policies.
- Compare candidate recall, precision/recall, Probe/Kept/Decoded counts, and fingerprint/compare/end-to-end time for each interval/frame budget using real short-form, episode, and film datasets.
- Verify the UI makes no decoder-savings claim and CPU/GPU/fallback paths preserve identical verdict semantics.

Expose only policies that have passed V's algorithm/accuracy gates. This GUI scope is a design proposal; implementation, manual GUI acceptance, and performance measurements remain outstanding.
