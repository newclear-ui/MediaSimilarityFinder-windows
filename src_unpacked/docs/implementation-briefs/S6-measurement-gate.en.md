# S6 Measurement Gate — Controlled A/B Benchmark Measurement

Written: 2026-10-01
Status: **Design document. No product code changed. S6 is NOT CLOSED.**

This document defines *what must be measured, and under which identical conditions and
repetition procedure*, before S6 can enter its verdict layer. Existing measurement
contracts are reused; only the procedure and the entry/exit conditions are new.

---

## 1. Why this Gate exists

The S6 pipeline (`ingestion → normalization → grouping → aggregation → comparison
candidates → comparison metrics`) is complete, but the **entry conditions for the verdict
layer are not met.**

| Condition | Current state | How it was checked |
| --- | --- | --- |
| 2 or more Known-commit builds | **Not met** — Known 1, Legacy 30 | S6-5 measurement |
| Success case sample | **Not met** — every `images`-scope case is Skipped | S6-4 measurement |
| Controlled measurement environment | **Not met** — undefined | S2-PERF |
| Repeated runs | **Not met** — no repeats of one condition | S6 store measurement |
| Measurement variation / error range | **Partially met** — a figure already exists (§3) | 0.9.4.32 |

**Code cannot produce these conditions.** S6 only reads journals. Real measurement is
required, and without it no threshold is justified.

> **The verdict layer is not CLOSED.** This document defines the preconditions for
> *starting* it. It does not implement it.

---

## 2. Facts actually measured while writing this document

Every figure below was **measured directly**, not estimated.

### 2-1. Measurement environment

| Item | Measured |
| --- | --- |
| OS | Microsoft Windows 11 Pro, build 26300 |
| CPU | AMD Ryzen 7 5800X3D (8 core / 16 logical) |
| GPU | NVIDIA GeForce RTX 3080 Ti **present** |
| Power scheme | High performance (GUID `381b4222-f694-41f0-9685-ff5bb260df2e`) |

### 2-2. Candidate dataset

`C:\project\test_sample_img_vid`, confirmed with
`prepare_dataset.ps1 -FingerprintOnly` (read-only):

| Item | Measured |
| --- | --- |
| File count | **3347** |
| Total bytes | **102,475,315** |
| Directory count | 102 |
| Composition | `bulk` 240, `images` 707, `tree` 2400 |
| Extensions | `.bmp` 2700, `.jpg` 200, `.png` 200, `.webp` 200, `.gif` 20, `.ico` 12, `.tif` 4, `.tiff` 10, **`.md` 1** |

### 2-3. What the current benchmark store actually contains (important)

The 31 runs in `build-windows-cpu\Release\Benchmark\Console` are **not a real dataset.**
The case paths point at `C:\Users\newcl\AppData\Local\Temp\msf_s5_e2e`, and that fixture
holds **10 files**. It is the residue of the S5 end-to-end test.

So the store's `caseMedian ≈ 619–645 ms` and `run wall 0 ms` are **properties of a
10-file fixture** and cannot serve as performance evidence.

### 2-4. What the current CLI offers

```
MediaSimilarityFinder.exe --benchmark <folder> [--mode <list>]
                        [--suite <id>] [--log-dir <dir>] [--log <file>]
                        [--media images|videos|all]
```

- `--mode` default `auto,cpu,gpu-max`, execution order `AUTO → CPU → GPU-max`
- `runId` = `run-YYYYMMDD-HHMM-SS` (**one-second resolution**). A source comment states
  this was chosen precisely because a per-process counter "would collide with an earlier
  run in the same suite."
- `suiteId` defaults to the same stamp; `--suite` allows an explicit id

**The CLI has no `--resource` or `--cpu-percent`.** Both were rejected in the S1/S5
stages. So resource policy cannot be set from the CLI and is not recorded in the
journal (§8).

---

## 3. Measurement contracts that already exist (reuse, do not rewrite)

This development line already has measurement rules, and **no new baseline is invented.**

### 3-1. `AGENTS.md` item 9

- "**A difference inside measurement error is not an improvement.** Record the execution
  count (5 or more recommended) with median / min / max / range."
- Recommended status values: `BASELINE` / `PASS` / `NOT ACCEPTED` / `REJECTED` /
  `DEFERRED` / `LOW PRIORITY` / `INCONCLUSIVE` / `SUPERSEDED`
