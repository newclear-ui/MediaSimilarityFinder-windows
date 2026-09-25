# Development Session Handoff — 2026-09-26

> 새 채팅/새 OpenCode 세션에서 MediaSimilarityFinder 개발을 이어가기 위한 인계 문서.
> 이 문서는 소스 코드를 변경하지 않으며, 현재 개발 방향과 상태를 복구하기 위한 기준이다.

## 반드시 가장 먼저 확인할 두 가지

### 1. project-local vcpkg 유지

현재 project-local `vcpkg_installed`를 그대로 사용한다.

현재 Windows CPU 빌드 + 실행 + 전체 검증이 완전히 끝나기 전에는 공용/public vcpkg 경로로 이동하지 않는다.

마이그레이션이 필요해지는 시점에는 기존 설치를 즉시 삭제하지 않는다. 먼저 사용자에게 **“이제 vcpkg 정리/이동 단계입니다”**라고 알리고 단계적으로 진행한다.

현재 local 경로:
`C:\MediaSimilarityFinder-v0.9.2.33-Windows-CPU-Test-source-ready\vcpkg_installed\x64-windows`

### 2. Windows CPU build 문제 추적

Windows CPU 테스트에서 발견되는 모든 build/compatibility 문제는 계속 기록한다.

전체 CPU build + 실행 + 검증이 끝난 후 `docs/build-history/`에 KO/EN 쌍으로 정리한다.

기록 항목:
- 변경 필요성
- 기존 구조
- 변경 구조
- 선택 이유
- 원래 오류
- 해결된 문제
- 정확성 영향
- Windows/GPU/Linux compatibility 영향
- 실제 build/test 결과
- 향후 영향

---

# 1. 프로젝트 기준선

프로젝트:
`newclear-ui/MediaSimilarityFinder-windows`

공식 보존 기준선:

**0.9.2.32**

이 기준선은 절대로 수정하거나 덮어쓰지 않는다.

현재 기존 개발/검증 기준:

**0.9.3.19**

최근 확인된 0.9.3.19 source commit:
`6b75837c2f83698225afdcacd633808dbb176484`

현재 개발 방향:

**0.9.4 development line**

중요: 0.9.4는 아직 전체 구현 완료 상태가 아니다. 현재는 설계/개발 관리 문서 체계를 정리한 단계이며, 0.9.3.19 source는 아직 legacy fixed CPU/GPU percentage policy와 GPU percentage UI를 포함한다.

---

# 2. 현재 가장 중요한 개발 관리 원칙

이제부터는 **개발 단계와 빌드 번호를 절대로 1:1로 매핑하지 않는다.**

잘못된 방식:

```
0.9.4.0 = Foundation
0.9.4.1 = Scheduler
0.9.4.2 = Pipeline
0.9.4.3 = Adaptive Decode
```

새 방식:

```
Roadmap:
A -> B -> C -> D -> E -> F -> G -> H

Version:
0.9.4.0 -> 0.9.4.1 -> 0.9.4.2 -> 0.9.4.3 -> ...
```

버전은 실제로 **검증된 코드 상태**가 만들어질 때 증가한다.

하나의 Node에서 여러 버전이 발생할 수 있다.

문제 발생 시:

```
B
|
+-- B1 diagnose
+-- B2 fix
+-- B3 regression
+-- B4 validation
|
+---- fail -> B1/B2
+---- pass -> B gate
```

이렇게 현재 Node 내부의 recovery branch로 기록한다.

---

# 3. 현재 Development Roadmap

```
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
```

### Node A
GPU terminology, GPU ON/OFF policy, build naming, benchmark instrumentation의 공통 기반.

### Node B
Adaptive Scheduler. 고정 50:50 금지. 하드웨어 capability, calibration, live load, throughput, queue, transfer cost, workload cost를 이용.

### Node C
Calibration + INI Performance Profile.

### Node D
CPU/GPU pipeline 및 queue optimization. barrier와 worker starvation 감소.

### Node E
Adaptive Video Decode Planner. sequential/hybrid/sparse 전략을 workload에 맞춰 선택.

### Node F
Hardware Video Decode Backend. Software FFmpeg을 기준/폴백으로 유지하면서 우선 NVIDIA NVDEC 연결.

### Node G
Vulkan / AMD HIP-ROCm / Intel Level Zero를 실제 검증 기반으로 독립 검토. 가짜 지원 금지.

### Node H
CPU-only, GPU OFF, GPU ON/AUTO, low-end simulation, external load, decoder success/fallback, cancellation/partial, accuracy parity, stability, cache compatibility 등을 통합 검증.

