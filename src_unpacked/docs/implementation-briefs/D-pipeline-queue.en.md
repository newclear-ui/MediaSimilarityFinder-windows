# Implementation Brief — Node D Pipeline / Queue Optimization

## 1. Purpose and Status

Node D is the **Pipeline / Queue observability and optimization stage** that turns Node B's abstract CPU/GPU allocation into an efficient execution flow.

The boundary is:

```
B = where and how much work to allocate
D = how allocated work actually flows through the pipeline
```

This document is the detailed implementation contract for D. It does not claim that every target structure already exists in the current source. Work proceeds as **current-structure inspection → instrumentation → bottleneck evidence → limited change → regression validation**.

The current D entry point is the completed Node C state at 0.9.4.14. The official preserved baseline 0.9.2.32 must not be modified.

---

## 2. Entry Contracts

D must preserve:

- CPU/GPU Adaptive Scheduler decision interface
- abstract CPU/GPU work-share representation
- Scheduler telemetry
- GPU backend abstraction
- `MeasureState`
- CPU reference/fallback path
- existing search verdicts and thresholds
- existing DB/cache/index compatibility
- GPU ON/OFF semantics

If D reveals a Scheduler policy defect, record it in the appropriate B recovery branch rather than silently redesigning B inside D.

Node C's rule also remains: a Performance Profile is an initial estimate; live runtime measurements have precedence.

---

## 3. Current Source Baseline

The current implementation must not be treated as an empty pipeline.

### 3.1 Image path

`MediaPipeline::imageBatch()` already contains:

```
paths
  |
  +--> bounded parallel CPU decode
  |
  +--> normalized 32x32 grayscale packing
  |
  +--> GPU batch hash or CPU pHash fallback
  |
  +--> crop / color-thumbnail pass
  |
  +--> result
```

CPU decoding is already parallelized with bounded `std::async` work and results remain input-index ordered. GPU hashing runs in bounded batches and falls back to the CPU pHash reference path when GPU execution fails.

Therefore D1 is not “add parallelism from scratch.” It is to **measure actual waiting and overlap across the existing decode, GPU-batch, crop, and thumbnail stages**.

### 3.2 Scan path

`MediaSearchEngine::scan()` already has a queue between the directory walker and the scan consumer:

```
Scanner walker
     |
     v
FileState queue
     |
     v
processOne()
     |
     +--> image batch
     |
     +--> video async range
     |
     v
DB / matching
```

This is a **directory-walk producer/consumer queue**. It must not automatically be described as a dedicated CPU execution queue or GPU execution queue.

Image and video also use different execution paths.

- Image: CPU decode → GPU/CPU hash → crop/thumbnail
- Video: bounded asynchronous analysis → FFmpeg/video fingerprint path → optional GPU video work

Therefore CPU Queue / GPU Queue / Result Queue are D3 candidates only if instrumentation shows they are necessary.

### 3.3 Existing benchmark basis

Benchmark already contains D-related fields for:

- image queue wait
- image transfer time
- image execution time
- CPU/GPU queue depth
- CPU/GPU queue wait
- stage durations
- GPU batch time
- video decoded/sampled frame counts
- resource samples
- cancellation/partial state

However, **having a telemetry field does not prove that every field is currently populated from a real producer/consumer event.**

D1 must connect actual events to telemetry fields. Unmeasured fields remain `not_measured` / `not_available` rather than being encoded as numeric zero.

---

## 4. D Stage Map

```
D0 Design Review
  |
  v
D1a Image-Path Observability (synchronous boundaries, first)
  |
  v
D1b Walker/Video Observability (async, next)
  |
  v
D2 Barrier / Serialization Reduction
  |
  v
D3 Queue Optimization
  |
  v
D4 Transfer / Compute Overlap
  |
  v
D5 Batching Optimization
  |
  v
D6 Worker Starvation / Dependency Analysis
  |
  v
D7 Scheduler Execution Binding (allocation-proportional dispatch;
  the full version depends on D3 infrastructure — see D7)
  |
  v
D8 End-to-End Validation
  |
  v
D-Gate
```

Each stage must produce a separately validated state before the next stage begins. Problems use a recovery branch inside the current D stage.

---

