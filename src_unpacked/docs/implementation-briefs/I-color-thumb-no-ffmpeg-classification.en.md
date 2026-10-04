# Implementation Brief — I-Color Thumb no-FFmpeg Classification (Pre-register)

Status: **PRE-REGISTERED** (audit complete, before fixture/production correction)

```text
Base         v0.9.4.45 / 97db24f
Experiment   I-color_thumb no-FFmpeg classification audit
Product change   NO — this step is audit + contract definition only
```

## 1. Purpose

Determine whether the `color_thumb` display path changes **media kind
classification** depending on FFmpeg availability, and define a contract that
separates the two axes.

```text
Classification  !=  Decoder availability
```

## 2. Audit result — the current classification path (every claim has file:line evidence)

The single source of classification truth is `Scanner::isVideoPath`, and it looks
at the extension only.

- `src/scanner.cpp:12-16` — `.mp4 .mkv .avi .mov .webm .m4v .wmv` (video)
- `src/scanner.cpp:17-22` — the image allow-list otherwise (`.jpg .jpeg .png .bmp .webp .gif .tif .tiff`)
- `src/media_search_engine.cpp:38` — `kindOf()` calls only `isVideoPath()` (no content probe)
- `gui/mainwindow.cpp:366-369` — GUI `isVideoExt()` covers the same 7 (not used by core, duplicated)

What does **not** participate in classification (audit result = intended structure):

- no container probe. `avformat_open_input` decodes, it does not classify (`src/video_decoder.cpp:22`)
- no decoder / FFmpeg / WIC capability check. `MSF_HAS_FFMPEG` is a compile-time macro only,
  not a runtime availability flag (`CMakeLists.txt:640`, `src/video_decoder.h:57`)
- a decode failure only yields `fingerprint=0` or result exclusion; `kind` stays unchanged
  (`src/media_search_engine.cpp:511`, `:594`, `src/media_pipeline.cpp:53`)

**Verdict: classification is identical in all three states.** That is intentional, not accidental.

## 3. The three states, separated explicitly

| State | Code path | Classification | Display result |
|---|---|---|---|
| FFmpeg available | `frameAtColor` native (`video_decoder.cpp:58-90`) | stays Video | color frame |
| FFmpeg unavailable | `frameAtColor` is `(void)o; return false;` (`video_decoder.cpp:91-93`) | **stays Video** | no color frame → gray engine thumb / placeholder |
| runtime decode failure | `open()` or `frameAtColor` fails | **stays Video** | same fallback |

The no-FFmpeg `false` from `frameAtColor` is **documented intent**
(`src/video_decoder.h:30-32`, "Display path (previews) ... Returns false without
linked FFmpeg"). However the
configure message at `CMakeLists.txt:640`, "falls back to ffmpeg/ffprobe command
line", does not reflect that `frameAtColor` (color) and
`framesAt96Plus32ExactSparse` (`video_decoder.cpp:280`) have **no** CLI fallback.
That message is an **overstatement** and is a correction target.

## 4. Evidence that color_thumb does not change classification

- production: `src/media_pipeline.cpp:149-157` (`hasColorThumb` only when `decodeColorAspect` succeeds)
- storage: `src/media_search_engine.cpp:189-214` LRU (max `kColorThumbMax = 2048`)
- consumption: `gui/mainwindow.cpp:2476-2487` — `isVid` is **extension** based and independent of
  color-thumbnail success or failure
- on failure: `hasColorThumb=false` → `getColorThumb` returns `false` → `px.clear()` → next
  fallback lane. **There is no kind-assignment code anywhere on this path.**

## 5. Correctness risks actually found by this audit

### R1 (high) `color_thumb_test` is always red in a no-FFmpeg build

- `CMakeLists.txt:426-428` — `color_thumb_test` is registered **unconditionally**, independent of
  `MSF_ENABLE_FFMPEG`
- `tests/color_thumb_test.cpp:42-47` — `return 4` when `std::system("ffmpeg ...")` fails,
  `return 5` when `open()` fails, `return 6` when `frameAtColor` fails. **No skip handling**
- `src/video_decoder.cpp:91-93` — in a no-FFmpeg build `frameAtColor` always returns `false`
- `MSF_ENABLE_FFMPEG=OFF` is a supported configuration (`tests/video_decode_planner_probe.cpp:294`
  comment). In that configuration this test always fails.
- This **classifies the previously unclassified** `color_thumb_test exit 5` from the worklog
  `2026-10-02` run 082 (no link + no CLI → `open()` fails → exit 5).
- Inconsistent with the same intent elsewhere: `tests/benchmark_integration_test.cpp:761-767`
  prints `[SKIP]`.
- Secondary: the raw `std::system` at `tests/color_thumb_test.cpp:43` violates AGENTS.md item 11
  (only `src/proc_capture.h::captureSilent` may spawn external processes).

### R2 (medium) dual classification truth sources

- `src/media_search_engine.cpp:672` — `kindOf(x.path)` (extension)
- `src/media_search_engine.cpp:674` — `files_.push_back(..., (MediaKind)x.kind, ...)` (DB stored value)
- `src/media_search_engine.cpp:675` — `loadVideoAnchors` is called on the extension decision only
- `src/media_search_engine.cpp:115` — `rebuildCandidateIndexes()` splits image/video candidate indexes
  on the DB `x.kind`
- If they diverge, anchor loading and candidate indexing use different classifications.
  There is no cross-check.

### R3 (medium) the extension list is duplicated four times

`src/scanner.cpp:15`, `src/scanner.cpp:21`, `src/monitor.cpp:119/243/320`,
`gui/mainwindow.cpp:366-369`. All four are currently the same set so there is no behavioural
difference, but drift diverges silently. `src/monitor.cpp:323` builds `kind=image?1:2` in an
else branch, so a divergence there pushes non-media files into the video branch.

### R4 (medium) the shell thumbnail unconditionally overwrites the engine color thumbnail, and pollutes stats

- `gui/mainwindow.cpp:2482` — the shell lane overwrites `pm` with **no** `pm.isNull()` guard
- the later lanes do guard (`:2487`, `:2526`)
- result: the engine color thumbnail is discarded while `thumbStatEngine_` was already
  incremented, and one budget slot is consumed for nothing. The comment at `:2477` also
  disagrees with the actual execution order.

### R5 (low) overstated configure message — see §3.

### R6 (low) magic-number `MediaKind` ↔ int mapping (`src/benchmark_core.cpp:88-94`,
`src/monitor.cpp:323`, public API `int kind`). Zero `static_assert`.

