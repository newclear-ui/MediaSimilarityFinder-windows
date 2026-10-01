# S6 Data-mining Automation — Implementation Brief

Version baseline: `0.9.4.43` (single source: `CMakeLists.txt`)
Preceding brief: `docs/implementation-briefs/S5-console-benchmark-execution.ko.md` / `.en.md`
Preceding verification: `docs/build-history/S5-verification.ko.md` / `.en.md`
Design basis: `docs/architecture/benchmark-telemetry-roadmap.en.md` §22, §25, §28, §30, `docs/architecture/storage-design.md`

**This is an S6 design document, not an implementation result report.** No S6 code
was written at this stage. The "verified" labels below are facts confirmed from actual
source or actual journals, "decision" labels are design fixed by this brief, and
"deferred" labels are intentionally left undecided.

---

## 1. Purpose

S5 made benchmark results accumulate permanently in an append-only `runs.jsonl`
journal. S6 automates **repeatable analysis and comparison** over that accumulated
journal.

The definition the roadmap already gave S6 (already fixed, not newly decided here):

```text
benchmark-telemetry-roadmap §28
  "S6 Data-mining automation: suite 자동 실행, dataset fingerprint 검증, 비교 요약"
benchmark-telemetry-roadmap §30
  "S6: Suite 자동 실행, fingerprint 검증, 비교/데이터 마이닝"
```

So the roadmap's S6 is **not a single analysis feature but three axes**:

1. **Automation** — automated Suite execution
2. **Validation** — dataset fingerprint checks
3. **Comparison / data-mining** — comparison summary

**The scope of this brief is axes 2 and 3.** Axis 1 (automated suite execution) is
recommended to be split into a separate brief inside S6 and is not defined here
(see the deferral in section 14).

`benchmark-telemetry-roadmap` §22 already defines the nature of S6's data source.

> "Console is the long-term comparison and data-mining path. Results are not automatically deleted."

In other words the Console suite storage is itself S6's input store, and S6 only
**reads** it.

## 2. Problem definition

However much the journal accumulates, a human still has to repeat the following
manual work every time.

```text
Search for suite directories under Benchmark/Console
Collect the list of runs.jsonl files per suite
Parse journal JSONL and classify truncated tail / commitless case
Select only finished runs (those with case_complete + run_finished)
Group by datasetFingerprint
Separate by buildVersion
Aggregate elapsed by requested/effective mode
Separate by media (Image/Video)
Compare elapsed delta / ratio across runs
```

**No tool exists for this today.** Verified:

- No program anywhere in the repository reads `runs.jsonl`, `summary.json`,
  `auto.json`, `cpu.json`, `gpu-max.json` or any `Benchmark/` directory.
- The only code that reads the journal is `replayJournal()` + `buildSummaryJson()`
  in `src/benchmark_journal.cpp`, and its output is **counts and a `totalElapsedMs`
  sum only**. There is no median / percentile / trend / regression logic.
- **Python is not used in this project at all.** Zero `.py` files, zero
  `requirements.txt` / `pyproject.toml` / `Pipfile` / `setup.py`. Both CI workflows
  use `shell: pwsh`.
- The scripting convention is PowerShell (14 files under `scripts/`, 7 under
  `scripts/validation/`).
- The closest precedents do not read the journal; they read unrelated inputs.
  `scripts/validation/i3_decode_decomposition.ps1` (per-bucket median),
  `scripts/validation/i3_paired_analysis.ps1` (paired median ratio),
  `tests/dataset_baseline.cpp` (repeated-run distribution output).

## 3. Scope

What S6 does. Per-stage responsibility is in sections 9 to 13.

- **Discovery and collection** of journals under `Benchmark/Console` (read-only)
- Ingestion that parses journals and **honours the S3 recovery rules**
- **Separated representation** of derived values versus measured values (section 7)
- **Grouping** centred on `datasetFingerprint`
- **Aggregation** at run / mode / case level
- **Cross-run comparison** (same dataset, same conditions, different build)
- **Regression candidate** detection (threshold undecided, section 14)
- Reproducible analysis **output** with provenance

## 4. Non-scope

S6 does **not**:

- Implement a benchmark **execution engine**. It reuses the S2
  `BenchmarkRunner` / `ProductionBenchmarkExecutor` and creates no new run path.
- **Append / modify / delete / recover** journals. S6 only reads them.
- Change S2 / S3 / S5 **code or contracts**. It does not bump a schema or add a record.
- **Migrate** the journal schema. It does not reinterpret an old schema and rewrite.
- Change, reuse or migrate the legacy `BenchmarkRecorder` schema (section 11).
- Change the GUI.
- Change GPU / CUDA / NVDEC.
- **Present a derived value as a measurement.** A derived value is never stored or
  presented as a measured one.
- Fix an arbitrary statistical threshold (section 14).
- **Automate suite execution** (sections 1 and 14).

## 5. Data source

### 5-1. Single authoritative source

```text
runs.jsonl  (S3 append-only journal)
```

This is the only authoritative input, identical to `benchmark-telemetry-roadmap` §29.2
and the "terminal rendering contract" in `storage-design.md`.

> "Terminal display content is not the data source; journal/summary is the canonical source."

S6 opens the journal **read-only**. It takes no file lock, appends no record, and
writes nothing into the suite directory.

### 5-2. Not authoritative

The following are **never treated as an analysis source under any circumstance.**

