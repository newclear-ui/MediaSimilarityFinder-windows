# CPU/GPU Adaptive Resource Scheduling

## 문서 상태

- 상태: **승인된 목표 아키텍처**
- 기준 코드: 0.9.4.0
- 공식 보존 기준선: 0.9.2.32
- 이 문서는 현재 구현을 모두 설명하는 문서가 아니라, 다음 자원관리 구조로의 개발 방향을 명시한다.
- 0.9.4.0(Node A) 기준으로 수동 GPU 퍼센트 UI는 제거됐다. CPU 프리셋 의미는 그대로이며, 내부 GPU cap은 Node B 스케줄러가 대체할 때까지 deprecated 상태로 유지한다.

## 1. 기본 원칙

MediaSimilarityFinder는 처음부터 CPU와 GPU를 함께 사용하는 미디어 검색 프로그램이다.

단, 모든 시스템에서 CPU와 GPU를 같은 비율로 사용하는 것이 목표는 아니다.

목표는 다음과 같다.

1. CPU는 시스템 안정성 확보를 위한 사용자 제어 정책을 유지한다.
2. GPU는 사용자가 직접 사용률을 지정하지 않고 ON/OFF만 결정한다.
3. GPU가 ON이면 프로그램이 하드웨어 능력, 현재 시스템 부하, 실제 처리량, 작업 대기열을 관찰하여 GPU 작업량을 자동 결정한다.
4. CPU와 GPU 사이의 작업 비율은 고정된 50:50이 아니라 실제 처리능력에 따라 동적으로 결정한다.
5. 외부 프로그램이 CPU/GPU를 사용하면 MediaSimilarityFinder도 그 상황을 반영하여 작업량을 줄이거나 늘린다.
6. GPU가 느리거나 전송 비용 때문에 불리한 시스템에서는 CPU 중심 또는 CPU-only로 자동 수렴할 수 있어야 한다.
7. GPU 가속 실패 시 CPU 경로가 항상 검색의 정합성과 계속 실행을 보장하는 기준 경로로 남는다.

## 2. 사용자 모드

사용자에게 노출되는 Resource Mode는 다음 다섯 가지를 유지한다.

| 모드 | CPU 정책 | GPU 정책 |
|---|---|---|
| **Maximum** | CPU를 가장 적극적으로 사용 | ON이면 AUTO |
| **High** | 높은 수준의 CPU 사용 | ON이면 AUTO |
| **Balanced** | 균형 잡힌 CPU 사용 | ON이면 AUTO |
| **Gaming** | 게임/무거운 작업과 공존하도록 보수적 CPU 사용 | ON이면 AUTO + 외부 부하에 적극 반응 |
| **Manual** | 사용자가 CPU 자원 한도를 직접 지정 | ON이면 AUTO |

기존의 CPU 프리셋 값은 다음 기준을 유지한다.

- Maximum: 90%
- High: 75%
- Balanced: 55%
- Gaming: 25%
- Manual: 사용자가 CPU 한도를 지정

여기서 위 퍼센트는 작업량을 정확히 그 비율로 나누라는 뜻이 아니라 **CPU 자원 사용 정책의 상한/목표**를 의미한다.

### GPU 설정 원칙

GPU에는 사용자 지정 사용률 옵션을 두지 않는다.

- GPU ON: Adaptive GPU Scheduler가 자동 제어
- GPU OFF: GPU 연산을 사용하지 않고 CPU 경로로 처리

따라서 GPU 25% / 50% / 75% / 100% 같은 수동 사용률 조절 UI는 최종 설계에서 제외한다.

## 3. Adaptive Scheduler

Adaptive Scheduler는 정적인 CPU/GPU 비율을 저장하여 강제하는 기능이 아니다.

각 시점의 **실효 처리능력**을 다음 정보로 계산한다.

- CPU 기본 성능 프로파일
- GPU 기본 성능 프로파일
- 디코더/backend capability
- 현재 CPU 부하
- 현재 GPU 부하
- 메모리/VRAM 여유
- CPU 작업 queue 상태
- GPU 작업 queue 상태
- 실제 최근 처리량(throughput)
- CPU↔GPU 데이터 이동 비용
- 작업 종류별 처리 비용

