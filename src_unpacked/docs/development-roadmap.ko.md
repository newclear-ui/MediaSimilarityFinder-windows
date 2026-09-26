# Development Roadmap — 0.9.4 개발선

## 문서 계층

Roadmap과 세부 implementation brief는 역할을 분리합니다.

- **Roadmap**: 전체 방향, 의존관계, Node 경계, 변경 관리 규칙.
- **Progress**: 현재 실제 Node, blocker, 검증 상태, recovery history.
- **Implementation Brief**: 현재 활성 Node를 구현하기 위한 집중된 기술 계약. 단계별 범위, 경계, telemetry, 종료 조건을 기록합니다.
- **Build History**: 실제 버전에서 무엇을 변경했고 어떻게 검증했는지의 증거.

현재 B/C/D 세부 문서:

- `docs/implementation-briefs/B-adaptive-scheduler.ko.md / .en.md`
- `docs/implementation-briefs/C-calibration-profile.ko.md / .en.md`
- `docs/implementation-briefs/D-pipeline-queue.ko.md / .en.md`

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

B는 **실효 처리능력 기반의 CPU/GPU 작업 배분 정책**을 담당합니다. 고정 50:50이 아니며 GPU utilization 자체를 최적화 목표로 삼지 않습니다.

세부 구현:
- `docs/implementation-briefs/B-adaptive-scheduler.ko.md`
- B1 최소 배분 → B2 throughput → B3 live load → B4 안정화 → B5 cost → B6 Resource Mode → B7 최종 gate

B의 경계는 "작업을 어디에 얼마나 배분할 것인가"이며 실제 queue/worker/pipeline 구현은 D에 둡니다.

## Node C — Calibration / INI Performance Profile

C는 기존 profile/benchmark 구조를 **부분 재사용하면서 확장**합니다.

상대적으로 안정적인 baseline과 live measurement를 분리하고 profile id/version/confidence 등을 INI에 저장하여 Scheduler의 초기 추정값으로 제공합니다. Live runtime state가 항상 우선합니다.

세부 구현:
- `docs/implementation-briefs/C-calibration-profile.ko.md`

## Node D — Pipeline / Queue Optimization

D는 **신규 pipeline/queue 설계**입니다. Scheduler가 결정한 작업을 실제로 어떻게 흘려보낼지 담당하며 barrier, worker starvation, queue imbalance, transfer stall, 불필요한 serialization을 줄입니다.

첫 단계에서는 기존 구조를 크게 바꾸지 않고 관측성을 확보한 뒤 단계적으로 최적화합니다.

세부 구현:
- `docs/implementation-briefs/D-pipeline-queue.ko.md`

B와 D는 역할을 섞지 않습니다. B는 allocation policy, D는 execution pipeline입니다.

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
