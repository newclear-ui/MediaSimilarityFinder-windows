# A — Node A: Foundation / Terminology / Instrumentation

**상태: CLOSED** (완료된 Node 의 참조 계약). 실제 구현·측정 근거는
`docs/build-history/0.9.4.0.ko.md` / `.en.md` 이다.

> 이 문서는 종료된 Node 의 계약을 한 곳에 모아 두기 위한 **참조용** 문서이며,
> 원본 근거는 build-history 0.9.4.0 과 `docs/development-roadmap.ko.md` Node A 절이다.
> Node A 는 이미 종료되었으므로 이 문서를 근거로 **새 설계를 시작하지 않는다.**
> Node A 영역을 다시 건드려야 하면 먼저 build-history 0.9.4.0 을 읽고 현재 코드와 대조한다.

## 1. 목적

상위 GPU 추상화를 vendor-neutral 한 용어로 정리하고, 사용자에게 GPU 를 ON/OFF
두 값으로만 노출하며, build 명명과 benchmark instrumentation 을 성립시킨다.
검색 판정 로직은 건드리지 않는다.

## 2. 확정한 용어 / 구조 (구현 결과)

| 항목 | 확정한 형태 |
|---|---|
| GPU backend | `GpuBackendKind { Auto, Cuda, Cpu }` + `backendName()`. Auto 기본, CUDA 있으면 CUDA, 없으면 CPU fallback |
| CMake 옵션 | `MSF_ENABLE_GPU` + `MSF_GPU_BACKEND`(AUTO/CUDA/CPU). `MSF_ENABLE_CUDA` 는 deprecated 호환 별칭 |
| 소스 매크로 | `MSF_HAS_GPU` 와 `MSF_HAS_CUDA` **둘 다** 정의한다. 정식어는 `MSF_HAS_GPU` |
| Build entry point | `build-windows-cpu` / `build-windows-gpu` + `scripts/build_windows_cpu.ps1` / `scripts/build_windows_gpu.ps1` |
| 측정 상태 | `MeasureState` = measured / not_measured / not_available / partial / failed / fallback |
| 프레임 계측 | `decodedFrames` 와 `sampledFrames` **분리**. sampler 미실행이면 `not_measured` |
| CPU 제어 | `cpuPercent` 만 사용자 편집 가능. `gpuPercent` 는 deprecated 내부값 |
| CPU fallback | 항상 유지 |

## 3. 종료 조건 (모두 충족)

- 기존 GPU UI(GPU 사용률 슬라이더·숫자spin·모니터 항목) 제거, GPU 는 ON/OFF 체크박스만
- GPU ON/OFF 구조 완비, backend 미가용 시 GUI 오류 표시
- benchmark 의 "미측정 = 0" 문제 제거
- CPU fallback 동작 문제 없음 + 회귀 테스트
- 검색 판정에 닿는 코드 무변경 (지문·유사도·임계·캐시·타임스탬프)
- build / CMake 정리, 버전 일치

## 4. 반드시 유지할 불변식

- **계측은 검색 결과를 바꾸지 않는다.** 추가된 것은 읽기 전용 누적 + JSON 직렬화뿐.
- **미측정 값을 0 으로 기록하지 않는다.** 반드시 `MeasureState` 로 구분한다.
- **CPU fallback 은 항상 유지한다.**
- benchmark JSON 은 additive 확장이다. 기존 키를 유지하고 새 키를 추가하며, `schemaVersion` 은
  엔진/DB 버전과 독립적으로 1 에서 시작한다.

## 5. deprecated 상태인 항목 (2026-10-04 현재 코드 확인)

아래는 **아직 남아 있다.** Node A 가 정한 방향과 어긋나므로, 건드릴 때 함께 정리한다.

| 항목 | 현재 상태 | 비고 |
|---|---|---|
| `ResourcePolicy::gpuPercent` | **존재 (25 참조)** | `resource_policy.h:6` 주석이 "deprecated internal" 이라고 명시. 사용자 편집 불가. `resource_policy.cpp` 가 mode 별 기본값으로 계속 설정 |
| `MSF_ENABLE_CUDA` CMake option | 존재 (CMakeLists deprecated alias) | src/gui 소스 참조는 0건. 별칭으로만 유지 |
| `MSF_HAS_CUDA` | 정의됨 (12 참조) | `MSF_HAS_GPU` 와 병존. 정식어는 `MSF_HAS_GPU` |
| `CMakePresets.json` 의 `windows-cuda` / `windows-cuda-release` | 존재 | `binaryDir` 는 `build-windows-cuda` |
| `scripts/build_windows_cuda.ps1` | 존재 | canonical 은 `build_windows_gpu.ps1` |
| `build-windows-cuda/` 트리 | **없음** | 구 트리는 보존하지 않고 이름 변경 재사용도 하지 않는다 |

> `gpuPercent` 삭제는 원래 Node B 스케줄러와 함께 처리하기로 했으나, Node B 가 CLOSED 가
> 된 현재에도 남아 있다. 정리 여부는 별도 판단으로 남긴다.

## 6. 검증 (구축 당시 실측)

- CPU Release: CTest **62/62 PASS**, `--version` 0.9.4.0, `--smoke` PASS
- GPU Release(`build-windows-gpu`, clean configure): CTest **63/63 PASS**
- `benchmark_test`: schemaVersion·runId·completionReason·gpuBackend, stage/scheduler/calibration/files 상태,
  decoded/sampled 분리, sampler 미실행 `not_measured`, 드라이브 문자 없는 root 의 `not_available`
- `gpu_backend_policy_test`: Auto 기본·`backendName()` 정합(CUDA/CPU)·Cpu 고정 시 미가용

> 위 수치는 **0.9.4.0 구축 당시 값**이다. 현재 baseline 은 CPU 102/102, GPU 103/103 이며
> `docs/node-status-gate-matrix.ko.md` 를 참조한다.

## 7. 관련 문서

- `docs/build-history/0.9.4.0.{ko,en}.md` — 실제 변경과 검증의 원본
- `docs/architecture/resource-scheduling.{ko,en}.md`
- `docs/architecture/gpu-backend-roadmap.{ko,en}.md`
- `docs/development-roadmap.{ko,en}.md` Node A 절
- 후속 Node: `B-adaptive-scheduler.*` (스케줄러), `C-calibration-profile.*` (calibration)