- **Values that were not measured are recorded as `N/A` or `Not measured`. Never filled in
  by estimation.**

### 3-2. Variation has already been measured (no re-measurement needed)

`0.9.4.32` build history and the worklog already record run-to-run variation.

| Target | Measured spread | Source |
| --- | --- | --- |
| Full execution (R384/R512) | **3.7 – 7.5 %** | 0.9.4.32 |
| Short probe execution | **19.4 – 45.9 %** | 0.9.4.32 |

**This is where the Gate starts.** Two facts are already known:

> With 5 repeats and run-to-run variation at 3.7–7.5 %, **any difference below 7.5 % sits
> inside measurement error** and cannot be declared an improvement or a regression. Short
> probes vary by 19–46 %, so adding repeats does not by itself make them usable — **the
> measurement unit (the run) must not be short.**

### 3-3. Measurement defects already found (carried forward)

Two defects of the same family have already been found on this development line, and the
Gate assumes them.

- **Reproducibility does not guarantee correctness.** A 5-times reproduction was
  sufficient, yet the result was a **reproduced artifact**.
- **The pair being judged must live in the same world.** A baseline score had the previous
  file's candidate buffers mixed in. Reproducibility was adequate and the result was still
  an artifact.
- **Read a suspiciously tidy result as a defect signal.** A monotone trend (2.63 → 9.19)
  was a bug signal.

> Therefore a Gate success condition of "reproducibility secured" would **pass both of
> those defects unchanged.** §11 keeps a separate pair-identity check.

---

## 4. Definition of Build A / Build B

The two builds are **not arbitrary builds with different commit hashes.** The difference
between them must be stated.

| Build | Definition |
| --- | --- |
| **Build A** | The measurement reference. Which code state it is, fixed by commit |
| **Build B** | The comparison target. **What was changed relative to A**, stated in one sentence |

If Build B's change cannot affect execution (a documentation-only change, for example), an
identical result is **expected**, not a defect. The comparison is still valid, and it means
"something other than the code was measured." Build B's change scope must therefore always
be recorded.

> Names stay neutral as `Build A` / `Build B`. The words `baseline` and `candidate` imply
> superiority, so this Gate does not use them. Assigning roles is the job of a later stage
> where a user picks explicitly.

### 4-1. Known provenance requirement (mandatory, no workaround)

Each build must satisfy:

1. Git was usable at configure time, and the result landed in the generated header.
2. `MSF_BUILD_GIT` is **never back-filled** by invoking git during a run. The value is
   **fixed at build time**.
3. The journal's `run_started.gitCommit` **matches** the binary's generated value.

Currently measured (identical for both builds):

```
MSF_BUILD_VERSION "0.9.4.43"
MSF_BUILD_GIT     "83cced3"
```

**If both builds carry the same commit, an A/B comparison is impossible.** Build B must be
built from a different commit, and that commit must exist on `origin/main` so it can be
restored (AGENTS.md backup rule: the source zip requires `HEAD == origin/main`).

Verification:

```
MediaSimilarityFinder.exe --version      # confirms MSF_BUILD_VERSION
# read MSF_BUILD_GIT from generated/msf_build_version.h and compare with the journal
```

---

## 5. Dataset conditions

The same dataset is used on both sides of A/B. **The dataset is not changed during
measurement.**

| Item | Measured now |
| --- | --- |
| root | `C:\project\test_sample_img_vid` |
| file count | 3347 |
| total bytes | 102,475,315 |
| `datasetFingerprint` | **recorded at Gate execution via the product's own report** (§5-1) |

### 5-1. How the fingerprint is recorded

`prepare_dataset.ps1 -FingerprintOnly` reports the file count and total bytes, and says to
read the fingerprint through `msf_dataset_report`.

**Never run it without `-FingerprintOnly`.** That regenerates the dataset and deleted
fixtures once already (AGENTS.md item 10). The Gate only **reads** the dataset.

If the fingerprint differs, S6 excludes that cohort from same-condition comparison. The
fingerprint is therefore mandatory in the measurement record, and is not recomputed
afterwards to compare.

### 5-2. Finding: one `.md` file

