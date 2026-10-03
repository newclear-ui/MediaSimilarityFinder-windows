# Benchmark and Runtime Telemetry Roadmap

## 목적

0.9.4.x의 CPU/GPU Adaptive Architecture는 기존 benchmark schema와 단순 GPU duty 측정만으로는 충분히 검증할 수 없다.

벤치마크는 단순히 총 시간을 보여주는 기능이 아니라 다음을 함께 기록하는 진단 계층으로 승격한다.

1. 실제 검색 성능
2. CPU/GPU/decoder의 실제 처리량
3. Adaptive Scheduler가 특정 작업 배분을 선택한 근거
4. 실패, 폴백, 미측정 항목을 포함한 실행 상태

따라서 0.9.4.x에서는 Benchmark / Telemetry / Scheduler Diagnostics를 하나의 일관된 측정 체계로 재설계한다.

## 2. 현재 benchmark의 한계

0.9.3.19 benchmark는 wall/stage time, image/video count, GPU hash count, GPU batch time, resource sampling, video GPU/fallback count 등에 의존했다. 0.9.4.0(Node A)은 기존 키를 유지하면서 benchmark schemaVersion 1, runId, 측정 상태, stage 객체, decoded/sampled 프레임 분리, scheduler/calibration 구조, 취소/부분/파일 진행 기록을 추가한다.

상세 benchmark가 비활성화된 실행에서는 영상 상세 계측이나 resource sampling이 실행되지 않을 수 있다. 따라서 JSON의 0은 실제로 0건 처리 또는 0% 사용을 의미하지 않을 수 있다.

0.9.4.x에서는 이 모호성을 제거한다.

**측정되지 않은 값은 numeric zero로 기록하지 않는다.**

## 3. 측정 상태

중요한 측정값은 최소 다음 상태를 구분한다.

- measured
- not_measured
- not_available
- partial
- failed
- fallback

## 4. Benchmark schema version

benchmark JSON에는 독립적인 schemaVersion을 둔다.

권장 메타데이터:

- schemaVersion
- benchmarkRunId
- completed
- completionReason
- appVersion
- engineVersion
- databaseVersion

## 5. 검색 단계 계측

Global stages:

- file enumeration / walk
- incremental classification
- image analysis
- video analysis
- candidate index build
- similarity / verification
- persistence
- revalidation
- total wall time

Image stages:

- decode
- normalization
- resize/conversion
- CPU fingerprint
- GPU fingerprint
- crop fingerprint
- GPU batch queue wait
- GPU submit/kernel time
- GPU transfer time
- fallback

Video stages:

- container open
- metadata
- cache lookup
- cache load
- decoder initialization
- seek/setup
- decode
- sampled frames
- decoded frames
- kept frames
- frame conversion
- resize
- variance filter
- scene detection
- fingerprint
- crop fingerprint
- cache save
- queue wait

특히 sampledFrames와 decodedFrames를 별도로 기록한다.

## 6. CPU/GPU Scheduler telemetry

Adaptive Scheduler 실행 중 다음 정보를 기록한다.

- initial CPU capacity estimate
- initial GPU capacity estimate
- current effective CPU capacity
- current effective GPU capacity
- CPU work share
- GPU work share
- CPU queue depth
- GPU queue depth
- CPU queue wait
- GPU queue wait
- scheduler adjustment count
- scheduler increase/decrease events
- throttling events
- external-load throttling events
- hysteresis state
- selected backend
- backend fallback count

단순히 GPU 70%라고 기록하는 것이 아니라 왜 그 배분을 선택했는지 재현 가능한 근거를 남긴다.

## 7. Hardware capability 기록

검색 시작 시 가능한 범위에서 다음을 기록한다.

CPU:
- vendor
- model
- logical threads
- relevant SIMD capability
- baseline profile id

GPU:
- vendor
- model
- integrated/discrete
- VRAM
- API/backend capability
- driver
- selected backend
- candidate backends
- capability failures

Video decode:
- codec
- profile
- pixel format
- bit depth
- resolution
- attempted backend
- selected backend
- fallback reason

## 8. Calibration benchmark

