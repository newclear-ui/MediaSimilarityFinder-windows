# GUI 상세 로그 / Diagnostic Logging Architecture

## 목적

MediaSimilarityFinder의 GUI는 benchmark 실행 도구가 아니라 실사용자 중심의 이미지/비디오 유사성 검색 프로그램이다.

GUI 상세 로그의 목적은 실제 사용자 작업 동안 pipeline, image/video decode, CPU/GPU 처리, Adaptive Scheduler, queue, transfer, fallback 및 병목을 관찰하고 빌드 전후 개선을 분석할 수 있는 진단 증거를 남기는 것이다.

## 1. GUI와 CLI의 semantic boundary

| 환경 | 목적 | AUTO / CPU / GPU-max의 의미 |
| --- | --- | --- |
| GUI | 실제 사용자 작업 + 진단 | 사용자 실행 자원 전략 |
| CLI | 개발/검증용 controlled benchmark | 비교 실험군 |

GUI 상세 로그와 CLI Benchmark는 서로 다른 목적의 기능이다.

## 2. GUI 실행

검색/업데이트 시작이 실제 실행 진입점이다.

[상세 로그]가 켜져 있으면 실제 검색 경로에 상세 telemetry를 연결한다.

GUI에는 다음을 추가하지 않는다:
- Benchmark Run
- Benchmark Stop
- Benchmark Pause
- AUTO / CPU / GPU-max 다중 benchmark 선택
- 동일 dataset의 세 mode 자동 반복 Suite

## 3. GUI Resource Control

AUTO / CPU 단독 / GPU 최대 활용은 사용자 실행 자원 전략이며 정확히 하나만 선택한다.

- AUTO: Adaptive Scheduler가 CPU/GPU 작업 배분을 판단한다.
- CPU 단독: GPU 작업을 사용하지 않는 CPU 중심 실행이다.
- GPU 최대 활용: 가능한 GPU 작업을 적극적으로 사용하되 필수 CPU 작업과 fallback은 유지한다.

CPU Resource Policy는 별도 축이다:
- Maximum
- High
- Balanced
- Gaming
- Manual 10–90%

관계: 실행 전략 + CPU Resource Policy → 실제 ResourcePolicy

## 4. [상세 로그]와 TelemetryRecorder

사용자 화면의 명칭은 KO '상세 로그', EN 'Detailed Logs'로 한다. GUI에서 Benchmark라는 명칭을 사용하지 않는다.

공통 계측 계층은 TelemetryRecorder로 정리한다.

TelemetryPurpose:
- UserDiagnostic ← GUI
- Benchmark ← CLI

GUI:
Search/Update → ScanWorker → TelemetryRecorder(UserDiagnostic) → 상세 로그

CLI:
--benchmark → BenchmarkSession → BenchmarkRunner → ProductionBenchmarkExecutor → TelemetryRecorder(Benchmark)

핵심 불변식: Benchmark는 Telemetry를 사용할 수 있지만, Telemetry가 Benchmark는 아니다.

## 5. 내부 명칭

- benchTgl_ → logTgl_
- benchmark_ → detailedLogEnabled_
- benchmarkEnabled → telemetryEnabled
- BenchmarkRecorder → TelemetryRecorder
- benchmarkJson 계열 → telemetry/log 결과 API

기존 legacy JSON/schema는 내부 명칭과 별개로 호환성을 유지한다.

## 6. GUI 로그의 관측 범위

- total/stage wall time
- image decode
- video open/metadata/decode
- decodedFrames / sampledFrames
- CPU/GPU backend
- scheduler decisions
- queue / transfer
- fallback
- slow file
- completion/cancel/failure

측정되지 않은 값은 0으로 추정하지 않는다.

## 7. 저장 경계

GUI 상세 로그는 실제 사용자 작업의 diagnostic evidence다. Console benchmark의 durable comparison history가 아니다.

Console benchmark는 Benchmark/Console/suite-<suite-id>/suite.json, runs.jsonl, summary.json을 사용한다.

GUI는 Console journal을 자동으로 읽지 않으며 Console은 GUI 상세 로그를 benchmark population으로 자동 편입하지 않는다.

GUI 상세 로그의 최종 durable path는 구현 단계에서 결정하되 이 semantic boundary는 변경하지 않는다.

## 8. 장기 불변식

GUI 상세 로그 = 실제 사용자 작업의 진단 telemetry

CLI Benchmark = 동일 조건에서 AUTO / CPU / GPU-max를 비교하는 개발용 실험

이 문서는 이후 S4/S5/S6 구현과 ChatGPT/OpenCode 지시의 최상위 semantic reference다.
