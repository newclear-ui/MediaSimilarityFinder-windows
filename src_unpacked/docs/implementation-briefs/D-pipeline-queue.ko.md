# Implementation Brief — Node D Pipeline / Queue Optimization

## 1. 문서 목적과 상태

Node D는 Node B의 Adaptive Scheduler가 결정한 CPU/GPU 작업 배분을 실제 실행 경로에서 효율적으로 흘려보내기 위한 **Pipeline / Queue 관측·최적화 단계**다.

핵심 구분은 다음과 같다.

```
B = 어디에 얼마나 많은 작업을 배분할 것인가
D = 배분된 작업을 실제 pipeline에서 어떻게 흘려보낼 것인가
```

이 문서는 D의 상세 구현 계약이다. 문서에 적힌 구조가 모두 현재 코드에 이미 구현되어 있다는 의미가 아니다. **현재 구조 → 계측 → 병목 확인 → 제한된 변경 → 회귀검증** 순서로 진행한다.

현재 저장소의 D 진입 기준은 0.9.4.14 / Node C 완료 상태다. 공식 보존 기준선 0.9.2.32는 변경하지 않는다.

---

## 2. D 진입 전제

D는 다음 계약을 유지해야 한다.

- CPU/GPU Adaptive Scheduler의 decision interface
- CPU/GPU work-share의 추상 표현
- Scheduler telemetry
- GPU backend abstraction
- `MeasureState`
- CPU reference/fallback 경로
- 기존 검색 판정과 threshold
- 기존 DB/cache/index 호환성
- GPU ON/OFF 의미

D에서 발견된 Scheduler 정책 자체의 결함은 가능한 경우 B recovery branch로 되돌려 기록한다. D가 Scheduler의 share formula, hysteresis, Resource Mode 의미를 다시 정의해서는 안 된다.

Node C의 Profile은 다음 실행의 initial estimate이고, live runtime measurement가 우선한다는 원칙도 유지한다.

---

## 3. 현재 코드 기준선

현재 코드를 “빈 pipeline”으로 가정하지 않는다.

### 3.1 이미지 경로

현재 `MediaPipeline::imageBatch()`에는 다음 구조가 이미 있다.

```
paths
  |
  +--> bounded parallel CPU decode
  |
  +--> normalized 32x32 grayscale packing
  |
  +--> GPU batch hash 또는 CPU pHash fallback
  |
  +--> crop / color thumbnail pass
  |
  +--> result
```

CPU decode는 bounded `std::async` worker 집합으로 병렬화되어 있으며, 결과 순서는 입력 index에 맞춰 유지한다. GPU hash는 bounded batch로 실행되고 GPU 실패 시 동일 의미의 CPU pHash 경로가 사용된다.

따라서 D1에서 해야 할 일은 “병렬화를 새로 만든다”가 아니라 **현재 병렬 decode, GPU batch, crop pass 사이에서 실제 대기와 중첩 정도를 측정하는 것**이다.

### 3.2 스캔 경로

현재 `MediaSearchEngine::scan()`에는 디렉터리 walker와 분석 스레드 사이를 연결하는 queue가 있다.

```
Scanner walker
     |
     v
FileState queue
     |
     v
processOne()
     |
     +--> image batch
     |
     +--> video async range
     |
     v
DB / matching
```

이 queue는 **directory-walk producer와 scan consumer를 연결하는 queue**다. 이것을 곧바로 “CPU execution queue” 또는 “GPU execution queue”라고 해석해서는 안 된다.

현재 이미지와 영상은 서로 다른 실행 경로를 사용한다.

- Image: CPU decode → GPU/CPU hash → crop/thumbnail
- Video: bounded async analysis → FFmpeg/video fingerprint path → optional GPU video work

따라서 D3에서 CPU Queue / GPU Queue / Result Queue를 추가하는 것은 **필요성이 계측으로 확인된 경우에만** 진행한다.

### 3.3 현재 Benchmark 계측 기반

