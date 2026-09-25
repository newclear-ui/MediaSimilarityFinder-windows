# Development Roadmap — 0.9.4 개발선

이 문서는 0.9.4 개발선의 상위 개발 방향과 실행 순서를 정의합니다.

핵심 원칙은 개발 단계와 빌드 번호를 분리하는 것입니다.

- A, B, C는 개발 방향과 의존관계입니다.
- B1, B2, B3은 현재 단계에서 발생한 문제의 진단·수정·회귀검증 분기입니다.
- 버전 번호는 단계에 미리 배정하지 않습니다.
- 검증된 코드 상태가 만들어질 때 실제 상황에 맞춰 0.9.4.0 → 0.9.4.1 → 0.9.4.2 → ... 순으로 증가합니다.
- 설계 방향은 Roadmap이, 실제 위치는 Progress가, 실제 코드와 테스트 증거는 build-history가 담당합니다.

## 개발 순서도

START
  |
  v
[A] Foundation / Terminology / Instrumentation
  |
  v
[B] Adaptive Scheduler
  |
  v
[C] Calibration / INI Performance Profile
  |
  v
[D] Pipeline / Queue Optimization
  |
  v
[E] Adaptive Video Decode Planner
  |
  v
[F] Hardware Video Decode Backend
  |
  +--> NVIDIA NVDEC
  |
  v
[G] Additional GPU Backends
  |
  +--> Vulkan
  +--> AMD HIP/ROCm
  +--> Intel Level Zero
  |
  v
[H] Regression / Stability / Performance Validation
  |
  v
NEXT DEVELOPMENT LINE

각 단계는 이전 단계의 종료 조건을 통과한 뒤 다음 단계로 이동합니다.

## 문제 발생 시 분기

B에서 문제가 발생한 경우의 예:

[B]
 |
 +--> [B1] 재현 / 원인 분석
 |       |
 |       v
 |     [B2] 수정 / 보완
 |       |
 |       v
 |     [B3] 회귀검증
 |       |
 |       +---- 실패 ----> [B1]
 |       |
 |       +---- 통과 ----> [B]
 |
 v
[C]

공통적으로 A → A1 → A2 → A3 → A, B → B1 → B2 → B3 → B와 같은 구조를 사용합니다.

전체 방향을 바로 바꾸지 않고 현재 단계 안에서 원인 규명 → 수정 → 검증을 먼저 수행합니다. 구조적 문제가 확인되면 Roadmap과 Progress를 함께 변경합니다.

## Node A — Foundation / Terminology / Instrumentation

상위 GPU 명칭을 vendor-neutral하게 정리하고 GPU ON/OFF 정책, build naming, benchmark instrumentation의 공통 기반을 만듭니다.

주요 내용:
- GPU 수동 사용률 제어 제거
- CPU Resource Mode 유지
- MSF_ENABLE_GPU / MSF_GPU_BACKEND 준비
- build-windows-cpu / build-windows-gpu
- 현재 CUDA를 concrete backend로 유지
- CPU fallback 유지
- measured / not_measured / not_available / partial / failed / fallback 상태
- decodedFrames / sampledFrames 분리
- scheduler / queue / transfer / decoder 계측

종료 조건:
- 상위 GPU와 실제 backend 명칭이 분리됨
- GPU ON/OFF 구조 명확
- benchmark의 미측정=0 오해 제거
- CPU fallback 및 기본 회귀 테스트 통과

## Node B — Adaptive Scheduler

고정 50:50이 아닌 실효 처리능력 기반으로 CPU/GPU 작업량을 동적으로 조정합니다.

입력:
- CPU/GPU baseline capacity
- backend/decoder capability
- 실시간 CPU/GPU 부하
- memory/VRAM
- CPU/GPU queue
- 최근 throughput
- transfer cost
- workload cost
- 외부 프로그램 부하

필수:
- GPU utilization 자체를 최적화 목표로 삼지 않음
- 느린 GPU는 CPU 중심 또는 CPU-only로 수렴 가능
- Gaming은 보수적 adaptive policy
- Manual은 CPU 제약만 사용자 지정
- moving average / hysteresis / minimum hold time
- scheduler decision을 benchmark에 기록

## Node C — Calibration / INI Performance Profile

장기 baseline과 현재 실행의 실측값을 분리합니다.

CPU/GPU fingerprint, resize/conversion, decode, transfer, queue latency 등을 짧게 측정하고 profile id/version/confidence/timestamp를 INI에 저장합니다.

INI는 다음 실행의 초기 추정값이며 live runtime state보다 우선하지 않습니다.

## Node D — Pipeline / Queue Optimization

CPU와 GPU의 병렬성을 높이고 barrier와 worker starvation을 줄입니다.

CPU decode / analysis → GPU hashing / verification → CPU result / DB

필요하면:

Hardware Decode → GPU resize / hash → CPU result / DB

worker 수 증가만으로 해결하지 않고 queue depth, wait, batching, transfer, stage overlap을 함께 측정합니다.

## Node E — Adaptive Video Decode Planner

필요 이상으로 decode하는 비용을 줄입니다.

- planner: sequential / hybrid / sparse seek
- backend: Software FFmpeg / hardware decoder
- sampled frames / decoded frames
- seek count / latency
- decode throughput
- keyframe/GOP cost
- conversion / resize
- fallback

## Node F — Hardware Video Decode Backend

우선 NVIDIA NVDEC을 실제 backend 후보로 연결합니다.

Software FFmpeg은 기준/폴백 경로로 유지하며 codec/profile/pixel-format/bit-depth/capability를 확인합니다. 초기화·seek·frame mapping·decode 실패는 파일 단위 fallback으로 처리합니다.

## Node G — Additional GPU Backends

abstraction이 안정화된 뒤 독립적으로 검토합니다.

- Vulkan
- AMD HIP/ROCm
- Intel Level Zero

실제 장비 검증 전에는 지원 완료로 표시하지 않습니다.

## Node H — Regression / Stability / Performance Validation

CPU-only, GPU OFF, GPU ON/AUTO, low-end simulation, acceleration-not-beneficial, external CPU/GPU load, hardware decode success/fallback, mixed workload, cancellation/partial, accuracy parity, stability, cache compatibility를 함께 검증합니다.

최종 기준은 GPU utilization 하나가 아니라 정확성 + end-to-end throughput + fallback correctness + 안정성 + observability입니다.

## 버전 번호 규칙

Roadmap Node와 버전 번호는 같은 개념이 아닙니다.

Roadmap: A → B → C → D → E → F → G → H
Version: 0.9.4.0 → 0.9.4.1 → 0.9.4.2 → 0.9.4.3 → ...

예를 들어 B 내부에서 B1/B2/B3 문제 해결을 거쳐 하나의 검증 상태가 만들어질 때 다음 버전으로 증가할 수 있습니다.

버전은 결과를 나타내고, Roadmap Node는 방향을 나타냅니다.

## 문서 역할

- development-roadmap.*: 전체 개발 방향과 순서도
- development-progress.*: 현재 단계와 문제 해결 상태
- build-history/*: 실제 버전의 코드 변경과 검증 결과
- architecture/*: 기술 영역별 상세 설계
- AGENTS.md: OpenCode 작업 규칙

OpenCode는 Roadmap과 Progress를 먼저 확인한 후 현재 단계의 상세 프롬프트를 적용합니다.

## 변경 관리

방향을 바꾸어야 할 정도의 문제가 생기면 Progress에 원인을 기록하고 Roadmap과 KO/EN 문서를 함께 수정합니다.