There is **one `.md` file** under `format`. The `prepare_dataset.ps1` comment states the
root "must contain media and nothing else."

Before Gate execution, confirm whether that file belongs in the dataset. If it is not
media, removing it is a **dataset change**, the fingerprint changes, and **both sides must
be re-measured under the new fingerprint.** Changing the dataset mid-measurement is
forbidden.

---

## 6. Scope selection (fixed by investigation)

The current dataset has **zero video files.** Measured extensions: `.bmp` `.jpg` `.png`
`.webp` `.gif` `.ico` `.tif` `.tiff` `.md` — `.mp4` / `.mkv` / `.avi` / `.mov` / `.webm` /
`.m4v` **0 occurrences**, and there is no `videos` directory at all.

| Scope | In this Gate |
| --- | --- |
| `images` | **Usable** (707 + 2400 = 3107 image files; whether `bulk`/`tree` are included needs confirming) |
| `videos` | **Impossible** — 0 files |
| `all` | **Usable, but it points at the same file set as `images` alone** |

> **`all` remains an independent scope** and is never decomposed (§7-1). However, this
> dataset has no video, so `all` and `images` refer to **the same file set.** That is
> recorded here, and results from the two scopes are not treated as a comparison of two
> different scopes.

**The Gate's primary scope is fixed to `images`.** The current store's `images`-scope cases
are all Skipped because of the **10-file e2e fixture**, not because of `images` itself.
Measuring the real 3347-file dataset should yield Success samples; if it does not, the
cause is recorded as a **measurement procedure** problem, not a dataset problem.

### 6-1. Whether `bulk` / `tree` are included

Confirm **once**, before Gate execution, which directory composition `--media images`
selects. `bulk` (240) and `tree` (2400) are `.bmp`-centric, and their real image analysis
cost may differ. The Gate records the confirmed composition and neither narrows nor widens
it arbitrarily.

---

## 7. Mode selection (fixed by investigation)

### 7-1. Measured today

| Mode | observed | eligible |
| --- | --- | --- |
| `AUTO/CPU` | 260 | **0** |
| `CPU/CPU` | 110 | 110 |
| `CUDA/CPU` | 60 | **0** |

`CUDA/CPU` with `SKIPPED` and `"mode unavailable in this environment"` is a **fallback**.

### 7-2. Important finding: the GPU is not missing

This machine **has an RTX 3080 Ti** (§2-1). CUDA was still recorded as unavailable.

The cause is the **CPU build**. The current store sits under `build-windows-cpu\Release`,
and that binary does not link CUDA. The 60 `CUDA → CPU / SKIPPED` records are therefore a
result of **build configuration, not missing hardware.**

> Earlier stages recorded these as "a fallback on a machine without GPU support." With the
> facts now measured, the **hardware exists and the binary lacked the capability**, so the
> interpretation is corrected. The observed values (60 records, SKIPPED) stay exactly as
> they are; only the reading of them changes.

### 7-3. Mode decision for this Gate

**Primary = `cpu` alone.** `--mode cpu`.

- It is the only mode that currently has successful samples.
- `auto` adds per-case overhead.
- `gpu-max` is **unavailable in a CPU build**, so it is not a measurement target.

Using the default `--mode auto,cpu,gpu-max` would pile up 260 more Skipped records and mix
unavailable samples with real ones in the same cohort. **The Gate states `--mode cpu`
explicitly.**

### 7-4. GPU measurement is a separate Gate

If a GPU comparison is wanted, the following must be satisfied **separately.** It is out of
scope here.

- Measure with the `build-windows-gpu` binary. Confirming that CUDA is actually linked is a
  **pre-register observation** and is not assumed.
- The GPU device must be unoccupied.
- Driver and VRAM state are recorded.

> Both builds print `--version` as `(CUDA/CPU)`, which is itself a **pre-register
> observation item.** Capability is not judged from that string alone.

---

## 8. Resource Policy

There is no `--resource` / `--cpu-percent` on the CLI, and no `resourcePolicy` in the
journal.

Decision:

1. **The Gate runs on the same machine, same OS session, same power scheme.** That is the
   maximum control currently obtainable.
2. The resource policy is **stated in the document but not recorded as a measurement.**
   Do not assume S6 analytics can later restore it as real provenance.
3. Separate it as a future S3 schema extension candidate. **This Gate does not change the
   journal schema.**

