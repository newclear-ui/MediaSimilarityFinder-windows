# Implementation Brief — Node C Calibration / INI Performance Profile

## 1. 목적

Node C는 Scheduler가 매 실행마다 하드웨어 baseline만 보고 시작하지 않도록 짧은 calibration으로 얻은 비교적 안정적인 처리능력과 비용을 INI Performance Profile로 보존하고 다음 실행의 initial estimate로 제공한다.

Node C는 Node B Scheduler 정책을 다시 설계하지 않는다.

핵심 흐름:

    환경 / backend identity
            |
            v
       Profile load
            |
       +----+----+
       |         |
     usable    stale/invalid
       |         |
       |         v
       |   Short Calibration
       |         |
       |         v
       |    Profile candidate
       |         |
       +----+----+
            |
            v
      Initial Estimate
            |
            v
       B Scheduler
            |
            v
      Live Measurement
            |
            v
    Decision / deviation check
            |
       +----+----+
       |         |
    stable   repeated deviation
       |         |
       v         v
  keep profile  opportunistic calibration

원칙은 Profile = 초기값, Live Runtime = 현재 실행의 권위 있는 값이다.

## 2. C의 책임과 경계

### C가 담당

- Performance Profile 데이터 모델
- INI 저장/로드
- profile identity / version / confidence
- stale / invalid 판정
- 짧은 initial calibration
- opportunistic recalibration
- Profile → Scheduler initial-estimate 연결
- calibration/Profile lifecycle을 기존 Benchmark CalibrationTelemetry에 기록
- CPU-only / GPU-off 동작 유지

### C가 담당하지 않음

- Scheduler 정책 자체 재설계
- worker / queue / barrier topology 변경
- Node D pipeline 최적화
- NVDEC 구현
- 신규 GPU backend
- video planner
- 검색 판정 알고리즘 변경

Node B에서 확정한 decision → execution binding은 그대로 유지한다.

## 3. 기존 코드와의 연결

Node A에서 이미 다음 기반이 있다.

- BenchmarkRecorder
- CalibrationTelemetry
- MeasureState
- benchmark schemaVersion 1
- CPU/GPU backend abstraction
- Scheduler telemetry

현재 CalibrationTelemetry에는 confidence, cpuThroughput, gpuThroughput, resizeThroughput, decodeThroughput, transferCostMs, queueLatencyMs, profileId, profileVersion이 이미 존재한다.

따라서 C는 새로운 benchmark telemetry 체계를 중복 생성하지 않고 기존 CalibrationTelemetry를 실제 calibration 결과와 Profile lifecycle에 연결한다.

C1에서는 benchmark schemaVersion을 올리지 않는다. 이미 존재하는 calibration 필드를 우선 채우며 새 JSON 키가 정말 필요한 경우에만 schema 변경을 별도로 검토한다.

## 4. Profile 저장 위치

초기 portable 기준:

    <application directory>/
      Index/
        PerformanceProfile.ini

이 Profile은 media root별 index가 아니라 해당 애플리케이션 실행 환경의 공통 performance profile이다.

기존 portable Index 영역을 사용하며 vcpkg나 새 사용자 전역 저장소를 추가하지 않는다.

Profile 파일이 없으면 정상적인 profile-not-present 상태로 시작한다.

## 5. Profile 데이터 계약

초기 profileVersion은 1.0으로 한다.

개념적 INI 구조:

    [meta]
    profileId=
    profileVersion=1.0
    createdAt=
    updatedAt=
    confidence=
    engineVersion=
    benchmarkSchemaVersion=1

    [identity]
    cpuFingerprint=
    gpuFingerprint=
    backend=
    driver=

    [cpu]
    fingerprintThroughput=
    fingerprintState=

    [gpu]
    fingerprintThroughput=
    batchThroughput=
    fingerprintState=
    batchState=

    [transform]
    resizeThroughput=
    resizeState=

    [transfer]
    bandwidthMBps=
    costState=

    [decode]
    cpuThroughput=
    cpuState=
    hardwareFeasibilityState=

    [queue]
    latencyMs=
    state=

    [lastUpdate]
    reason=
    oldProfileId=
    oldConfidence=
    newConfidence=

정확한 key spelling과 직렬화 구현은 C1에서 코드와 함께 고정한다.

### 저장 규칙

- measurement state와 numeric value는 별개다.
- 측정하지 않은 값은 숫자 0으로 저장하지 않는다.
- not_measured, not_available, partial, failed, fallback을 보존한다.
- 측정값이 없는 metric은 numeric key를 생략하거나 명시적으로 부재 상태로 남긴다.
- 새 Profile은 temp 파일로 완성한 뒤 대상 파일을 atomic replacement 한다.
- Profile write 실패가 검색 자체를 실패시키면 안 된다.

## 6. Profile identity

최소 identity:

- CPU fingerprint
- GPU fingerprint
- backend identity
- driver identity
- engine compatibility 정보
- profileVersion