Benchmark에는 이미 다음과 같은 D 관련 필드가 존재한다.

- image queue wait
- image transfer time
- image execution time
- CPU/GPU queue depth
- CPU/GPU queue wait
- stage duration
- GPU batch time
- video decoded/sampled frame count
- resource samples
- cancellation/partial state

그러나 **필드가 존재한다는 것과 모든 필드가 현재 실제 producer/consumer 이벤트에서 채워진다는 것은 다르다.**

D1의 첫 번째 작업은 실제 이벤트와 telemetry 필드를 연결하고, 측정 불가능한 항목은 `not_measured` / `not_available` 등으로 남기는 것이다. 미측정을 숫자 0으로 표현하지 않는다.

---

## 4. D의 전체 단계

D는 다음 순서로 진행한다.

```
D0 Design Review
  |
  v
D1a Image-Path Observability (동기 경계, 먼저)
  |
  v
D1b Walker/Video Observability (async, 다음)
  |
  v
D2 Barrier / Serialization Reduction
  |
  v
D3 Queue Optimization
  |
  v
D4 Transfer / Compute Overlap
  |
  v
D5 Batching Optimization
  |
  v
D6 Worker Starvation / Dependency Analysis
  |
  v
D7 Scheduler Execution Binding (allocation-proportional dispatch;
  정식 버전은 D3 인프라에 의존 — D7절 참조)
  |
  v
D8 End-to-End Validation
  |
  v
D-Gate
```

각 단계는 독립적인 검증 상태를 만든 뒤 다음 단계로 이동한다. 특정 D 단계에서 문제가 발견되면 해당 단계의 recovery branch에서 원인을 분석하고 수정한 뒤 다시 같은 gate를 검증한다.

---

# D0 — Design Review / Baseline Freeze

## 목적

D 구현 전에 B/C 계약과 현재 execution topology를 고정한다.

### 반드시 보존

- B Scheduler share formula
- Resource Mode 정책
- minimum hold / hysteresis
- live throughput 우선순위
- Profile initial estimate semantics
- GPU abstraction
- CPU fallback
- 검색 threshold와 verdict
- DB/cache schema
- 0.9.2.32 보존 기준선

### Baseline evidence

D 시작 시 다음을 기록한다.

- 현재 CPU/GPU CTest 결과
- `--version`
- `--smoke`
- benchmark JSON schema/version
- 대표 CPU-only / GPU OFF / GPU ON 결과
- 검색 결과 parity
- 현재 wall time과 stage telemetry

D에서 성능이 개선되었다고 주장하려면 D 이전 baseline과 동일한 조건으로 비교한다.

---

# D1a — Image-Path Observability (먼저)

D1은 한 게이트에 몰아넣지 않고 둘로 나눈다. 프로젝트의 게이트 규율
(B1~B7, C1~C4: 한 게이트 = 검증 가능한 작은 단위)을 D에서도 유지한다.

D1a는 동기 경계라 상대적으로 쉽다. `MediaPipeline::imageBatch()`의
기존 경계(decode → pack → hash → crop/thumbnail)를 그대로 계측한다.

### Queue telemetry

이미지 경로의 bounded handoff 중심으로 다음을 기록한다.
- enqueue/dequeue count (batch 단위)
- batch depth (진행 중 batch 수)
- wait count / cumulative wait time

### Worker telemetry (group 단위 집계 우선)

- decode worker active/idle/wait
- hash path (GPU vs CPU fallback) task count
- crop/thumbnail task count

### Stage telemetry

- decode / pack / hash / crop+thumbnail 누적 duration
- GPU batch time (기존 `addGpuBatchMs` 재사용)
- persist / candidate·match 단계는 엔진 스캔 측 경계라 D1b에서 함께
  관측한다 (imageBatch 밖이므로 D1a 범위 밖)

### Overlap telemetry

단순히 stage duration을 더하는 것과 실제 wall time은 다르다.

예:

```
CPU Decode  ─────────
GPU Hash        ───────
Crop                ──────
```

