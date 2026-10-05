# Implementation Brief — S4 GUI Detailed Logging

Status: IMPLEMENTED / VERIFICATION IN PROGRESS — current product baseline 0.9.4.45 (`619f74a`). The design reset remains the semantic contract; the implementation evidence is anchored at 0.9.4.44 (`6ada90f`). This brief records the current contract/state.

Related architecture: docs/architecture/gui-diagnostic-logging.ko.md / .en.md

## 1. Purpose

S4 GUI is not a standalone benchmark execution environment. It is a user-first application in which users perform real image/video similarity search and update operations.

[Detailed Logs] attaches detailed telemetry to the real search workload so pipeline, image/video decoding, CPU/GPU work, scheduler, queue, transfer, fallback, slow-file and completion/failure/cancellation information can be analyzed with ChatGPT/OpenCode across builds.

The real GUI execution entry remains Search/Update; no separate Benchmark Run / Stop / Pause workflow is added.

## 2. GUI and CLI purposes

GUI:
- Real user workload + diagnostics
- [Detailed Logs] enables telemetry for that workload.
- No controlled benchmark Suite is launched.

CLI:
- Controlled development/verification benchmark
- Compares AUTO / CPU-only / GPU-max
- Tracks pipeline / decoder / scheduler improvements on the same dataset and conditions.

## 3. GUI Resource Control

AUTO / CPU-only / GPU-max is not a benchmark mode; it is the user's execution resource strategy. Exactly one is selected. Implementation: the strategy selector is a single-select dropdown.

- AUTO: Adaptive Scheduler가 CPU/GPU 배분을 판단한다.
- CPU 단독: GPU 작업을 사용하지 않는 CPU 중심 실행이다.
- GPU 최대 활용: 가능한 GPU 작업을 적극 활용하되 필수 CPU 작업과 fallback은 유지한다.

CPU Resource Policy는 별도 축이다.

- Maximum
- High
- Balanced
- Gaming
- Manual (10–90%)

Relationship:

Execution strategy + CPU Resource Policy → effective ResourcePolicy

## 4. [Detailed Logs] naming and internal semantics

The GUI user interface does not use Benchmark as this feature name.

Recommended UI:
- KO: 상세 로그
- EN: Detailed Logs

The current benchTgl_ naming is ambiguous and should move toward the following semantic names.

- benchTgl_ → logTgl_
- benchmark_ → detailedLogEnabled_
- benchmarkEnabled → telemetryEnabled
- BenchmarkRecorder → TelemetryRecorder
- benchmarkJson 계열 → telemetry/log 결과 API

기존 legacy JSON/schema의 호환성은 내부 명칭 변경과 별개다. 명칭 변경만으로 기존 저장 형식을 불필요하게 깨뜨리지 않는다.

## 5. Shared TelemetryRecorder

GUI and CLI must not duplicate telemetry implementations. Shared instrumentation is organized around TelemetryRecorder and its purpose is supplied by the upper layer.

TelemetryPurpose:
- UserDiagnostic ← GUI
- Benchmark ← CLI

GUI 경로:
Search/Update → ScanWorker → TelemetryRecorder(UserDiagnostic) → 상세 로그

CLI 경로:
--benchmark → BenchmarkSession → BenchmarkRunner → ProductionBenchmarkExecutor → TelemetryRecorder(Benchmark)

Key invariant: Benchmark may use Telemetry, but Telemetry is not Benchmark.

## 6. GUI UI that does not exist

- Benchmark Run 버튼
- Benchmark Stop 버튼
- Benchmark Pause 버튼
- AUTO / CPU / GPU-max 다중 benchmark 선택 UI
- GUI에서 동일 dataset을 세 mode로 자동 반복하는 Suite workflow

Existing normal-search Pause/Cancel semantics remain unchanged.

## 7. Detailed-log semantics

Enabling [Detailed Logs] must not change search results or verdict semantics.

관측 대상 예:
- total/stage wall time
- image decode
- video open/metadata/decode
- decodedFrames / sampledFrames
- CPU/GPU backend
- scheduler decisions
- queue / transfer
- fallback
- slow file
- completed/cancelled/failed state

Unmeasured values are never guessed as zero.

## 8. GUI vs CLI storage/presentation

GUI detailed logs are diagnostic evidence from real user work and are not treated as the Console benchmark's long-term comparison history.

CLI benchmark uses suite.json, runs.jsonl and summary.json under Benchmark/Console/suite-<suite-id>/.

GUI does not automatically read Console benchmark history, and the Console does not automatically ingest GUI detailed logs as benchmark history.

The final durable path and report UI for GUI detailed logs are fixed during implementation without changing this semantic boundary.

## 9. Implementation rules

1. GUI용 별도 benchmark engine을 만들지 않는다.
2. 공통 instrumentation은 TelemetryRecorder에 둔다.
3. GUI는 UserDiagnostic 목적을 사용한다.
4. CLI는 Benchmark 목적을 사용한다.
5. TelemetryRecorder는 search semantics를 변경하지 않는다.
6. GUI Resource Control과 CLI Benchmark Mode는 문서와 코드에서 동일 개념으로 취급하지 않는다.
7. legacy 저장 결과 호환성을 가능한 범위에서 유지한다.

## 10. Exit condition

S4 is not CLOSED until the documents and code agree, [Detailed Logs] works on the real GUI search path, and CPU/GPU builds and regression tests pass.

---

## Current status (2026-10-03, at cf126c4)

- S4 implementation: PASS. GUI visible UI verified directly by the user: PASS.
- Functional Detailed Logs acceptance is DEFERRED: the real Search/Update -> Detailed Logs generation/display/save path is code-connected and regression-covered, but the final product acceptance audit has not yet been completed.
- S4 status: verification in progress (not CLOSED). The section 10 exit condition above stands as the design contract.