### 재사용 정책

Exact match:
CPU/GPU/backend identity가 맞고 profileVersion이 호환되면 정상 재사용한다.

Soft mismatch:
호환 가능한 driver 또는 engine 변경처럼 측정 의미는 유지되지만 성능이 달라질 수 있는 경우 profile을 읽을 수 있고 confidence를 낮추며 short calibration을 우선한다.

Hard mismatch:
CPU hardware fingerprint 변경, GPU hardware fingerprint 변경, backend identity 변경, profileVersion 호환 불가인 경우 기존 profile을 초기값으로 직접 사용하지 않고 새 calibration을 수행한다.

### Stale

초기 정책은 maxAgeDays = 30을 사용한다.

- 30일을 넘었다고 profile을 삭제하지 않는다.
- stale이면 confidence를 낮추고 short calibration을 우선한다.
- runtime deviation이 반복되면 age와 무관하게 opportunistic recalibration을 요청할 수 있다.

## 7. Confidence

confidence는 0.0 ~ 1.0 범위의 장기 Profile 신뢰도다.

초기 정책:

- 필수 초기 측정이 정상 완료된 새 profile은 높은 confidence로 시작
- partial calibration은 measurement coverage에 따라 낮게 시작
- soft mismatch는 감소
- stale은 감소
- calibration 실패는 기존 profile을 유지하고 감소
- 반복 runtime deviation은 감소
- 일관된 재측정이 누적되면 candidate confidence를 다시 높일 수 있다

구체적인 수치 조정은 C2/C3의 테스트 가능한 정책으로 고정한다. C1에서는 저장 계약과 상태만 확정한다.

## 8. Calibration 측정 범위

### C에서 실제 측정

CPU:
- fingerprint throughput

GPU:
- fingerprint throughput
- batch throughput

공통:
- transfer cost / bandwidth
- resize/conversion throughput

Decode:
- 기존 Software FFmpeg CPU decode baseline

### C에서 상태만 기록하고 아직 측정하지 않음

Hardware decode:
Node F/NVDEC capability가 나오기 전에는 hardware decode 성능을 만들어내지 않는다. 필요 시 not_available 또는 fallback으로 기록한다.

Queue latency:
Node D에서 실제 queue producer/consumer telemetry가 연결되기 전에는 가짜 값을 만들지 않는다. not_available 또는 not_measured로 기록한다.

이 경계는 C가 D/F를 선행하지 않도록 하기 위한 것이다.

## 9. Calibration 전략

### Initial calibration

다음 경우 수행한다.

- Profile이 없음
- Profile identity가 hard mismatch
- profileVersion이 호환되지 않음
- stale 또는 low-confidence 재사용이 부적절함

긴 종합 benchmark가 아니라 짧은 probe 집합으로 구현한다.

원칙:

- bounded workload
- 단위 명확
- measurement state 명확
- 전체 media root 재스캔 금지
- calibration 실패가 search failure가 되지 않음

구체적인 probe 크기와 time budget은 C2에서 결정하고 테스트로 고정한다.

## 10. Scheduler initial-estimate 계약

Profile을 읽었다고 live rate를 덮어쓰면 안 된다.

우선순위:

    1. current live measured rate
    2. valid profile baseline
    3. hardware/static baseline
    4. unknown / fallback

Profile 데이터는 Node B의 live throughput 입력과 분리한다.

권장 구조:

    SchedulerHardware
       |
       +-- live cpuRate / gpuRate
       |
       +-- profile initial estimate
       |      +-- cpuRate
       |      +-- gpuRate
       |      +-- transfer bandwidth
       |
       +-- hardware baseline

C는 profile initial-estimate 필드만 추가/연결하고 B의 share formula, hold, kill-band, Resource Mode 정책은 변경하지 않는다.

live rate가 유효하면 항상 Profile보다 우선한다.

## 11. Profile update 정책

단일 실행 이상값으로 기존 Profile을 즉시 교체하지 않는다.

갱신 기록에는 다음을 남긴다.

- old profileId
- new profileId
- reason
- confidence before
- confidence after

초기 구현에서는 lastUpdate 정보를 Profile에 보존한다.

full multi-revision history가 필요해지는 경우에는 별도 설계로 분리한다.

## 12. Opportunistic recalibration

usable Profile이 있어도 runtime observation이 반복적으로 다르면 짧은 recalibration을 시작할 수 있다.

초기 원칙:

- live rate와 profile baseline의 상대 오차를 비교
- 한 번의 outlier만으로 재측정하지 않음
- 반복 deviation에서만 short probe
- candidate가 기존 profile과 크게 충돌하면 바로 교체하지 않고 confidence를 낮춤
- 반복 probe에서 일관된 candidate가 확인되면 update 후보로 승격

정확한 deviation threshold와 repetition count는 C3에서 테스트 가능한 값으로 고정한다.