장시간 별도 벤치마크를 강제하지 않고 짧은 calibration과 검색 초기 실제 작업을 결합한다.

측정 대상:

- CPU fingerprint throughput
- GPU fingerprint throughput
- CPU/GPU transfer cost
- resize/conversion throughput
- CPU decode throughput
- hardware decode throughput
- queue latency

권장 이벤트:

- calibration.started
- calibration.completed
- calibration.durationMs
- calibration.confidence

## 9. INI profile과 benchmark의 관계

INI는 비교적 안정적인 baseline을 저장한다.

Benchmark는 이번 실행의 실제 관측값을 저장한다.

추적 관계:

INI baseline → initial scheduler estimate → live measurements → scheduler adjustments → final measured profile

프로파일이 갱신되면 old profile id, new profile id, reason, confidence change를 기록한다.

## 10. Runtime resource sampling

기존 250ms sampling 개념은 유지할 수 있지만 의미를 확장한다.

가능한 경우:

- CPU process/system load
- memory
- GPU utilization
- GPU memory
- GPU active/idle
- video decode activity
- disk read/write
- queue depth
- scheduler state

플랫폼이 제공하지 않는 항목은 not_available로 남긴다.

## 11. Human-readable / machine-readable 분리

사용자에게 보이는 요약은 간결하게 유지한다.

최소 표시:

- 총 검색 시간
- 분석 파일 수
- image/video 시간
- 평균 throughput
- CPU 평균/최대
- GPU active time
- selected GPU backend
- GPU fallback count
- video decode backend
- slowest file
- dominant bottleneck stage
- calibration 상태
- scheduler throttling count
- completed/cancelled/failed 상태

세부 진단은 JSON에 저장한다.

## 12. Benchmark UI

기존 단순 GPU duty 표시는 다음 개념으로 확장한다.

예:
GPU: AUTO · CUDA · Active 42% · Fallback 3
CPU: Balanced · 8 workers · Adaptive
Decoder: NVDEC H.264 / Software HEVC

필요한 진단 정보는 보여주되 구현 내부 세부사항을 과도하게 노출하지 않는다.

## 13. 취소와 부분 실행

단일 completed boolean만으로는 부족하다.

권장 정보:

- completed
- cancelled
- paused
- failed
- failed_stage
- completion_reason
- files_started
- files_completed
- files_remaining

부분 benchmark는 partial 상태가 명확히 표시되어야 한다.

## 14. Slow-file diagnostics

기존 top slow files를 유지하면서 가능한 경우 다음을 추가한다.

- path
- media type
- size
- duration
- resolution
- codec
- decoder backend
- backend fallback
- total analysis time
- decode time
- conversion/resize time
- fingerprint time
- queue wait
- scheduler state
- error/fallback reason

특수 인코딩이나 특정 codec/profile 때문에 특정 파일만 느려지는 문제를 식별할 수 있어야 한다.

## 15. Backend fallback diagnostics

단순 gpuFallback=1만 남기지 않는다.

예상 reason code:

- GPU_BACKEND_UNAVAILABLE
- CODEC_UNSUPPORTED
- PROFILE_UNSUPPORTED
- PIXEL_FORMAT_UNSUPPORTED
- BIT_DEPTH_UNSUPPORTED
- INITIALIZATION_FAILED
- SEEK_FAILED
- FRAME_MAP_FAILED
- DECODE_ERROR
- TRANSFER_ERROR
- RUNTIME_ERROR
- OUT_OF_MEMORY
- PERFORMANCE_NOT_BENEFICIAL
- EXTERNAL_LOAD_THROTTLE

## 16. Benchmark를 통한 아키텍처 검증

0.9.4.x benchmark는 다음 질문에 답할 수 있어야 한다.

A. 영상이 느린 원인이 decode인가?
→ decodedFrames / sampledFrames / decodeMs

B. GPU가 실제로 일을 하는가?
→ selected backend / GPU work / kernel time / transfer time

C. GPU 가속이 전체 검색을 실제로 단축했는가?
→ CPU-only baseline vs GPU-assisted throughput

D. 저사양 GPU가 CPU보다 빠른가?
→ workload-specific CPU/GPU throughput

