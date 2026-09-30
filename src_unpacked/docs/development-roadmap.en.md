# Development Roadmap — 0.9.4 Development Line

## Document Naming and Structure Rules

The canonical naming/location and role-separation rules are defined in `docs/document-naming.ko.md` / `.en.md`.

Key rules:
- `development-roadmap.ko/.en.md` and `development-progress.ko/.en.md` keep fixed names.
- Implementation Brief uses `<Node>-<topic>.ko.md` + `.en.md`.
- Build History uses `<version>.ko.md` + `.en.md` and is not renamed.
- Work Log uses `docs/worklog/<development-line>.ko.md` + `.en.md` as one cumulative file per development line; do not encode version ranges in the filename. It carries the **Performance / Tuning Experiment Index** linking the experiment lineage and the surviving / refuted candidates.
- Architecture documents are topic-based; a version suffix is allowed only for explicit historical/audit snapshots.
- New/moved documents must update the KO/EN pair, internal links, `STRUCTURE.md`, and `llms.txt` together.

See [Document Naming and Structure Rules](document-naming.en.md) for the full rule set.

## Document hierarchy

Roadmap and detailed implementation briefs have different roles.

- **Roadmap**: overall direction, dependency order, node boundaries, and change-management rules.
- **Progress**: actual current node, blocker, validation state, and recovery history.
- **Implementation Brief**: the focused engineering contract for the active node; it contains staged scope, boundaries, telemetry expectations, and exit criteria.
- **Build History**: evidence of what was actually changed and validated in each version. This is where the **detailed numbers, run conditions, refutation evidence and future revisit conditions** of a performance experiment are preserved; every tuning and profiling experiment is recorded here regardless of whether it succeeded.

Current B/C/D briefs:

- `docs/implementation-briefs/B-adaptive-scheduler.ko.md / .en.md`
- `docs/implementation-briefs/C-calibration-profile.ko.md / .en.md`
- `docs/implementation-briefs/D-pipeline-queue.ko.md / .en.md`

## Purpose

This document defines the high-level development direction and dependency order for the MediaSimilarityFinder 0.9.4 development line.

The key rule is to **separate development stages from build numbers**.