예를 들어 CPU가 100 units/s, GPU가 500 units/s라면 동일 작업에 대한 50:50 분배를 사용하지 않고 GPU 쪽에 더 많은 작업을 배정한다.

반대로 저사양 iGPU가 CPU보다 느리거나 데이터 이동 비용까지 포함하면 CPU 중심 또는 GPU 0%에 가까운 상태로 자동 전환할 수 있다.

## 4. 최초 실행과 성능 프로파일

프로그램은 검색 시작 전에 하드웨어 capability를 확인한다.

### Capability 확인

- CPU 모델/스레드/SIMD 등
- GPU 공급자/모델/세대/VRAM
- CUDA 및 가능한 GPU backend
- 가능한 하드웨어 video decoder
- codec/profile/pixel-format capability

### Lightweight calibration

긴 벤치마크를 별도로 강제하지 않고, 짧은 probe와 검색 초기 실제 작업을 이용하여 가능한 범위에서 다음을 측정한다.

- CPU fingerprint throughput
- GPU fingerprint throughput
- CPU/GPU resize 및 변환 비용
- CPU decode throughput
- GPU video decode throughput
- CPU↔GPU transfer 비용
- 작업별 queue 대기 시간

초기에는 이 값으로 scheduler의 기본 배분을 정하고, 실제 검색 결과가 누적되면 측정값을 보정한다.

## 5. INI 성능 프로파일

하드웨어의 비교적 장기적인 특성은 INI에 저장하여 다음 검색의 초기값으로 사용한다.

예상되는 정보:

- CPU 식별 정보와 기준 성능
- GPU 식별 정보와 기준 성능
- backend/decoder별 처리능력
- 프로그램/FFmpeg/driver 관련 profile version
- 마지막 calibration 시점
- profile confidence

INI의 성능 프로파일은 **초기 추정값**이며 현재 실행의 실시간 부하보다 우선하지 않는다.

또한 다음과 같은 변경이 발생하면 profile을 재검증하거나 다시 측정할 수 있어야 한다.

- CPU/GPU 교체
- GPU driver 변경
- FFmpeg/backend 변경
- 핵심 알고리즘 변경
- scheduler/backend 구조 변경

## 6. 실시간 시스템 부하 반영

INI의 기본 성능과 별도로 실행 중에는 현재 시스템 상태를 지속적으로 관찰한다.

개념적으로는 다음을 이용하여 현재의 effective capacity를 계산한다.

Baseline capacity + Current resource availability + Queue pressure + Measured throughput

다른 프로그램이 CPU를 많이 사용하는 경우:

MediaSimilarityFinder CPU workers ↓

다른 프로그램이 GPU를 많이 사용하는 경우:

MediaSimilarityFinder GPU workload ↓

반대로 시스템이 유휴 상태가 되면 작업량을 점진적으로 늘릴 수 있다.

스케줄러는 순간적인 변동에 바로 반응하지 않고 이동평균, hysteresis, 최소 유지시간 같은 안정화 장치를 사용하여 worker/workload가 빠르게 진동하지 않도록 한다.

## 7. Gaming 모드

Gaming은 단순히 CPU 수치를 낮게 고정하는 모드가 아니다.

게임 또는 다른 무거운 foreground workload가 존재할 가능성을 전제로:

- CPU worker/workload를 보수적으로 유지
- GPU workload도 AUTO로 보수적으로 시작
- 외부 CPU/GPU 부하가 증가하면 MediaSimilarityFinder를 자동 감속
- 외부 부하가 낮아지면 안전 범위에서 점진적으로 회복

하는 정책으로 정의한다.

## 8. Manual 모드

Manual은 CPU 자원 제어만 사용자가 직접 지정한다.

예:

CPU limit = 40%, GPU = ON

이면:

- CPU: 사용자가 지정한 정책을 준수
- GPU: Adaptive Scheduler가 자동 결정

Manual에서도 scheduler 자체를 끄는 것이 아니다. 사용자의 CPU 한도는 scheduler가 지켜야 할 제약 조건이며, 그 제약 안에서 worker 수와 작업량을 동적으로 조정한다.

## 9. CPU/GPU 병렬 처리의 목표

CPU와 GPU를 함께 사용하는 목적은 단순한 50:50 분배가 아니라 **pipeline parallelism**이다.