겹치는 구간을 식별할 수 있도록 stage activity interval 또는 group-level overlap evidence를 기록한다.

### D1a 구현 시작 기준선

- D0 baseline: v0.9.4.14
- first implementation: v0.9.4.15
- existing BenchmarkRecorder telemetry is reused
- only the existing synchronous imageBatch boundary is observed
- Scheduler/Profile/Calibration/CUDA/video/similarity/DB/cache are unchanged
- transfer remains not_measured without backend-internal hooks
- queue wait is not encoded as zero because no separate execution queue exists

### D1a 종료 조건

- image decode/hash/crop 경계가 관측 가능
- queue wait / worker wait / stage duration을 구분 가능
- 미측정 상태와 실제 0을 구분
- 기존 검색 결과 불변

---

# D1b — Walker/Video Observability (다음)

async 영역이라 D1a보다 어렵다. 무리하게 intra-decode를 열지 않는다.

### Walker queue

directory-walk producer와 scan consumer 사이의 실제 queue에 대해:

- enqueue/dequeue count, current/maximum depth
- producer blocked time, consumer idle time

이 queue를 CPU/GPU execution queue로 해석하지 않는다 (§3.2 유지).

### Video 경로

`VideoDecoder::framesAt()`는 seek 1회 후 선형 디코드라 프레임 단위
stage 경계가 코드에 없다. 따라서 range 수준을 우선한다:

- range 총 소요시간 (기존 `buildMs` 재사용)
- decodedFrames / sampledFrames / keptFrames (기존 `VideoBuildStats` 재사용)

intra-decode stage 분해는 후순위로 미룬다. 여기서 무리하면 E(Adaptive
Video Decode Planner)의 작업을 D가 선행 침범하게 된다.

### Transfer 관측 (D1a/D1b 공통 주의)

host-side에서 `hashBatch()` 호출을 감싸 재면 "전송+커널 실행"이 뭉쳐서
나온다 (B5 build-history 자인: 커널 내부를 뜯지 않았음).
`cuda_backend.cu`에 `cudaEvent_t` 기반 내부 타이머가 전혀 없으므로,
elapsed transfer time의 실측은 **backend abstraction 아래의 내부
timing point**가 필요하다. D4에만 있던 "backend abstraction 아래에서
구현" 조건을 D1 관측 절에도 동일하게 적용한다. 내부 후크가 없으면
transfer time은 `not_measured`로 남기고 0을 기록하지 않는다.

### D1b 종료 조건

- walker queue가 관측 가능
- video range 수준이 관측 가능
- 미측정 상태와 실제 0을 구분
- 기존 검색 결과 불변

---

# D2 — Barrier / Serialization Reduction

## 목적

D1에서 실제 병목으로 확인된 **불필요한 전체 단계 대기**와 global serialization을 줄인다.

### 기존 가능한 형태

```
A1 A2 A3 A4
 \ | | /
   WAIT
     |
B1 B2 B3 B4
```

### 개선 후보

```
A1 -> B1
A2 -> B2
A3 -> B3
A4 -> B4
```

또는 bounded batch 단위 overlap.

### 반드시 확인할 의존성

다음은 barrier를 제거하면 안 된다.

- 결과 순서가 명시적으로 필요한 단계
- DB transaction 경계
- candidate index consistency
- shared cache write ordering
- cancellation checkpoint
- 동일 파일의 dependent stage
- CPU fallback 이후 결과 확정

Barrier 제거는 “대기가 적을수록 좋다”가 아니라 **의존성이 없는 대기만 제거한다**는 원칙을 따른다.

### D2 종료 조건

- 제거한 barrier마다 이전/이후 dependency가 문서화됨
- result parity 유지
- cancellation/partial scan 유지
- stage overlap이 telemetry로 확인됨
- 실제 wall time 개선 또는 동등한 성능에서 대기 비용 감소가 증거로 남음

개선 효과가 없는 변경은 유지하지 않는다.

---