| Target | Reason |
| --- | --- |
| `--log` file (renderer text sink) | A display artifact. It is not regenerated from the journal and is not a subset of it |
| Console stdout / TTY output | Same |
| `summary.json` | By the S3 contract it is a **journal replay result and non-authoritative**. It can be used since it is regenerated after deletion, but it is not the source |
| `suite.json` | Suite metadata, a separate thing from journal records |
| `runtime/` tree | Execution scratch, not evidence |
| `Benchmark/GUI/*.json` | GUI snapshots. A **different schema** and a separate storage |
| legacy `BenchmarkRecorder` JSON | Schema 9. Completely separate from the journal (schema 1), see section 11 |
| Human-edited documents or tables | No basis |

The S6 implementation should **not read `summary.json` by default.** Reading the
journal yields the same information from a more trustworthy source. If
`summary.json` must be read (for example to check regeneration status), the output
must state that it is a derived artifact.

## 6. Journal schema — actual confirmed results

Below is the field list confirmed from **both the source and real journals**.
Fields that do not exist are recorded as **absent**; nothing was filled in by guess.

Verified against: `src/benchmark_journal.cpp` writer bodies plus a full key scan of
the build-output journals, **29 files / 862 lines**.

### 6-1. Common fields (every record)

| Field | Source | Note |
| --- | --- | --- |
| `journalSchemaVersion` | constant | `kBenchmarkJournalSchemaVersion = 1` |
| `eventType` | — | `run_started` / `mode_result` / `case_complete` / `run_finished` / `run_cancelled` |
| `recordId` | deterministic | The `case_complete:<runId>:<caseId>` form is what makes a repeated identical commit idempotent |
| `suiteId`, `runId` | S2 run | |
| `timestamp` | `benchmarkNowStamp()` | **Labelling problem exists — section 16** |

### 6-2. `run_started` (confirmed in 10 journals)

```text
mediaScope, scanImages, scanVideos, sourceRoot, sourceRootLabel, sourceRootId,
datasetFingerprint, buildVersion, startedAt, filesStarted
```

### 6-3. `mode_result` (one per mode per case)

```text
caseId, requestedMode, effectiveMode, status, started, completed, elapsedMs,
summary{ scanned, added, modified, unchanged, removed, analyzed, candidates,
         groups, indexedVideos, videoCandidatePairs },
errorMessage
```

`requestedMode` / `effectiveMode` value set: `AUTO` / `CUDA` / `CPU`.
`status` value set: `SUCCESS` / `FAILED` / `CANCELLED` / `SKIPPED`.

### 6-4. `case_complete` (commit marker)

```text
caseId, path, media, status, elapsedMs, errorMessage
```

`media` value set: `Image` / `Video` / `Unknown`.
After the S5-3 `Scanner` MediaKind fix, `Image` / `Video` are actually recorded.

### 6-5. `run_finished`

```text
status, filesStarted, filesCompleted, filesRemaining, completedAt, completionReason
```

`completionReason` is always `"completed"`.

### 6-6. `run_cancelled`

```text
status(=CANCELLED fixed), filesStarted, filesCompleted, filesRemaining, completionReason(=reason)
```

- **There is no `completedAt` field.** Confirmed from source. Its shape differs from
  `run_finished`.
- **Zero real journal samples exist.** Real Ctrl+C triggering was `NOT RUN` in the S5
  E2E, and the deterministic cancellation in `benchmark_integration_test` uses a
  temp journal. So no measured sample of this record exists yet (section 18).

## 7. Measured / Derived / Invalid three-way split

This split is enforced across all of S6.

### 7-1. Measured (values recorded in the journal)

Only values that **actually exist** in a journal record are treated as measured.

| Value | Record | Field |
| --- | --- | --- |
| case elapsed | `case_complete` | `elapsedMs` |
| mode elapsed | `mode_result` | `elapsedMs` |
| mode result | `mode_result` | `requestedMode` / `effectiveMode` / `status` / `started` / `completed` |
| case result | `case_complete` | `status` / `media` / `path` |
| dataset sameness | `run_started` | `datasetFingerprint` |
| build | `run_started` | `buildVersion` |
| scope | `run_started` | `mediaScope` / `scanImages` / `scanVideos` |
| scan counters | `mode_result.summary` | 10 counters |
| run progress | `run_finished` | `filesStarted` / `filesCompleted` / `filesRemaining` |
| execution time | both | `startedAt` / `completedAt` |

### 7-2. Derived (computed by S6)

Values S6 computes, and which **must be marked as derived**.

| Derived value | Basis | Display rule |
| --- | --- | --- |
| run wall duration | `completedAt - startedAt` | Both use the same formatter (localtime, no timezone) so they are **comparable with each other**. See section 16 |
| case/mode elapsed total | sum of record `elapsedMs` | State the population |
| mean / median / p95 / min / max | elapsed population | **State which population it is** (section 15) |
| mode-to-mode delta (GPU-max vs CPU) | elapsed across modes of the same `caseId` | |
| speedup ratio | across modes within one `caseId` | A single case value is very noisy; state the population |
| regression % | against a baseline run | threshold undecided (section 14) |
| failure rate | status counts / total | Follows the S2 aggregate precedence (section 17) |
| per-media aggregation | `case_complete.media` | |
| per-build aggregation | `buildVersion` | |
| incomplete / discarded counts | anomaly criteria (section 13) | |

### 7-3. Invalid / Incomplete (excluded from analysis)

The following are **not included in analysis.**