---

## 9. Repetition count and procedure

### 9-1. Count

The `AGENTS.md` item 9 recommendation of **5 or more** is adopted as the Gate's minimum
repetition count.

```
Build A × 5, Build B × 5   = 10 executions in total
```

**If fewer than 5 must be proposed**, state the reason and do **not** declare statistical
sufficiency. That result is recorded as `INSUFFICIENT` (§13).

### 9-2. Independence of repeats — the `runId` collision rule

`runId` is a **one-second-resolution** stamp (`run-YYYYMMDD-HHMM-SS`). Therefore:

> **Each repeat starts in its own suite, with start times at least 2 seconds apart.**

Actual rules:

| Item | Rule |
| --- | --- |
| suite | Use an **explicit** `--suite <label>` per repeat (e.g. `gateA-r1`, `gateB-r1`) |
| journal | Each suite creates its own `runs.jsonl` |
| `RunReference` | Keep using **`(sourceJournalPath, runId)`** |
| Collision check | Confirm `consoleSuiteIdIsSafe` accepts the value before running |

Why an explicit suite: the automatic stamp has one-second resolution, so simultaneous
starts collide. An explicit value is under the operator's control, which makes the
procedure reproducible.

### 9-3. What to confirm and record per repeat

| Item | Where it is recorded |
| --- | --- |
| suite id | `--suite` value (journal `suiteId`) |
| run id | journal `runId` (automatic stamp) |
| gitCommit | journal `run_started.gitCommit` |
| datasetFingerprint | journal `run_started` |
| mediaScope | journal `run_started` |
| start / end time | journal `startedAt` / `completedAt` |
| terminal state | journal terminal record |

---

## 10. A/B execution order

### 10-1. Decision: crossed order `A B B A B B A ...`

```text
Build A r1
Build B r1
Build B r2
Build A r2
Build A r3
Build B r3
Build B r4
Build A r4
Build A r5
Build B r5
```

Reasons:

- Linear drift (machine state changing over time) cancels out.
- Each build's executions alternate **before and after** along the time axis, so drift
  cannot pile up on one side.
- A simple `A B A B A B` leaves more drift influence. `A B B A` has the side effect that
  two adjacent same-build executions share cache state, but at 3347 files the influence of
  the previous run on the OS cache is small, so **drift cancellation takes priority.**

### 10-2. Order provenance

The order of each execution must be **reconstructable from the suite id.** Reflect the
order above directly in the `--suite` labels (`gateA-r1`, `gateB-r1`, `gateB-r2`,
`gateA-r2`, …).

> Changing the order does not change the journal schema. The order is recorded only through
> the suite id convention.

---

## 11. Cache policy (warm / cold)

### 11-1. Decision: method A — do not control it, apply the identical procedure to both sides

Complete control of the OS filesystem cache is **not claimed** in this environment.

S2-PERF already accepted the OS filesystem cache and process isolation as **uncontrolled**.
The Gate does not overturn that.

| State | Policy |
| --- | --- |
| **warm** (standard) | OS cache warmed after the first execution. Identical for both A and B |
| **cold** | Attempted but not mandatory. An OS cache flush affects the whole system, so it is **not performed** |

The Gate adopts **warm as the standard condition**, and separates the first execution as a
warming run so it can be excluded or labelled in the record.

### 11-2. What is actually controllable

| Factor | Controllable | Method |
| --- | --- | --- |
| Power scheme | **Yes** | High performance (already applied) |
| Background workload | **Partly** | No other build or test runs during measurement |
| Process isolation | **Partly** | A new process per execution (the CLI already does this) |
| Application internal cache | **Yes** | Runtime index is separated per suite (§3-2) |
| **OS filesystem cache** | **No** | Fixed as the warm condition, limitation stated |
| Scheduler / CPU frequency | **No** | Recorded as uncontrolled |
| Turbo / thermal state | **No** | Recorded as uncontrolled |

> Process isolation and the OS cache are **not presented as controlled.** The uncontrolled
> marking from S2-PERF is kept.

---

## 12. Metric selection

### 12-1. Primary = `ModeElapsed`