# D3 — Queue Optimization

## 목적

D1에서 queue imbalance가 확인된 경우에만 queue topology를 조정한다.

후보 구조:

```
CPU work queue
GPU work queue
Result queue
```

하지만 이것은 목표 후보일 뿐 현재 구현을 미리 확정하는 것이 아니다.

### Queue 설계 원칙

1. Scheduler가 queue 내부를 직접 조작하지 않는다.
2. Scheduler는 work allocation을 제공한다.
3. Pipeline policy가 allocation을 queue/worker 실행으로 변환한다.
4. queue는 bounded 상태를 지원할 수 있어야 한다.
5. queue가 무한히 커져 메모리가 증가하지 않도록 backpressure를 고려한다.
6. cancellation은 queue의 모든 대기 지점에서 관찰 가능해야 한다.

### Queue pressure

다음 지표를 사용할 수 있다.

```
pressure = currentDepth / capacity
```

단, pressure를 Scheduler share formula에 직접 삽입하는 것은 B 정책 변경이므로 D에서 임의로 하지 않는다. D에서는 실행 pipeline을 제어하거나 telemetry로 전달한다.

### D3 종료 조건

- queue depth/latency 측정 가능
- bounded behavior 확인
- queue imbalance 감소
- cancellation 정상
- memory growth 제한
- Scheduler와 queue implementation이 분리됨

---

# D4 — Transfer / Compute Overlap

## 목적

GPU acceleration에서 CPU↔GPU data movement와 compute가 서로 불필요하게 기다리는 상황을 줄인다.

목표 형태:

```
Transfer N+1
     ||
Compute N
     ||
Result N-1
```

### 후보

- asynchronous transfer
- pinned/staged buffer
- double/triple buffering
- batch overlap
- producer/consumer overlap

단, 구현은 현재 GPU backend abstraction 아래에서 이루어져야 한다.

상위 `MediaSearchEngine`에 CUDA-specific API를 확산하지 않는다.

### 판단 기준

Overlap을 적용한 후 다음을 비교한다.

- transfer time
- GPU execution time
- synchronization time
- CPU wait
- end-to-end wall time
- VRAM usage
- cancellation latency

Overlap 자체를 성공으로 보지 않는다. synchronization 비용이 더 커지면 기존 단순 경로가 유지될 수 있다.

---

# D5 — Batching Optimization

## 목적

현재 GPU batch와 CPU work batching의 크기를 workload와 resource 상태에 맞게 조정한다.

현재 image path에는 GPU batch size가 존재하고 backend가 recommended batch size를 적용한다. D5에서는 이 구조를 무시하고 별도 batch system을 만드는 것이 아니라 **현재 batch boundary가 실제 queue wait, throughput, memory에 어떤 영향을 주는지 측정**한다.

### 관측값

- batch size
- batch duration
- items/batch
- items/sec
- queue wait
- transfer bytes
- transfer time
- VRAM pressure
- cancellation latency
- fallback rate

### Trade-off

큰 batch:

- throughput 증가 가능
- transfer amortization 가능
- GPU utilization 증가 가능
- cancellation latency 증가 가능
- queue wait 증가 가능
- VRAM 사용 증가 가능

작은 batch:

- latency 감소 가능
- cancellation 반응 개선
- batching overhead 증가 가능
- GPU 효율 저하 가능

따라서 “최대 batch”를 목표로 하지 않는다.

---

# D6 — Worker Starvation / Dependency Analysis

## 목적

worker가 실제 작업을 수행하지 않고 다른 worker의 결과를 기다리는 시간을 줄인다.

대표적인 문제:

```
CPU worker
   ↓
GPU work 대기
   ↓
GPU worker
   ↓
CPU result 대기
```

이 구조가 실제로 존재하는지는 telemetry로 확인한다.

### 검사 항목

- CPU worker idle while GPU queue non-empty
- GPU worker idle while CPU prerequisite incomplete
- full queue causing producer block
- result queue full causing downstream block
- lock contention
- future.get() 대기
- condition_variable 대기
- DB serialization
- global mutex contention