# D0 — Design Review / Baseline Freeze

## Purpose

Freeze the B/C contracts and document the current execution topology before changing it.

Preserve:

- B Scheduler share formula
- Resource Mode semantics
- minimum hold / hysteresis
- live-throughput precedence
- Profile initial-estimate semantics
- GPU abstraction
- CPU fallback
- search thresholds and verdicts
- DB/cache schema
- 0.9.2.32 preserved baseline

Record at D entry:

- CPU/GPU CTest results
- `--version`
- `--smoke`
- benchmark schema/version
- representative CPU-only / GPU OFF / GPU ON results
- search-result parity
- baseline wall time and stage telemetry

Performance claims must use the same conditions as the D baseline.

---

# D1a — Image-Path Observability (first)

Do not pack D1 into a single gate: keep the project gate discipline
(one gate = one verifiable unit, as in B1–B7 and C1–C4) inside D too.

D1a covers synchronous boundaries and is relatively easy. Instrument the
existing `MediaPipeline::imageBatch()` boundaries (decode → pack → hash
→ crop/thumbnail) as they are.

### Queue telemetry

Centered on bounded image-path handoffs:

- enqueue/dequeue count (per batch)
- batch depth (in-flight batches)
- wait count / cumulative wait time

### Worker telemetry (group-level aggregation first)

- decode worker active/idle/wait
- hash path (GPU vs CPU fallback) task count
- crop/thumbnail task count

### Stage telemetry

- decode / pack / hash / crop+thumbnail cumulative durations
- GPU batch time (reuse existing `addGpuBatchMs`)
- persist / candidate·match stages sit on the engine-scan side, so they
  are observed together with D1b (outside imageBatch, outside D1a scope)

### Overlap telemetry

Stage durations alone cannot distinguish serialized work from overlap.

For example:

```
CPU Decode  ─────────
GPU Hash        ───────
Crop                ──────
```

D1 should retain enough interval or group-level evidence to identify overlap.

### D1a exit criteria

- image decode/hash/crop boundaries are observable
- queue wait, worker wait, and stage duration can be distinguished
- unmeasured and actual zero are distinct
- search results remain unchanged

---

# D1b — Walker/Video Observability (next)

The async region is harder than D1a. Do not force intra-decode open.

### Walker queue

For the real queue between the directory-walk producer and the scan
consumer:

- enqueue/dequeue count, current/maximum depth
- producer blocked time, consumer idle time

Do not interpret this queue as a CPU/GPU execution queue (§3.2 holds).

### Video path

`VideoDecoder::framesAt()` decodes linearly after a single seek, so no
frame-level stage boundaries exist in code. Prioritize range level:

- range total time (reuse existing `buildMs`)
- decodedFrames / sampledFrames / keptFrames (reuse existing
  `VideoBuildStats`)

Defer intra-decode decomposition: forcing it here would pull E (Adaptive
Video Decode Planner) work into D ahead of order.

### Transfer observability (shared D1a/D1b note)

Wrapping `hashBatch()` calls host-side inevitably bundles
"transfer + kernel execution" (self-admitted in the B5 build-history:
the kernel was never opened). `cuda_backend.cu` has no `cudaEvent_t`
internal timers, so measured elapsed transfer time needs **internal
timing points under the backend abstraction**. Apply the "implemented
under the backend abstraction" condition from D4 to the D1
observability clause as well. Without internal hooks, transfer time
stays `not_measured`, never zero.

### D1b exit criteria

- walker queue is observable
- video range level is observable
- unmeasured and actual zero are distinct
- search results remain unchanged

---

# D2 — Barrier / Serialization Reduction

## Purpose

Remove only **unnecessary whole-stage waits and global serialization** demonstrated by D1.

Possible serialized shape:

```
A1 A2 A3 A4
 \ | | /
   WAIT
     |
B1 B2 B3 B4
```

Possible overlap:

```
A1 -> B1
A2 -> B2
A3 -> B3
A4 -> B4
```

or bounded batch-level overlap.

Do not remove barriers that protect:

- required result ordering
- DB transaction boundaries
- candidate-index consistency
- shared-cache write ordering
- cancellation checkpoints
- same-file dependent stages
- finalization after CPU fallback

