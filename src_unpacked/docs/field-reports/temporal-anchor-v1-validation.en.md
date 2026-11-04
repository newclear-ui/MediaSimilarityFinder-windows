# Build 0.9.4.172 and Test Report, in Detail — TemporalAnchorV1

> Plainly: **we added a second pair of eyes that recognizes videos with ads prepended, but it is experimental, so the default eyes (V3) stay as they are.**

## 1. What this build made (0.9.4.171 -> 0.9.4.172)

| File | Content |
|---|---|
| `src/temporal_anchor.{h,cpp}` (new, ~500 lines) | The new reading path itself. See §2 |
| `src/video_policy.h` | Reading-strategy choice added (`LegacyV3` default / `TemporalAnchorV1` experimental) |
| `src/video_fingerprint.{h,cpp}` | Anchor-index builder + 3 measurement-only stats. Existing fingerprint code untouched |
| `src/scan_worker.cpp`, `src/backend_main.cpp` | Config propagation + 1 log line |
| 4 files under `gui/` | "Reading strategy" combo on the video tab + save/propagate (LegacyV3 default) |
| `tests/video_temporal_anchor_test.cpp` (new) | 13 synthetic tests |
| `tests/ui_settings_test.cpp` | Combo default/save/mapping pins |
| `CMakeLists.txt` | Version 0.9.4.172, new module + test registered |

Unchanged: verdict engine version (1.5.2), DB/cache formats, V3 fingerprints/caches, all default behavior.

## 2. What the new reading path does (plain explanation)

Legacy V3 sweeps the whole video at fixed intervals, keeps representative scenes, and aligns two videos in time order. A 30 s ad prefix shifts everything by 30 s and breaks the alignment — that was the miss.

TemporalAnchorV1 reads like this:

1. **Opening print**: memorize 3-10 s where the real main content starts (skip black/fade openings, start at the first textured scene).
2. **Locate**: search for that print from the candidate's first frame. Found at 30 s → "offset = +30 s". Reversed order doesn't count; a single matching frame doesn't count.
3. **Anchor check**: just after the start, 5/10/15 min, just before the ending — 3 frames at ~3 s each per anchor. 2 of 3 must match in order. Flat (information-free) frames don't count as evidence.
4. **Offset consistency**: all anchors must agree on the offset. A change mid-way (e.g. mid-roll ad) is recorded, never forced into a DUP.
5. **Ending**: 3 frames from the last 9 s. Ending-only or credits-only matches never confirm DUP.
6. **Verdict**: 2+ independent main anchors + consistent offset = DUP. Thin evidence = UNCERTAIN (don't know). Different = DIFF. Black/logo/intro alone never yields DUP.

## 3. The 13 unit tests (all PASS, synthetic frames)

| # | Test | Meaning |
|---|---|---|
| 1 | Plan shape | Search timetable generated sorted at 1 s steps |
| 2 | 30 s leader search | Finds start 30 in leader-prefixed video + reverse direction |
| 3 | Single-frame rejection | One matching frame is never a start |
| 4 | Reversed rejection | Reversed order is rejected |
| 5 | Mirror tolerated | Mirrored video recognized (vertical flip unsupported) |
| 6 | 700 s + 30 s leader DUP | Offset 30, 2+ anchors → DUP verdict |
| 7 | Different content DIFF | Never forced |
| 8 | All-flat UNCERTAIN | Never forced to DUP/DIFF |
| 9 | Ending-only non-confirmation | No DUP when mains differ |
| 10 | Shared black leader non-confirmation | No DUP when mains differ |
| 11 | Mid-roll drift | Drift recorded, DUP never forced |
| 12 | Short thin UNCERTAIN | Says don't-know when evidence is thin |
| 13 | Real decode path | Prints built from real 32x32 frames |

Each pin was derived from a real measured failure (e.g. tie-ordering instability, black-opening false starts, missing (0,0) hypothesis).

## 4. Full suite results

- CPU: **141/141 PASS** (incl. new test #66 and GUI strategy round-trip)
- GPU: **142/142 PASS** (+1 CUDA)
- All legacy V3 tests PASS (evidence default behavior is unchanged)

## 5. Lead-dataset validation results (MSF_VIDEO_ANCHOR=1)

L 20 files + f 100 files, same pairs as V3:

| Pair | V3 | Anchor V1 |
|---|---|---|
| L reenc/flip same | DUP 85-99 | DUP 85-95 (2-4 anchors) |
| L01-lead (45 s) | 7.7 miss | **DUP 85 (offset 45.0, 4 anchors)** |
| L04-lead (30 s) | 12.8 miss | **DUP 95 (offset 30, 4 anchors)** |
| L05-lead (45 s) | 48.8 miss | UNCERTAIN (2 anchors, score 95) |
| L03-lead (60 s) | 6.1 miss | UNCERTAIN (offset 61 found, 1 anchor) |
| L02-lead (HDR) | 13.5 miss | DIFF (dark HDR) |
| f lead same 50 | DUP 20/50 @50, 5/50 @87.5 | **DUP 11/50 @50, 0/50 @87.5** (44 UNCERTAIN, 5 DIFF) |
| f lead cross FPs @87.5 | 16 | **0** |
| L cross FPs | 0 | **0** |

How to read: the anchor path caught 2 long-form leads V3 missed (L01/L04 DUP). Shorts mostly answer don't-know for lack of evidence (UNCERTAIN — honest, not wrong). Zero false alarms.

## 6. Limits and next work (honest record)

- Short leaders: 0 DUP (44 UNCERTAIN) — by design (never force on thin evidence), but a recall task remains.
- Dark openings/HDR: unstable pHash breaks the search (L02) — feature-strengthening follow-up.
- Single-frame 87.5 bar is strict — review after distribution measurement.
- Ending classifies NoCredits/Uncertain only (PreCredits reserved); anchor JSON aggregation is follow-up.
- No Gate PASS declared, no default promotion. Rollback: LegacyV3 in the GUI (the default).

## 7. Reproduction

- Unit: `msf_video_temporal_anchor_test` (Release build trees).
- Validation: `MSF_VIDEO_ANCHOR=1` + temporary harness + `msf_video_field/{orig,lead}` (§5 conditions).
- Source evidence: `docs/build-history/0.9.4.172.*`, `docs/worklog/0.9.4.*` 0.9.4.172 entry.
