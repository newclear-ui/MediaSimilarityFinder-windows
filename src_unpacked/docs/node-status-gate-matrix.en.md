# Node Status / Gate Matrix — 0.9.4 Development Line Integrated Status Board

Baseline: `0.9.4.157` (thumb fix + AV1) - CPU CTest 131/131 - GPU CTest 132/132
Last updated: 2026-10-25

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
| **F** | Hardware Video Decode Backend | pre-register | E | NVDEC investigation | **exactness FAIL** | **CONDITIONAL** / **PRODUCTION ADOPTION = NO** | NVDEC mismatch. 13 of 14 dataset files are file-start IDR yet **1360x0808 mismatched 20/20** and **still mismatched 20/20 when restarted from a confirmed IDR**. 1080x1920 also 1/20. NVDEC is **2.1x slower per frame** (CPU 4.438 s vs NVDEC 9.397 s, 870f) | `exactnessVerified` introduced as a necessary condition for `Safe`. `Unsafe`/`Unknown` collapse onto CPU fallback. **F-2 production integration forbidden.** The 1360x808 root cause is `INCONCLUSIVE` | post-1.0 HW phase (after Vulkan) | `implementation-briefs/F-random-access-safety.*`, `F-hardware-video-decode-backend.*`, `build-history/0.9.4.43.*` |
| **G** | Additional GPU Backends | pre-register | 1.0 line | not started | — | **DEFERRED** | deferred to post-1.0 (Vulkan roadmap after CPU+CUDA stabilization; HW/libraries after Vulkan support) | Intel Level Zero / AMD HIP/ROCm / Vulkan are independent backend candidates. **The architecture is not pinned to NVIDIA** | after the 1.0 line opens | `development-roadmap.en.md` Node G |
| **H** | Regression / Stability / Performance Validation | — | G | not started | — | **NOT STARTED** | waiting for the 1.0 line (after G) | — | — | `development-roadmap.en.md` Node H |

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
| **S4** | GUI Detailed Logging | S3 | implementation complete | **functional acceptance PASS (0.9.4.112) + field rendering seen + product acceptance ACCEPTED** | **CLOSED** | — (linkage met, CLOSED declared in 2026-10-18 Work Log) | S4 semantic reset applied. `TelemetryPurpose{UserDiagnostic,Benchmark}` separates GUI from CLI. GUI string `[Benchmark]` -> `[Detailed Logs]`. Save/exit acceptance automated PASS (.112), field rendering seen, product acceptance ACCEPTED for final CLOSED | S5 | `implementation-briefs/S4-gui-diagnostic-logging.*`, `architecture/gui-diagnostic-logging.*` |
| **S5** | Console benchmark execution | S4 contract | infra complete + `.126` recover CLI | **XMP-REAL-001: 7 cases, 21 modes SUCCESS (partial measurement)** | **NOT CLOSED** | normal-completion cohort and threshold pending | `runConsoleBenchmark()` reuses S3 `BenchmarkSession` + S2 `BenchmarkRunner`. Uses `QCoreApplication` so no platform plugin is required. `.126` `--benchmark-recover` regenerates the summary of a force-killed suite lock-free. GATE OPEN, but CLOSED needs a normal-completion cohort and thresholds | S6 | `implementation-briefs/S5-console-benchmark-execution.*` |
| **S6** | Data-mining automation / measurement gate | S5 | gate prepared | **measurement not executed** | **NOT STARTED** | needs a real dataset | The threshold is **still undefined**. The reported gate conditions are satisfied but the threshold is not set | S7 | `implementation-briefs/S6-data-mining-automation.*`, `S6-measurement-gate.*` |
| **S7** | Help / usability | S6 | not started | — | **NOT STARTED** | — | — | S8 | — |
| **S8** | Full verification / release gate | S7 | not started | — | **NOT STARTED** | — | — | — | — |

> **Benchmark and Telemetry are not synonyms.** GUI `[Detailed Logs]` is
> `TelemetryRecorder/UserDiagnostic`; CLI `--benchmark` is
> `TelemetryRecorder/Benchmark`. Neither is auto-ingested by the other.