The rule is not “less waiting is always better.” It is **remove only waiting without a real dependency**.

### D2 exit criteria

- each removed barrier has documented dependency reasoning
- result parity remains unchanged
- cancellation/partial behavior remains correct
- overlap is visible in telemetry
- wall-time improvement or a measured reduction in waiting overhead is demonstrated

A change with no measurable benefit should not be retained merely because it is more asynchronous.

---

# D3 — Queue Optimization

## Purpose

Change queue topology only when D1 demonstrates imbalance or avoidable blocking.

Candidate topology:

```
CPU work queue
GPU work queue
Result queue
```

This is a candidate, not a pre-existing fact or mandatory target.

### Queue principles

1. Scheduler does not manipulate queue internals.
2. Scheduler provides work allocation.
3. Pipeline policy maps allocation to queue/worker execution.
4. Queues can be bounded where necessary.
5. Backpressure prevents unbounded memory growth.
6. Cancellation is observable at every blocking point.

Queue pressure can be measured conceptually as:

```
pressure = currentDepth / capacity
```

But inserting this pressure directly into the Scheduler share formula would be a B policy change and is out of scope for D.

### D3 exit criteria

- queue depth/latency measurable
- bounded behavior verified
- queue imbalance reduced where it was demonstrated
- cancellation works
- memory growth is bounded
- Scheduler and queue implementation remain separated

---

# D4 — Transfer / Compute Overlap

## Purpose

Reduce unnecessary waiting between CPU↔GPU data movement and GPU computation.

Target pattern:

```
Transfer N+1
     ||
Compute N
     ||
Result N-1
```

Candidates include:

- asynchronous transfer
- pinned/staged buffers
- double/triple buffering
- batch overlap
- producer/consumer overlap

Implementation must stay behind the existing GPU backend abstraction. CUDA-specific APIs must not spread through the upper search engine.

Evaluate:

- transfer time
- GPU execution time
- synchronization time
- CPU wait
- end-to-end wall time
- VRAM usage
- cancellation latency

Overlap is not itself a success criterion. If synchronization costs dominate, the simpler path may remain preferable.

---

# D5 — Batching Optimization

## Purpose

Measure and, where justified, tune existing CPU/GPU batch boundaries.

The current image path already has a GPU batch size and backend-recommended batch sizing. D5 must not create an unrelated batching system. It must first determine how current batch boundaries affect:

- batch size
- batch duration
- items/sec
- queue wait
- transfer bytes
- transfer time
- VRAM pressure
- cancellation latency
- fallback rate

Large batches may improve throughput and amortize transfers, but may also increase queue wait, memory use, and cancellation latency. Small batches may improve responsiveness but increase scheduling and launch overhead.

Therefore the objective is not “maximum batch size.”

---

# D6 — Worker Starvation / Dependency Analysis

## Purpose

Reduce time workers spend waiting for other workers rather than doing useful work.

Potential pattern:

```
CPU worker
   ↓
waiting for GPU work
   ↓
GPU worker
   ↓
waiting for CPU result
```

Whether such a pattern actually exists must be established by telemetry.

Inspect:

- CPU worker idle while GPU queue is non-empty
- GPU worker idle while CPU prerequisite is incomplete
- producer blocked by a full queue
- downstream blocked by a full result queue
- lock contention
- `future.get()` waits
- condition-variable waits
- DB serialization
- global mutex contention

Preferred order:

1. remove circular waits
2. remove unnecessary blocking
3. reduce lock scope
4. consider asynchronous completion/continuation
5. only then consider changing worker counts

Simply adding workers is not the default D solution.

---

# D7 — Scheduler Execution Binding (allocation-proportional dispatch)

## Purpose

Safely map B's abstract allocation into pipeline execution.

**Boundary with B7 (important):** fresh-read binding of decisions is
already closed by B7 (0.9.4.8, `schedUseGpuNow`). Redoing it here would
repeat completed work. D7 has exactly one remaining job: **executing
real work split by the shares ratio.** Today only the binary `gpuUsed`
is passed, so a 70/30 split never reaches execution (recorded in
telemetry only — verified against code).

Preferred:

```
Scheduler
   |
   v
CPU share / GPU share
   |
   v
Pipeline policy
   |
   +--> CPU work
   |
   +--> GPU work
```

Avoid hard coupling:

```
Scheduler -> 7 CPU workers
Scheduler -> 3 GPU workers
```

The Scheduler should not need to know the worker topology.

### D3 dependency (loose coupling)

The full version (mid-batch re-adjustment, backpressure, cancellation
responsiveness) needs D3 queue infrastructure. But a thin version is
possible without D3: when building today's `for(k=from;k<to;++k)`
batches, statically split leading indices to the GPU path and trailing
ones to the CPU path by gpuShare (no re-adjustment). So D3 is not a hard
predecessor of D7 — "crude without D3, proper with D3". Documenting the
order anyway pins the full version's precondition on D3 and blocks
later improvisation ("let's do a thin D7 before D3").

Rules:

- GPU OFF → no GPU work
- GPU unavailable → CPU fallback
- GPU runtime failure → affected work falls back to CPU
- unknown allocation → safe fallback
- live throughput remains above Profile baseline in precedence
- Profile never directly forces execution

D does not change B's share formula.

---

# D8 — End-to-End Validation

Final evaluation uses the complete scan.

### Performance

Measure:

- total wall time
- files/sec
- images/sec
- video sampled frames/sec
- CPU stage time
- GPU stage time
- queue wait
- barrier wait
- transfer time
- worker idle time
- DB/persistence time

### Stability

Validate:

- cancellation latency
- pause/resume
- partial scan
- GPU OFF
- GPU unavailable
- GPU fallback
- mixed image/video
- large directories
- long-running scans

### Correctness

Validate:

- CPU reference parity
- CPU/GPU fingerprint parity
- similarity-verdict parity
- video temporal verdict parity
- crop/mirror parity
- incremental-scan parity

Comparisons must use the same dataset (record a dataset fingerprint in the
benchmark JSON as comparison evidence; the standard dataset itself is
defined at D0/D8 — the current `test_sample_img_vid/` root placeholder is
empty and needs fixtures before it can serve as an asset), DB/cache state,
build configuration, Resource Mode, backend/driver, background load, and
storage condition.

Single-run claims are insufficient; repetitions and variation must be recorded.

---

## 5. Measurement-State Rules

D continues the Node A/C measurement-state contract.

| State | Meaning |
| --- | --- |
| measured | actual measurement obtained |
| not_measured | not measured yet |
| not_available | cannot be measured in the current environment/capability |
| partial | only part of the requested measurement completed |
| failed | measurement was attempted but failed |
| fallback | fallback path was used |

Never collapse these into numeric zero:

```
not_measured != 0
not_available != 0
failed != 0
fallback != 0
```

If a D telemetry field is not yet connected to a real event, it remains explicitly unmeasured/unavailable.

---

## 6. Boundaries with B/C/E/F

### B — Adaptive Scheduler

B owns:

- allocation
- throughput feedback
- live load
- hysteresis
- minimum hold
- Resource Mode

D owns:

- queues
- workers
- barriers
- execution overlap

Scheduler formula problems discovered in D should return to B recovery.

### C — Calibration

C owns:

- Performance Profile
- calibration
- confidence
- initial estimate
- runtime deviation

D owns actual queue/stage/worker observation and execution topology.

If queue latency becomes a Profile metric, it should only be connected after D provides a real measurement.

### E — Adaptive Video Decode Planner

E owns:

- sequential / hybrid / sparse seek
- decodedFrames vs sampledFrames
- GOP/keyframe cost
- seek strategy

D does not implement E's planner.

### F — Hardware Decode

F owns:

- NVDEC
- hardware decoder backend
- capability
- codec/profile/pixel-format handling
- decode fallback

D does not implement F early.

---

## 7. Explicitly Forbidden

D must not:

- redesign B Scheduler share formula
- change Resource Mode semantics
- change Profile semantics
- implement NVDEC early
- implement the adaptive video decode planner early
- add a new GPU backend
- spread CUDA APIs through the upper engine
- change search thresholds
- change similarity algorithms
- change DB/cache schema merely for performance
- use GPU utilization as the sole performance objective
- redesign all queues merely because one queue exists
- encode missing telemetry as zero
- claim an overlap optimization succeeded without evidence