- A case with `mode_result` records but no `case_complete` (an uncommitted transaction)
- A run with no `run_finished` / `run_cancelled` (in progress, or crashed)
- A final tail line with no newline (`TruncatedTail` — discarded per the S3 rule)
- A complete line that fails to parse (`MidFileCorruption` — **fatal** per the S3 rule)
- `recordId` collision with a differing payload (`DuplicateConflicting` — **reported as
  an anomaly, first record kept**)
- A run with an empty `datasetFingerprint` (cannot be grouped; the
  `DatasetFingerprint::state` was probably `not_available` / `failed`)
- The elapsed of a `status = SKIPPED` mode (not measured. **Must not be treated as 0**)

**Invalid data is excluded from the calculation, but the excluded count must be
printed.** Silently dropping it makes the analysis look better than it is.

## 8. Analysis possible / impossible with the current journal

This section is the core of the S6 design. **What exists and what does not are
recorded separately.**

### 8-1. Analysis possible today (journal alone)

| Analysis | Backing field |
| --- | --- |
| Group identical datasets | `datasetFingerprint` |
| Compare elapsed across builds for the same dataset and scope | + `mediaScope` / `scanImages` / `scanVideos` / `buildVersion` |
| Compare modes within one build (AUTO / CPU / CUDA) | `requestedMode` / `effectiveMode` |
| Compare requested versus effective (capability observation) | the two mode fields |
| case-level elapsed distribution | `case_complete.elapsedMs` |
| mode-level elapsed distribution | `mode_result.elapsedMs` |
| Image versus Video separated aggregation | `case_complete.media` |
| success / failed / cancelled / skipped counts | the status fields |
| scan counter comparison (`analyzed`, `groups`, `candidates`, ...) | `mode_result.summary` |
| run regression candidate detection (delta against a baseline) | the combinations above |
| identifying incomplete / cancelled runs | presence of a terminal record |

### 8-2. Analysis **impossible** today — the field does not exist in the journal

Measured by full-string key scan across 29 journals / 862 lines:

| Wanted analysis | Required field | Journal measurement | Outcome |
| --- | --- | --- | --- |
| **Distinguish builds by git commit** | `git` / `buildGit` / `commit` | **0 occurrences** | **Resolved 2026-10-01** - `run_started.gitCommit` added |
| **Separate by CPU Resource policy** | `resourcePolicy` / `cpuPercent` / `gpuPercent` | **0 occurrences** | **Impossible** |
| **Distinguish GPU on/off** | `gpuEnabled` / `gpuBackend` | **0 occurrences** | **Impossible** |
| **Compare by distance** | `distance` | **0 occurrences** | **Impossible** |
| run wall duration | `duration` | **0 occurrences** | **Derivable** from `startedAt`/`completedAt` (section 16) |
| run-level requested mode list | `requestedModes[]` | absent | **Derivable** by scanning all `mode_result` records |

**The root cause is that `MSF_BUILD_GIT` is not recorded in the journal.** S5-3 added
`MSF_BUILD_GIT` to the generated header and displayed it in the Console header, but
the journal is the S3 schema and was not changed. So the journal holds only
`buildVersion` (e.g. `0.9.4.43`), and **two different commits of the same version
number cannot be told apart.**

> **Updated 2026-10-01**: this gap is resolved. `run_started.gitCommit` now records S5's
> `MSF_BUILD_GIT` as an additive field, and a real run was confirmed to write a journal
> value identical to the binary's generated value. The 0-occurrence figures in the table
> above are **preserved as the measured values at survey time**. See section 22 item 1.

This **directly conflicts** with `benchmark-telemetry-roadmap` §25, which requires a
Run to store "`appVersion/gitCommit`", and with §28/§30, which define the core of S6
as "fingerprint validation + comparison summary". In other words, **the roadmap's S6
cannot be completed in its current state**, because the only key that could compare
two builds of the same version does not exist today.

`distance` is the same situation. `BenchmarkRun::distance` exists in S2 but the
journal writer does not record it. `resourcePolicy` also exists in the S2
`BenchmarkRequest` but is absent from the journal.

**Decision**: S6 can start without these fields (the 8-1 scope is immediately
doable), but without the `git` item the "cross-build comparison" the roadmap
defines **cannot be completed.** Journal field augmentation is therefore classified
as an S6 entry condition (section 22).

**This brief does not change the journal schema.** Having the S6 implementation bump
a schema is S3's area and needs a separate decision (see the deferral in section 14).

## 9. Ingestion responsibility

Moving the journal into an analysable form. **The S3 recovery rules are not
reimplemented.**

- Journal **parsing/recovery** **reuses `msf::replayJournal()`**. The semantics S3
  fixed are not re-created: truncated tail discarded, mid-file corruption fatal,
  identical duplicate ignored, conflicting duplicate reported as an anomaly with the
  first record kept.
- Suite discovery reads only the `<storageRoot>/Benchmark/Console/suite-*/runs.jsonl`
  pattern. It does not read `runtime/`, GUI storage or legacy artifacts.
- Several runs can accumulate in one journal, so separation is per `(runId, caseId)`.
  S3 already provides this via `replayJournal(path, runIdFilter)`, and it is the item
  fixed as a real defect in `S3-BUG` (keying `pending` on `caseId` alone merged and
  committed mode records from different runs as one case). S6 follows this rule
  unchanged.
- Ingestion keeps these counters: `journalFiles` / `runs` / `committedCases` /
  `incompleteCases` / `anomalies` / `fatal`.

