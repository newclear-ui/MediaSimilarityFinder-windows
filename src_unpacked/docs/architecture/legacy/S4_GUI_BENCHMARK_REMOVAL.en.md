# S4 GUI Benchmark worker/storage removal record (provenance)

- Snapshots of the removed state: `gui/benchmark_worker.h/.cpp`,
  `src/benchmark_gui_store.h/.cpp` and the related tests
  (`S4_GUI_BENCHMARK_WORKER_TEST.cpp`, `S4_GUI_BENCHMARK_STORAGE_TEST.cpp`,
  `S4_GUI_BENCHMARK_E2E_TEST.cpp`).
- Reason: S4 semantic reset — the GUI is not a benchmark runner, and
  `[Detailed Logs]` is a diagnostic feature that attaches
  `TelemetryRecorder(UserDiagnostic)` to the real search path. The dormant
  wiring through which the GUI called `BenchmarkRunner` and the
  `Benchmark/GUI` snapshot storage were removed from the active source.
- The CLI `--benchmark` → `BenchmarkSession` → `BenchmarkRunner` →
  `ProductionBenchmarkExecutor` path is untouched and independent of these files.
- Nothing in this directory is built (no CMake references).
