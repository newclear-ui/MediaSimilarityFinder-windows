# Current State

> The one-page summary a new session reads **first**. Keep it within 80 lines.
> This document is a summary/index; the **originals of verdicts and figures are the linked documents**.

- Updated: 2026-11-19
- Version: **0.9.4.174**
- engine **1.5.2** / cache format **11** (source: `BUILD_STATUS.md`)
- DB **1.0.6** / benchmark schema **9** (source: `src/database.h`, `docs/llms.txt`)
- CPU CTest **145/145**, GPU CTest **146/146**

## Current work

- Process split **P1-P4 complete**, G4/G6 verified (.111)
- **0.9.4.174**: TemporalAnchorV3 user-specified algorithm, product implementation (experimental opt-in, default unchanged, V1/V2 frozen)
- V1 (.172) / V2 (.173) / V3 (.174) implemented in order. None adopted as default

## Continuation (next 3)

1. Run and record the real-Windows acceptance items (LocalDumps+PDB, full user-dataset rescan, Test Mode toggle/scratch isolation, preview/selection, Tiles/ListMode, large-dataset traversal)
2. Re-confirm the final Search/Index/Comparison acceptance
3. Close the GPU MAX share boost gap (documentation basis)

## Blockers

- **Real Windows acceptance** not finished
- **Sparse production** blocked by `ExactnessPolicy::RefuseAll`
- **F-1 CONDITIONAL**: NVDEC production adoption **NO**
- GPU MAX share boost: documented gap

## Node / verdict summary

| Area | State |
|---|---|
| A / B / C / D / I | CLOSED / COMPLETE |
| E | CLOSED / NOT ACCEPTED (sparse exactness unproven, RefuseAll) |
| F-1 | CONDITIONAL (NVDEC production NO) |
| S0–S3 | CLOSED |
| S4 | CLOSED (functional acceptance .112 + product acceptance) |
| S5 | NOT CLOSED (XMP-REAL-001 partial measurement) |
| S6 | NOT STARTED / DEFERRED (no measurement run) |
| S7 / S8 | NOT STARTED |

## Invariants (do not break)

- engine **1.5.2** / cache format **11** — record the reason in build-history before changing
- **V1/V2 frozen**; V3 is experimental opt-in, not the default
- Never promote an existing verdict (CLOSED / CONDITIONAL / DEFERRED / NOT ACCEPTED) on your own

## Do not retry (refuted by measurement)

D9b, cache capacity 32, scoring-path duplication, `WICBitmapInterpolationModeFant`, EXIF skip, cache mutex contention, Factory2 fallback, raw OS file-open.
Evidence: the experiment index in `docs/worklog/0.9.4.en.md`.

## New-session reading order

1. **This document** (required)
2. Only the **Active Build Queue section** of `docs/development-progress.en.md` (required)
3. Active node brief — when starting work
4. matrix / roadmap — only when node-level detail is needed
5. build-history / architecture / worklog / version-index — for the task at hand

## Do not read at startup (on demand)

- `docs/version-index.md` (77 KB), `docs/worklog/0.9.4.en.md` (50 KB), `docs/development-progress-detail.en.md` (167 KB), `docs/build-history/README.en.md` (117 KB)
- Read them only when a lookup or citation is needed. Do not read them whole at session start.
