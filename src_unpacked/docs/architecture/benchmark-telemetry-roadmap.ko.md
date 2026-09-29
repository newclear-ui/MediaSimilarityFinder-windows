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


## 20. Benchmark 실행 모델 — Run / Suite / Media Scope

벤치마크는 Run과 Suite를 분리한다.

- Run: 하나의 benchmark mode를 한 번 측정한 결과
- Suite: 같은 source dataset, media scope, 실행 조건에서 AUTO / CPU 단독 / GPU 최대화를 묶은 논리적 실행군
- GUI는 각 JSON에 동일한 suiteId를 저장하면 되며 별도 suite.json은 필수가 아니다.
- Console은 장기 분석을 위해 suite.json과 개별 Run JSON을 함께 보관할 수 있다.

### Benchmark mode

- AUTO: 현재 Adaptive Scheduler 사용
- CPU 단독: GPU backend를 사용하지 않는 CPU 기준선
- GPU 최대화: GPU가 처리할 수 있는 작업은 최대한 GPU로 보내되 필수 CPU 작업과 fallback은 유지

GPU 최대화는 GPU-only 또는 CPU 0%를 의미하지 않는다.

### Media Scope

현재 GUI의 이미지/동영상 선택 범위를 Console에서도 동일하게 제공한다.

~~~text
--media images
--media videos
--media all
~~~

- images: 이미지 only
- videos: 동영상 only
- all: 이미지 + 동영상
- 기본값: all
- 일반 검색과 benchmark에서 동일한 의미를 사용한다.

mode와 media scope는 독립 축이다. 따라서 AUTO+images, CPU+images, GPU-max+images를 하나의 Suite로 실행할 수 있고, 다른 Suite에서는 videos만 실행할 수 있다.

각 Run JSON에는 최소한 다음을 저장한다.

~~~text
suiteId
runId
mode
mediaScope
scanImages
scanVideos
sourceRoot
sourceRootLabel
sourceRootId
datasetFingerprint
~~~

## 21. GUI Benchmark 보존 정책

GUI는 최신 결과 표시 목적이므로 source folder마다 다음 세 mode의 최신 결과만 자동 유지한다.

~~~text
Benchmark/GUI/<source-label>_<root-id-short>/
    auto.json
    cpu.json
    gpu-max.json
~~~

- AUTO / CPU 단독 / GPU 최대화는 기본 모두 선택
- 기존 GUI의 이미지/동영상 선택은 선택된 모든 benchmark mode에 공통 적용
- 같은 source + 같은 mode의 새 결과는 이전 결과를 교체
- media scope가 달라져도 해당 mode 파일은 최신 실행으로 교체되고 JSON의 mediaScope가 실제 범위를 식별
- 과거 이미지 only와 동영상 only 결과를 동시에 보존하려면 Console benchmark 사용
- 기존 수동 Save JSON은 export 기능으로 유지할 수 있으나 자동 저장이 기본 보존 경로
- GUI는 Console benchmark 저장소를 자동으로 읽지 않는다.

## 22. Console Benchmark 보존 정책

Console은 장기 비교와 데이터 마이닝용이며 결과를 자동 삭제하지 않는다.

~~~text
Benchmark/Console/suite-<suite-id>/
    suite.json
    auto.json
    cpu.json
    gpu-max.json
~~~

- 기본 누적 보관
- --log-dir로 출력 root 변경
- --log으로 개별 JSON 경로 지정
- GUI와 Console의 저장 경로와 loading path를 분리
- 장기 image/video/all 비교는 별도 Suite로 보관

## 23. Console CLI 설계

일반 검색과 benchmark가 모두 동일한 media scope를 사용한다.

~~~text
MediaSimilarityFinder.exe --scan <folder> --media all

MediaSimilarityFinder.exe --benchmark <folder> --mode auto --media all
MediaSimilarityFinder.exe --benchmark <folder> --mode cpu --media images
MediaSimilarityFinder.exe --benchmark <folder> --mode gpu-max --media videos
MediaSimilarityFinder.exe --benchmark <folder> --suite auto,cpu,gpu-max --media all
~~~

주요 benchmark 저장 옵션:

~~~text
--log-dir <dir>
--log <file>
~~~

기계 판독의 canonical 결과는 JSON이며 표준 출력은 진행 상태와 최종 요약 중심으로 유지한다.

## 24. Benchmark 격리와 공정성