E. 외부 부하에서 scheduler가 제대로 감속했는가?
→ throttling events / resource samples

F. hardware decode fallback이 정확성을 유지했는가?
→ fallback reason + CPU/reference parity

G. INI profile이 다음 실행에서 유용했는가?
→ initial estimate vs measured throughput

## 17. Regression benchmark

표준 회귀 시나리오:

- CPU-only
- GPU OFF
- GPU ON / AUTO
- GPU available but acceleration not beneficial
- low-end GPU simulation
- high-end GPU
- external CPU load
- external GPU load
- hardware decode success
- hardware decode fallback
- mixed image/video workload
- cancelled scan
- partial scan

우선순위는 단일 GPU utilization이 아니라 end-to-end throughput, accuracy parity, fallback correctness, system stability다.

## 18. Development Roadmap 게이트에서의 benchmark 역할

Benchmark redesign은 특정 빌드 번호에 묶지 않습니다. Roadmap 각 node가 종료되기 위해 필요한 관측값을 해당 node와 함께 구현합니다.

A Foundation
  └─ schema / measurement state / stage instrumentation
        ↓
B Adaptive Scheduler
  └─ scheduler decisions / work share / throttling
        ↓
C Calibration
  └─ calibration / profile confidence / baseline-vs-observed
        ↓
D Pipeline / Queue
  └─ queue depth / wait / batch / transfer / overlap
        ↓
E Adaptive Video Decode
  └─ decoded-vs-sampled / seek / planner decision
        ↓
F Hardware Decode
  └─ backend capability / success / fallback reason
        ↓
G Additional Backends
  └─ capability / parity / fallback / availability
        ↓
H Validation
  └─ end-to-end regression evidence

하나의 benchmark 구조 변경이 여러 개발 버전에 걸쳐 이어질 수 있습니다. 중요한 것은 버전 숫자가 아니라 현재 Roadmap node의 검증 가능성입니다.

## 19. 구현 원칙



벤치마크는 장식 기능이 아니다.

- 측정되지 않은 값을 0으로 만들지 않는다.
- GPU utilization 하나로 GPU 성능을 판단하지 않는다.
- benchmark overhead가 의미 있으면 기록한다.
- instrumentation을 켜고 끄더라도 검색 의미가 달라지면 안 된다.
- instrumentation이 검색 정확성에 영향을 주면 안 된다.
- JSON schema가 변경되면 schemaVersion을 올린다.


## 20. 최상위 Semantic 분리 — GUI 상세 로그 / CLI Benchmark

### GUI
- GUI는 controlled benchmark를 실행하지 않는다.
- 검색/업데이트 시작이 실제 실행 진입점이다.
- [상세 로그]는 실제 사용자 작업에 diagnostic telemetry를 연결한다.
- GUI에는 Benchmark Run / Stop / Pause UI가 없다.
- AUTO / CPU 단독 / GPU 최대 활용은 사용자 실행 자원 전략이며 정확히 하나를 선택한다.
- Maximum / High / Balanced / Gaming / Manual은 CPU Resource Policy의 별도 축이다.

### CLI
- --benchmark는 개발/검증용 controlled benchmark다.
- AUTO / CPU 단독 / GPU 최대 활용이 비교 실험군이다.
- 기본 Suite는 세 mode를 비교하며 파일 단위 AUTO → CPU → GPU-max 순서를 유지한다.
- 동일 dataset/media scope/build 조건으로 pipeline / decoder / scheduler 개선을 추적한다.

## 21. 공통 Telemetry 계층

TelemetryRecorder를 공통 계측 계층으로 사용하고 상위 호출자가 목적을 전달한다.

TelemetryPurpose
- UserDiagnostic ← GUI
- Benchmark ← CLI

Benchmark는 Telemetry를 사용할 수 있지만 Telemetry가 Benchmark는 아니다.

GUI 경로:
Search/Update → ScanWorker → TelemetryRecorder(UserDiagnostic) → 상세 로그

CLI 경로:
--benchmark → BenchmarkSession → BenchmarkRunner → ProductionBenchmarkExecutor → TelemetryRecorder(Benchmark)

