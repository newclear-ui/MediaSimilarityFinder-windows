# S4 Phase 3-4 Regression / Judgement Record (2026-09-30)

Baseline commit: `4a12fb3` (this phase is not committed)
Version: `0.9.4.43` (single source: `project(... VERSION 0.9.4.43)` in `CMakeLists.txt`)

---

## 1. Files implemented (Phase 3-1 to 3-3)

| Phase | File | Content |
| --- | --- | --- |
| 3-1 | `src/benchmark_gui_store.{h,cpp}` | GUI suite paths, S3 suite lock reuse, per-mode snapshots, atomic replace |
| 3-2 | `gui/benchmark_worker.{h,cpp}` | Worker with a single `BenchmarkRunner::run(request, selectedModes)` call |
| 3-3 | `gui/mainwindow.{h,cpp}` | Three mode checkboxes, run button, stop button, status label, serial-execution gate |
| 3-1..3-4 | `tests/benchmark_gui_store_test.cpp` | Storage layer |
| | `tests/benchmark_worker_test.cpp` | Worker / execution boundary |
| | `tests/ui_benchmark_test.cpp` | Real MainWindow controls and gating |
| | `tests/ui_benchmark_e2e_test.cpp` | **Real engine E2E** (ffmpeg-generated media) |

## 2. Actual verification results (not estimates)

### Benchmark self-checks

| Target | checks |
| --- | --- |
| `benchmark_core_test` (S2) | 63 |
| `benchmark_journal_test` (S3) | 51 |
| `benchmark_store_test` (S3) | 51 |
| `benchmark_integration_test` (S3) | 137 |
| `benchmark_gui_store_test` (S4) | 100 -> **110** (Resource Policy recording/compatibility added) |
| `benchmark_worker_test` (S4) | 26 -> **35** (policy plumbing + fingerprint added) |
| `ui_benchmark_test` (S4) | 34 |
| `ui_benchmark_e2e_test` (S4, real engine) | 31 -> **40** (measured policy/fingerprint added) |

### CTest

- **CPU: 94/94 PASS**
- **GPU: 95/95 PASS**

### S1 CLI regression (confirmed unchanged)

- `--version` → `Media Similarity Finder 0.9.4.43 (CUDA/CPU)`
- `--help` → usage printed
- `--smoke` → `smoke: window created` / `smoke: event loop ok`
- invalid option → `Error: unknown option: --nope` (exit 2)
- `--benchmark` → `Error: unknown option: --benchmark` — **still rejected until S5**

### Values observed in the real GUI E2E

- **CPU build**: `gpu-max.json` status = **`SKIPPED`** (no CUDA backend)
- **GPU build**: `gpu-max.json` status = **`SUCCESS`** (real CUDA execution)

That is S2's `modeAvailable` / effective-mode rule reflected correctly on both builds.
It does not report success when there is no GPU.

## 3. What the GUI E2E actually verifies

`ui_benchmark_e2e_test` uses no fake executor. It generates real media with ffmpeg
(24 images + 1 video), **clicks the real MainWindow controls**, and runs the real
`ProductionBenchmarkExecutor` → `MediaSearchEngine` path.

Verified items and results:

- AUTO / CPU-only / GPU-max all selected → real run → `auto.json`, `cpu.json`, `gpu-max.json` all created
- Re-running a selected subset → the **unselected `cpu.json` is preserved byte for byte**, the selected `auto.json` is replaced
- **No production Index pollution**: the `Index/` entry set was compared by **content** before and after.
  Entries added by MainWindow construction: 0. Entries added by the benchmark run: **0**.
  (Pre-existing entries come from earlier CLI/test runs and are unrelated to the benchmark.)
- The benchmark index is created only under `Benchmark/GUI/<label>_<id>/runtime/run-<id>/<mode>/`
- The scanned folder gains **zero** json/jsonl/Index/db artifacts
- suite lock busy: an external holder owns the suite → the run **does not start** and the existing snapshot is **unchanged**
- Cancel: stopping mid-run → UI returns to idle; run button, scan button and mode checkboxes re-enable
- Mutual exclusion while running: `scan` disabled, `pause` disabled, all three mode checkboxes locked, `stop` enabled

## 4. Unresolved issues (why S4 is not complete)

### 4-1. Resource Policy delivery — **RESOLVED**

Investigation result (from reading the code, not inference):

- `msf::BenchmarkRequest` had **no field** carrying a `ResourcePolicy`; the only such field was
  `distance`.
- `ProductionBenchmarkExecutor::runMode()` constructed a local `MediaSearchEngine` and started from
  `engine.resourcePolicy()`, which is the default-constructed `policy_{}`. There was no channel for
  the GUI's `preset_` / `cpu_` values.