---

# 4. 현재 Progress

현재 Node:

**A — Foundation / Terminology / Instrumentation**

현재 단계:

**설계/문서 기준선 정리 완료 → 소스 구현 진입**

현재 version:

**0.9.3.19**

현재 GPU compute 기준:

**NVIDIA CUDA**

CPU fallback:

**유지**

이번 단계에서 아직 하지 않은 것:

- Adaptive Scheduler 실제 구현 완료 아님
- INI calibration 실제 구현 완료 아님
- Pipeline redesign 완료 아님
- Adaptive Video Decode 구현 완료 아님
- NVDEC backend 구현 완료 아님
- Vulkan/HIP/Level Zero 구현 완료 아님

---

# 5. 현재 문서 구조

Roadmap → Progress → Build/Test → Build History 관계를 사용한다.

```
Development Roadmap
        |
        v
Development Progress
        |
        v
Build / Test
        |
        v
Build History
```

주요 문서:

```
src_unpacked/docs/
├─ development-roadmap.ko.md
├─ development-roadmap.en.md
├─ development-progress.ko.md
├─ development-progress.en.md
├─ architecture/
│  ├─ resource-scheduling.ko.md
│  ├─ resource-scheduling.en.md
│  ├─ gpu-backend-roadmap.ko.md
│  ├─ gpu-backend-roadmap.en.md
│  ├─ benchmark-telemetry-roadmap.ko.md
│  └─ benchmark-telemetry-roadmap.en.md
├─ build-history/
├─ STRUCTURE.md
└─ llms.txt
```

`AGENTS.md`에는 이 문서 체계를 따르는 작업 규칙이 들어 있다.

---

# 6. 문서 역할

### Development Roadmap
전체 개발 방향과 순서를 정의한다.

### Development Progress
현재 실제 Node, Version, Active Substep, Blocker, Validation Result, Next Gate를 기록한다.

### Implementation Brief
향후 각 Node별 상세 실행 지침을 저장할 예정.

권장 위치:
`src_unpacked/docs/implementation-briefs/`

예:
`A-foundation.ko.md`
`A-foundation.en.md`

### Architecture
기술 상세 설계.

### Build History
실제 버전별 변경과 검증의 증거.

---

# 7. 현재 OpenCode 운영 방식

OpenCode에는 전체 0.9.4 구현을 한 번에 시키지 않는다.

OpenCode가 먼저:

1. `AGENTS.md`
2. Development Roadmap
3. Development Progress
4. 현재 Node의 Architecture 문서
5. 현재 source/test

를 읽는다.

그 후 **현재 Node 하나만 구현한다.**

Node 종료 조건을 통과하기 전 다음 Node를 구현하지 않는다.

문제 발생 시 현재 Node의 A1/B1/C1 형태 하위 작업으로 기록한다.

---

# 8. 다음 실제 작업

가장 먼저 할 작업은:

**Node A Implementation Brief를 만들고 저장**

권장 파일:

```
src_unpacked/docs/implementation-briefs/
├─ A-foundation.ko.md
└─ A-foundation.en.md
```

기존 대형 OpenCode 프롬프트를 그대로 저장하지 말고, 다음 원칙으로 압축한다.

- Roadmap/Progress에 이미 있는 내용은 중복하지 않는다.
- Architecture 문서를 참조한다.
- 현재 Node A에서 실제로 해야 할 코드 변경만 기술한다.
- 완료 조건과 테스트를 명확하게 기술한다.
- 문제가 발생했을 때 Recovery Branch를 Progress에 기록하도록 한다.

OpenCode에는 이후 다음 정도의 짧은 지시만 전달하는 것을 목표로 한다.

```
Read AGENTS.md.
Read development-roadmap.
Read development-progress.
Identify the current node.
Read that node's implementation brief and architecture documents.
Implement only the current node.
Record problems as node substeps.
Build, test, validate, and update Progress.
Do not enter the next node before the current node gate passes.
```

---

# 9. Node A 핵심 구현 내용

Node A는 다음 범위를 다룬다.

### GPU abstraction

상위 계층:

```
GPU
 |
 +-- CUDA
 +-- Vulkan (future)
 +-- HIP/ROCm (future)
 +-- Level Zero (future)
```

현재 실제 backend는 CUDA만 존재한다.

### User resource model

CPU:

- Maximum
- High
- Balanced
- Gaming
- Manual

GPU:

- ON
- OFF

