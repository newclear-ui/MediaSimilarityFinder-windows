# S4 GUI Benchmark worker/storage 제거 기록 (provenance)

- 제거 커밋 이후 상태의 snapshot: `gui/benchmark_worker.h/.cpp`,
  `src/benchmark_gui_store.h/.cpp` 및 관련 테스트
  (`S4_GUI_BENCHMARK_WORKER_TEST.cpp`, `S4_GUI_BENCHMARK_STORAGE_TEST.cpp`,
  `S4_GUI_BENCHMARK_E2E_TEST.cpp`).
- 제거 이유: S4 semantic reset — GUI는 benchmark 실행 도구가 아니며
  `[상세 로그]`는 `TelemetryRecorder(UserDiagnostic)`를 실제 검색 경로에
  연결하는 진단 기능이다. GUI가 `BenchmarkRunner`를 호출하는 dormant
  배선과 `Benchmark/GUI` 스냅샷 저장소는 active source에서 제거했다.
- CLI `--benchmark` → `BenchmarkSession` → `BenchmarkRunner` →
  `ProductionBenchmarkExecutor` 경로는 그대로 유지되며 이 파일들과 무관하다.
- 이 디렉터리의 파일은 빌드되지 않는다 (CMake 참조 없음).
