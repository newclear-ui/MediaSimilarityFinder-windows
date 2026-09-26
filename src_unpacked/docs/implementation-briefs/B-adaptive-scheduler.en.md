# Implementation Brief — Node B Adaptive Scheduler

## 1. Purpose

Node B builds a Scheduler that dynamically decides CPU/GPU work allocation from effective capability and runtime state rather than a fixed ratio.

This is not a one-shot instruction to implement a complete scheduler. Work proceeds through small, verifiable stages.

```
B1 Minimal Adaptive Allocation
        ↓
B2 Runtime Throughput Feedback
        ↓
B3 Live Load Awareness
        ↓
B4 Stability Control
        ↓
B5 Transfer / Workload Cost
        ↓
B6 Resource Mode Integration
        ↓
B7 Final Scheduler Gate
```

## 2. Core principles

- GPU utilization itself is not the optimization target.
- Prioritize end-to-end throughput and search correctness.
- A slow GPU may legitimately converge toward CPU-centric or CPU-only execution.
- CPU fallback is always retained.
- User-facing GPU control remains ON/OFF only.
- Manual constrains CPU resources; it does not expose a GPU-percentage target.
- The Scheduler must not become tightly coupled to internal Pipeline implementation.

## 3. B/D boundary

B decides **where and how much work should be allocated**.

```
Workload → Scheduler → CPU/GPU Work Allocation → Pipeline
```

D decides **how those allocations actually flow through queues/workers/pipeline**.

Therefore B does not freeze worker counts, queue topology, or barrier structure.

## 4. B1 — Minimal Adaptive Allocation

The first implementation uses only:

- CPU baseline capacity
- GPU baseline capacity
- GPU availability
- GPU ON/OFF

Outputs:

- CPU work share
- GPU work share
- scheduler decision

The initial version recomputes allocation on a simple internal interval. The interval is not exposed in the UI.

B1 does not yet implement:

- moving average
- hysteresis
- transfer-cost model
- external-load model
- workload-specific cost model

## 5. First B1 validation

The following states must be distinguishable.

| State | Expected scheduler behavior |
| --- | --- |
| GPU OFF | CPU 100% |
| GPU ON + useful GPU | CPU + GPU |
| Slow GPU | CPU-centric allocation is allowed |
| GPU unavailable | CPU fallback |
| Search correctness | Same result as CPU reference |

B1 succeeds first by proving **correct decisions and result parity**, not by maximizing speed.

## 6. B2 — Runtime Throughput Feedback

Add recent runtime throughput:

- CPU recent throughput
- GPU recent throughput
- CPU effective capacity
- GPU effective capacity

Start with a simple recent observation window. More complex smoothing belongs in B4.

## 7. B3 — Live Load Awareness

Add:

- CPU load
- GPU load
- GPU memory pressure
- CPU/GPU queue state
- external CPU/GPU load

The goal is rational total system throughput, not higher GPU utilization.

## 8. B4 — Stability Control

Prevent excessive rebalancing by adding:

- moving average
- hysteresis
- minimum hold time

Target:

```
lower sensitivity to short-lived noise
+
suppressed unnecessary CPU↔GPU oscillation
```

## 9. B5 — Transfer / Workload Cost

Account for CPU↔GPU transfer and workload-specific cost.

The decision must not be based only on `GPU throughput > CPU throughput`; it must consider total cost.

Example:

```
GPU computation is fast
      +
transfer is expensive
      ↓
overall GPU benefit falls
      ↓
CPU-centric allocation can be correct
```

## 10. B6 — Resource Mode Integration

Map Resource Mode to Scheduler policy.

- Maximum — actively use available resources
- High — leave more system headroom
- Balanced — favor coexistence with normal user workloads
- Gaming — conservative adaptive policy
- Manual — user constrains CPU only; GPU remains AUTO

## 11. B7 — Final Scheduler Gate

### Function

- CPU-only
- GPU OFF
- GPU ON/AUTO
- GPU-unavailable fallback
- CPU-centric behavior for slow GPUs
- Manual CPU constraint
- Gaming conservative policy

### Stability

- CPU↔GPU oscillation is controlled
- hysteresis works
- minimum hold works
- long-running stability

### Correctness

Results must match the CPU reference.

### Performance

Measure real end-to-end throughput against CPU-only. Do not use GPU utilization alone as the success criterion.

### Observability

Where available, each scheduler decision records:

- timestamp
- reason
- CPU capacity
- GPU capacity
- CPU/GPU work share
- recent throughput
- queue state
- selected backend
- fallback state

## 12. Explicitly out of scope

Node B does not complete:

- large-scale Pipeline redesign
- barrier elimination
- worker topology redesign
- hardware video decode
- NVDEC
- additional GPU backends
- complete calibration system
- adaptive video decode planner

These belong to later nodes.

## 13. Completion gate

Node B is ready for final gate review when all four are true:

1. Scheduler decisions are appropriate to runtime conditions.
2. Search correctness is preserved.
3. Long-running execution is stable.
4. End-to-end throughput can be measured.

Build numbers are not preassigned to B1-B7; actual versions represent validated code states.
