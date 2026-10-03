# Implementation Brief — S4 GUI 상세 로그

Status: DESIGN RESET / PRE-IMPLEMENTATION — 2026-10-03 현재 기준 문서. 코드 구현은 이 설계가 확정된 뒤 별도 지시로 수행한다.

관련 상위 설계: docs/architecture/gui-diagnostic-logging.ko.md / .en.md

## 1. 목적

S4의 GUI는 독립적인 benchmark 실행 환경이 아니다. GUI는 사용자가 실제로 이미지/비디오 유사성 검색과 업데이트를 수행하는 사용자 우선 응용 프로그램이다.

[상세 로그]는 실제 검색 작업에 상세 telemetry를 연결하여 pipeline, 이미지/비디오 decode, CPU/GPU 처리, scheduler, queue, transfer, fallback, slow file 및 실패/취소 상태를 기록하고, ChatGPT/OpenCode와 함께 빌드 전후 개선을 분석하기 위한 진단 기능이다.

GUI의 실제 실행 진입점은 기존 검색/업데이트 시작이며, 별도의 Benchmark Run / Stop / Pause workflow를 추가하지 않는다.

## 2. GUI와 CLI의 목적

GUI:
- 실제 사용자 작업 + 진단
- [상세 로그]가 실제 작업의 telemetry를 켠다.
- controlled benchmark suite를 실행하지 않는다.

CLI:
- 개발/검증용 controlled benchmark
- AUTO / CPU 단독 / GPU 최대 활용을 비교한다.
- 동일 dataset과 조건으로 pipeline / decoder / scheduler 개선을 추적한다.

## 3. GUI Resource Control

AUTO / CPU 단독 / GPU 최대 활용은 benchmark mode가 아니라 사용자 실행 자원 전략이다. 정확히 하나만 선택한다.

- AUTO: Adaptive Scheduler가 CPU/GPU 배분을 판단한다.
- CPU 단독: GPU 작업을 사용하지 않는 CPU 중심 실행이다.
- GPU 최대 활용: 가능한 GPU 작업을 적극 활용하되 필수 CPU 작업과 fallback은 유지한다.

CPU Resource Policy는 별도 축이다.

- Maximum
- High
- Balanced
- Gaming
- Manual (10–90%)

관계는 다음과 같다.

실행 전략 + CPU Resource Policy → 실제 ResourcePolicy

## 4. [상세 로그] 명칭과 내부 semantic

GUI 사용자 화면에서는 Benchmark라는 명칭을 사용하지 않는다.

권장 UI:
- KO: 상세 로그
- EN: Detailed Logs

현재 benchTgl_ 등은 의미가 불명확하므로 다음과 같이 정리한다.

- benchTgl_ → logTgl_
- benchmark_ → detailedLogEnabled_
- benchmarkEnabled → telemetryEnabled
- BenchmarkRecorder → TelemetryRecorder
- benchmarkJson 계열 → telemetry/log 결과 API

기존 legacy JSON/schema의 호환성은 내부 명칭 변경과 별개다. 명칭 변경만으로 기존 저장 형식을 불필요하게 깨뜨리지 않는다.

## 5. 공통 TelemetryRecorder

GUI와 CLI가 서로 다른 telemetry 구현을 복제하지 않는다. 공통 계측 계층을 TelemetryRecorder로 정리하고 상위 호출자가 목적을 전달한다.

TelemetryPurpose:
- UserDiagnostic ← GUI
- Benchmark ← CLI

GUI 경로:
Search/Update → ScanWorker → TelemetryRecorder(UserDiagnostic) → 상세 로그

CLI 경로:
--benchmark → BenchmarkSession → BenchmarkRunner → ProductionBenchmarkExecutor → TelemetryRecorder(Benchmark)

핵심 불변식: Benchmark는 Telemetry를 사용할 수 있지만, Telemetry가 Benchmark는 아니다.

## 6. GUI에 존재하지 않는 것

- Benchmark Run 버튼
- Benchmark Stop 버튼
- Benchmark Pause 버튼
- AUTO / CPU / GPU-max 다중 benchmark 선택 UI
- GUI에서 동일 dataset을 세 mode로 자동 반복하는 Suite workflow

기존 일반 검색의 Pause/Cancel semantics는 그대로 유지한다.

## 7. 상세 로그 semantics

[상세 로그]가 켜져도 검색 결과나 판정 semantics를 변경하지 않는다.

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

측정할 수 없는 값은 0으로 추정하지 않는다.

## 8. GUI와 CLI의 저장/표시 분리

GUI 상세 로그는 실제 사용자 작업의 diagnostic evidence이며 Console benchmark의 장기 비교 history와 동일하게 취급하지 않는다.

CLI benchmark는 Benchmark/Console/suite-<suite-id>/ 아래의 suite.json, runs.jsonl, summary.json을 사용한다.

GUI는 Console benchmark history를 자동으로 읽지 않고, Console은 GUI 상세 로그를 benchmark history로 자동 편입하지 않는다.

GUI 상세 로그의 최종 durable path와 report UI는 코드 구현 단계에서 정하되 이 semantic boundary를 바꾸지 않는다.

## 9. 구현 원칙

1. GUI용 별도 benchmark engine을 만들지 않는다.
2. 공통 instrumentation은 TelemetryRecorder에 둔다.
3. GUI는 UserDiagnostic 목적을 사용한다.
4. CLI는 Benchmark 목적을 사용한다.
5. TelemetryRecorder는 search semantics를 변경하지 않는다.
6. GUI Resource Control과 CLI Benchmark Mode는 문서와 코드에서 동일 개념으로 취급하지 않는다.
7. legacy 저장 결과 호환성을 가능한 범위에서 유지한다.

## 10. 종료 조건

문서와 실제 코드가 일치하고, GUI 실제 검색에서 [상세 로그]가 정상적으로 생성되며, CPU/GPU 빌드와 회귀 테스트가 모두 통과하기 전에는 S4를 CLOSED로 선언하지 않는다.