## 6. Do the existing tests pin real no-FFmpeg semantics?

**No.** Evidence:

- `scanner_test` pins case-insensitivity, Unknown, and counts, but never verifies the
  relation "classification is independent of FFmpeg state"
- `image_pipeline_test` pins `hasColorThumb` success only. **Failure behaviour is unpinned**
- `color_thumb_test` covers only the success path and, per R1, fails in the no-FFmpeg configuration
- nothing in `tests/` **simulates** FFmpeg being unavailable (no env override, no stub binary).
  `#ifdef MSF_HAS_FFMPEG` is a build-configuration guard, not a runtime simulation.

→ **Absence of a test is not asserted as a bug.** R1 is proven directly by the CMake
registration and the exit codes, so it is a bug; the rest are classified as "unverified".

## 7. Expected behavior (contract candidate)

```text
Classification   : one extension-based rule. Independent of FFmpeg / decode / thumbnail results.
Color thumb fail : classification unchanged, next fallback lane proceeds, no kind assignment.
Video no-FFmpeg  : kind stays Video, no color preview (gray / placeholder), no misclassification.
Image -> video path entry : forbidden.
```

## 8. Fixture plan (next step, not implemented here)

1. **Classification-invariance fixture** — pin that `MediaKind` for a given extension list is
   identical regardless of FFmpeg linkage. Feasible structure: a classification-only target run
   from an `MSF_ENABLE_FFMPEG=OFF` build.
2. **Color-thumbnail-failure fixture** — an undecodable image fixture pinning that
   `hasColorThumb=false` while `kind=Image` is preserved. The standard dataset is not modified.
3. **R1 skip/pass** — branch the CTest registration so `color_thumb_test` skips, or run only the
   color lane conditionally, depending on linkage. Follow `tests/benchmark_integration_test.cpp:761-767`.
4. Replace `std::system` with `captureSilent` (AGENTS.md item 11).

## 9. Acceptance criteria

- [ ] In a no-FFmpeg build, CTest ends green or with explicit skips
- [ ] `MediaKind` is identical across the three states (available / unavailable / runtime decode
      failure), pinned by a test
- [ ] Color-thumbnail failure does not change `kind`, pinned by a test
- [ ] The configure message at `CMakeLists.txt:640` reflects the real fallback coverage
- [ ] R2/R3/R4 each remain separate decisions and are not pulled into this brief's production
      correction scope

## 10. Forbidden changes (this step and the fixture step)

- Scanner / MediaKind semantics changes
- FFmpeg fallback path changes
- thumbnail pipeline changes
- VideoDecoder / ImageDecoder changes
- CandidateIndex / threshold changes
- GUI display changes
- standard dataset modification

## 11. Boundaries of this step

```text
color_thumb production correction = NOT PERFORMED
color_thumb fixture implementation = NOT PERFORMED
CPU/GPU regression for color_thumb = NOT PERFORMED (no product code change)
S4 final GUI visual/save acceptance  = DEFERRED
S5 product benchmark                = DEFERRED
S6                                = DEFERRED
NVDEC production adoption          = DEFERRED
```