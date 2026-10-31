# Video Similarity and Duplicate Search Architecture

> **Design status: PROPOSED / NOT IMPLEMENTED / NOT VALIDATED**
>
> Baseline date: 2026-10-31  
> Purpose: Design a duration- and structure-aware algorithm that selects a small set of representative frames and timeline features, while separating whole-video duplicates from partial-clip search.  
> This document does not choose CPU/GPU decoder implementations or hardware-acceleration paths.

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