가능한 경우 다음처럼 서로 다른 단계가 동시에 진행될 수 있어야 한다.

CPU decode/analysis → GPU hashing/verification → CPU result/DB

또는

NVDEC → GPU resize/hash → CPU result

GPU가 매우 빠른 시스템에서는 GPU가 더 많은 작업을 담당하고, 저사양 시스템에서는 CPU가 대부분을 담당할 수 있다.

따라서 "GPU 사용률을 높이는 것" 자체를 최적화 목표로 삼지 않는다.

최적화 목표는 **검색 전체의 실효 throughput과 사용자 시스템 안정성**이다.

## 10. Video Decode / NVDEC와의 관계

GPU video decode는 별도의 VideoDecodeBackend 계층으로 취급한다.

목표 구조:

- Software FFmpeg decoder: 기준/폴백 경로
- NVIDIA NVDEC: 선택적 가속 backend
- 향후 다른 hardware backend: 같은 추상화에 추가 가능

파일별로 codec/profile/pixel format/driver/backend capability를 확인하고, 사용 가능한 경우에만 hardware decode를 시도한다.

초기 hardware decode 경로는 기존 FFmpeg 추상화를 최대한 활용한다. 저수준 NVDEC API를 핵심 엔진 전체에 직접 확산시키지 않는다.

hardware decode 초기화, seek, frame mapping, decode 등에 실패하면 해당 파일 또는 작업을 Software FFmpeg 경로로 안전하게 전환할 수 있어야 한다.

## 11. 정확성 원칙

CPU와 GPU는 서로 다른 결과를 내기 위한 별도 알고리즘이 아니다.

가능한 경우 CPU/reference 경로를 정합성 기준으로 삼고 GPU는 동일 의미의 연산을 가속한다.

검증 기준은 최소한 다음을 포함한다.

- fingerprint parity
- timestamp/sample alignment
- similarity verdict parity
- hardware decode 실패 시 CPU fallback
- 다양한 codec/profile/pixel format에서의 회귀 테스트

GPU 사용량이나 처리속도가 높아지는 대신 검색 판정이 달라지는 것은 허용하지 않는다.

## 12. 개발 순서도와 상세 구현의 관계

이 문서는 자원관리의 상세 설계를 설명합니다. 실제 구현 순서는 `docs/development-roadmap.ko.md`의 상위 순서도를 따릅니다.

A Foundation / Instrumentation
        |
        v
B Adaptive Scheduler
        |
        v
C Calibration / INI Profile
        |
        v
D Pipeline / Queue
        |
        v
E Adaptive Video Decode
        |
        v
F Hardware Decode Backend
        |
        v
G Additional GPU Backends
        |
        v
H Regression / Validation

문제가 발생하면 현재 node 안에서 A1/B1/C1 같은 recovery branch를 사용합니다. 진단 → 수정 → 회귀검증 후 같은 node gate로 돌아갑니다.

Benchmark / Telemetry는 모든 node에서 함께 발전하는 계층입니다. 상세 schema와 측정 상태는 benchmark-telemetry-roadmap 문서를 따릅니다.

## 13. 금지할 단순화



다음 방식은 목표 아키텍처로 채택하지 않는다.

- CPU/GPU를 항상 50:50으로 고정
- GPU 사용률을 사용자가 직접 25/50/75%로 설정
- GPU utilization 하나만 보고 작업량 결정
- GPU가 있다고 해서 모든 파일을 강제로 GPU decode
- 저사양 GPU에서도 GPU 사용을 강제
- NVDEC 실패를 전체 검색 실패로 처리
- 단순히 CPU worker 수만 늘려 해결하려는 방식
- GPU 사용률 자체를 성능 목표로 삼는 방식

## 14. 기준선 보존

이 설계는 공식 GPU 기준선 0.9.2.32를 수정하거나 덮어쓰지 않는다.

구현은 현재 개발선 0.9.3.x에서 단계적으로 진행한다.

CPU fallback은 항상 유지하며, hardware acceleration은 선택 가능한 backend로 취급한다.

 
## 15. 실측 기반 1.0 이후 성능 개선 백로그