## 22. CLI Benchmark Run / Suite / Media Scope

Benchmark mode:
- AUTO
- CPU 단독
- GPU 최대 활용

GPU 최대 활용은 GPU-only가 아니며 필수 CPU 작업과 fallback을 유지한다.

Media Scope:
- --media images
- --media videos
- --media all

mode와 media scope는 독립 축이다.

## 23. GUI 상세 로그 보존 / Console Benchmark 보존

GUI 상세 로그는 실제 사용자 작업의 diagnostic evidence다. 최종 durable path는 S4 구현에서 확정한다.

Console:
Benchmark/Console/suite-<suite-id>/
  suite.json
  runs.jsonl
  summary.json

runs.jsonl은 recovery source이며 summary.json은 파생 결과다. GUI와 Console은 서로의 결과를 자동으로 history로 편입하지 않는다.

## 24. CLI Benchmark 격리와 공정성

- Benchmark runtime/index/cache는 normal Search Index와 분리한다.
- mode 간 분석용 중간 결과를 공유하지 않는다.
- 동일 Suite의 비교 Run은 dataset fingerprint, sourceRoot, mediaScope 및 관련 execution condition이 일치해야 한다.
- OS filesystem cache와 process isolation은 완전 통제되지 않으며 명시적인 측정 한계로 남긴다.
- GUI 상세 로그는 Console benchmark population에 자동 편입하지 않는다.

## 25. Resource Policy 경계

CLI benchmark mode와 CPU Resource Policy는 서로 다른 축이다. 현재 S5는 Balanced를 기본 실행 조건으로 사용한다. 향후 CLI에서 resource option을 추가하더라도 mode와 합치지 않는다.

GUI Resource Policy는 사용자가 다른 PC 작업과 병행하기 위해 CPU 점유를 제어하는 사용자 기능이며 CLI benchmark mode와 동일 개념이 아니다.

## 26. GUI / Console 기능 대응

| 항목 | GUI | Console |
| --- | --- | --- |
| 실제 사용자 검색 | 핵심 | headless scan 지원 |
| 상세 로그 | 핵심 옵션 | benchmark telemetry 일부 |
| AUTO / CPU / GPU-max | 사용자 실행 전략, 하나 선택 | 비교 benchmark mode |
| CPU Resource Policy | 지원 | benchmark 조건 축 |
| 독립 Benchmark 실행 버튼 | 없음 | --benchmark |
| 장기 journal | 아님 | 기본 |
| 사용자 작업 진단 | 핵심 목적 | 부차적 |
| 개발 regression 비교 | 수동 분석 자료 | 핵심 목적 |

## 27. QuickLook 도움말

기존 설계를 유지하며 benchmark semantic과 무관하다.

## 28. S4~S8 구현 책임

- S4 GUI Detailed Logging: 상세 로그 명칭/semantic, TelemetryRecorder 경계, UserDiagnostic purpose, legacy compatibility, real GUI validation
- S5 Console Benchmark Execution: --benchmark, --mode, --suite, Console renderer/journal, Benchmark purpose
- S6 Data-mining Automation: Console benchmark journal 분석 및 build comparison
- S7 help/usability
- S8 full verification

S4와 S5는 공통 계측을 사용할 수 있지만 동일 기능으로 간주하지 않는다.

## 29. Console Benchmark 최종 실행/터미널 UI 확정안 — 2026-09-30

이 절은 기존 S0~S8 개요를 보완하는 **최종 설계 결정**이다. 이후 구현 시 본 절의 계약을 우선한다.

### 29.1 파일 단위 실행 순서

Suite 전체를 mode별로 한 번에 실행하지 않고, **파일 단위로 다음 순서를 반복**한다.

~~~text
파일 준비/식별
  → AUTO
  → CPU 단독
  → GPU 최대화
  → 해당 파일의 결과 즉시 journal 기록
  → 다음 파일
~~~

- 파일 식별/입력 준비는 공통으로 사용할 수 있다.
- 그러나 실제 분석, decode, intermediate 결과는 mode 간 공유하지 않는다. 공유하면 CPU/AUTO/GPU 비교 조건이 오염될 수 있다.
- 각 mode의 실행 context는 독립적으로 취급한다.
- 한 파일의 세 mode가 모두 끝난 뒤 즉시 결과를 append-only journal에 기록한다.