- A, B, C, ... are development nodes.
- B1, B2, B3, ... are recovery and verification substeps inside a node.
- Build numbers are never pre-assigned to roadmap nodes.
- A version advances when a reproducible, validated code state is established.
- One node may therefore produce several versions such as 0.9.4.0 → 0.9.4.1 → 0.9.4.2 → ...
- Failed experiments or one-off debug states do not need to become meaningful release versions.
- Actual location and per-version evidence are tracked by development-progress.* and build-history/*.

This is the development-direction anchor. Implementation must read it together with the current Progress document before selecting the next task.

## Development Flow

text flow:
START
  |
  v
[A] Foundation / Terminology / Instrumentation
  |
  v
[B] Adaptive Scheduler
  |
  v
[C] Calibration / INI Performance Profile
  |
  v
[D] Pipeline / Queue Optimization
  |
  v
[I] Analyze / Matching Performance        <-- inserted after D8b evidence (see Node I)
  |
  v
[E] Adaptive Video Decode Planner          <-- closed (E-1/E-2/E-3 done, E-3C/E-4 closed)
  |
  v
[F] Hardware Video Decode Backend
  |
  +--> NVIDIA NVDEC (sole investigation/experiment/implementation scope of this F)
  |
  v
[G] Additional GPU Backends
  |
  +--> Vulkan
  +--> AMD HIP/ROCm
  +--> Intel Level Zero
  |
  v
[H] Regression / Stability / Performance Validation
  |
  v
NEXT DEVELOPMENT LINE

Each node must pass its exit criteria before the next node becomes active.

### Recovery branches

Example when a scheduler problem appears in B:

[B]
 |
 +--> [B1] Reproduce / Diagnose
 |       |
 |       v
 |     [B2] Fix / Refine
 |       |
 |       v
 |     [B3] Regression / Verification
 |       |
 |       +---- fail ----> [B1]
 |       |
 |       +---- pass ----> [B]
 |
 v
[C]

Common pattern:
A -> A1 -> A2 -> A3 -> A
B -> B1 -> B2 -> B3 -> B
C -> C1 -> C2 -> C3 -> C

A problem does not automatically redesign the whole direction. First reproduce, diagnose, fix, and regression-test inside the current node, then return to that node's gate.

If evidence requires a structural direction change, update Roadmap and Progress together and record why.

## Node A — Foundation / Terminology / Instrumentation

Goal:
- Make GPU terminology vendor-neutral at higher layers.
- Expose GPU ON/OFF only and remove manual GPU utilization control.
- Preserve CPU Resource Mode semantics.
- Establish MSF_ENABLE_GPU / MSF_GPU_BACKEND structure.
- Establish build-windows-cpu / build-windows-gpu.
- Keep current CUDA as a concrete backend.
- Preserve CPU fallback.
- Promote Benchmark / Telemetry to first-class instrumentation.

Core telemetry:
- measured / not_measured / not_available / partial / failed / fallback
- separate decodedFrames and sampledFrames
- scheduler / queue / transfer / backend / decoder observations
- cancellation / partial state

Exit criteria:
- Generic GPU terminology and concrete backend names are separated.
- GPU ON/OFF policy is explicit.
- Benchmark cannot misrepresent unmeasured values as zero.
- CPU fallback remains intact.
- Basic CPU/GPU regression tests pass.
- Vendor-specific APIs do not spread through the high-level search engine.

## Node B — Adaptive Scheduler

B owns the **CPU/GPU work-allocation policy based on effective capacity** rather than fixed 50:50 sharing. GPU utilization itself is not the optimization target.

Detailed implementation:
- `docs/implementation-briefs/B-adaptive-scheduler.en.md`
- B1 minimal allocation → B2 throughput → B3 live load → B4 stability → B5 cost → B6 Resource Mode → B7 final gate

The B boundary is "where and how much work to allocate"; internal queue/worker/pipeline execution belongs to D.

## Node C — Calibration / INI Performance Profile

C **partially reuses and extends** the existing profile/benchmark concepts.

Separate relatively stable baselines from live runtime measurements. Store profile identity/version/confidence and related measurements in INI for use as the Scheduler's initial estimate. Live runtime state always has precedence.

Detailed implementation:
- `docs/implementation-briefs/C-calibration-profile.en.md`

## Node D — Pipeline / Queue Optimization

D is **new pipeline/queue design work**. It owns how Scheduler-assigned work actually flows through queues and workers, reducing unnecessary barriers, worker starvation, queue imbalance, transfer stalls, and serialization.

Start with observability before making large structural changes, then optimize incrementally.

Detailed implementation:
- `docs/implementation-briefs/D-pipeline-queue.en.md`

B and D keep separate responsibilities: B is allocation policy; D is execution pipeline.

**Node D outcome (recorded 0.9.4.22):** D1a/D1b observability, D2 barrier
review, D3-Minimal bounded walker queue, D4a backend internal timing, and
D8a/D8b reproducible + scaled dataset were completed. Measured on 2,700
files: `walker maxDepth` 964/4096 with `blocked_ticks` 0, and the GPU batch at
0.026 % of engine wall. **D3+D4 addressable ceiling is 0.044 %**, so D4b
overlap and Full D3 topology are deferred on evidence rather than assumption.
D produced the structure; the remaining cost lives outside its scope.

## Node I — Analyze / Matching Performance

**Change-management record (required by the Roadmap's own rule).**

1. **Problem recorded in Progress:** at 0.9.4.22, the stage breakdown showed
   `analyze` consuming **98.62 %** of engine wall, with walk 1.40 %, image
   stage 0.59 %, and GPU batch 0.04 %.
2. **Original path and root cause:** D was expected to keep optimizing the
   pipeline. Measurement showed D-owned work is 0.044 % of wall, so the
   original path had no headroom. Root cause: the dominant stage was never
   owned by any node, and it is the final matching/grouping stage
   (`MediaPipeline::analyze`), which is outside the D brief by design.
3. **Roadmap updated:** Node I inserted between D and E.
4. **Reason recorded in both KO and EN:** the bottleneck is real and measured,
   but unowned; leaving it unowned would mean the 0.9.4 line has no
   meaningful performance work left despite a 100× measured gap.
5. **Continue on the new path.**

Goal:
Make the stage that dominates a scan observable, then reduce its cost —
without changing any search verdict.

Scope:
- `analyze` internal stage decomposition (index build / candidate scan /
  image SSIM verification / video temporal)
- verify-stage counters: calls, cache hits, decode misses, SSIM evaluations
- optimization of that stage **only after** the decomposition identifies the
  cause

Out of scope:
- search verdict semantics, thresholds, SSIM algorithm definition
- anything D already measured and deferred

Detailed implementation:
- `docs/build-history/0.9.4.23.ko.md` / `.en.md` (D9a pre-register)

## Node E — Adaptive Video Decode Planner

Goal:
Reduce workload-specific cost from decoding more frames than necessary.

Keep planner and backend separate:
- planner: sequential / hybrid / sparse seek
- backend: Software FFmpeg / hardware decoder

Required telemetry:
- requested sample frames
- sampled frames
- decoded frames
- seek count / latency
- decode throughput
- keyframe/GOP cost
- conversion / resize
- fallback

Exit criteria:
The benchmark clearly exposes the sampled-vs-decoded gap and planner choices can be validated against real throughput.

### Closure status (per the 0.9.4.42 record)

Sparse seek was **refused for production adoption**, so no real efficiency gain was
achieved. The result is "sequential is confirmed". What the node does leave behind
is the correct decision criteria and the validation structure, both of which stand.

- E-1 / E-2 / E-3 complete
- **E-3C is not promoted to a separate roadmap Stage, is not migrated to F, and is
  closed within Node E's scope.**
- **E-4 (production integration + end-to-end validation) is likewise closed within
  Node E's scope.** Because `ExactnessPolicy::RefuseAll` makes the sparse production
  path unreachable, no "qualitative" path is left to integrate. E-4 must not be read
  as a reason to reintroduce sparse.

### Node E completion / closure conditions (met per 0.9.4.42)

1. Confirm production sequential decode as the baseline
2. Verify whether production exactness for sparse sampling can be established
3. Confirm that actual results can differ from the production baseline
4. Refuse sparse production adoption
5. Retain `ExactnessPolicy::RefuseAll`
6. Complete the related correctness fixes and regression verification
7. Do not reintroduce sparse while there is no evidence for further sparse adoption
8. Allow future revisiting only once new production-parity evidence is obtained

**E-4**: because the sparse production path does not exist, it is **not carried out as a
separate sparse integration stage and is absorbed into Node E closure.**

**Note**: E-3C may inform F's architecture design in future (as a reference only). It
is not migrated as an F work item.

The basis for closure and the revisit conditions are in
`docs/build-history/0.9.4.42.*`. The earlier E-2A/E-2B exactness figures were
**self-referential** and must not be reused as production exactness evidence.

## Node F — Hardware Video Decode Backend

Connect NVIDIA NVDEC as the first real hardware-decoder backend candidate.

**The actual investigation, experiment and implementation scope of this F is NVDEC
alone.** Investigation and verification of other hardware decode backends are not
included in this F.

The **architecture is not fixed to NVIDIA**, however. The backend abstraction is
expected to accommodate additional hardware decode backends such as Intel/AMD in
future. Their implementation and verification are **out of scope for this F**.

### F progress (0.9.4.43)

- **F-1 Random-Access Safety Contract: `CONDITIONAL` / `PRODUCTION ADOPTION = NO`**
- **F-2 production integration is forbidden.** F-1 found that exactness **breaks even under
  normal conditions** (IDR-start fixtures): 2 of 4 IDR-start H.264 files mismatched. Do not
  proceed to production integration while correctness is unestablished.
- Established contract:
  ```text
  RandomAccessSafe / RandomAccessUnsafe / RandomAccessUnknown
  Unsafe → CPU fallback,  Unknown → CPU fallback
  ```
- **Core rule: structure (keyframe start) is necessary but not sufficient.** `Safe` requires a
  recorded exactness proof. Capability and safety are separate axes.
- Unresolved: the 1360x808 mismatch root cause (`INCONCLUSIVE`); no 4K IDR-start H.264 fixture.
- Details: `docs/build-history/0.9.4.43.*`, `docs/implementation-briefs/F-random-access-safety.*`

Rules:
- Software FFmpeg is the reference/fallback.
- Inspect codec/profile/pixel-format/bit-depth/capability per file.
- Fall back on initialization / seek / frame-mapping / decode failure.
- Do not spread low-level vendor APIs through the high-level engine.
- Record backend selection and fallback reason in benchmark.

If NVDEC is not beneficial for a file, CPU decode or another path must remain selectable.

## Node G — Additional GPU Backends

Review independently after the GPU abstraction is stable:
- Vulkan
- AMD HIP/ROCm
- Intel Level Zero

Do not claim support completion without real hardware validation.

## Node H — Regression / Stability / Performance Validation

Validate:
- CPU-only
- GPU OFF
- GPU ON / AUTO
- low-end GPU simulation
- acceleration not beneficial
- high-end GPU
- external CPU load
- external GPU load
- hardware decode success
- hardware decode fallback
- mixed image/video
- cancellation / partial scan
- accuracy parity
- system stability
- cache compatibility
- existing CUDA behavior

Acceptance is based on correctness + end-to-end throughput + fallback correctness + stability + observability, not a single GPU-utilization number.

## Version number policy

Version numbers are not roadmap node numbers.

Roadmap: A -> B -> C -> D -> I -> E -> F -> G -> H
Version: 0.9.4.0 -> 0.9.4.1 -> 0.9.4.2 -> 0.9.4.3 -> ...

Example:
0.9.4.0
  B
  |
  +-- B1 scheduler oscillation discovered
  +-- B2 hysteresis fixed
  +-- B3 regression
  |
  +--> 0.9.4.1
       B gate passed
       |
       v
       C

This is only an example. Several versions may occur inside one node, and one version may contain several documentation/code tasks.

The version represents the result; the roadmap node represents the direction.

## Document roles

| Document | Role |
| --- | --- |
| docs/development-roadmap.ko.md / .en.md | Overall development direction, flow, nodes, recovery rules |
| docs/development-progress.ko.md / .en.md | Actual node, status, blockers, and B1/B2/B3 recovery history |
| docs/build-history/<version>.ko.md / .en.md | Actual code changes, reasons, fixes, and validation evidence |
| docs/architecture/*.ko.md / .en.md | Detailed technical design |
| AGENTS.md | OpenCode/developer work rules |

OpenCode reads Roadmap and Progress first, determines the current node and next gate, and then uses the node-specific detailed prompt.

## Change management

Do not skip roadmap nodes arbitrarily.

A roadmap change is justified when there is a structural blocker, hardware/codec/backend evidence invalidates an assumption, correctness or CPU fallback is threatened, or end-to-end throughput is harmed.

When that happens:
1. Record the problem in Progress.
2. State the original path and root cause.
3. Update the Roadmap.
4. Record the reason in both KO and EN.
5. Continue along the new path.

This document is not a complete implementation prompt; it is the anchor that prevents loss of development direction.


## Benchmark / Console CLI Cross-Cutting Track

Benchmark/Telemetry remains a cross-cutting infrastructure track rather than a new roadmap node. Because F-1 currently forbids NVDEC production adoption, organizing Benchmark/Console CLI does not authorize F-2 integration.

Fixed contract:

- benchmark mode: AUTO / CPU-only / GPU-max
- media scope: images / videos / all
- Console canonical selector: --media images|videos|all
- Run / Suite separation
- GUI keeps only the latest three mode results per source folder
- Console keeps cumulative long-term results
- normal Search Index and benchmark index/cache isolation
- human-readable source-folder label + short stable id
- Run stores source identity, dataset fingerprint, media scope, scheduler configuration, environment, and failure/fallback state
- GPU-max is not GPU-only; mandatory CPU work and fallback remain enabled

### Build / implementation schedule policy

Version numbers are not pre-assigned.

S0 design/pre-register
→ S1 Console entry foundation

**S2 implementation status (0.9.4.43)**: Run/Suite benchmark core **complete**. A case is one file, requested/effective are recorded separately in modeResults[], and the aggregate precedence is Cancelled > Failed > Success > Skipped. Execution goes through an injected BenchmarkExecutor boundary that reuses the production scan path, with no second search engine. 59 unit checks, CPU CTest 87/87, GPU CTest 88/88.
The per-file measurement is implemented with ignoredPaths, and because scan() walks the folder on every call the cost is **O(N²)**. S2 accepts this in favour of correctness and defers the large-scale optimisation. Process isolation and the OS filesystem cache are **uncontrolled**.
Still not implemented: durable JSONL journal (S3), storage isolation (S3), terminal renderer (S5), public benchmark CLI options, the final AUTO/CPU/GPU-max policy, and NVDEC integration.

**S1 implementation status (0.9.4.43)**: Console Entry Foundation **complete**. 40 parser unit checks, CPU CTest 86/86, GPU CTest 87/87.
The CLI supports only help, version, smoke and scan with a media selector; running with no arguments still opens the existing GUI, and the CLI path never constructs a MainWindow.
Still not implemented: Benchmark Engine, per-file AUTO/CPU/GPU-max, JSONL Journal, Cancellation persistence, Terminal Renderer (S2 onward).
→ S2 Run/Suite benchmark core
→ S3 Benchmark storage isolation
→ S4 GUI benchmark integration
→ S5 Console benchmark execution
→ S6 Data-mining automation
→ S7 Help/usability
→ S8 Full verification/release gate

Every stage closes with source change → CPU/GPU build → CTest → CLI/GUI execution verification → documentation → Build History when applicable → commit.

Detailed contracts and storage layout are maintained in docs/architecture/benchmark-telemetry-roadmap.*.

## Final Benchmark Console Design Amendment — 2026-09-30

The existing benchmark S0-S8 schedule is finalized with the following contract:

- Execute AUTO -> CPU -> GPU-max per file and persist that file's results immediately.
- Do not share analytical intermediate results between benchmark modes.
- Ctrl+C cancellation and partial-result persistence are first-class behavior.
- Console CPU resource reuses the existing Maximum / High / Balanced / Gaming / Manual(10-90%) policy. Balanced (55%) is the recommended default.
- A linear extrapolation from a Balanced run is not an actual Maximum benchmark result.
- The interactive console header is compressed to three fixed information rows and never auto-wraps.
- Long Target paths use middle ellipsis on screen only; JSON stores the full path.
- Priority header fields are Target / Scope / IMG+VID progress / Mode / CPU Resource / GPU / Distance / Suite ID / Build+Git identity.
- The lower area is reserved for detailed CURRENT FILE AUTO/CPU/GPU-max information.
- Completed history is compact, one line per file.
- TTY and non-interactive output are separate renderers over the same journal/JSON data.
- The existing benchmark source/schema remains a permanent legacy baseline.

With this amendment, S2 owns benchmark execution and the per-file journal contract, while S5 owns Console rendering and CLI execution.

