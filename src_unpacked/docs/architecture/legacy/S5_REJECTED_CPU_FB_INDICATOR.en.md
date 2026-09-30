# S5 rejected design record — Console `CPU FB` indicator

Status: **REJECTED** — this item is not implemented in the product, and it was not
implemented-then-removed.

---

## 1. What it was

`CPU FB` was a display item the Console benchmark mockup
(`uimock/benchmark-console-mockup.html`) put on each mode result row in the
CURRENT FILE area. Its intent was to make **whether a CPU fallback occurred**
visible for each mode execution.

Mockup original:

```text
AUTO     DONE    Time 12.41 ms  Policy Adaptive        Backend Scheduler  CPU FB NO
CPU      DONE    Time 18.08 ms  Policy Balanced        Backend Software   Workers 10
GPU-MAX  RUNNING Time  7.32 ms  Policy GPU Preferred  Backend CUDA       CPU FB NO
```

## 2. Why it is not adopted

The S2 benchmark execution/storage contract has **no such indicator**.

- `BenchmarkModeResult` records `requestedMode` / `effectiveMode` / `status` /
  `started` / `completed` / `elapsedMs` / `summary` / `errorMessage`.
- `errorMessage` is a human-readable message, not a structured fallback verdict.
- `BenchmarkScanSummary` does not carry CPU fallback either.

Computing or judging `CPU FB` from the current measured data would therefore
**arbitrarily extend the meaning of benchmark results**.

## 3. Wrong alternatives that were considered (none implemented)

| Alternative | Why not |
| --- | --- |
| Reinterpret `effectiveMode == Cpu` as `CPU FB YES` | `effectiveMode` is the **backend resolution of the requested mode**. That is a different statement from "a fallback occurred". A GPU-max request resolving to CPU is not the same event as CPU assist during GPU processing. Reinterpreting merges two meanings. |
| Infer it from `gpuEnabled == false` | `gpuEnabled` is derived from the **requested mode** (S2 rule) and is unrelated to whether a fallback actually happened at runtime. |
| Detect it by searching `errorMessage` | Depending on message text couples the display to string formatting rather than to a contract. |
| Implement temporarily in the renderer, then remove | Forbidden by the directive. **It is never implemented in the product.** |

## 4. Decision applied to the product

- The S5 Console renderer **does not display a `CPU FB` column**.
- No new measurement or estimation logic is added.
- The benchmark result model and the journal schema are not changed.

## 5. Future revisit condition

Re-review only when one of the following holds; until then the status stays
`REJECTED`.

1. **A fallback verdict field is formally added to the S2/S3 contract.** If
   `BenchmarkModeResult` or `SearchReport` gains a structured CPU fallback
   indicator and the journal schema is extended alongside it, that value can be
   displayed directly. The **contract extension must come first**, not the
   display.
2. **GPU fallback is promoted to a measured indicator.** If the Node F /
   Adaptive Scheduler work settles on a fallback count or ratio as a real
   metric, `CPU FB` may be re-adopted as a *display name* for it. Using the name
   before the metric exists is still a meaning extension and stays forbidden.
3. **The column is redefined as a non-measured display of user input** (for
   example the selected modes). That would be a different indicator of the same
   name and is out of scope for this record.

Any revisit must re-check the absence evidence in sections 2-3; it does not become
valid merely because it is convenient.

## 6. Cross-references

- Directive: `C:\project\지시\S5 A2 CPU FB 결정 정정.md`
- Original mockup: `uimock/benchmark-console-mockup.html`
- Related contract: `docs/architecture/benchmark-telemetry-roadmap.ko.md` / `.en.md`
  (mode definitions — why GPU-max keeps CPU fallback)
- Worklog index: Performance / Tuning Experiment Index row `S5-CPUFB` in
  `docs/worklog/0.9.4.ko.md` / `.en.md`
- S5 implementation brief: `docs/implementation-briefs/S5-console-benchmark-execution.ko.md`
  (to be created) — states `CPU FB = REJECTED`