**Decision**: S6 does **not write a new journal parser.** Re-parsing the journal in
PowerShell or Python would duplicate the S3 recovery rules, which section 10 forbids.

## 10. Normalization responsibility

- Journal values into an internal representation. `status` / `mode` / `media` use
  **S3's own name strings unchanged.** Mapping them into an S6-specific enum must not
  change their meaning.
- Paths: `case_complete.path` is a full absolute path. **Analysis output uses the
  basename only.** (The journal must keep the original verbatim, and S6 does not
  change the original.)
- Distinguish absent from 0. A missing or unmeasured `elapsedMs` is **excluded from
  analysis**, not 0. This follows `AGENTS.md` item 9: "unmeasured values must be
  recorded as `N/A` or `Not measured`".
- Units: journal `elapsedMs` is milliseconds. The S6 output states its unit explicitly.

## 11. Legacy benchmark separation

`legacy benchmark != new durable journal analytics` is maintained. **Verified.**

| Item | legacy | journal |
| --- | --- | --- |
| Owner | `BenchmarkRecorder` (`src/benchmark.{h,cpp}`) | `BenchmarkJournalWriter` (`src/benchmark_journal.{h,cpp}`) |
| schema version | `kBenchmarkSchemaVersion = 9` | `kBenchmarkJournalSchemaVersion = 1` |
| Storage | **Writes no file.** `toJson()` returns a string | `runs.jsonl` append |
| Readers | None. The GUI dialog only reads it in memory | `replayJournal()` |
| Schema changes | Forbidden | Forbidden |

- The two version numbers are **deliberately independent** and are never bumped
  together (stated in the `benchmark_store.h` and `benchmark_gui_store.h` comments).
- The only shared code is the single JSON escape function, and that is function reuse,
  not schema or meaning reuse (as the `benchmark_store.h` comment states).
- The GUI snapshot schema is yet another independent number (1), different from the
  journal's.
- S6 **neither reads nor writes** legacy JSON. No migration either.

## 12. Grouping responsibility

Grouping keys use **only fields that exist**. A key that does not exist must not be
used.

### 12-1. Primary: `datasetFingerprint`

The strongest sameness key. Per the D8a decision in `worklog 0.9.4`, dataset identity
deliberately contains **no absolute path, timestamp or hardware name**, so identical
content shares a fingerprint even under different roots. That property is the basis
for S6 comparison.

However, **do not assume that fingerprint sameness means all conditions are
identical.** The following must be carried as comparison conditions alongside it.

### 12-2. Conditions to compare alongside (a separate axis from fingerprint)

| Axis | Field | Note |
| --- | --- | --- |
| media scope | `mediaScope` / `scanImages` / `scanVideos` | `all` is never decomposed into images+videos |
| build | `buildVersion` | Carries the provenance state (`Known`/`Unknown`/`Legacy`) too |
| requested mode | `mode_result.requestedMode` | The run-level list is derived |
| effective mode | `mode_result.effectiveMode` | Capability observation. **Never merged with the requested mode** |
| case sameness | `caseId` | The key for per-file comparison, paired with the dataset |

### 12-3. Keys that **must not** be used as grouping (8-2)

```text
resourcePolicy      — not in the journal
gpuEnabled          — not in the journal
gpuBackend          — not in the journal
distance            — not in the journal
```

Using them would make the claim "same conditions" false. **They are not used, and
the output states that the condition was not recorded.**

> **2026-10-01 update**: `git commit` was **removed** from the list above. S3 added
> `run_started.gitCommit`, so it now exists in the journal (see 8-2). Its presence
> in the journal is not the same as being able to compare by commit, so the three
> provenance states (`Known`/`Unknown`/`Legacy`) are kept alongside it. `Legacy` has
> no field at all, is not a commit-level comparison target, and is never filled in
> with the current HEAD.

### 12-4. Why there is no single composite key (S6-3 decision)

A key combining `datasetFingerprint + mediaScope + buildVersion + gitCommit +
requestedMode + effectiveMode` puts every run in a cohort of its own and makes
**cross-build comparison structurally impossible**. It looks tidier, and it quietly
deletes the comparison, so it was not adopted.

Instead each analysis axis is a **typed key struct**, and the caller picks the view
its question needs (`src/benchmark_data_grouping.h`).

| View | Key | Question it answers |
| --- | --- | --- |
| `DatasetCohortKey` | `datasetFingerprint` | Observing one dataset across builds and modes |
| `ScopeCohortKey` | `datasetFingerprint` + `mediaScope` | The baseline population for cross-build observation |
| `BuildCohortKey` | `buildVersion` + `gitCommitState` + `gitCommit` | Which runs came from the same build |
| `ModeCohortKey` | `requestedMode` + `effectiveMode` | Fallback and capability observations |
| `CaseCohortKey` | `datasetFingerprint` + `caseId` | Per-file comparison |

### 12-5. What `caseId` actually means (S6-3, measured)

S2 derives `caseId` as `IndexManager::folderId(file.path)`, a hash of the canonical
path. So it is **not run-scoped: the same file in two runs yields the same
`caseId`** (confirmed by measurement — two runs over the same file produced the same
id). No new hash is invented; the existing `caseId` is used, but **paired with the
dataset fingerprint** rather than trusted alone, because it hashes an absolute path.

### 12-6. Grouping does not judge comparability