---

## Current focus

```text
baseline          0.9.4.157 (CPU CTest 131/131, GPU CTest 132/132)
active status     Process split P1-P4 complete + G4/G6 verified (.111) + S4 save/exit acceptance done (.112) + independent-audit items B1-B3/B7 applied (.113) + crash hotfixes (.114/.115) + phantom-group auto-repair (.116) + bulk-apply stall removed (.117) + churn removed (.118) + throughput labels clarified (.119) + GUI-less restore + runbook (.120) + detail-pane widening (.121) + right-click-path I/O removal (.122) + view-flake fix + PDBs + dumps (.123) + comment correction (.124) + pipe loss-proofing + color_thumb keep-as-is (.125) + recover CLI (.126) + Test Mode XMP regression (.127) + shared decode source (.128) + Auto(AB) live-match fix (.129) + match-baseline self-heal (.130) + displayed-set counters (.131) + Load summary fill (.132) + CPU metric fix (.133) + fast-rescan + forced re-verify (.134) + Test Mode diff/adopt (.135) + preview gauge (.136/.137) + cell-width text (.138) + EXE icons + sort overhaul (.139) + scan-start thumb keep (.140) + list/selection keep (.141) + view freeze (.142) + stale-pair prune + silent re-apply (.143) + failed-file reasons (.144) + total failed count (.145) + sort agreement + single instance (.146) + None order memory (.147) + reverify placement fix (.148) + Load/Re-verify single slot (.149) + Date sort (.150) + audit finding fixes (.151) + directed re-review fixes (.152) + P1-A/P1-B exit-path fixes (.153) + kill-durability D1 crash marker (.154) + honest decodedFrames (.155) + V video tab round 1 (.156) + thumbnail fix and AV1 (.157); product acceptance ACCEPTED (18/18, 2026-10-18) -> S4 final CLOSED -> S5 GATE OPEN
next              remaining field GUI acceptance (manual check) -> XMP full-corpus coverage -> S5 repeated measurement + threshold decision -> S6 gate
G4/G6 verified    G4: stress PASS — a 3 s consumer stall stays lossless (0 gaps, 600 delivered), no deadlock, bounded RSS (20/20/21 MB). No defect confirmed (design review: GUI->Backend bounded by pending cap; Backend->GUI backlog = output size).
                  G6: two real defects fixed — fixed 3 s exit delay (3049->6 ms; `waitForFinished` handle wait with re-entry blocked) + Job Object Win32 return checks/logs. Parent-crash E2E: the job reaps the wedged backend; the `--no-job` control survives, proving the job is load-bearing.
S4 save/exit      Real defect fixed — the partial/zero-write false-success is removed (`saveDetailedLogTo`, exact byte count). Real button -> file picker -> write -> status E2E PASS (`save=ok`, 3 repeats). On-screen rendering stays recorded as "not done" (headless). Product acceptance ACCEPTED, so the linkage condition is met -> final CLOSED.
Audit B1-B3/B7    B1 (FAIL reproduced): the badge compared size + the 64 KiB quick hash only, so a differing tail was identical=true. Fix: the quick hash is a candidate gate only; passing pairs require full-content SHA-256 equality (per-file memo, >512 MiB loses the badge; quick-load uses the same contract). A data race found during verification was fixed (16 concurrent shard inserts -> `std::mutex`). Measured cost: ~1.5 GiB extra reads per full scan (user index). B2: settings -> received telemetry `"mode":"A"/"B"` round-trip tests. B3: no-FFmpeg lane SKIP (exit 0) verified. B7: S4 row/chain wording reconciled.
Hotfix .114/.115  .114: field crash (Load/Start Scan, exit code 62097) — the loadMatches memo's map-internal reference (UAF) fixed by returning by value + a field-scale regression test (3,000 pairs).
                  .115: identified 62097 (`KillProcessExitCode=0xf291` = the supervisor's kill). D1 (.113 regression): `loadIndex` ran synchronously on the IPC event thread + the badge confirmation's candidate reads (~1.5 GiB) -> health timeout -> terminate (ineffective) -> kill. D2 (latent): `spawn()` never stopped `escTimer_` -> a stale timer killed each fresh backend with no reason (self-sustaining loop). Leftover: the Load row's quick-hash-only badge. Fix: Load on a QThread worker + `escTimer_.stop()` + rows use `m.contentIdentical`; tests Test 1b/3c (pre-fix FAIL reproduced), checks 39->45. Field-confirmed (2026-10-17: zero kills on scan and Load).
Phantom .116      Field report: group 275 (representative Keren1286444-…) — deleted similar files lingered as phantom members (0-byte size, blank detail) and even a rescan did not remove them. Cause: scan reconciliation used `Database::remove()` (files row only), leaving matches/thumbs behind. Fix: `forgetRows` + `forgetNoTransaction` (cleanup in the scan's transaction), `removePath` -> `forget`, and `cleanupMissingOnDisk` (validates match endpoints -> forgets + logs; offline no-op) runs automatically on Load and scan quick-load. Test FAIL-first (exit 29) -> PASS. Field visual check needed.
Bulk fill .117    Probe (9,055 groups): a synchronous result apply took **609.7 s** (freeze) -> **applyMs 84 ms + an 18.1 s asynchronous fill (91 ticks)**. Three causes: the tie sorting's per-path FILE_META requests (cache misses; ffprobe fallback on the loopback — 1,000 groups 65.9 s), fillPair's synchronous bulk fill, thumbCatchUpVisible's per-row visualItemRect. Fix: cached-only `filePixels`, a `GroupFillJob` deferred fill (>800 rows / 100 rows per turn / generation cancels / anchor restore), a bounded catch-up window, `resultsApply`/`groupFill` logs. Regression probe `group_fill_probe` (pre-fix FAIL). Field check needed (progressive fill + responsiveness).
current verdict   .105 file-operation/preview/index logs and .106 badge/real-process IPC await field evidence (remaining field GUI acceptance).
                  Search/Index/Comparison = ACCEPTED (18/18, 2026-10-18); S4 final CLOSED; XMP = CONDITIONAL (real-file decode/verdict/session evidence held, full-corpus remains);
                  color_thumb keep-as-is confirmed (user decision); S5 GATE OPEN (partial measurement) / S6 NOT STARTED (threshold + comparison samples pending).
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
color_thumb production correction     NOT PERFORMED - R1 fixture/skip done (.113); keep-as-is confirmed (user decision)
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
| product acceptance (Search/Index/Comparison) | **ACCEPTED (2026-10-18 field report, 18/18)** — past `NOT ACCEPTED` -> 0.9.4.46 defect fixes -> field items resolved | `build-history/0.9.4.46.*`, the acceptance audit entry in `worklog/0.9.4.*` |
| GUI Stop unresponsiveness | **fixed (0.9.4.48, engine level)** | `build-history/0.9.4.48.*`. The actual GUI button click is NOT_VALIDATED |
| `color_thumb` no-FFmpeg classification | R1 done (0.9.4.113) — keep-as-is confirmed (user decision) | `implementation-briefs/I-color-thumb-no-ffmpeg-classification.*`, `build-history/0.9.4.113.*` |
| Register nvcc warnings in the release gate | undecided | `worklog/0.9.4.*` |
| Dedicated console-output regression test | not added | `worklog/0.9.4.*` |
| V — Video Content Matching | **PROPOSED / DEFERRED** — includes a designed Video tab (Automatic recommended / Custom) with short-/long-form intervals and frame budgets; V5 UI follows V0–V4 accuracy/cost gates. Not implemented/measured; outside the Active Build Queue | development-roadmap.{ko,en}.md, implementation-briefs/V-video-content-matching.*, architecture/video-similarity-search.* |

## Version invariance check

`kEngineVersion 1.5.1` - `kDatabaseVersion 1.0.6` - `kBenchmarkSchemaVersion 9` -
`kCacheFormatVersion 9`. 0.9.4.82 bumped `kEngineVersion` 1.5.0 -> 1.5.1 because EXIF-rotated image fingerprints change by up to +/-1 LSB, so stored pairs revalidate. DB/schema/cache unchanged.