현재 Adaptive Scheduler와 이미지 검색 경로는 **1차 제품 구현**으로 분류한다. 다만 0.9.4.59 실제 GUI 실측에서 다음 현상이 관찰되었으므로 1.0 이후 성능 개선 대상으로 명시한다.

- Maximum(90%) 정책에서도 실제 process CPU 사용률은 장시간 구간에서 약 20~70%로 진동했고 평균은 30.8%였다. 사용자 CPU 정책 퍼센트와 실제 CPU 점유율을 동일 개념으로 해석해서는 안 되며, worker occupancy/admission/backpressure 모델을 더 정교하게 만들어야 한다.
- Balanced는 초기 관찰에서 8코어/16스레드 환경에서도 실제 작업 스레드 활용이 약 4개 수준으로 낮게 시작했다. worker 수만 늘리는 것이 아니라 I/O 대기, decode latency, queue admission을 함께 모델링해야 한다.
- 32,494개 이미지가 GPU 경로를 사용한 실측에서도 GPU duty는 약 0.2%였다. GPU kernel 자체보다 WIC/CPU decode, crop, 파일 I/O, verification 및 짧은 GPU 작업 구간 사이의 빈 구간이 더 큰 병목 후보이다.
- 관찰된 drive I/O 활용도 역시 대체로 15~40% 수준에서 변동했다. 따라서 단순히 CPU worker를 추가하는 것만으로는 전체 throughput을 안정적으로 높인다고 볼 수 없다.

### 1.0 이후 scheduler 고도화 방향

다음 개선은 **GPU utilization 자체를 목표로 하지 않는다.** 목표는 end-to-end throughput, latency 안정성, 사용자 시스템과의 공존이다.

1. 실제 CPU worker occupancy와 CPU policy의 관계를 계측하고 admission을 조정한다.
2. 파일 read → decode → crop → hash 사이의 bounded queue와 backpressure를 workload-aware하게 조정한다.
3. GPU batch가 너무 짧게 실행되거나 CPU 단계 때문에 장시간 idle하는 구간을 줄여 host↔device overlap을 높인다.
4. scheduler 입력을 aggregate CPU/GPU utilization만으로 제한하지 않고 stage bottleneck, 자기 사용량, I/O wait, queue starvation을 분리한다.
5. live load SMA/hysteresis만으로는 해결되지 않는 장주기 oscillation과 initial under-utilization을 workload-level admission 문제로 재평가한다.

이 항목들은 **0.9.4.59 correctness 수정 범위에 포함하지 않으며**, 1.0 이후 별도 성능 개선 트랙으로 다룬다. 현재의 낮은 GPU duty는 correctness failure나 GPU backend failure로 해석하지 않는다.

### 이미지 검색 엔진 상태

이미지 검색 엔진은 **1차적으로 제품 경로가 완성된 상태**다. 이후 개선은 correctness를 깨지 않는 범위에서 실제 dataset 기반으로 진행하며, 새 성능 후보는 exactness/semantic parity 검증을 선행한다.

## 15. Benchmark / Telemetry 연계

Adaptive Scheduler는 benchmark 없이는 충분히 검증할 수 없으므로 benchmark를 독립적인 핵심 개발 단계로 취급한다.

0.9.4.x에서는 scheduler가 결정한 CPU/GPU 배분, calibration 결과, backend 선택, queue 상태, fallback 이유, 실제 throughput을 benchmark가 기록해야 한다.

특히 benchmark가 꺼져 있거나 측정할 수 없는 항목을 0으로 기록하여 실제 사용량이 0인 것처럼 보이게 하지 않는다.

상세 benchmark 설계는 [Benchmark and Runtime Telemetry Roadmap](benchmark-telemetry-roadmap.ko.md)을 따른다.

### Development Roadmap 공통 benchmark 조건

다음 항목은 특정 빌드 번호에 고정하지 않고 각 Roadmap node의 완료조건으로 검증한다.

- benchmark schemaVersion
- measured / not_measured / not_available / partial / failed / fallback 상태
- scheduler decision telemetry
- calibration telemetry
- GPU backend capability 및 selected backend
- video decoder/backend/fallback reason
- decodedFrames와 sampledFrames 분리
- queue wait / transfer time
- cancellation / partial-result 상태
- human-readable 요약과 machine-readable JSON의 분리