S6-3 only *constitutes* cohorts and never concludes `comparable = true`.
`BuildCohort::commitComparable` expresses **provenance quality only** (true only for
`Known`) and is not a judgement about the data. The absent resourcePolicy, distance
and GPU backend, the undefined controlled environment and the insufficient repeat
runs all carry forward unchanged.

Mode ordering uses the canonical `AUTO → CPU → GPU-MAX` order. That differs from the
`GpuBackendKind` declaration order (`Auto, Cuda, Cpu`), so an explicit rank is used.
This ordering is for **display and group results** and does not redefine execution
order.

## 13. Aggregation responsibility

The three populations are **explicitly distinguished** and never mixed.

| Level | Unit | Measured value | Caution |
| --- | --- | --- | --- |
| case-level | one `case_complete` | `elapsedMs` | One whole file (sum over its modes) |
| mode-level | one `mode_result` | `elapsedMs` | One mode execution |
| run-level | one run | `completedAt - startedAt` | Derived. Wall clock |

- `case_complete.elapsedMs` is by S2 definition the **sum of that case's mode elapsed
  values** and excludes discovery. So a case-level value is "file processing time",
  not "whole run time".
- run-level wall duration and the case-level sum are **not equal**. The two are not
  compared on the same axis. (This connects to the §25 implementation principle
  "record benchmark overhead".)

Every aggregation result must carry:

```text
n            — population size
mean, median, p95, min, max, range
sourceRunIds — the runs the result rests on
```

### 13-1. S6-4 implemented contract (2026-10-01)

Aggregation nests the S6-3 `dataset → scope → build → mode` hierarchy rather than
flattening it. Each level accumulates its own samples; statistics are never assembled
afterwards, because a mean of means is not a mean.

Every statistics block carries `MetricLevel` and `MetricResolution` together.

| Level | `MetricLevel` | `MetricResolution` | Why |
| --- | --- | --- | --- |
| mode | `ModeElapsed` | `Recorded` | written per record by S2 |
| case | `CaseElapsed` | `Recorded` | written per record by S2 |
| run | `RunWallDuration` | `OneSecond` | derived from second-resolution stamps |

### 13-2. Eligibility differs per level

| Level | Eligible when | Exclusion reasons |
| --- | --- | --- |
| mode | `status == Success` **and** an elapsed exists | skipped / failed / cancelled / missingElapsed |
| case | `status == Success` | skipped / failed / cancelled |
| run | a status exists, is `Success`, and a wall exists | skipped / failed / cancelled / missingElapsed / **invalid** |

- mode `elapsedMs` is optional, so `missingElapsed` is possible there. **A Success
  with no duration is excluded as `missingElapsed`, never read as 0.**
- case `elapsedMs` is a non-optional `double`, so `missingElapsed` **cannot occur** at
  that level. Status alone decides.
- A run with no terminal record has no status at all. That is excluded as `invalid`
  rather than assumed Success, and it is tallied under no existing status value: it is
  counted separately as `runsWithoutStatus`.

### 13-3. `observed = eligible + excluded` is enforced everywhere

`SampleAccounting` preserves that identity and keeps the **reason** for every exclusion.
A bare excluded count is not an acceptable result, because it cannot be told apart from
a bug. That 384 of 384 real exclusions are `skipped` is only explicable in this form.

### 13-4. `all` is never decomposed

`all` is its own scope. It is never converted to `images + videos`, and its elapsed is
never distributed between them, because the journal does not record how many cases of
each scope an `all` run actually covered.

### 13-5. Logical entities, not raw journal lines, are aggregated

The real store's `suite-ORDER-A/B/C` wrote the same journal more than once. Averaging raw
lines would weight a repeatedly-written suite above an identical one written once, so
aggregation uses the logical `(journal, runId, caseId, mode)` entities that S6-1/S6-2
recovered and normalized.

> **runId is not unique in a real store.** `suite-ORDER-A/B/C` share one `runId`, so
> **31 accepted runs carry only 29 distinct runIds**. Per-file populations are therefore
> keyed on `(sourceJournalPath, runId)`, and the collision is only **reported** through
> `runsWithCollidingIdentity`. Whether those are one measurement recorded three times or
> three separate measurements is not something the journal can answer, so **neither
> interpretation is applied silently.**

### 13-6. Statistics and the rounding rule

`count / min / max / mean / median / p95` are provided.

- median: the middle sample for an odd count, the mean of the two middle samples for an
  even one
- p95: nearest rank, `index = ceil(0.95 * n) - 1`. It is computed and **not hidden** for
  `n = 1` and `n = 2` either, and travels with its sample count. Interpreting it is a
  later stage's job.
- every reported value is rounded to **6 decimal places** (`aggregationRoundMs`). Double
  summation can vary slightly with order, so the printed form is fixed.
- with zero eligible samples the statistic **does not exist** (and is not 0). A 0 is a
  measurement.

## 14. Comparison / Regression — threshold undecided

### 14-1. Comparison possible today

Mode-to-mode elapsed comparison across the same `caseId`, within the same
`datasetFingerprint` + `mediaScope` + `buildVersion`. And delta / ratio against a
baseline run.

### 14-2. Regression verdict threshold — **deferred, not decided in this brief**

The directive says not to arbitrarily fix criteria such as 5% / 10% / 20%. That is
followed. In addition, **this repository already has a governing rule and no new
baseline is invented here.**

- `AGENTS.md` item 9: "**A difference within the measurement error range is not to be
  declared an improvement.** Record the run count (5 or more recommended) plus
  median/min/max/range."
