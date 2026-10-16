# Node Status / Gate Matrix — 0.9.4 Development Line Integrated Status Board

Baseline: `0.9.4.113` (independent-audit items B1-B3, B7 — full-content badge confirmation + analyze-mode round trip + no-FFmpeg SKIP) - CPU CTest 122/122 - GPU CTest 123/123
Last updated: 2026-10-16

## Role of this document

**This document is an index, not the source of truth.** It collects states and
verdicts on one page and links out to the origin document for figures and contracts.

| Layer | Origin document | Role |
|---|---|---|
| Direction, dependencies | `development-roadmap.{ko,en}.md` | Node definitions, prerequisites, boundaries |
| Actual current position, blockers | `development-progress.{ko,en}.md` | Execution state, recovery history |
| Execution contract | `docs/implementation-briefs/<Node>-*.{ko,en}.md` | Design/implementation contract (active node) |
| Technical structure | `docs/architecture/*.md` | Architecture |
| Change evidence | `docs/build-history/<version>.{ko,en}.md` | Per-version changes and measurements |
| Reasoning lineage | `docs/worklog/0.9.4.{ko,en}.md` | Why something was adopted or rejected |

The same figure or verdict is not copied into several documents. If a table entry
below is surprising, **follow the link to its origin.**

## Reading order (at OpenCode / ChatGPT agent session start)

1. **This document** — the overall Node/Gate entry point
2. **`development-progress.{ko,en}.md`** — active execution queue, current position, blockers, and completed major milestones
3. `development-roadmap.{ko,en}.md` — direction and prerequisites
4. The active node's `docs/implementation-briefs/<Node>-*.md`
5. The required `docs/architecture/*.md`
6. Relevant `docs/build-history/` + `docs/worklog/`
7. Source / test

> **`src_unpacked/AGENTS.md` working rule**: read steps 1-4 before changing source,
> and do not start a node the current gate does not permit.
> In particular, the Active Build Queue in `development-progress` determines the current execution priority.
> **Progress synchronization rule**: whenever Node/Build implementation, verification, verdict, gate, or next work changes, update `development-progress.ko.md` / `.en.md` immediately. This document must remain the latest entry point for current execution state, not the store of detailed completed evidence.

## Verdict values (no progress percentages)

This project has genuinely been 80 % implemented with a correctness failure, and
100 % coded with acceptance incomplete. Therefore state values and gates are
recorded instead of percentages.

| Value | Meaning |
|---|---|
| `NOT STARTED` | not started |
| `DESIGNED` | design fixed, implementation not started |
| `IN PROGRESS` | implementation/verification ongoing |
| `PASS` | verification passed |
| `CONDITIONAL` | conditionally adopted; production adoption impossible while a precondition is unmet |
| `DEFERRED` | on hold; a separate revisit condition exists |
| `NOT ACCEPTED` | did not succeed under current conditions; not a permanent rejection |
| `CLOSED` | finished; may proceed to the next node |
| `COMPLETE` | finished; regression risk remains |

---

## Main track — Main Development Nodes

Dependency chain: `A -> B -> C -> D -> I -> E -> F -> G -> H`