### 우선순위

1. circular wait 제거
2. unnecessary blocking 제거
3. lock scope 축소
4. async completion/continuation 검토
5. 마지막으로 worker 수 조정

단순히 worker 수를 늘리는 것은 D의 기본 해결책으로 사용하지 않는다.

---

# D7 — Allocation-Proportional Dispatch

## 목적

B Scheduler의 추상 allocation을 실제 pipeline 실행으로 안전하게 연결한다.

**B7과의 경계 (중요):** decision의 fresh read 바인딩은 B7 (0.9.4.8,
`schedUseGpuNow`)에서 이미 닫혔다. D7이 같은 일을 다시 하면 재수행이
된다. D7의 남은 일은 딱 하나다: **shares 비율대로 실제 작업을 쪼개
집행하는 것.** 오늘은 `gpuUsed` 이진값만 전달되어 70/30 같은 배분이
실행에 반영되지 않는다 (telemetry에만 기록됨 — 코드 대조 확인됨).

예:

```
Scheduler
    |
    v
CPU share / GPU share
    |
    v
Pipeline policy
    |
    +--> CPU work (share만큼)
    |
    +--> GPU work (share만큼)
```

### D3 의존성 (느슨한 결합)

제대로 된 버전(배치 도중 재조정, backpressure, 취소 반응성)은 D3의
큐 인프라가 필요하다. 그러나 최소 버전은 D3 없이 가능하다: 지금의
`for(k=from;k<to;++k)` 배치 생성 시 gpuShare 비율만큼 앞쪽 인덱스는
GPU 경로, 뒤쪽은 CPU 경로로 나누는 정적 분할 (재조정 없음).
따라서 "D3가 D7의 필수 선행"이 아니라 "D3 없이 조잡한 버전, D3
있으면 제대로 된 버전"이다. 순서를 문서화하는 이유: 나중에 "D7을
D3보다 먼저 얇게 하자"는 임기응변을 막기 위해, 정식 버전의 선행조건이
D3임을 여기에 못박는다.

예:

```
Scheduler
   |
   v
CPU share / GPU share
   |
   v
Pipeline policy
   |
   +--> CPU work
   |
   +--> GPU work
```

피해야 할 결합:

```
Scheduler -> CPU worker 7개
Scheduler -> GPU worker 3개
```

Scheduler가 특정 worker topology를 알아야 하면 B/D 경계가 무너진다.

### 실행 binding 규칙

- GPU OFF → GPU work를 실행하지 않음
- GPU unavailable → CPU fallback
- GPU runtime failure → affected work CPU fallback
- allocation이 unknown이면 안전한 fallback
- live throughput은 Profile보다 우선
- Profile은 execution baseline을 직접 강제하지 않음

D는 B의 share 계산식을 변경하지 않는다.

---

# D8 — End-to-End Validation

## 평가 대상

개별 stage가 빨라졌는지만 보지 않는다.

전체 scan을 기준으로 다음을 측정한다.

### 성능

- total wall time
- files/sec
- images/sec
- video sampled frames/sec
- CPU stage time
- GPU stage time
- queue wait
- barrier wait
- transfer time
- worker idle
- DB/persistence time

### 안정성

- cancellation latency
- pause/resume
- partial scan
- GPU OFF
- GPU unavailable
- GPU fallback
- mixed image/video
- large directory
- long-running scan

### 정확성

- CPU reference parity
- GPU/CPU fingerprint parity
- similarity verdict parity
- video temporal verdict parity
- crop/mirror matching parity
- incremental scan parity

### 성능 비교 조건

동일한:

- dataset (+ dataset fingerprint를 benchmark JSON에 기록 — 같은 조건
  비교의 증거. 표준 dataset 자체는 D0/D8에서 정의하며, 현재 루트의
  `test_sample_img_vid/`는 빈 플레이스홀더라 자산으로 쓰려면
  픽스처부터 채워야 한다)