---

## 8. Test Strategy

D tests prioritize structural contracts and state transitions over absolute performance values on one machine.

### Pre-register

Record the following before changing code in each D stage. Objectivity
comes from pre-committed numbers, not post-hoc "less effective than
hoped":

- measured bottleneck (cite D1 evidence)
- expected gain (numeric range)
- rollback criteria (revert below this)

Place 1–2 lines of "expected gain / rollback criteria" directly under
the "Why the change was needed" item in `docs/build-history/<version>.en.md`.
This executes the D2 exit rule ("do not keep changes without benefit").

### Unit tests

- queue depth accounting
- enqueue/dequeue accounting
- bounded queue behavior
- cancellation
- stage timing
- measurement state
- batch accounting
- fallback accounting

### Integration tests

- CPU-only
- GPU OFF
- GPU ON
- GPU unavailable
- GPU failure → CPU fallback
- mixed image/video
- cancellation during queue wait
- cancellation during GPU batch
- high queue pressure
- empty/small workload
- single-file workload

### Parity

For the same dataset:

```
CPU reference result
        ==
GPU-assisted result
```

must remain true.

---

## 9. Performance Evaluation Rules

D does not define “higher GPU utilization” as success.

Priority:

1. search correctness
2. stability / fallback correctness
3. end-to-end throughput
4. latency / responsiveness
5. resource efficiency
6. GPU utilization as supporting telemetry

Every performance claim records the measured value and conditions.

Example:

```
Baseline:  100.0 s
D build:    91.0 s
Change:      9.0%
Condition: same dataset / cache / mode / backend
```

If a change provides no improvement under a controlled workload, that result is retained as evidence rather than hidden.

---

## 10. D-Gate

### Structure

- major real queues/handoffs observable
- major stage durations observable
- worker wait/idle observable
- unnecessary global barriers identified and reduced
- CPU/GPU overlap available where justified
- Scheduler and execution topology remain separated

### Performance

- queue/barrier/transfer overhead quantified
- end-to-end throughput measurable
- controlled baseline comparison
- both improvement and non-improvement evidence retained

### Stability

- CPU fallback
- GPU OFF
- GPU unavailable
- cancellation
- mixed image/video
- long-running stability

### Correctness

- CPU/GPU parity
- image verdict parity
- video verdict parity
- crop/mirror parity
- incremental-scan parity

### Observability

Benchmark JSON can distinguish at least:

- queue depth
- queue wait
- stage duration
- transfer time
- batch information
- worker wait/idle
- overlap
- fallback
- cancellation/partial state

After D-Gate, development proceeds to Node E — Adaptive Video Decode Planner.

---

## 11. Documentation and Version Rules

D1–D8 are roadmap substeps, not build numbers.

```
D1 -> validated code state -> 0.9.4.x
D2 -> validated code state -> 0.9.4.x+1
...
```

Actual build numbers advance only when a validated code state is produced.

Each implementation build history records:

- reason for change
- previous structure
- new structure
- rationale
- bottleneck addressed
- correctness impact
- Windows/GPU/Linux compatibility impact
- exact test results
- benchmark conditions
- known limitations

Implementation Brief defines the D contract. Build History records what actually changed.

---

## 12. Recommended Execution Order

```
D0 baseline freeze
   ↓
D1a image observability
   ↓
D1a regression
   ↓
D1b walker/video observability
   ↓
D1b regression
   ↓
D2 barrier reduction
   ↓
D2 regression
   ↓
D3 queue optimization
   ↓
D3 regression
   ↓
D4 transfer/compute overlap
   ↓
D4 regression
   ↓
D5 batching
   ↓
D5 regression
   ↓
D6 starvation/dependency
   ↓
D6 regression
   ↓
D7 scheduler binding
   ↓
D7 regression
   ↓
D8 end-to-end validation
   ↓
D-Gate
```

**Do not begin large D2–D6 topology changes before D1a/D1b observability is established.**

The current source already contains a walker queue, bounded asynchronous decode/video processing, and GPU batching. Therefore the first D implementation should not invent a new execution architecture; it should first **measure the execution architecture that already exists**.
