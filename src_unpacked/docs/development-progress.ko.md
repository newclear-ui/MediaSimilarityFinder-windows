# Development Progress — 0.9.4 개발선

## 문서 목적

이 문서는 development-roadmap.ko.md의 **현재 실제 실행 상태**를 기록합니다.

Roadmap은 개발 방향의 뼈대이고, Progress는 실제 위치, 문제, 회복 분기를 기록합니다.

## 현재 상태

| 항목 | 상태 |
| --- | --- |
| 기준 코드 | 0.9.3.19 |
| 공식 보존 기준선 | 0.9.2.32 |
| 개발선 | 0.9.4 |
| 현재 노드 | A — Foundation / Terminology / Instrumentation |
| 현재 단계 | 설계/문서 기준선 정리 완료 → 소스 구현 진입 |
| 현재 버전 | 0.9.3.19 |
| GPU 구현 기준 | NVIDIA CUDA |
| CPU fallback | 유지 |
| 프로젝트-local vcpkg | 유지, 이전하지 않음 |

## 개발 순서 상태

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
 v
[G] Additional GPU Backends
 |
 v
[H] Regression / Stability / Performance Validation

현재 문서 작업은 A의 기준선을 만든 것이며, 현재 0.9.3.19 소스가 0.9.4 구조로 완전히 전환되었다는 뜻은 아닙니다.

## 완료된 준비 작업

### A0 — Development framework

완료:
- Development Roadmap KO/EN
- Development Progress KO/EN
- CPU/GPU Adaptive Resource Scheduling 설계 정리
- GPU Backend Roadmap 정리
- Benchmark / Telemetry Roadmap 정리
- AGENTS.md에 0.9.4 작업 규칙 반영
- STRUCTURE.md / llms.txt / README 계열에서 새 문서 체계 반영

이 변경은 문서 구조 변경이며 0.9.3.19의 scheduler/backend 소스 자체를 변경한 것은 아닙니다.

## Node A — Foundation / Terminology / Instrumentation

### 현재 목표

- 상위 GPU 명칭 일반화
- GPU ON/OFF만 UI에 남기고 수동 GPU 퍼센트 제어 제거
- Resource Mode CPU 정책 보존
- MSF_ENABLE_GPU / MSF_GPU_BACKEND 구조 준비
- build-windows-gpu 명칭 정리
- 기존 CUDA backend를 concrete backend로 유지
- benchmark schemaVersion
- measurement states
- scheduler / queue / transfer / decoder telemetry
- decodedFrames / sampledFrames 분리
- cancellation / partial 상태
- 기존 검색 정합성 보존

### Node A 종료 조건

- CPU-only 정상
- GPU OFF 정상
- GPU ON에서 기존 CUDA 경로가 backend abstraction을 통해 실행
- 수동 GPU utilization UI 제거
- benchmark의 unmeasured=0 문제 제거
- benchmark instrumentation이 검색 결과를 변경하지 않음
- 기본 regression tests 통과
- build tree / CMake naming 정리
- 문서/코드/테스트 버전 일치

## Recovery branch 기록

문제가 생기면 버전 번호가 아니라 현재 노드의 하위 작업으로 기록합니다.

예:

A
|
+-- A1: CMake migration error
|
+-- A2: option compatibility fix
|
+-- A3: CPU build regression
|
+-- A4: GPU smoke
|
+-- A5: final A gate
|
+---- fail --> A1/A2/...
+---- pass --> B

각 하위 작업은 다음을 기록합니다.

- 증상
- 재현 조건
- 원인
- 해결책
- 변경 파일
- 테스트
- 실패했던 시도
- 해결 후 결과
- 다음 gate 영향

## 버전 진행 규칙

버전은 **통과한 코드 상태**를 기록합니다.

예:
- 0.9.4.0 = A 초기 구현 기준점
- 0.9.4.1 = A 수정/검증 완료
- 0.9.4.2 = B 진입 가능한 검증 상태
- 0.9.4.3 = B 내부 다음 안정화 지점

위 숫자는 예시일 뿐이며 실제 의미는 build-history 문서가 최종 증거입니다.

## Roadmap / Progress / Build History

Development Roadmap
        |
        v
Development Progress
        |
        v
Build / Test
        |
        v
docs/build-history/<version>.ko.md
docs/build-history/<version>.en.md

즉 Roadmap → Progress → Build History 순서로 보면 설계 의도 → 현재 위치 → 실제 코드/테스트 결과를 확인할 수 있습니다.

## OpenCode 작업 원칙

OpenCode는 새 작업을 시작할 때 다음을 먼저 읽습니다.

1. AGENTS.md
2. development-roadmap.ko.md 또는 .en.md
3. development-progress.ko.md 또는 .en.md
4. 현재 노드에 해당하는 architecture 문서
5. 필요한 기존 소스와 테스트

현재 노드의 종료 조건을 먼저 확인한 뒤 구현합니다.

문제가 발생하면 A1/B1/C1 같은 하위 작업으로 기록하고 해결 후 같은 노드의 gate로 복귀합니다.

## 다음 상태 갱신

소스 구현이 시작되면 다음 항목을 실제 값으로 갱신합니다.

- Current Node
- Current Version
- Active Substep
- Blocker
- Validation Result
- Next Gate

**이 문서는 미래 계획을 예측하는 문서가 아니라 현재 개발 위치를 잃지 않기 위한 상태 기록입니다.**
