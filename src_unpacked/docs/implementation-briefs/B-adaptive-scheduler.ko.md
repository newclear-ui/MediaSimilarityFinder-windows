# Implementation Brief — Node B Adaptive Scheduler

## 1. 목적

Node B는 CPU/GPU 작업 배분을 고정 비율이 아니라 실제 처리능력과 실행 상태를 기준으로 동적으로 결정하는 Scheduler를 구축한다.

이 문서는 한 번에 완성형 Scheduler를 구현하기 위한 지시서가 아니다. 작은 검증 단계를 순서대로 진행한다.

```
B1 Minimal Adaptive Allocation
        ↓
B2 Runtime Throughput Feedback
        ↓
B3 Live Load Awareness
        ↓
B4 Stability Control
        ↓
B5 Transfer / Workload Cost
        ↓
B6 Resource Mode Integration
        ↓
B7 Final Scheduler Gate
```

## 2. 핵심 원칙

- GPU utilization 자체를 최적화 목표로 삼지 않는다.
- 실제 end-to-end throughput과 검색 정합성을 우선한다.
- 느린 GPU는 CPU 중심 또는 CPU-only로 수렴할 수 있다.
- CPU fallback은 항상 유지한다.
- GPU는 사용자에게 ON/OFF만 노출한다.
- Manual은 CPU 제약만 지정하며 GPU percentage를 직접 지정하지 않는다.
- Scheduler는 Pipeline 내부 구현에 강하게 결합하지 않는다.

## 3. B와 D의 경계

B는 **어디에 얼마나 많은 작업을 배분할지** 결정한다.

```
Workload → Scheduler → CPU/GPU Work Allocation → Pipeline
```

D는 **결정된 작업을 실제 queue/worker/pipeline으로 어떻게 흘려보낼지** 담당한다.

따라서 B에서는 특정 worker 수, queue topology, barrier 구조를 확정하지 않는다.

## 4. B1 — Minimal Adaptive Allocation

첫 구현에서는 다음만 사용한다.

- CPU baseline capacity
- GPU baseline capacity
- GPU availability
- GPU ON/OFF

출력:

- CPU work share
- GPU work share
- scheduler decision

초기 버전은 일정한 내부 주기로 배분을 재계산한다. 재평가 주기 자체는 사용자 UI에 노출하지 않는다.

B1에서는 다음을 아직 구현하지 않는다.

- moving average
- hysteresis
- transfer cost model
- 외부 부하 모델
- workload-specific cost model

## 5. B1 최초 검증

다음 상태가 명확하게 구분되어야 한다.

| 상태 | 예상 Scheduler |
| --- | --- |
| GPU OFF | CPU 100% |
| GPU ON + 유효한 GPU | CPU + GPU |
| GPU가 느림 | CPU 중심 가능 |
| GPU unavailable | CPU fallback |
| 검색 정확성 | CPU reference와 동일 |

B1의 성공 기준은 성능 향상보다 **올바른 의사결정과 결과 정합성**이다.

## 6. B2 — Runtime Throughput Feedback

실행 중 최근 throughput을 Scheduler 입력으로 추가한다.

- CPU recent throughput
- GPU recent throughput
- CPU effective capacity
- GPU effective capacity

초기에는 단순 recent-window 관측으로 시작한다. 복잡한 smoothing은 B4에서 다룬다.

## 7. B3 — Live Load Awareness

다음 상태를 추가한다.

- CPU load
- GPU load
- GPU memory pressure
- CPU/GPU queue state
- 외부 CPU/GPU 부하

목표는 높은 GPU utilization이 아니라 현재 시스템에서 합리적인 총 처리량을 유지하는 것이다.

## 8. B4 — Stability Control

Scheduler가 너무 자주 방향을 바꾸지 않도록 다음을 추가한다.

- moving average
- hysteresis
- minimum hold time

목표:

```
관측 변동에 대한 민감도 감소
+
불필요한 CPU↔GPU oscillation 억제
```

## 9. B5 — Transfer / Workload Cost

CPU↔GPU transfer 및 workload-specific cost를 반영한다.

단순한 `GPU throughput > CPU throughput` 비교가 아니라 실제 전체 비용을 기준으로 판단한다.

예:

```
GPU 계산은 빠름
      +
transfer 비용이 큼
      ↓
GPU 전체 이득 감소
      ↓
CPU 중심 배분 가능
```

## 10. B6 — Resource Mode Integration

Resource Mode의 의미를 Scheduler 정책과 연결한다.

- Maximum — 가능한 자원을 적극 사용하는 adaptive policy
- High — 여유 자원을 더 남기는 adaptive policy
- Balanced — 사용자 작업과 검색의 균형을 중시
- Gaming — 보수적인 adaptive policy
- Manual — CPU 제약만 사용자가 지정, GPU는 AUTO

## 11. B7 — Final Scheduler Gate

### 기능

- CPU-only
- GPU OFF
- GPU ON/AUTO
- GPU unavailable fallback
- 느린 GPU의 CPU 중심 전환
- Manual CPU constraint
- Gaming conservative policy

### 안정성

- 반복적인 CPU↔GPU oscillation 억제
- hysteresis 정상
- minimum hold 정상
- 장시간 실행 안정성

### 정확성

CPU reference와 검색 결과가 동일해야 한다.

### 성능

CPU-only 대비 GPU-assisted의 실제 end-to-end throughput을 측정한다. GPU utilization만으로 성공을 판정하지 않는다.

### 관측성

각 scheduler decision에 가능한 범위에서 다음을 기록한다.

- timestamp
- reason
- CPU capacity
- GPU capacity
- CPU/GPU work share
- recent throughput
- queue state
- selected backend
- fallback state

## 12. 구현에서 제외

Node B에서는 다음을 완성하지 않는다.

- 대규모 pipeline 재설계
- barrier 제거
- worker topology 전면 개편
- hardware video decode
- NVDEC
- 신규 GPU backend
- 완성형 calibration system
- adaptive video decode planner

이 항목은 각 후속 Node에서 처리한다.

## 13. 완료 기준

Node B는 다음 네 가지가 동시에 성립하면 종료 게이트를 검토한다.

1. Scheduler가 상황에 맞는 결정을 내린다.
2. 검색 정합성이 유지된다.
3. 장시간 실행에서 안정적이다.
4. 실제 end-to-end throughput을 측정할 수 있다.

버전 번호는 B1~B7에 미리 배정하지 않는다. 각 검증된 코드 상태에서 실제 버전을 결정한다.