### 29.2 중단 및 부분 결과 보존

- Interactive Console에서는 Ctrl+C를 취소 신호로 사용한다.
- 현재 원자적 작업을 안전하게 마무리한 뒤 종료한다.
- 완료된 file/mode 결과는 이미 journal에 존재하므로 프로세스 중단 후에도 부분 Suite를 복구할 수 있어야 한다.
- 종료 시 cancelled, completionReason, filesCompleted, filesRemaining, runsCompleted를 기록한다.
- 터미널 표시 내용은 데이터 원본이 아니며 journal/summary가 canonical source다.

### 29.3 CPU Resource Policy

Console benchmark는 기존 Resource Policy를 재사용한다.

~~~text
--resource maximum|high|balanced|gaming|manual
--cpu-percent 10..90
~~~

- 기본 benchmark resource는 **Balanced (55%)**를 권장한다.
- Maximum/High 등은 사용자가 명시적으로 선택할 수 있다.
- Balanced 측정값에서 Maximum 결과를 단순 선형 보간하여 실제 benchmark 결과로 취급하지 않는다.
- Maximum은 필요하면 실제 측정한다. 예상값은 별도의 projection으로 표시할 수 있으나 첫 구현에서는 benchmark 결과와 섞지 않는다.
- CPU-only와 GPU-max benchmark의 비교 조건은 실행 중 임의의 자동 throttling으로 변경하지 않는다.

### 29.4 Console 고정 헤더

Interactive terminal의 상단은 **3줄 + 구분선**을 기본으로 한다. 각 줄은 자동 줄바꿈하지 않는다.

~~~text
MediaSimilarityFinder Benchmark
================================================================================================================
Target : D:\\Media\\TestSet                  | Scope : ALL       | Files : IMG 12/640  VID 3/207
Mode   : AUTO → CPU → GPU-MAX              | CPU : Balanced 55% | GPU : ON / CUDA
Distance : 8                               | Suite ID : 20260930-0801-01 | Build : 0.9.4.43 | Git : 22c3ac9
================================================================================================================
~~~

고정 헤더에 최소한 다음 정보는 보존한다.

- Target
- Media Scope
- 이미지/비디오 완료 진행률
- Benchmark Mode 순서
- CPU Resource
- GPU 상태/backend
- Distance
- Suite ID
- Build 및 Git 식별자(폭이 허용되는 경우 우선 유지)

### 29.5 한 줄 보장 및 폭 대응

- 상단 헤더는 절대 자동 줄바꿈하지 않는다.
- 콘솔 폭이 부족하면 덜 중요한 문자열부터 축약한다.
- 긴 Target 경로는 **중간 생략(middle ellipsis)**으로 앞/뒤를 보존한다.
- Balanced (55%) → Balanced 55%, ON / CUDA → CUDA와 같이 화면용 문자열을 압축할 수 있다.
- 화면에서 값이 축약되어도 JSON/journal에는 원본 값을 그대로 저장한다.
- 터미널 폭에 따라 wide / normal / compact 표시를 사용할 수 있으나 줄 수는 늘리지 않는다.

### 29.6 CURRENT FILE 상세 영역

현재 파일의 전체 순번과 이름은 하단에서 상세하게 표시한다.

~~~text
CURRENT FILE
----------------------------------------------------------------------------------------------------------------
[16 / 847] sample_0012.jpg
Type : Image | Size : 4.82 MB | IMG : 12/640

AUTO                  CPU                   GPU-MAX
------------------    ------------------    ------------------
DONE                  DONE                  RUNNING
12.41 ms              18.08 ms              7.32 ms
Scheduler             Software              CUDA
                      Workers : 10          CPU FB : NO
----------------------------------------------------------------------------------------------------------------
~~~

비디오에서는 codec, resolution, fps, duration, decoder/backend, CPU fallback reason 등 매체에 특화된 정보를 우선 표시한다.

### 29.7 완료 이력과 최종 상태