- `AGENTS.md` item 9 recommended status values: `BASELINE` / `PASS` / `NOT ACCEPTED` /
  `REJECTED` / `DEFERRED` / `LOW PRIORITY` / `INCONCLUSIVE` / `SUPERSEDED`.
- `worklog` entry `E-3B-BUG` item ③ is an **actual precedent**: a simple threshold of
  my own (`0.5 x framesPerSample`) was removed as brief-forbidden because it
  contradicted the measurement.

**Decision**: S6-1 lists regression **candidates** as numeric deltas plus population
statistics, and does **not** declare a regression. The verdict threshold is decided
separately after the measurement error range is measured from real repeated runs
(section 22 entry condition).

### 14-3. Fundamental limit of regression analysis (inherited from S2-PERF)

`worklog` `S2-PERF` already records and **accepted** the following.

> "`scan()` walks the folder on every call, so N files means N walks = O(N^2)."
> "Process isolation and the OS filesystem cache are recorded as **uncontrolled**."

**So even on the same machine with the same dataset, elapsed varies with the OS
filesystem cache state, external load and concurrent execution.** Ignoring this and
reading a before/after code delta as a "regression" measures something other than
the code.

**Decision**: S6 output provides a regression **candidate plus a measurement-condition
warning**, not a regression verdict. It prints whether cache / load were controlled
and states plainly when they were not.

## 15. Cancellation / Failure handling

- The S2 aggregate precedence is **not redefined**:
  `Cancelled > Failed > Success > Skipped` (the `benchmark_core.h` contract).
- **A cancelled run is not included as a normal success sample.** A run with
  `run_cancelled` is classified separately, and even its `SUCCESS` cases must be
  reported as "not a fully executed condition" sample. If the run was cancelled then
  its cases are **not used as a normal comparison baseline run** (checked as an entry
  condition).
- `SKIPPED` is not a measured value. It is excluded from distribution statistics and
  only counted.
- `FAILED` preserves `errorMessage`. But following the S3 contract, **`errorMessage`
  strings are not used to infer fallback (CPU/GPU bypass).** The S5 `A2` CPU FB
  REJECTED decision already established this rule
  (`docs/architecture/legacy/S5_REJECTED_CPU_FB_INDICATOR.*`).
- `errorMessage` is a human-readable diagnostic string. It is **not used as an
  aggregation or grouping key.**

## 16. Timestamp handling

### 16-1. The problem (found in S5, S3 code unchanged)

`benchmarkNowStamp()` in `src/benchmark_store.cpp` prints the result of `localtime_s`
through `strftime("%Y-%m-%dT%H:%M:%SZ")`. In other words it **appends a literal `Z`
to a local time value.** It is not UTC.

The timestamps in this repository have **split four ways.** All confirmed:

| Location | Format | Origin |
| --- | --- | --- |
| `benchmark.cpp` `localTimeStr()` | `%Y-%m-%dT%H:%M:%S` | `localtime_s`, no timezone |
| `benchmark_core.cpp` `isoNow()` -> `run.startedAt` | `%Y-%m-%dT%H:%M:%S` | `localtime_s`, no timezone |
| `benchmark_store.cpp` `benchmarkNowStamp()` -> journal `timestamp` | `%Y-%m-%dT%H:%M:%SZ` | `localtime_s` + literal `Z` |
| `console_benchmark_cli.cpp` suite id | `YYYYMMDD-HHMM-SS` | **real UTC** (`gmtime`) |

Measured values from the same run:

```text
record timestamp      2026-10-01T05:02:38Z
run_started.startedAt 2026-10-01T05:02:38
run_finished.completedAt 2026-10-01T05:02:45
```

### 16-2. S6 handling rules

S6 **neither hides this problem nor silently reinterprets it as UTC.**

1. **Do not order runs by the journal record `timestamp`.** The `Z` is false, so it
   is not a UTC comparison. The ordering keys are instead:
   - 1st `suiteId` (real UTC `YYYYMMDD-HHMM-SS`, lexicographic equals chronological)
   - 2nd `runId` (`run-YYYYMMDD-HHMM-SS`)
   - 3rd physical line order within the journal (append-only guarantee)
2. **Run wall duration is computed only as `completedAt - startedAt`.** Both use the
   **same formatter** (localtime, no timezone) so they are comparable with each other.
   This value is a **monotonic local duration**, not an absolute instant. It is
   inaccurate across a DST boundary, and the output states that.
3. **Never present an absolute instant without stating the timezone.** Output shows
   the original `startedAt`/`completedAt` values and labels them "local time,
   timezone not recorded". It is never called UTC.
4. If the journal `timestamp` appears in output, the timezone labelling problem is
   printed alongside it.

### 16-3. Prerequisite / follow-up classification

This is already recorded as S3 follow-up debt in
`docs/build-history/S5-verification.ko.md` section 8. S6 classifies it as **a
separate S3 follow-up, not an S6 prerequisite.** Reason: the rules in 16-2 alone make
S6 analysis **possible** (both the ordering key and the duration derivation come from
other fields). Analysis does not become possible only after timestamps are fixed.
This debt is re-examined whenever trend / time-series analysis is extended.

## 17. Output format

### 17-1. Candidates and selection basis

