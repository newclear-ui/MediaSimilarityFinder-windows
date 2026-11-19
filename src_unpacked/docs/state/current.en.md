# Current State

> The first file a new session should read. Keep it under 80 lines.
> This document is a summary/index. Verdicts and numbers live in the linked sources.

- Updated: 2026-11-19
- Version: **0.9.4.174**
- engine **1.5.2** / cache format **11** (source: `BUILD_STATUS.md`)
- DB **1.0.6** / benchmark schema **9** (source: `docs/llms.txt` line 5, as of 0.9.4.148 — needs re-verification)
- CPU CTest **145/145**, GPU CTest **146/146** (source: `docs/node-status-gate-matrix.en.md`)

## In progress

- Process split **P1-P4 complete**, G4/G6 verified (.111)
- **0.9.4.174**: TemporalAnchorV3 user-specified algorithm, product implementation
  (experimental opt-in, default unchanged, V1/V2 frozen)
- V1(.172) / V2(.173) / V3(.174) implemented in sequence. Nothing promoted to default

## Blockers

- **Real Windows acceptance** outstanding: LocalDumps+PDB capture, full user-dataset rescan,
  Test Mode toggle/scratch isolation, preview/selection, Tiles/ListMode, large-dataset traversal
- **Sparse production**: blocked by `ExactnessPolicy::RefuseAll`
- **F-1 CONDITIONAL**: NVDEC production adoption **NO**
- GPU MAX share boost: documented gap

## Next 3 actions

1. Run and record the real Windows acceptance items
2. Final Search/Index/Comparison acceptance
3. Close out the GPU MAX share boost gap (documentation basis)

## Invariants (do not change)

- engine **1.5.2** / cache format **11** — record rationale in build-history when changed
- **V1/V2 frozen**; V3 is experimental opt-in and not the default
- Do not promote existing verdicts (CLOSED / CONDITIONAL / DEFERRED / NOT ACCEPTED) arbitrarily

## Do not retry (rejected by measurement)

| Candidate | Verdict |
|---|---|
| D9b | NOT ACCEPTED |
| Cache capacity 32 | rejected |
| Scoring-path duplication | rejected |
| WICBitmapInterpolationModeFant | rejected |
| EXIF skip | rejected |
| Cache mutex contention | rejected |
| Factory2 fallback | rejected |
| Raw OS file-open handling | rejected |

Sources: `BUILD_STATUS.md`, `docs/worklog/0.9.4.en.md`

## Read order for a new session

1. **This document**
2. `docs/node-status-gate-matrix.en.md` — Node/Gate board
3. `docs/development-progress.en.md` — active queue and blockers
4. `docs/development-roadmap.en.md` — direction and prerequisites
5. Active Node brief (`docs/implementation-briefs/`)
6. As needed: `docs/build-history/`, `docs/worklog/`, `docs/version-index.md`