완료 이력은 한 파일을 한 줄로 압축해 누적한다.

~~~text
0011 image_0011.jpg         AUTO 10.8ms | CPU 14.7ms | GPU  8.2ms
0012 image_0012.jpg         AUTO 12.4ms | CPU 18.1ms | GPU  9.6ms
0013 sample_0013.mp4        AUTO 842ms  | CPU 711ms  | GPU 438ms
~~~

취소 시에는 CANCELLATION REQUESTED 이후 부분 저장 완료 여부와 완료/잔여 파일 수를 명확히 표시한다. 정상 종료 시에는 BENCHMARK COMPLETE와 AUTO/CPU/GPU-max의 누적 요약을 표시한다.

### 29.8 Interactive / non-interactive 분리

- TTY/Interactive: 고정 헤더 + CURRENT FILE + 누적 이력 + 최종/부분 요약
- Redirect/CI/non-interactive: ANSI 재작성에 의존하지 않는 line-oriented 출력
- 두 경로가 기록하는 benchmark 데이터는 동일한 journal/JSON 구조를 사용한다.

### 29.9 Legacy Benchmark 보존

기존 benchmark source와 schema는 **legacy baseline으로 영구 보존**한다.

- 기존 benchmark 구현을 새 구조로 덮어써서 이력을 잃지 않는다.
- v0.9.4.43 Git tag/source backup 및 기존 문서 기록을 legacy 기준점으로 유지한다.
- 새 benchmark architecture로 전환할 때 legacy source, tag, backup, documentation snapshot 간 추적성을 유지한다.
- 이번 결정 자체는 문서 변경이며 기존 product source/benchmark implementation을 삭제하거나 교체하지 않는다.

## 30. 최종 S0~S8 세부 책임

- **S0**: Run/Suite, mode, media scope, Resource Budget, journal, isolation, cancellation, terminal contract 확정
- **S1**: Console entry, help/version, headless scan, media/resource option 연결
- **S2**: 파일 단위 AUTO→CPU→GPU-max 실행 core, 독립 mode context, per-file result event/journal contract
- **S3**: benchmark 저장소 격리, append-only journal, crash-safe/partial persistence, summary 생성
- **S4**: GUI benchmark 연동 및 최신 3개 보존
- **S5**: Console benchmark CLI 실행과 interactive/non-interactive terminal renderer
- **S6**: Suite 자동 실행, fingerprint 검증, 비교/데이터 마이닝
- **S7**: help/usability/exit code/verbose
- **S8**: CPU/GPU build, tests, CLI/GUI 검증, JSON/journal 검증, 문서 및 release gate

실제 성능 실험을 시작하는 순간부터 기존의 **pre-register-first** 규칙은 그대로 적용한다.
## 31. Console Benchmark 시각 목업 — 2026-09-30

텍스트 예시만으로는 최종 Console UI의 밀도와 상단 고정영역의 no-wrap 동작을 충분히 검토하기 어렵기 때문에, 확정 설계를 시각 목업으로 함께 보존한다.

프로젝트 목업 파일:

~~~text
project/uimock/benchmark-console-mockup.html
JPG preview: benchmark-console-mockup.jpg (generated review artifact)
~~~

목업은 다음 최종 계약을 반영한다.

- 상단 고정영역 3개 정보 행
- Target / Scope / IMG·VID 진행률을 같은 행에 배치
- Mode / CPU Resource / GPU를 같은 행에 배치
- Distance / Suite ID / Build / Git를 같은 행에 배치
- 고정영역 자동 줄바꿈 금지
- 긴 Target 경로는 화면에서 middle ellipsis 가능
- 전체 원본 값은 JSON/journal에 보존
- CURRENT FILE을 하단 상세 영역으로 분리
- AUTO / CPU / GPU-max 상태를 가로 3열로 표시
- 완료 파일은 compact one-line history로 누적

이 목업은 실제 benchmark 실행 화면이 아니라 **구현 전 UI 계약을 검토하기 위한 정적 reference**다. 실제 구현에서는 데이터 값, 터미널 폭, 파일 특성에 따라 표시 문자열이 달라진다.