| Candidate | Assessment |
| --- | --- |
| **C++ analysis engine + CLI** (`msf_core` plus a CLI command) | **Adopted.** Reusing `replayJournal()` avoids duplicating the S3 recovery rules (sections 9 and 10). Re-parsing the journal in PowerShell or Python would reimplement them. Distribution is the same exe as today |
| standalone exe | Unsuitable. The product build already includes it, so a separate distribution gains nothing |
| PowerShell helper | Cannot use `replayJournal()`; it would **reimplement** the journal parser, violating section 10. Allowed only as a **CSV post-processing helper**, which matches the existing `scripts/validation/` convention |
| Python | **Unsuitable.** Zero `.py` files in the repository, zero packaging files, CI is `pwsh` only. Adding a new language and distribution dependency costs this project far more than it gains |
| Markdown report | Unsuitable for storage. Limited to a human-readable summary |
| JSON / CSV export | **Adopted (machine-readable).** Other tools can reuse it, and the `scripts/validation/i3_*` convention is already used to consuming CSV |

### 17-2. Decision

S6 provides **two layers of output**.

1. **machine-readable (required)** — JSON or CSV. Every `derived` item is marked as
   `derived`, and provenance is included.
2. **human-readable (required)** — a console table. Both outputs come from the **same
   computed result** and must not differ in value (section 19).

It does not overwrite `summary.json` and does not write into the suite directory
(5-2).

## 18. Reproducibility

Same input -> same output is the default direction.

- **Identical journal set + identical options must produce byte-identical output.**
- The analysis result does **not** embed an analysis timestamp (otherwise output
  changes on every run). Because provenance requires execution environment
  information, the `analysisTimestamp` in section 21 is separated as **metadata
  outside the output scope** and is not in the default output. Bringing it into the
  default output is not allowed even with a fixed epoch-style value.
  -> **Decision: no timestamp in the default output; enabled only via `--provenance`.**
- All ordering is **deterministic** (lexicographic or explicit key order). Iteration
  order of a hash container (`std::unordered_map`) is never printed as-is.
- Floating point output uses fixed decimal places. The rounding rule is stated.
- Environment-dependent values such as execution time, host name and user name are
  **not mixed into the result.**

## 19. Provenance

Where possible, every analysis result records its origin.

```text
sourceJournalRoot     — the storage root read
sourceJournalFiles    — the runs.jsonl list read (path plus SHA-256, or size/line count)
runIds                — included runs
suiteIds              — included suites
datasetFingerprints   — included datasets
buildVersions         — included builds
analysisToolVersion   — analysis tool version (CMake project VERSION)
analysisCommand       — the options used
```

- **`analysis timestamp` and `benchmark timestamp` are not confused.** The former
  appears only in `--provenance` output; the latter is a journal original value with
  its own label.
- `runIds` and `datasetFingerprints` are **required**, because without them it is
  impossible to reproduce "which data produced this conclusion".

## 20. Testing strategy

Defined before actual implementation. Cases are aligned to the real journal contract.

### 20-1. Ingestion

- A normal journal (run_started / mode_result xN / case_complete xN / run_finished)
- **Incomplete tail** — final line with no newline -> discarded, everything up to the
  last complete record used
- **Commitless case** — `mode_result` present, `case_complete` absent ->
  `incompleteCases`
- **Cancelled run** — `run_cancelled` present, no `completedAt`
- **Duplicate identical** — ignored
- **Duplicate conflicting** — reported as an anomaly, first record kept
- **Mid-file corruption** — fatal, no auto-repair
- **Empty journal / missing file** — not an error but "no data"
- Several runs accumulated in one journal must separate by `(runId, caseId)`
  (`S3-BUG` regression guard)
- On `fatal`, partial results must not be emitted as normal statistics

### 20-2. Grouping

- Same `datasetFingerprint` + same scope -> same group
- Same fingerprint + different `mediaScope` -> **different group**
- Same fingerprint + different `buildVersion` -> different group
- Whether the output **surfaces that two different commits of the same
  `buildVersion` cannot be told apart** (absence of git, 8-2) — this test is what
  makes the 8-2 gap visible to the user
- A run with an empty `datasetFingerprint` is excluded from grouping + counted

### 20-3. Aggregation

- case-level / mode-level / run-level are **not mixed** (population label verified)
- A group with `n = 1` must not have statistics forced out of it
- `SKIPPED` does not enter elapsed statistics
- Non-comparable conditions (different fingerprint / different scope) are excluded
  from comparison
- An absent value is not displayed as 0

### 20-4. Comparison

- Same dataset / different build
- Same build / different mode
- Mode-to-mode delta within one `caseId`
- Behaviour with 0 or 1 comparable runs
- **Regression percentage calculation** (a value, not a verdict)

### 20-5. Reproducibility

- Two runs over the same journal set -> **byte-identical output**
- Output identical regardless of file discovery order / `unordered_map` order
- Deterministic rounding

### 20-6. Empty / partial

- 0 journals -> "no runs", not an error
- 0 comparable runs -> explicit no-comparable state
- Only partial runs present -> marked as partial
- All cases incomplete -> 0 aggregate plus the reason

## 21. Implementation sequence (internal S6 decomposition)

**This does not create a new official roadmap node**; it is an internal breakdown of
the single S6 node. The roadmap's S0 to S8 ordering is unchanged.