- DB/cache 상태
- build configuration
- Resource Mode
- GPU ON/OFF
- driver/backend
- background load
- storage condition

에서 비교한다.

단일 실행 결과로 결론을 내리지 않고 반복 실행과 분산을 기록한다.

---

# 5. D의 측정 상태 규칙

D에서도 Node A/C의 measurement state 규칙을 그대로 사용한다.

| 상태 | 의미 |
| --- | --- |
| measured | 실제 측정값 확보 |
| not_measured | 아직 측정하지 않음 |
| not_available | 현재 환경/기능에서 측정 불가 |
| partial | 일부 구간만 측정 |
| failed | 측정을 시도했으나 실패 |
| fallback | 원래 경로 대신 fallback 경로가 사용됨 |

중요:

```
not_measured != 0
not_available != 0
failed != 0
fallback != 0
```

D에서 telemetry field가 아직 실제 이벤트와 연결되지 않았다면 0으로 채우지 않는다.

---

# 6. D와 Node B/C/E/F의 경계

## B — Adaptive Scheduler

B:

- allocation
- throughput feedback
- live load
- hysteresis
- minimum hold
- Resource Mode

D:

- queue
- worker
- barrier
- execution overlap

D에서 발견한 allocation formula 문제는 B recovery로 보낸다.

## C — Calibration

C:

- Performance Profile
- calibration
- confidence
- initial estimate
- runtime deviation

D:

- 실제 queue/stage/worker 관측
- execution topology

Queue latency가 C profile에 들어갈 필요가 생기더라도 D에서 실제 측정 가능해진 뒤 연결한다.

## E — Adaptive Video Decode Planner

E가 담당:

- sequential / hybrid / sparse seek
- decodedFrames vs sampledFrames
- GOP/keyframe cost
- seek strategy

D는 decode planner 자체를 재설계하지 않는다.

## F — Hardware Decode

F가 담당:

- NVDEC
- hardware decoder backend
- capability
- codec/profile/pixel-format
- decode fallback

D는 hardware decoder 구현을 선행하지 않는다.

---

# 7. 금지사항

다음은 D의 범위에서 금지한다.

- B Scheduler share formula 재설계
- Resource Mode 의미 변경
- Profile semantics 변경
- NVDEC 선행 구현
- Adaptive Video Decode Planner 선행 구현
- 신규 GPU backend 추가
- CUDA API를 상위 engine 전체로 확산
- 검색 threshold 변경
- similarity algorithm 변경
- DB/cache schema를 성능 개선 명목으로 임의 변경
- GPU utilization을 단독 성능 목표로 사용
- queue가 존재한다는 이유만으로 queue topology 전면 개편
- benchmark 값이 없다는 이유로 0을 기록
- 실제 증거 없이 “overlap으로 빨라졌다”고 결론

---

# 8. 테스트 전략

D 테스트는 특정 PC의 절대 성능값보다 **구조적 계약과 상태 전이**를 우선한다.

### 사전 등록 (pre-register)

각 D 단계의 코드 변경 전에 다음을 먼저 기록한다. 사후 "효과가
없네요"가 아니라 사전 숫자로 객관성을 확보한다.

- 측정된 병목 (D1 증거 인용)
- 예상 이득 (수치 범위)
- 롤백 기준 (이 수치에 못 미치면 revert)

`docs/build-history/<version>.ko.md`의 "변경 필요성" 항목 바로 아래에
"예상 이득 / 롤백 기준" 1~2줄을 둔다. D2 종료조건의 "효과 없는 변경은
유지하지 않는다"를 실행하는 방식이다.

### 단위 테스트

- queue depth accounting
- enqueue/dequeue count
- bounded queue
- cancellation
- stage timing
- measurement state
- batch accounting
- fallback accounting

### 통합 테스트

- CPU-only
- GPU OFF
- GPU ON
- GPU unavailable
- GPU failure → CPU fallback
- mixed image/video
- cancellation during queue wait
- cancellation during GPU batch
- large queue pressure
- empty/small workload
- single-file workload