일반 Search Index/Video Cache를 benchmark가 재사용하면 CPU / AUTO / GPU 최대화 비교가 오염될 수 있다.

따라서 benchmark Run은 전용 index/cache 상태를 사용한다.

~~~text
normal search
    └─ Index/<root-id>/...

benchmark
    └─ Benchmark/<GUI|Console>/...
        └─ isolated index/cache state
~~~

- benchmark가 일반 GUI 검색 DB를 변경하지 않는다.
- Run 사이 index/cache 오염을 방지한다.
- CPU → GPU → AUTO를 한 프로세스에서 연속 실행하는 것보다 Run별 독립 프로세스를 우선 검토한다.
- OS filesystem cache는 완전 통제가 불가능하므로 uncontrolled 상태를 명시한다.
- fresh benchmark index/cache와 cold OS filesystem cache를 같은 의미로 취급하지 않는다.
- 동일 Suite의 비교 Run은 dataset fingerprint, sourceRoot, mediaScope와 relevant execution conditions가 일치해야 한다.

## 25. Benchmark 환경 / 스케줄 기록

각 Run은 결과와 함께 실행 조건을 저장한다.

### 환경
- Windows/OS build
- appVersion / build configuration / gitCommit
- CPU model / logical threads / RAM
- GPU model / VRAM / driver
- CUDA/runtime 및 FFmpeg 정보
- selected/available backend

### 실행 설정
- benchmark mode
- mediaScope
- distance
- image/video enable state
- CPU Resource Mode
- normalized CPU percentage 10–90
- worker count
- GPU ON/OFF policy
- GPU batch
- scheduler initial estimate / live adjustments
- decoder policy
- benchmark index/cache state
- process isolation state

### 결과 식별
- suiteId / runId / runIndex
- sourceRoot / sourceRootLabel / rootIdShort
- datasetFingerprint / fileCount / byteCount
- startedAt / completedAt
- completion status

개발 **빌드 스케줄 자체도 문서에 저장하되 버전 번호는 사전 배정하지 않는다.** 검증된 코드 상태가 만들어질 때만 다음 0.9.4.x 번호를 부여한다.

## 26. GUI / Console 기능 대응

| 항목 | GUI | Console |
| --- | --- | --- |
| 일반 검색 | 지원 | 지원 |
| 이미지 only | 기존 선택값 | --media images |
| 동영상 only | 기존 선택값 | --media videos |
| 모두 | 기존 선택값 | --media all |
| AUTO | 지원 | 지원 |
| CPU 단독 | 지원 | 지원 |
| GPU 최대화 | 지원 | 지원 |
| 최신 3개만 유지 | 지원 | 하지 않음 |
| 장기 누적 | 기본 아님 | 기본 |
| 사용자 log dir | export 중심 | 지원 |

## 27. QuickLook 도움말

--help에는 Windows Store QuickLook을 선택적 편의 도구로 안내한다.

확인된 Microsoft Store 주소:
https://www.microsoft.com/store/apps/9nv4bs3l1h4s

QuickLook은 MediaSimilarityFinder의 필수 종속성이 아니다.

## 28. Benchmark 구현 / 빌드 스케줄

버전 번호를 미리 고정하지 않고 다음 순서로 진행한다.

- S0 설계/pre-register: Run/Suite, mode, media scope, 저장/격리/JSON/CLI 계약 확정
- S1 Console entry foundation: --help, --version, headless scan, --media 연결
- S2 Run/Suite benchmark core: BenchmarkConfig/Recorder/JSON/environment 연결
- S3 storage isolation: GUI/Console root, benchmark index/cache, atomic/crash-safe 저장
- S4 GUI integration: 세 mode checkbox, 기본 모두 선택, 기존 media 선택 결합, 최신 3개 보존
- S5 Console benchmark execution: --benchmark / --mode / --suite / --media / --log-dir / --log
- S6 data-mining automation: suite 자동 실행, dataset fingerprint 검증, 비교 요약
- S7 help/usability: 명령 예제, media 선택 예제, QuickLook 안내, exit code
- S8 full verification: CPU build → GPU build → CTest → CLI 실행 → GUI 검증 → JSON 검사 → 문서 → 필요 시 Build History → commit

실제 benchmark 성능 실험이 시작되는 시점부터는 기존 pre-register-first 규칙을 적용하고 성공/실패/기각을 Build History와 Performance / Tuning Experiment Index에 남긴다.
