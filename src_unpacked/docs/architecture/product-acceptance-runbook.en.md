# Product acceptance runbook / checklist (Search · Index · Comparison)

Purpose: the executable procedure for the 0.9.4 line's **final product acceptance** (Search/Index/Comparison, currently `NOT ACCEPTED`). Results are recorded in `docs/build-history/<version>.*` and `docs/worklog/0.9.4.*`; this document defines the procedure only.

## 0. Prerequisites

- Build: `--version` matches the target build (Finder/Backend both). Real runs use the portable package or the `build-windows-gpu\Release` tree.
- Data: the real dataset root (e.g. `G:\Downloads\ss_twit`). Distinguish runs with an existing index from a first scan, and record which.
- Tools: `%TEMP%\msf_scan.log` (capture from before the run), saved detailed-log JSON (finish popup -> [Detailed Logs] -> Save), screenshots for visual items.
- Rule: **no speculative fixes** — on a crash, collect dump/PDB/logs via `crash-response-runbook` first.

## 1. Index (Load/index) checklist

| # | Check | Expected / evidence |
|---|---|---|
| I-1 | Load completes | The list fills progressively with a responsive window (no freeze). `msf_scan.log` `resultsApply`/`groupFill` numbers |
| I-2 | Scan start/end list | **No churn** - scroll/rows preserved, new groups appended at the bottom only (0.9.4.118+) |
| I-3 | No phantom groups | Deleted files never remain as group members. `diskCleanup missing=N` line present (0.9.4.116+) |
| I-4 | Incremental fingerprint | Only changed/new files counted as `analyzed` (`summary.analyzed` == actual change count) |
| I-5 | Backend survival | No kills/restarts (no 62097), `alive` heartbeats normal |
| I-6 | Summary readability | Rate bases labelled (`throughput (analysis stage)`, `overall/new files/s`, 0.9.4.119+) |

## 2. Search checklist

| # | Check | Expected / evidence |
|---|---|---|
| S-1 | Progress accuracy | analyzed/scanned/unchanged, progress/ETR consistent with reality |
| S-2 | Stop/resume | Stop keeps partial progress (by design); restart honours the frontier |
| S-3 | analysis-failed/unchanged/modified semantics | Detailed logs/detail pane match the stored schema (0.9.4.46+) |
| S-4 | Verification modes (A/B/AB) | Detailed-log `mode` == requested; the AB final pass verifies only one-side-changed pairs (0.9.4.107+) |
| S-5 | Crash/abort resilience | UI recovers on stop/restart/backend loss (one auto-resume, 0.9.4.73+) |
| S-6 | Summary panel | Total/read/index/confirmed pair counts match the telemetry (monotonic) |

## 3. Comparison (results) checklist

| # | Check | Expected / evidence |
|---|---|---|
| C-1 | Group/member/pair counts | `matches` candidates/pairs/groups match the UI and the detailed-log summary |
| C-2 | Badge semantics | `identicalContent` only on **full-content** SHA-256 equality (0.9.4.113+); distinguished from perceptual matches |
| C-3 | Derivatives/copies | Resized/re-encoded copies grouped, no false positives (0.9.4.106 fixtures + real data) |
| C-4 | reduction display | The 100.0% condition matches its documented meaning |
| C-5 | Sort/filter | Sort changes rebuild correctly; the scan-time lock behaves |
| C-6 | File-action integration | After delete/move, `delete/move/prune/forget` logs match the list (0.9.4.105+) |

## 4. Verdicts and recording

- Each item: PASS / FAIL (with reproduction) / N-A (with reason). FAILs become immediate fix-build candidates.
- Summary numbers (wall/analyzed/pairs etc.) are cross-checked 1:1 against the detailed-log JSON and recorded in build-history (see the 0.9.4.118 field cross-check format).
- Final verdicts are per Search/Index/Comparison; promoting `NOT ACCEPTED` -> `ACCEPTED` requires all three PASS and is updated in the same session as the S4/S5 promotion conditions.

## 5. References

- `docs/build-history/0.9.4.118.*` - churn field cross-check example; `0.9.4.119.*` - summary labels.
- `docs/architecture/crash-response-runbook.{ko,en}.md` - crash handling.
- `docs/development-progress.{ko,en}.md` - current queue/verdicts.