| Candidate | Verdict |
| --- | --- |
| **`ModeElapsed`** | **Primary.** A single mode execution's time. It expresses the execution unit most directly and is least affected by the short-probe spread (19–46 %). With `--mode cpu` alone this cohort is a pure CPU measurement |
| `CaseElapsed` | **Secondary cross-check.** By S2 definition it is the case's mode sum, so it carries nearly the same information. The only difference is that it includes file preparation cost |
| `RunWallDuration` | **Unsuitable as primary.** Resolution is **one second**, which limits fine-grained regression judgement. Fixed as a contract in S6-2, so it is used for **cross-check only** |

### 12-2. Why `RunWallDuration` is not primary

S3 timestamps use `localtime_s` with a literal `Z`, so they record whole seconds only. A
run wall duration is therefore **always a multiple of 1000 ms**, and a run shorter than one
second is exactly `0`.

The measured store shows the consequence: the 21-run cohort's wall median is `0.0`.
**A 0 ms means "finished within one second," which is a real measurement, not a missing
value** (S6-2 contract).

Run wall duration therefore yields only the coarsened information "did it finish within one
second." Using it as the basis for millisecond regression judgement loses resolution
immediately.

> `durationResolution = OneSecond` always travels with the result as metadata. Its median
> and mean are never placed on the same axis as another metric.

### 12-3. What to obtain from the measurement

Obtain the following from the repeats. **This Gate adds no new statistic to the code.**
S6-4 already computes `count / min / max / mean / median / p95`, so those are consumed.

Additionally record, **as documentation only**:

| Value | Purpose |
| --- | --- |
| Run-to-run spread (max − min) | Direct expression of the spread |
| Coefficient of variation | Relative spread |
| Whether the `AGENTS.md` item 9 recommended count was met | 5 repeats reached or not |

---

## 13. Measurement variation versus measurement error

The two are not the same thing.

| Term | Meaning | In this Gate |
| --- | --- | --- |
| **Execution variation** | Difference between repeated runs of the same build and condition | **Measured** in §14 |
| **Measurement error** | Uncertainty of the measuring device itself: timer resolution, system scheduling, cache | Recorded separately, **not quantified** |

There is already evidence for keeping them apart.

- The short-probe spread (19.4–45.9 %) is not a timer-resolution problem. It is the
  execution being short so that relative variation becomes large. Lengthening the run
  reduces it.
- The full-execution figure of 3.7–7.5 % is markedly more stable.

> **Do not call it "measurement error."** What is observable is run-to-run variation; this
> procedure does not quantify the absolute error of the measuring device. Values that are
> not quantified are left as `Not measured`.

### 13-1. What the Gate actually produces

```text
ModeElapsed run-to-run spread (5 repeats)
-> this is the sole basis for later threshold design
-> the threshold is decided afterwards, in a separate decision stage
```

---

## 14. Gate success condition (exit condition)

**Discovering a regression is not the success condition.** The success condition is that
**measured data satisfying the following exists.**

```
[ ] Build A   - Known gitCommit, run_started.gitCommit == MSF_BUILD_GIT
[ ] Build B   - Known gitCommit, a different commit from A, present on origin/main
[ ] identical datasetFingerprint, dataset unchanged between A and B
[ ] identical mediaScope (primary: images)
[ ] identical mode semantics (--mode cpu)
[ ] real SUCCESS case samples present (eligible > 0)
[ ] 5 repeats per build
[ ] ModeElapsed run-to-run spread computed (min/max/mean/median/p95)
[ ] execution order recorded (crossed A B B A)
[ ] environment elements recorded, separating controllable from uncontrollable
[ ] both sides pass the S6 pipeline and yield comparison candidate > 0
```

When all of the above hold, mark **S6 verdict layer entry as possible.** The threshold is
still undefined at that point, and a separate decision stage remains.

### 14-1. How it is verified (with S6's own tools)

The Gate changes no product code and is verified with **S6's read-only diagnostics.**

```
build-windows-cpu\Release\msf_benchmark_data_mining_test.exe               <storageRoot>
build-windows-cpu\Release\msf_benchmark_data_aggregation_test.exe         <storageRoot>
build-windows-cpu\Release\msf_benchmark_data_comparison_test.exe           <storageRoot>
build-windows-cpu\Release\msf_benchmark_data_comparison_metrics_test.exe  <storageRoot>
```

Entry criteria, all mechanically checkable:

| Check | Criterion |
| --- | --- |
| Known build cohorts | **2 or more** |
| Provenance | `known >= 2`, with no dependence on `unknown` / `legacy` |
| Candidates | `eligibleCandidates > 0` |
| Comparison metrics | `absolute deltas available > 0` |
| Failures / exclusions | `missing-provenance = 0`, `no-comparable-samples = 0` |

---

## 15. Invalid measurement conditions

The following are **not included automatically** in a comparison. Each row states the basis
for its judgement.

| Condition | Basis |
| --- | --- |
| Dataset fingerprint mismatch | Compare journal `run_started.datasetFingerprint` |
| Scope mismatch | Compare journal `run_started.mediaScope` |
| Mode mismatch | Compare `mode_result.requestedMode` / `effectiveMode` |
| Build provenance missing | `run_started.gitCommit` absent, or `Legacy` / `Unknown` |
| All samples skipped | Mode cohort `eligible = 0` |
| All samples failed | Mode cohort `eligible = 0` |
| Cancelled / incomplete run | No terminal record, or `run_cancelled` |
| Dataset changed | Fingerprint changed (editing the dataset mid-measurement is forbidden) |
| Environment interrupted | **See the criteria below** |

### 15-1. Objective criteria for an "obvious" environment interruption

The word `obvious` is not used as a subjective judgement. A run is excluded, and the fact
recorded, if any of the following applies.

- Another build or test started running during the execution (confirmed from work history)
- The power scheme changed
- Execution time exceeded **3× the median** of the other runs under the same condition
- `run_wall` is `Not measured` (no `completedAt`)
- The OS was updated or rebooted

> Even when none of these apply, a **suspected** run is not excluded but included and
> labelled. Exclusion happens only with a basis.

---

## 16. Execution procedure (Gate run sheet)

Run in order for each repeat.

```
1. Before running: confirm no other build or test is active
2. After running: open the journal read-only and confirm
   - runId / suiteId / gitCommit / fingerprint / mediaScope
   - eligible case samples > 0
   - if fewer than 2 samples, record the reason and continue to the next repeat
3. After all repeats: run the 4 S6 diagnostics (read-only)
4. Compute the ModeElapsed run-to-run spread
5. Record the real numbers in the document
```

### 16-1. Command form

```
build-windows-cpu\Release\MediaSimilarityFinder.exe ^
    --benchmark C:\project\test_sample_img_vid ^
    --media images ^
    --mode cpu ^
    --suite <gateA-r1 | gateB-r1 | ...> ^
    --log-dir <Gate-specific log directory>
```

> `--log-dir` points the Gate at a dedicated store so it does not mix with the existing
> store. This Gate's output is measured evidence and is kept in a separate location.

---

## 17. Boundary — not changed in this stage

- S2 benchmark execution
- S3 journal schema / recovery / summary
- S6 ingestion / normalization / grouping / aggregation / comparison / comparison metrics
- Scanner / GUI / Console renderer / CLI
- legacy benchmark

If a change turns out to be necessary, split it into a **separate implementation task.**
This stage is documentation only and there is no product code change.

### 17-1. Not implemented in this stage

- Benchmark execution automation
- A new measurement engine
- Threshold code / regression code / anomaly code
- Report formatter
- GUI / CLI
- Journal schema change

---

## 18. Existing limitations (still in force)

| Limitation | Status |
| --- | --- |
| `distance` unavailable | Kept — not in the journal |
| `resourcePolicy` unavailable | Kept — absent from both CLI and journal (§8) |
| `gpuBackend` unavailable | Kept — not in the journal |
| Controlled environment undefined | **Partially defined by this Gate**, still not fully controlled (§11) |
| Repeated runs insufficient | **Resolved by this Gate** |
| No real `run_cancelled` sample | Kept |
| Run wall one-second resolution | Kept — excluded from the primary metric (§12) |
| OS filesystem cache / process isolation uncontrolled | Kept (§11) |

---

## 19. Document history

| Date | Content |
| --- | --- |
| 2026-10-01 | First version. Environment, dataset, CLI and store figures measured. The existing 0.9.4.32 variation reused. The `CUDA→CPU SKIPPED` interpretation corrected to "build configuration." Zero video files confirmed, so the `videos` scope is excluded. |