GPU utilization percentage를 사용자가 직접 지정하지 않는다.

### CMake/build naming

목표:

- `build-windows-cpu`
- `build-windows-gpu`

상위 옵션:

- `MSF_ENABLE_GPU`
- `MSF_GPU_BACKEND`

필요하면 기존 `MSF_ENABLE_CUDA`를 compatibility alias로 일시 유지할 수 있다.

기존 `build-windows-cuda` tree는 새 GPU tree가 검증되기 전까지 보존한다.

### Benchmark / Telemetry

Benchmark는 핵심 instrumentation layer다.

반드시 measurement state를 구분:

- measured
- not_measured
- not_available
- partial
- failed
- fallback

측정하지 않은 값을 numeric zero로 표현하지 않는다.

특히:

- decodedFrames
- sampledFrames

를 분리한다.

또한 scheduler decision, queue, transfer, backend, decoder, fallback 정보를 수용할 수 있어야 한다.

---

# 10. 정확성 보호 원칙

Benchmark instrumentation이나 scheduler 변경 때문에 검색 결과가 바뀌면 안 된다.

CPU/reference와 GPU의 의미가 달라지지 않아야 한다.

확인 대상:

- fingerprint parity
- similarity result
- timestamp/sample alignment
- cache semantics
- fallback correctness

---

# 11. 하드웨어 Decode 방향

VideoDecodeBackend를 별도로 둔다.

현재:

**Software FFmpeg = reference/fallback**

미래:

**NVIDIA NVDEC**

planner와 backend를 분리한다.

Planner:

- sequential
- hybrid
- sparse seek

Backend:

- Software FFmpeg
- hardware decoder

hardware decode 실패 사유는 benchmark에 남긴다.

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

---

# 12. 기존 중요한 기술 문제

0.9.3.19 video path에서 이미 확인된 중요한 병목:

- `video_decoder.cpp::framesAt()`가 sample target 사이의 모든 frame을 decode하는 구조
- sample마다 `sws_getContext` 생성/삭제
- `processVideoRange`의 future/barrier 구조로 straggler가 발생할 수 있음
- 기존 GPU hash path의 global `hashMutex_`
- crop pass에서 decode/open이 중복될 가능성
- GPU utilization/benchmark가 detail=false에서 실제 작업을 0으로 표현할 수 있었음
- video benchmark에서 decodedFrames와 sampledFrames가 분리되지 않음

따라서 Adaptive Video Decode는 단순 NVDEC 연결만으로 해결하는 것이 아니라 decode waste 자체를 줄이는 방향이어야 한다.

---

# 13. Build / Environment 주의

Windows:

- Windows 10 Pro 2009 build 26200
- Visual Studio Community 2026 / 18.10.12201.205
- MSVC 19.51.36257.0
- Windows SDK 10.0.26100.0
- CMake 4.4.2
- Git 2.55.0.windows.5

vcpkg:

- project-local `vcpkg_installed` 유지
- `C:\src\vcpkg`는 bootstrap/root context로만 존재
- project-local install을 먼저 사용

GPU:

- 현재 NVIDIA CUDA implementation

현재 old build tree:

`build-windows-cuda`

향후:

`build-windows-gpu`

기존 tree를 즉시 rename-in-place하지 않고 clean configure 방식으로 전환한다.

---

# 14. GitHub 현재 문서 상태

이번 세션에서 다음 개발 관리 문서를 GitHub에 추가/정리했다.

- `src_unpacked/docs/development-roadmap.ko.md`
- `src_unpacked/docs/development-roadmap.en.md`
- `src_unpacked/docs/development-progress.ko.md`
- `src_unpacked/docs/development-progress.en.md`

관련 문서에도 Roadmap/Progress 구조를 연결했다.

루트 README와 AGENTS, STRUCTURE, llms, resource-scheduling, gpu-backend-roadmap, benchmark-telemetry-roadmap을 이 모델에 맞춰 수정했다.

이번 문서 정리에서는 **실제 0.9.4 source implementation은 하지 않았다.**

---

# 15. 다음 채팅에서 바로 사용할 시작 문장

```
MediaSimilarityFinder 개발을 이전 세션에서 이어간다.

먼저 GitHub의
src_unpacked/docs/development-chat-handoff.ko.md
와 AGENTS.md를 읽어라.

그 다음 development-roadmap과 development-progress를 읽고
현재 Node와 Active Substep을 확인하라.

현재 Node만 작업하고 다음 Node로 넘어가지 마라.
```