| Node | Purpose | Design | Prerequisite | Implementation | Build/Verify | Current verdict | Blocker cause | Resolution/Conclusion | Next gate | Detail |
|---|---|---|---|---|---|---|---|---|---|---|
| **A** | Foundation / terminology / instrumentation | complete | — | complete | PASS | **CLOSED** | — | GPU abstraction `GpuBackendKind{Auto,Cuda,Cpu}`, `MSF_ENABLE_GPU` + `MSF_GPU_BACKEND`, split build naming, 6 `MeasureState` values, `decodedFrames`/`sampledFrames` split, no recording 0 for unmeasured | B | `implementation-briefs/A-foundation-terminology-instrumentation.*` (reference contract), `build-history/0.9.4.0.*`, `architecture/resource-scheduling.*`, `architecture/gpu-backend-roadmap.*` |
| **B** | Adaptive Scheduler | complete | A | complete | PASS | **CLOSED** | fixed 50:50 split | Dynamic split from capability + calibration + live load + throughput + queue + transfer cost. Manual GPU percentage control removed | C | `implementation-briefs/B-adaptive-scheduler.*` |
| **C** | Calibration / INI Performance Profile | complete | B | complete | PASS | **CLOSED** | calibration lifecycle boundary | C1 Profile Foundation -> C2 Initial -> C3 Opportunistic -> C4 Gate. Live runtime state always wins | D | `implementation-briefs/C-calibration-profile.*` |
| **D** | Pipeline / Queue Optimization | complete | C | complete | PASS | **CLOSED** | the real bottleneck share was only 0.044 % | bounded walker queue, transfer stall removed. D3+D4 addressable ceiling 0.044 % -> further optimization held | I | `implementation-briefs/D-pipeline-queue.*` |
| **I** | Analyze / Matching Performance | complete | D | complete | PASS | **COMPLETE** | 98.62 % of total wall time is analyze | I-2 production path complete. decode is 94.90 % of verify, and decode itself is 1/20 (open+factory 89.54 %) | E | `implementation-briefs/I-decode-once-resize-twice.*`, `I-shared-wic-source*.{ko,en}` |
| **E** | Adaptive Video Decode Planner | complete | I | complete | **correctness failure** | **CLOSED** | sparse exactness | The E-2A/E-2B "exact" figures were self-referential (both shared the same `av_seek_frame` + `avcodec_flush_buffers` reference-state loss). The first measurement including the production from-zero sweep found one genuine divergence on a 4K H.264 file, and sparse was **17.38 % slower end to end** -> **`ExactnessPolicy::RefuseAll` default**, production sequential for all files | F | `build-history/0.9.4.42.*`, `implementation-briefs/E-*.{ko,en}` |
| **F** | Hardware Video Decode Backend | pre-register | E | NVDEC investigation | **exactness FAIL** | **CONDITIONAL** / **PRODUCTION ADOPTION = NO** | NVDEC mismatch. 13 of 14 dataset files are file-start IDR yet **1360x0808 mismatched 20/20** and **still mismatched 20/20 when restarted from a confirmed IDR**. 1080x1920 also 1/20. NVDEC is **2.1x slower per frame** (CPU 4.438 s vs NVDEC 9.397 s, 870f) | `exactnessVerified` introduced as a necessary condition for `Safe`. `Unsafe`/`Unknown` collapse onto CPU fallback. **F-2 production integration forbidden.** The 1360x808 root cause is `INCONCLUSIVE` | G / H | `implementation-briefs/F-random-access-safety.*`, `F-hardware-video-decode-backend.*`, `build-history/0.9.4.43.*` |
| **G** | Additional GPU Backends | pre-register | F | not started | — | **DEFERRED** | F unresolved | Intel Level Zero / AMD HIP/ROCm / Vulkan are independent backend candidates. **The architecture is not pinned to NVIDIA** | after F resolves | `development-roadmap.en.md` Node G |
| **H** | Regression / Stability / Performance Validation | — | G | not started | — | **NOT STARTED** | prerequisite F unresolved | — | — | `development-roadmap.en.md` Node H |

> **Standing rule for Node G/H**: adding another hardware decode backend requires a
> codec / profile / pixel-format / bit-depth / capability map, timeline seek and
> frame mapping, and decode error fallback. **These are entry conditions, not an
> incremental feature path, and no backend currently satisfies them because of the
> NVDEC problem.**

---

## S track — Validation / Benchmark Track

This is a **parallel track** to the A-H main line. It does not depend on the main
gates and owns only the verification/measurement layer. The integrated track contract is
`docs/implementation-briefs/S-validation-benchmark-track.{ko,en}.md`; the design
authority is `docs/architecture/benchmark-telemetry-roadmap.{ko,en}.md`.

Dependency chain: `S0 -> S1 -> S2 -> S3 -> S4 -> S5 -> S6 -> S7 -> S8`

