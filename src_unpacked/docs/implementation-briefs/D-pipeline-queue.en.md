# Implementation Brief — Node D Pipeline / Queue Optimization

## 1. Purpose

Node D reduces wait time, barriers, worker starvation, and unnecessary serialization while Scheduler-assigned work moves through the real processing pipeline.

Core distinction:

```
B = where and how much work to allocate
D = how that work actually flows
```

Node D is treated as new design work.

## 2. Entry prerequisites

Before D starts, keep these contracts stable:

- CPU/GPU Scheduler decision interface
- CPU/GPU work-share representation
- scheduler telemetry
- backend abstraction
- measurement states
- CPU fallback

D improves the internal pipeline through these interfaces.

## 3. Main bottlenecks

Measure rather than assume:

- future/barrier structure
- worker starvation
- global serialization
- CPU↔GPU transfer stalls
- queue imbalance

Do not perform a large redesign based only on speculation.

## 4. D1 — Pipeline Observability

Do not change the structure substantially at first.

### Queue

- queue depth
- enqueue/dequeue
- queue wait

### Worker

- active time
- idle time
- wait

### Stage

- start/end
- duration
- overlap

### Transfer

- transfer count
- transfer bytes
- transfer time

D1 exists to **make bottlenecks visible**, not to maximize speed immediately.

## 5. D2 — Barrier Reduction

After D1 identifies unnecessary whole-stage barriers, remove them incrementally.

Existing shape:

```
A1 A2 A3 A4
 \  |  |  /
   WAIT
     ↓
B1 B2 B3 B4
```

Possible overlap:

```
A1 → B1
A2 → B2
A3 → B3
A4 → B4
```

Batch-level overlap may also be appropriate.

Preserve any logic that genuinely depends on result ordering.

## 6. D3 — Queue Optimization

Separate queues where needed.

Example:

```
CPU Queue
GPU Queue
Result Queue
```

Use bounded queues where necessary to prevent memory growth.

Queue policy may consume Scheduler allocation, but the Scheduler should not directly manipulate queue internals.

## 7. D4 — Transfer / Compute Overlap

Overlap transfer and compute when beneficial.

```
Transfer N+1
   ||
Compute N
   ||
Result N-1
```

Overlap is not automatically faster. If synchronization cost dominates, a simpler path may be preferable.

## 8. D5 — Batching

Batch GPU work where useful.

Consider:

- batch throughput
- queue wait
- latency
- VRAM usage
- transfer size
- cancellation responsiveness

Never increase batch size blindly.

## 9. D6 — Worker Starvation

Focus on waits such as:

```
CPU worker
   ↓
waiting for GPU work
   ↓
GPU worker
   ↓
waiting for CPU result
```

Remove unnecessary blocking or circular waits.

Use asynchronous completion/continuation where appropriate.

## 10. D7 — Scheduler integration

Scheduler may produce an abstract decision such as:

```
CPU share = 70
GPU share = 30
```

D maps that to worker/queue policy.

Avoid hard coupling such as:

```
Scheduler → 7 CPU workers
```

Prefer:

```
Scheduler
   ↓
CPU/GPU work allocation
   ↓
Pipeline policy
   ↓
workers / queues
```

## 11. D8 — End-to-End validation

Final evaluation uses the whole search, not a single stage.

Measure:

- total wall time
- CPU/GPU stage time
- queue wait
- barrier wait
- transfer time
- worker idle time
- throughput
- cancellation latency

## 12. Out of scope

- Scheduler policy redesign
- Calibration profile design
- NVDEC
- video decode planner
- additional GPU backends
- optimization for every workload
- forced GPU-utilization tuning

If D reveals a Scheduler policy problem, record it in B's recovery branch where possible.

## 13. Completion criteria

### Structure
- major queues/stages observable
- unnecessary global barriers reduced
- worker starvation reduced
- CPU/GPU overlap possible

### Performance
- end-to-end throughput measurable
- queue/barrier/transfer overhead quantifiable
- improvement validated on real workloads

### Stability
- cancellation works
- CPU fallback works
- GPU OFF works
- mixed image/video works
- result parity preserved

### Observability
Benchmark JSON can distinguish at least:

- queue depth
- queue wait
- stage duration
- transfer time
- batch information
- worker wait/idle
- overlap
