# S — Validation / Benchmark Track (S0-S8 integrated)

**This document is the integrated contract of the whole S track.** Per-stage
detailed contracts stay in their existing documents. This document does not
replace them; it is the entry point for seeing at a glance where the track
currently stands and which rules apply between stages.

| Layer | Document |
|---|---|
| Whole-track design (authority) | `docs/architecture/benchmark-telemetry-roadmap.{ko,en}.md` |
| Per-stage execution contracts (authority) | `implementation-briefs/S4-*`, `S5-*`, `S6-*` |
| One-page current status | `docs/node-status-gate-matrix.{ko,en}.md` |
| Actual change evidence | `docs/build-history/<version>.{ko,en}.md` |
| Reasoning lineage | `docs/worklog/0.9.4.{ko,en}.md` |

## 1. Position of the S track

The S track is a **parallel track** to the main A-H line. It does not depend on the
main gates and owns only the verification/measurement layer. It closes each stage
in the order `S0 -> S1 -> S2 -> S3 -> S4 -> S5 -> S6 -> S7 -> S8` before moving on.

Per-stage closure order: source change -> CPU/GPU build -> CTest -> execution
verification -> document update -> build history if needed -> commit

## 2. Stage definitions and current status

| Stage | Design definition (roadmap section 30) | Prerequisite | Implementation | Verify | Current verdict |
|---|---|---|---|---|---|
| **S0** | fix the Run/Suite, mode, media scope, Resource Budget, journal, isolation, cancellation, and terminal contract | — | complete | PASS | `CLOSED` |
| **S1** | console entry, help/version, headless scan, media/resource options | S0 | complete | PASS | `CLOSED` |
| **S2** | per-file AUTO->CPU->GPU-max execution core, shared mode context, per-file result event/journal contract | S1 | complete | PASS | `CLOSED` |
| **S3** | separate benchmark execution artifacts, append-only journal, crash-safe/partial persistence, derived summary | S2 | complete | PASS | `CLOSED` |
| **S4** | fix the GUI benchmark purpose (3-line summary), later turned into `Detailed Logs` by the semantic reset | S3 | implementation complete | acceptance incomplete | `IN PROGRESS` |
| **S5** | console benchmark CLI and interactive/non-interactive terminal renderer | S4 contract | infra complete | product benchmark not executed | `NOT CLOSED` |
| **S6** | repeated measurement within a suite, dataset fingerprint change detection, parallel/sequential comparison policy | S5 | gate prepared | measurement not executed | `NOT STARTED` |
| **S7** | help / usability / exit code / verbose | S6 | not started | — | `NOT STARTED` |
| **S8** | CPU/GPU build, tests, CLI/GUI execution, JSON/journal consistency, final release gate | S7 | not started | — | `NOT STARTED` |

The evidence for closing S0-S3 together and the per-stage detail are in the S track
schedule section of `docs/development-progress.{ko,en}.md`.

## 3. Standing rules across all stages

- **No second search engine.** The S layers reuse the production search path
  unchanged through the S2 `BenchmarkRunner` plus an injected `BenchmarkExecutor`.
- **Never record an unmeasured value as 0.** Distinguish it with `MeasureState`
  (measured / not_measured / not_available / partial / failed / fallback).
- **Separate the Normal Search Index from benchmark index/cache.** Never write
  benchmark artifacts into the scanned folder.
- **Automate benchmark execution.** Results count only as real measurements;
  simple linear estimation is forbidden. Partial suite results are preserved after
  cancellation.
- **The threshold stays undefined until it is defined.** Satisfying the reported
  gate conditions alone is not a pass.

## 4. Semantic boundary (the most frequently misread point)

```text
GUI  Search/Update -> [Detailed Logs] -> TelemetryRecorder / UserDiagnostic
CLI  --benchmark                    -> BenchmarkSession / BenchmarkRunner
                                      -> TelemetryRecorder / Benchmark
```

- **Benchmark and Telemetry are not synonyms.**
- The GUI does not automatically read console benchmark history.
- The console does not automatically ingest GUI detailed logs as benchmark history.
- Do not create new Benchmark Run/Stop/Pause UI in the GUI merely to collect
  diagnostics.
- `runs.jsonl` is the recovery origin; `summary.json` is derived and never
  authoritative.

## 5. Current focus and blocker

```text
active stage   S4 (GUI Detailed Logging acceptance) + the main-line product acceptance audit
direct cause   product acceptance of the implemented Search/Index/Comparison semantics is incomplete
next           S5 real-dataset product benchmark -> S6 controlled measurement gate
unresolved     the S6 threshold is undefined; a real measurement dataset is needed
```

S4 is not raised to CLOSED because this environment is headless and the actual
on-screen rendering of the result dialog could not be visually verified. That is
recorded as *not done* and is not claimed as *passed*.

## 6. Track boundaries

```text
S4 final GUI visual/save acceptance   DEFERRED
S5 product benchmark execution        DEFERRED (depends on S4 acceptance)
S6 measurement gate execution         DEFERRED (needs a real dataset)
S7 / S8                               NOT STARTED
main line F/G/H                       follow the main-line rules (independent of this track)
```

No progress percentages are used. In this track the implementation can be complete
while acceptance is still open, and acceptance can be complete while no real
measurement exists, and either one blocks the next stage.