```text
S6-1  Data source / schema contract
      Fix the journal field inventory, decide on the 8-2 gap augmentation,
      ingestion plus provenance, statistic and output status values

S6-2  Ingestion / normalization
      replayJournal reuse wrapper, path / absent value / unit normalization

S6-3  Grouping
      datasetFingerprint + scope + build + mode axes

S6-4  Aggregation
      case / mode / run three populations, n + mean/median/p95/min/max/range

S6-5  Cross-run comparison
      same caseId comparison, condition mismatch exclusion rules

S6-6  Regression candidate / anomaly reporting
      numeric delta plus measurement condition warning. The verdict threshold is
      decided after S6-1

S6-7  Full verification
      CPU/GPU CTest, CLI E2E against real journals, reproducibility verification
```

Each stage starts only after the previous one is actually verified (section 23).

## 22. Entry conditions

Conditions needed to start S6-1. **Item ① below was resolved on 2026-10-01.**

1. ~~**`git` (or an equivalent build identity) must be recorded in the journal.**~~ -> **Resolved**
   - How it was resolved: `gitCommit` was added to the S3 journal's `run_started` as an
     additive field. The value reuses S5's `MSF_BUILD_GIT` as-is, and no git command is
     re-executed during a benchmark run, so the journal value always matches the build
     provenance of the binary that actually ran.
   - **The schema version stays 1.** Three independent pieces of evidence, all confirmed
     by measurement. (1) `kBenchmarkJournalSchemaVersion` is **written** at 7 sites and
     read nowhere; no code branches on it. (2) The replay parser extracts only the keys
     it knows, ignores missing fields, and does not reject unknown fields. (3) The
     repository already settled this judgement in `benchmark_schema_test`: "meta.schemaVersion
     is still 9 (**additive fields did not force a bump**)".
   - **Measured verification**: `run_started.gitCommit` from a real Console benchmark run
     matched that binary's generated `MSF_BUILD_GIT` **exactly**. Comparing the
     `run_started` field sets of an old and a new journal showed `gitCommit` as the only
     added field and zero removed fields. A run with no provenance records `"unknown"`,
     and the 29 pre-existing journals that have no such field still replay (no existing
     journal was modified).
   - Remaining constraint: `distance` and `resourcePolicy` are still absent from the
     journal (item ②). Commit-level comparison is now possible, but two of the
     condition-sameness axes remain empty.
2. **Decide whether `distance` and `resourcePolicy` are recorded.** See 8-2.
   **Unresolved (DEFERRED)** — extending the journal schema further is a separate
   decision and is out of this change's scope.
3. **Define a controlled measurement environment.** `S2-PERF` accepted the OS
   filesystem cache and process isolation as **uncontrolled**. Regression
   interpretation is not trustworthy without that control (14-3). **Unresolved** — it
   is a product decision.
4. **Repeated run data.** `AGENTS.md` item 9 recommends 5 or more runs. The current
   real journals are S5 E2E output and hold no repeated-run statistics. A threshold
   can only be decided after measured repeated data (14-2). **Unresolved.**
5. **A `run_cancelled` measured sample.** Currently 0 in the journal (6-6). The
   cancelled run analysis rule is verified only against the source contract.
   **Unresolved.**

## 23. Exit conditions

Criteria for closing S6.

- Every stage `S6-1` to `S6-7` is verified by an actual CPU/GPU build plus CTest.
- The section 20 test categories are implemented and pass.
- Every item in 8-1 "analysis possible today" is produced end-to-end from a real
  journal.
- The impossible items in 8-2 are **explicitly surfaced in the output** (not silently
  missing).
- Reproducibility (same input -> same output) is confirmed against a real journal.
- There is no case where a derived value is presented as measured.
- The S2/S3/S5 contracts and the journal schema are unchanged.
- The legacy benchmark is unchanged.
- The S3 timestamp debt remains recorded in the documents.

## 24. Deferred / undecided items

Intentionally undecided. Nothing is fixed arbitrarily and the reason is recorded.

| Item | Status | Reason |
| --- | --- | --- |
| Regression verdict threshold | **DEFERRED** | `AGENTS.md` item 9 forbids declaring a difference outside the measurement error range an improvement. The `E-3B-BUG` precedent shows arbitrary numeric thresholds are forbidden (14-2) |
| Adding `git` / `distance` / `resourcePolicy` to the journal | **DEFERRED** | S3 schema territory. S6 does not change it. Three options in section 22 ① |
| Final choice of S6 output format (JSON vs CSV) | **DEFERRED** | Section 17-2 fixes only the "machine-readable plus human-readable, two layers" shape; the JSON/CSV choice follows once real consumers are confirmed at implementation |
| Automated suite execution | **DEFERRED** | Roadmap §28/§30 include it in S6 (section 1), but execution must use the S2 path, so a separate brief split is recommended |
| Statistical distribution function (p95 algorithm) | **DEFERRED** | Approximate versus exact gives different results on small populations. Decided after inspecting the real data distribution |
| `run_cancelled` analysis rule refinement | **DEFERRED** | 0 measured samples (6-6) |
| Fixing the S3 timestamp | **separate S3 follow-up** | Already recorded in the S5 verification section 8. S6 works around it with the 16-2 rules |

## 25. Confirmation that implementation does not start

This stage produces **a design document only.** The following is not done.

- S6 analysis CLI / engine implementation
- Writing a Python or PowerShell analysis script
- Journal schema change or bump
- S2 / S3 / S5 / GUI / benchmark execution code changes
- Performance tuning, GPU/CUDA changes
- Test implementation

Output of this stage:

```text
S6 design investigation (sections 6, 8, 11, 16 of this document)
+
S6 implementation brief (KO/EN)
+
roadmap / progress record of the S6 brief-prepared status
```