### parity 테스트

동일 dataset에서:

```
CPU reference result
        ==
GPU-assisted result
```

가 유지되어야 한다.

---

# 9. 성능 검증과 판정 규칙

D는 “GPU 사용률이 높아졌다”를 성공 기준으로 사용하지 않는다.

우선순위:

1. 검색 정합성
2. 안정성 / fallback correctness
3. end-to-end throughput
4. latency / responsiveness
5. resource efficiency
6. GPU utilization은 보조 관측값

성능 개선은 baseline 대비 수치와 조건을 함께 기록한다.

예:

```
Baseline:  100.0 s
D build:    91.0 s
Improvement: 9.0%
Condition: same dataset / cache / mode / backend
```

이와 같은 증거를 남기며, 특정 구성에서 개선되지 않으면 그 사실도 그대로 기록한다.

---

# 10. D-Gate 완료 조건

## 구조

- 주요 실제 queue/handoff 관측 가능
- 주요 stage duration 관측 가능
- worker wait/idle 관측 가능
- 불필요한 global barrier가 식별·감소
- 필요한 경우 CPU/GPU overlap 가능
- Scheduler와 execution topology가 분리

## 성능

- queue/barrier/transfer overhead 수치화
- end-to-end throughput 측정
- 동일 조건 baseline 비교
- 개선 또는 비개선 결과 모두 증거화

## 안정성

- CPU fallback 정상
- GPU OFF 정상
- GPU unavailable 정상
- cancellation 정상
- mixed image/video 정상
- 장시간 실행 안정성 확인

## 정확성

- CPU/GPU parity
- image verdict parity
- video verdict parity
- crop/mirror parity
- incremental scan parity

## 관측성

Benchmark JSON에서 최소한 다음을 구분할 수 있어야 한다.

- queue depth
- queue wait
- stage duration
- transfer time
- batch information
- worker wait/idle
- overlap
- fallback
- cancellation/partial state

D-Gate 통과 후 다음 Roadmap Node인 E — Adaptive Video Decode Planner로 이동한다.

---

# 11. 문서 및 버전 관리 규칙

D1~D8은 버전 번호가 아니다.

```
D1 -> 검증된 코드 상태 -> 0.9.4.x
D2 -> 검증된 코드 상태 -> 0.9.4.x+1
...
```

실제 빌드 번호는 검증된 코드 상태에 따라 증가한다.

각 구현 버전은 다음을 기록한다.

- 변경 필요성
- 기존 구조
- 새 구조
- 선택 이유
- 해결된 병목
- 정확성 영향
- Windows/GPU/Linux 호환성 영향
- 테스트 결과
- benchmark 조건
- 알려진 제한

Build History는 실제 변경만 기록하고, 이 Implementation Brief는 D의 목표와 계약을 기록한다.

---

# 12. 권장 구현 순서

실제 구현은 다음 순서를 권장한다.

```
D0 baseline freeze
   ↓
D1a image observability
   ↓
D1a regression
   ↓
D1b walker/video observability
   ↓
D1b regression
   ↓
D2 barrier reduction
   ↓
D2 regression
   ↓
D3 queue optimization
   ↓
D3 regression
   ↓
D4 transfer/compute overlap
   ↓
D4 regression
   ↓
D5 batching
   ↓
D5 regression
   ↓
D6 starvation/dependency
   ↓
D6 regression
   ↓
D7 scheduler binding
   ↓
D7 regression
   ↓
D8 end-to-end validation
   ↓
D-Gate
```

특히 **D1a/D1b 관측성이 확보되기 전에는 D2~D6의 대규모 구조 변경을 시작하지 않는다.**

현재 코드에는 이미 walker queue, bounded async decode/video processing, GPU batch가 있으므로 D의 첫 구현은 새로운 실행 구조를 만드는 것이 아니라 **현재 실행 구조를 정확히 측정하는 것**이 우선이다.
