# Implementation Brief — Node C Calibration / INI Performance Profile

## 1. 목적

Node C는 Scheduler가 매 실행마다 완전히 초기 상태에서 시작하지 않도록 CPU/GPU 처리능력과 주요 비용을 짧게 측정하고 비교적 안정적인 값을 INI Performance Profile로 보존한다.

Node C는 완전한 신규 구조가 아니다. 기존 profile 및 benchmark 개념을 재사용하면서 Scheduler가 사용할 수 있도록 확장한다.

핵심은:

```
기존 profile 재사용
+
측정 항목 확장
+
profile identity
+
confidence
+
versioning
+
Scheduler 초기값 연결
```

## 2. Long-term Profile과 Live Runtime State

### Long-term Profile

INI에 저장한다.

- CPU fingerprint baseline
- GPU fingerprint baseline
- resize/conversion baseline
- transfer baseline
- decode baseline
- queue baseline

### Live Runtime State

현재 실행의 실제 관측값이다.

- 최근 throughput
- 현재 load
- 현재 queue
- 현재 transfer
- 현재 backend 상태

우선순위:

```
INI Profile
   ↓
Initial Estimate
   ↓
Live Measurement
   ↓
Scheduler Decision
```

INI Profile은 live runtime state를 덮어쓰지 않는다.

## 3. Profile Identity

최소 정보:

- profileId
- profileVersion
- createdAt / updatedAt
- confidence
- CPU fingerprint
- GPU fingerprint
- backend identity
- 관련 driver/backend 정보

현재 환경과 profile이 맞지 않으면 재사용을 제한하거나 낮은 confidence로 시작한다.

## 4. Calibration 범위

처음부터 긴 종합 benchmark를 실행하지 않는다.

측정 후보:

### CPU
- fingerprint throughput

### GPU
- fingerprint throughput
- batch throughput

### 공통
- CPU↔GPU transfer
- resize/conversion

### Decode
- CPU decode baseline
- hardware decode feasibility

### Queue
- queue latency

측정 불가능한 항목은 명시적인 measurement state로 남긴다.

## 5. Measurement State

Node A의 상태 체계를 그대로 사용한다.

- measured
- not_measured
- not_available
- partial
- failed
- fallback

미측정값을 numeric zero로 저장하지 않는다.

## 6. Calibration 전략

### Initial Calibration

profile이 없거나 profile identity가 현재 환경과 맞지 않는 경우 수행한다.

### Opportunistic Recalibration

profile은 존재하지만 runtime 관측과 반복적으로 차이가 날 때 짧게 재측정한다.

모든 실행에서 긴 calibration을 강제하지 않는다.

## 7. Confidence

profile에는 confidence를 저장한다.

driver/backend 변경, 하드웨어 변경, engine 변경, 반복적인 runtime deviation, calibration 실패 등은 confidence를 낮추는 원인이 될 수 있다.

## 8. Profile Versioning

profile 구조나 측정 의미가 바뀌면 profileVersion을 변경한다.

다음 버전은 서로 다른 개념으로 유지한다.

- Application Version
- Engine Version
- Database Version
- Benchmark Schema Version
- Profile Version

## 9. Scheduler 연결

Node C의 출력은 Scheduler의 initial estimate다.

```
Profile
  ↓
CPU/GPU initial capacity
transfer/conversion estimate
  ↓
Scheduler
  ↓
Live measurement
  ↓
updated decision
```

오래된 profile이 live runtime보다 우선해서는 안 된다.

## 10. Profile 갱신

단일 실행의 이상값으로 profile을 즉시 덮어쓰지 않는다.

profile 갱신 구조는 다음 정보를 추적할 수 있어야 한다.

- old profile
- new profile
- reason
- confidence before
- confidence after

## 11. 범위 밖

- Scheduler 정책 재설계
- 대규모 Pipeline 변경
- worker topology 변경
- NVDEC 최적화
- video planner
- 신규 GPU backend

## 12. 완료 기준

- profile 생성
- profile 재사용
- profile identity 검증
- confidence 기록
- stale/invalid profile 식별
- Scheduler initial estimate 연결
- live runtime 우선
- calibration이 검색 정합성에 영향을 주지 않음
- measurement state 명확
- CPU-only 환경에서 정상 동작

Node C의 목표는 완벽한 예측값이 아니라 **다음 실행의 합리적인 초기값과 runtime 교정의 기반**을 제공하는 것이다.
