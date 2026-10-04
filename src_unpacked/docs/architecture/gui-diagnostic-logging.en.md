# GUI Detailed Logging / Diagnostic Logging Architecture

## Purpose

MediaSimilarityFinder GUI is a user-first image/video similarity application, not a benchmark execution tool.

GUI Detailed Logs preserve diagnostic evidence from real user workloads so pipeline, image/video decoding, CPU/GPU processing, Adaptive Scheduler behavior, queues, transfers, fallbacks and bottlenecks can be analyzed across builds.

## 1. Semantic boundary

| Environment | Purpose | AUTO / CPU-only / GPU-max |
| --- | --- | --- |
| GUI | Real user workload + diagnostics | User execution resource strategy |
| CLI | Controlled development benchmark | Comparison experiment groups |

GUI Detailed Logs and the CLI Benchmark are different features with different purposes.

## 2. GUI execution

Search/Update start is the real execution entry.

[Detailed Logs] attaches detailed telemetry to that real workload.

The GUI does not add Benchmark Run, Benchmark Stop, Benchmark Pause, multi-select benchmark modes, or a GUI benchmark Suite workflow.

## 3. GUI Resource Control

Exactly one of AUTO / CPU-only / GPU-max is selected. Implementation: the strategy selector is a single-select dropdown.

- AUTO: Adaptive Scheduler decides CPU/GPU work allocation.
- CPU-only: GPU work is disabled and execution is CPU-centric.
- GPU-max: GPU-capable work is used aggressively while required CPU work and fallback remain.

CPU Resource Policy is a separate axis:
- Maximum
- High
- Balanced
- Gaming
- Manual (10-90%)

## 4. TelemetryRecorder

The shared instrumentation layer is TelemetryRecorder.

TelemetryPurpose:
- UserDiagnostic
- Benchmark

GUI:
Search/Update -> ScanWorker -> TelemetryRecorder(UserDiagnostic) -> Detailed Logs

CLI:
--benchmark -> BenchmarkSession -> BenchmarkRunner -> ProductionBenchmarkExecutor -> TelemetryRecorder(Benchmark)

Benchmark may use Telemetry, but Telemetry is not Benchmark.

## 5. Naming

Recommended internal naming:
- benchTgl_ -> logTgl_
- benchmark_ -> detailedLogEnabled_
- benchmarkEnabled -> telemetryEnabled
- BenchmarkRecorder -> TelemetryRecorder
- benchmarkJson family -> telemetry/log result API

Existing legacy JSON/schema compatibility is a separate concern and should not be broken merely for renaming.

## 6. Long-term invariant

GUI Detailed Logs = diagnostic telemetry from real user work.

CLI Benchmark = controlled developer experiment comparing AUTO / CPU-only / GPU-max.

This document is the top-level semantic reference for subsequent S4/S5/S6 implementation and ChatGPT/OpenCode instructions.