## 13. 상태 흐름

    No Profile
        |
        v
    Initial Calibration
        |
        +---- failed/partial ----> explicit-state Profile
        |
        v
    Profile Ready
        |
        v
    Identity Check
        |
        +---- hard mismatch ----> Recalibration
        |
        +---- stale/low confidence ----> Recalibration preferred
        |
        +---- usable ----> Initial Estimate
                               |
                               v
                         Live Runtime
                               |
                    +----------+----------+
                    |                     |
                 stable              repeated deviation
                    |                     |
                    v                     v
               keep profile      Opportunistic Calibration
                                             |
                                  +----------+----------+
                                  |                     |
                              consistent           inconsistent
                                  |                     |
                                  v                     v
                            update candidate      lower confidence

## 14. C1~C4 단계

### C1 — Profile Foundation

범위:

- PerformanceProfile data model
- ProfileStore
- INI serializer/loader
- Profile identity representation
- profileVersion
- confidence
- measurement state
- stale/invalid classification
- atomic save
- Scheduler initial-estimate interface 정의
- C1 unit tests

C1에서는 실제 calibration 실행을 넣지 않는다.

종료조건:

- profile round-trip
- exact identity reuse
- soft mismatch
- hard mismatch
- stale
- not_measured ≠ numeric zero
- atomic write/read
- CPU-only profile subsystem

### C2 — Initial Calibration

범위:

- short calibration runner
- CPU fingerprint measurement
- GPU fingerprint/batch measurement
- transfer/resize measurement
- CPU decode baseline
- calibration result → Profile candidate
- Profile → Scheduler initial-estimate 연결
- CalibrationTelemetry 실측 기록

종료조건:

- no Profile → calibration → Profile 생성
- valid Profile에서 불필요한 full calibration 생략
- CPU-only
- GPU-off
- GPU-on
- live measurement가 Profile보다 우선
- search-result parity

### C3 — Opportunistic Recalibration

범위:

- runtime/Profile deviation detector
- repeated-deviation policy
- short recalibration trigger
- confidence update
- candidate Profile update
- stale/invalid 재판정

종료조건:

- 단일 outlier로 profile 교체 금지
- 반복 deviation에서 recalibration 시작
- 일관된 candidate만 update
- 실패 시 기존 profile 유지
- live runtime 우선
- search correctness 불변

### C4 — Calibration Gate

범위:

- CPU-only
- GPU OFF
- GPU ON/AUTO
- no Profile
- exact Profile reuse
- stale Profile
- hard mismatch
- soft mismatch
- partial/failed calibration
- runtime deviation
- profile update
- persistence/reload
- benchmark calibration state

종료조건:

- Profile lifecycle 전체 통과
- Scheduler initial-estimate 연결
- live runtime precedence
- measurement-state consistency
- accuracy parity
- regression suite

C4 통과 시 Node C를 종료하고 다음 Gate D로 이동한다.

## 15. 테스트 설계 원칙

C 테스트는 절대 성능값보다 관계와 상태를 검증한다.

검증 대상:

- 단위와 measurement state의 정합성
- identity classification
- Profile lifecycle
- precedence
- failure/partial/fallback
- persistence
- search parity

특정 시스템에서 정확히 X images/sec 같은 절대값 테스트는 만들지 않는다.

실제 측정값은 benchmark evidence로 남기고 테스트는 contract와 state transition을 검증한다.

## 16. C / B / D 연결

### B

C는 initial estimate만 제공한다.

B의 throughput feedback, live load, hysteresis, minimum hold, Resource Mode, execution binding은 그대로 유지한다.

### D

C는 queue topology를 변경하지 않는다.

queue latency가 아직 측정 불가능하면 not_available / not_measured로 기록하고 D의 실제 queue telemetry가 생긴 뒤 metric을 활성화할 수 있다.

### E/F

E/F에서 capability가 나오기 전 hardware decode metric을 강제로 활성화하지 않는다.

## 17. 구현 순서

    C0 문서/설계 검토
          |
          v
    C1 Profile Foundation
          |
          v
    C2 Initial Calibration
          |
          v
    C3 Opportunistic Recalibration
          |
          v
    C4 Calibration Gate
          |
          v
    Node C Complete
          |
          v
    Node D

버전 번호는 C1/C2/C3/C4에 미리 고정하지 않고 검증된 코드 상태에 따라 증가한다.

## 18. 금지사항

- Profile 값을 live rate처럼 직접 덮어쓰기
- 미측정 항목을 숫자 0으로 기록
- calibration을 위해 전체 media root 강제 재스캔
- B Scheduler policy 재설계
- D queue/worker topology 선행 구현
- NVDEC 선행 구현
- 신규 GPU backend 추가
- 검색 정합성 변경
- Profile write failure를 search failure로 전파

Node C의 목표는 완벽한 예측값이 아니라 다음 실행의 합리적인 초기값과 runtime correction의 기반을 제공하는 것이다.
