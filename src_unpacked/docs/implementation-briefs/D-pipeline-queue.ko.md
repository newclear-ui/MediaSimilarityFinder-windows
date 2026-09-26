# Implementation Brief — Node D Pipeline / Queue Optimization

## 1. 목적

Node D는 CPU/GPU Scheduler가 결정한 작업이 실제 processing pipeline을 통과하면서 발생하는 대기, barrier, worker starvation, 불필요한 serialization을 줄인다.

핵심 구분:

```
B = 작업을 어디에 얼마나 배분할 것인가
D = 그 작업을 실제로 어떻게 흘려보낼 것인가
```

Node D는 신규 설계로 취급한다.

## 2. 진입 전제

D 시작 전에 다음 계약을 유지한다.

- CPU/GPU Scheduler decision interface
- CPU/GPU work share 표현
- scheduler telemetry
- backend abstraction
- measurement states
- CPU fallback

D는 이 인터페이스를 이용해 내부 pipeline을 개선한다.

## 3. 주요 병목

현재 또는 향후 실제 계측으로 확인해야 할 대상:

- future/barrier 구조
- worker starvation
- global serialization
- CPU↔GPU transfer stall
- queue imbalance

기존 구조를 추측만으로 대규모 변경하지 않는다. 먼저 관측한다.

## 4. D1 — Pipeline Observability

첫 단계에서는 구조를 크게 바꾸지 않는다.

### Queue

- queue depth
- enqueue/dequeue
- queue wait

### Worker

- active time
- idle time
- wait

### Stage

- start/end
- duration
- overlap

### Transfer

- transfer count
- transfer bytes
- transfer time

D1의 목표는 성능 향상이 아니라 **병목의 가시화**다.

## 5. D2 — Barrier Reduction

D1에서 확인된 불필요한 전체-stage barrier를 단계적으로 제거한다.

기존:

```
A1 A2 A3 A4
 \  |  |  /
   WAIT
     ↓
B1 B2 B3 B4
```

가능한 개선:

```
A1 → B1
A2 → B2
A3 → B3
A4 → B4
```

또는 적절한 batch 단위 overlap을 사용할 수 있다.

결과 순서에 의존하는 로직은 명시적으로 보존한다.

## 6. D3 — Queue Optimization

필요한 범위에서 queue를 분리한다.

예:

```
CPU Queue
GPU Queue
Result Queue
```

필요하면 bounded queue를 사용해 메모리 폭증을 막는다.

Queue 정책은 Scheduler의 allocation과 연결되지만 Scheduler가 queue 내부 구현을 직접 조작하지 않게 한다.

## 7. D4 — Transfer / Compute Overlap

가능한 작업은 transfer와 compute를 겹친다.

```
Transfer N+1
   ||
Compute N
   ||
Result N-1
```

그러나 overlap이 항상 빠른 것은 아니다. synchronization 비용이 커지면 단순 경로가 더 나을 수 있다.

## 8. D5 — Batching

GPU 작업을 필요할 때 batch로 묶는다.

고려:

- batch throughput
- queue wait
- latency
- VRAM usage
- transfer size
- cancellation responsiveness

batch를 무조건 크게 하지 않는다.

## 9. D6 — Worker Starvation

다음과 같은 대기 구조를 중점적으로 찾는다.

```
CPU worker
   ↓
GPU 작업 대기
   ↓
GPU worker
   ↓
CPU 결과 대기
```

불필요한 blocking 또는 circular wait를 제거한다.

가능한 경우 asynchronous completion/continuation을 사용한다.

## 10. D7 — Scheduler 연결

Scheduler는 다음과 같은 추상적 결정을 전달할 수 있다.

```
CPU share = 70
GPU share = 30
```

D는 이를 실제 worker/queue 정책에 반영한다.

가급적 다음 결합을 피한다.

```
Scheduler → CPU worker 7개
```

대신:

```
Scheduler
   ↓
CPU/GPU work allocation
   ↓
Pipeline policy
   ↓
workers / queues
```

형태를 유지한다.

## 11. D8 — End-to-End 검증

최종 평가는 개별 stage 하나가 아니라 전체 검색으로 한다.

측정:

- total wall time
- CPU/GPU stage time
- queue wait
- barrier wait
- transfer time
- worker idle time
- throughput
- cancellation latency

## 12. 범위 밖

- Scheduler 정책 재설계
- Calibration profile 설계
- NVDEC
- video decode planner
- 신규 GPU backend
- 모든 workload에 대한 최적화
- GPU utilization 강제 tuning

D에서 Scheduler의 정책 문제가 발견되면 가능한 한 B의 recovery branch로 기록한다.

## 13. 완료 기준

### 구조
- 주요 queue/stage 관측 가능
- 불필요한 global barrier 감소
- worker starvation 감소
- CPU/GPU overlap 가능

### 성능
- end-to-end throughput 측정 가능
- queue/barrier/transfer overhead를 수치화
- 실제 workload에서 개선 효과 검증

### 안정성
- cancellation 정상
- CPU fallback 정상
- GPU OFF 정상
- mixed image/video 정상
- 결과 정합성 유지

### 관측성
Benchmark JSON이 최소한 다음을 구분해 기록할 수 있어야 한다.

- queue depth
- queue wait
- stage duration
- transfer time
- batch information
- worker wait/idle
- overlap