| Node | Purpose | Prerequisite | Implementation | Build/Verify | Current verdict | Blocker cause | Resolution/Conclusion | Next gate | Detail |
|---|---|---|---|---|---|---|---|---|---|
| **S0** | Design fix / pre-register | — | complete | PASS | **CLOSED** | — | Run = one measurement of one mode; Suite = a set of modes over the same dataset/media scope. Search Index is separated from benchmark index/cache | S1 | `architecture/benchmark-telemetry-roadmap.*` |
| **S1** | Console entry foundation | S0 | complete | PASS | **CLOSED** | — | CLI parsing layer. No separate engine from the production search engine | S2 | `development-progress.*` |
| **S2** | Run / Suite benchmark core | S1 | complete | PASS | **CLOSED** | — | ordering / status aggregation / cancellation / result preservation. Reuses the production scan path through an injected `BenchmarkExecutor` | S3 | `architecture/benchmark-telemetry-roadmap.*` |
| **S3** | Benchmark storage isolation | S2 | complete | PASS | **CLOSED** | — | `runs.jsonl` append-only journal. `summary.json` is derived and never authoritative. `kBenchmarkSchemaVersion` 9 | S4 | `implementation-briefs/S4-*`, `S5-*` |
| **S4** | GUI Detailed Logging | S3 | implementation complete | **functional acceptance PASS (0.9.4.112)** | **IN PROGRESS** | final CLOSED not declared | S4 semantic reset applied. `TelemetryPurpose{UserDiagnostic,Benchmark}` separates GUI from CLI. GUI string `[Benchmark]` -> `[Detailed Logs]`. Save/exit acceptance automated PASS (.112) — on-screen rendering stays recorded as "not done" (headless). **Not raised to CLOSED** (tied to product acceptance) | S5 | `implementation-briefs/S4-gui-diagnostic-logging.*`, `architecture/gui-diagnostic-logging.*` |
| **S5** | Console benchmark execution | S4 contract | infra complete | **product benchmark not executed** | **NOT CLOSED** | depends on product acceptance | `runConsoleBenchmark()` reuses S3 `BenchmarkSession` + S2 `BenchmarkRunner`. Uses `QCoreApplication` so no platform plugin is required. **No measured benchmark run** | S6 | `implementation-briefs/S5-console-benchmark-execution.*` |
| **S6** | Data-mining automation / measurement gate | S5 | gate prepared | **measurement not executed** | **NOT STARTED** | needs a real dataset | The threshold is **still undefined**. The reported gate conditions are satisfied but the threshold is not set | S7 | `implementation-briefs/S6-data-mining-automation.*`, `S6-measurement-gate.*` |
| **S7** | Help / usability | S6 | not started | — | **NOT STARTED** | — | — | S8 | — |
| **S8** | Full verification / release gate | S7 | not started | — | **NOT STARTED** | — | — | — | — |

> **Benchmark and Telemetry are not synonyms.** GUI `[Detailed Logs]` is
> `TelemetryRecorder/UserDiagnostic`; CLI `--benchmark` is
> `TelemetryRecorder/Benchmark`. Neither is auto-ingested by the other.

---

## Current focus

```text
baseline          0.9.4.113 (CPU CTest 122/122, GPU CTest 123/123 — results recorded in Work Log)
active status     Process split P1-P4 complete + G4/G6 verified (.111) + S4 save/exit acceptance done (.112) + independent-audit items B1-B3/B7 applied (.113); field GUI acceptance for .105/.106 and final Search/Index/Comparison acceptance remain open
next              .105/.106 field acceptance -> real-dataset Search/Index/Comparison acceptance -> XMP coverage -> S5 benchmark -> S6 gate
G4/G6 verified    G4: stress PASS — a 3 s consumer stall stays lossless (0 gaps, 600 delivered), no deadlock, bounded RSS (20/20/21 MB). No defect confirmed (design review: GUI->Backend bounded by pending cap; Backend->GUI backlog = output size).
                  G6: two real defects fixed — fixed 3 s exit delay (3049->6 ms; `waitForFinished` handle wait with re-entry blocked) + Job Object Win32 return checks/logs. Parent-crash E2E: the job reaps the wedged backend; the `--no-job` control survives, proving the job is load-bearing.
S4 save/exit      Real defect fixed — the partial/zero-write false-success is removed (`saveDetailedLogTo`, exact byte count). Real button -> file picker -> write -> status E2E PASS (`save=ok`, 3 repeats). On-screen rendering stays recorded as "not done" (headless). Final CLOSED remains tied to product acceptance (§10).
Audit B1-B3/B7    B1 (FAIL reproduced): the badge compared size + the 64 KiB quick hash only, so a differing tail was identical=true. Fix: the quick hash is a candidate gate only; passing pairs require full-content SHA-256 equality (per-file memo, >512 MiB loses the badge; quick-load uses the same contract). A data race found during verification was fixed (16 concurrent shard inserts -> `std::mutex`). Measured cost: ~1.5 GiB extra reads per full scan (user index). B2: settings -> received telemetry `"mode":"A"/"B"` round-trip tests. B3: no-FFmpeg lane SKIP (exit 0) verified. B7: S4 row/chain wording reconciled.
current verdict   .105 file-operation/preview/index logs and .106 badge/real-process IPC await field evidence.
                  Search/Index/Comparison = NOT ACCEPTED; S4 functional acceptance automated PASS (final CLOSED pending); XMP = CONDITIONAL;
                  color_thumb R1 done (.113), production correction decision pending; S5/S6 await prerequisites.
measurement       Fast(A) user-dataset run=1 is recorded in Work Log/DBV §7.3 on 2026-10-15, but confounders limit comparison.
                  Auto(AB) measurement completed 2026-10-16 (64 changed, 1 verify) — final-pass semantics/repetitions are follow-ups. Verify build/runtime conditions for CUDA-unavailable records.
remaining risk     thumbCatchUpVisible() scans the full list each tick; lower-priority performance risk, not a correctness blocker.
                  Exact caller in the original .72 crash dump remains unconfirmed due to missing PDB; retain new crash evidence via runbook.
```