**Resolution (minimal, additive)**

- Added `BenchmarkRequest::resourcePolicy` as `std::optional<ResourcePolicy>`.
  **When empty (the default) the existing S2 behaviour is preserved exactly** — it starts from the
  engine default policy and adjusts only `policy.gpuEnabled` from the mode. Existing S2/S3 callers do
  not set it and are unaffected.
- `ProductionBenchmarkExecutor::runMode()` now starts from `request.resourcePolicy` when present and
  otherwise from `engine.resourcePolicy()` as before. **gpuEnabled is still derived from the mode in
  both cases**, so the S2 rule is intact.
- The GUI passes `req.resourcePolicy = policy_`, i.e. the policy MainWindow had **already resolved**.
  `policy_` is produced only by `msf::make_policy()` inside `resourceChanged()` /
  `customResourceChanged()`, so no preset/CPU interpretation is duplicated and no new policy
  creation path exists.

**Verified (real GUI E2E)**: selecting "Maximum 90%" in the toolbar preset combo and running for real
records `"resourcePolicy":{"mode":1,...,"cpuPercent":90,...}` in the snapshot, and the Balanced
default of 55 is no longer present. The storage test also verifies the recorded block and that
omitting a policy leaves the block absent so existing snapshots stay compatible (110 checks).

**Remaining constraint**: `gpuEnabled` is decided by the benchmark mode (S2 rule retained).
The GUI's `gpuEnabled_` checkbox therefore does not affect AUTO / GPU-max runs. The preset axis and
the mode axis stay separate.

### 4-2. `datasetFingerprint` — **RESOLVED**

Investigation result:

- The hash function itself **does exist**: `msf::computeDatasetFingerprint(root)`
  (`src/dataset_fingerprint.{h,cpp}`), and `MediaSearchEngine` already calls it from
  `src/media_search_engine.cpp` for the legacy recorder.
- `DatasetFingerprint` is a public-member struct (`state` / `fingerprint` / `fileCount` / `totalBytes`),
  so **a direct accessor to the computed canonical value already exists**; no accessor was needed.

**Resolution**

- When no fingerprint is supplied, the worker uses
  `msf::computeDatasetFingerprint(request_.sourceRoot).fingerprint` verbatim.
  **No new hash and no new serialisation format were invented**, and no other fields were
  concatenated.
- The content-reading walk runs on the **worker thread** so the GUI is not blocked; it is the same
  existing call path a normal scan uses.
- The empty-string state is no longer a normal completion state. It can only be empty when the root
  is missing or unreadable, in which case `DatasetFingerprint::state` is `not_available` / `failed`.

**Verified (real GUI E2E)**: a 64-character lowercase hex value is recorded in the snapshot and is
**exactly equal** to `computeDatasetFingerprint(root).fingerprint`, e.g.
`3793e510219a2f85312aad725d7fcf9ff6f12386bf17cfe9b0194d2871598c1a`

### 4-3. `selectedBenchModes()` is private

It was not promoted to public API. Current coverage:

- `ui_benchmark_test`: indirectly verifies the UI gate accepts all 7 combinations
- `benchmark_worker_test`: verifies the runner executes a supplied vector in file-level order

The direct mode-combination → runner-vector correspondence is therefore verified indirectly.
If direct verification is wanted later, adding public API is worse than verifying through the
real execution path and signals.

### 4-4. Progress display limitation (an S2 contract fact, not a defect)

S2 emits a hook only when a case **completes**, and exposes no intra-case progress hook, so the
in-flight file/mode cannot be known. The UI therefore shows **"Completed k/N · last <file>"**, and
both the code comments and the string say so. Nothing is guessed.

## 5. Remaining items and S5 entry conditions

Resource Policy delivery (4-1) and datasetFingerprint (4-2) are resolved, so S4 can be recorded as
CLOSED. Two items are however **deliberately left open**:

- the O(N²) folder walk is retained (an S2 characteristic; S3 is a storage layer and does not touch
  the scan)
- `gpuEnabled` is decided by the benchmark mode → the GUI `gpuEnabled_` checkbox does not affect
  AUTO / GPU-max runs. The S2 rule was kept as instructed. Whether users accept this needs
  confirmation.

`selectedBenchModes()` stays private (verified indirectly, 4-3).

Three conditions for entering S5:

1. Decide whether the O(N²) walk is accepted as is, or a file-count cap / prior warning is added
2. Confirm that GPU-max recording as `SKIPPED` on a CPU build is sufficient wording
3. If the GUI GPU toggle should affect benchmark runs, decide in advance whether the S2 rule changes