**Why S4 acceptance is blocked**: this environment is headless, and the actual
on-screen rendering of the result dialog was not visually verified. That is recorded
as *not done* and is not claimed as *passed*.

## Held / forbidden / revisit conditions

```text
sparse seek production resume         DEFERRED - no codec with production-parity proof
NVDEC production adoption             PRODUCTION ADOPTION = NO - F-1 CONDITIONAL
F-2 production integration            forbidden - absence of an IDR-start fixture confirmed
1360x808 mismatch root cause          INCONCLUSIVE - revisit with a video cohort
Intel/AMD/Vulkan backend              entry conditions not met
color_thumb production correction     NOT PERFORMED - R1 fixture/skip done (.113); production correction decision/approval pending
XMP production acceptance             CONDITIONAL - fixtures PASS, coverage/regression absent
vcpkg migration                       do not perform (keep project-local)
```

## Live candidates with a non-passing state

This table records **verdict states only**. Figures, conditions, and revisit timing
stay in the origin documents.

| Candidate | State | Origin |
|---|---|---|
| `ExactnessPolicy::RefuseAll` (E) | adopted — production sequential for all files | `build-history/0.9.4.42.*` |
| sparse seek | `NOT ACCEPTED` — revisit conditions recorded | `build-history/0.9.4.42.*` |
| `exactnessVerified` required condition (F-1) | `CONDITIONAL` | `implementation-briefs/F-random-access-safety.*` |
| XMP Orientation Fallback | `CONDITIONAL` | `build-history/0.9.4.45.*`, `implementation-briefs/I-xmp-orientation-fallback.*` |
| product acceptance (Search/Index/Comparison) | `NOT ACCEPTED` -> **defects fixed (0.9.4.46)** | `build-history/0.9.4.46.*`, the acceptance audit entry in `worklog/0.9.4.*` |
| GUI Stop unresponsiveness | **fixed (0.9.4.48, engine level)** | `build-history/0.9.4.48.*`. The actual GUI button click is NOT_VALIDATED |
| `color_thumb` no-FFmpeg classification | R1 done (0.9.4.113) — production correction decision pending | `implementation-briefs/I-color-thumb-no-ffmpeg-classification.*`, `build-history/0.9.4.113.*` |
| Register nvcc warnings in the release gate | undecided | `worklog/0.9.4.*` |
| Dedicated console-output regression test | not added | `worklog/0.9.4.*` |

## Version invariance check

`kEngineVersion 1.5.1` - `kDatabaseVersion 1.0.6` - `kBenchmarkSchemaVersion 9` -
`kCacheFormatVersion 9`. 0.9.4.82 bumped `kEngineVersion` 1.5.0 -> 1.5.1 because EXIF-rotated image fingerprints change by up to +/-1 LSB, so stored pairs revalidate. DB/schema/cache unchanged.
