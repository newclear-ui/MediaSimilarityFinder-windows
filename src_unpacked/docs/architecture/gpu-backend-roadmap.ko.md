# GPU Backend and Build Naming Roadmap

## 목적

MediaSimilarityFinder의 상위 아키텍처와 사용자 문서에서는 특정 GPU 공급자나 API를 전체 GPU를 대표하는 이름으로 사용하지 않는다.

- 사용자/제품 수준: CPU / GPU
- 공통 엔진 계층: GPU backend
- 실제 구현 계층: CUDA / Vulkan / HIP(ROCm) / Level Zero 등

현재 0.9.3.19는 NVIDIA CUDA 구현을 사용하지만, 앞으로 Intel/AMD iGPU와 dGPU를 연결할 수 있는 구조를 처음부터 유지한다.

## 1. GPU backend 계층

목표 구조는 공통 GpuBackend 인터페이스 아래에 구체적인 backend를 독립적으로 연결하는 것이다.

- CudaGpuBackend — NVIDIA CUDA, 현재 기준 구현
- VulkanGpuBackend — vendor-neutral Vulkan compute 후보
- HipGpuBackend — AMD HIP/ROCm 후보
- LevelZeroGpuBackend — Intel oneAPI Level Zero 후보
- CPU reference/fallback은 GPU backend와 별도의 기준 경로로 유지

Vulkan은 Khronos가 명시하는 cross-platform/cross-vendor graphics and compute API이므로 Intel/AMD/NVIDIA를 가로지르는 후보 계층으로 검토한다. 다만 Vulkan을 자동으로 모든 GPU에서 CUDA와 동등한 기능/성능을 보장하는 것으로 간주하지 않는다. 실제 device capability, driver, shader feature, subgroup support, memory behavior, transfer cost를 측정해야 한다. 

Intel GPU는 Vulkan과 함께 oneAPI Level Zero를 선택적 고성능 backend 후보로 둔다. Intel 문서에서는 Level Zero를 저수준 GPU/가속기 인터페이스로 설명하며, higher-level runtime이 필요로 하는 명시적 제어를 제공하는 계층으로 정의한다.

AMD GPU는 Vulkan을 범용 후보로 두고, Windows 지원 하에서 AMD ROCm/HIP를 선택적 backend 후보로 둔다. 실제 GPU/driver/backend 조합의 지원 여부는 capability probe로 확인한다.

## 2. Backend 선택 원칙

사용자에게 NVIDIA/AMD/Intel을 직접 선택하게 하지 않는다.

GPU ON 시:

1. GPU 장치 탐색
2. 지원 backend 탐색
3. backend capability 확인
4. 짧은 calibration 및 실제 throughput 측정
5. 현재 시스템 부하 확인
6. 작업 종류별 효율 평가
7. 가장 적합한 backend/작업 배분 선택

가능한 경우 backend마다 capability를 별도로 기록한다.

- CUDA: available
- Vulkan: available
- HIP: unavailable
- LevelZero: available
- NVDEC: available

이 정보는 향후 재사용할 수 있는 hardware profile의 일부가 된다.

## 3. Vulkan의 위치

0.9.4.x에서 처음부터 전체 검색 연산을 Vulkan으로 재작성한다는 의미가 아니다.

0.9.4.x의 우선 목표는:

- vendor-neutral GPU interface 확립
- device discovery/capability layer 확립
- backend registration 구조 확립
- CPU fallback 보존
- 현재 CUDA backend의 동작 보존
- Vulkan을 다음 구현 후보로 연결할 수 있는 interface 확보

이후 Vulkan compute pilot을 별도 단계로 진행한다.

처음부터 Vulkan을 CUDA의 완전한 대체재로 만들려 하지 않는다. 정확성 parity와 실제 end-to-end throughput을 먼저 검증한다.

## 4. Intel iGPU / dGPU 연결

Intel integrated GPU와 discrete GPU 모두 같은 GPU abstraction으로 취급한다.

후보 backend:
1. Vulkan
2. Level Zero
3. 필요 시 기타 oneAPI 계층

GPU 자체가 약한 iGPU라도 decode, resize, hash 등 일부 작업에서 CPU보다 유리할 수 있으므로 전체 GPU 성능 하나로 사용 여부를 결정하지 않는다.

## 5. AMD iGPU / dGPU 연결

AMD integrated graphics와 discrete Radeon GPU 모두 같은 abstraction으로 취급한다.

후보 backend:
1. Vulkan
2. HIP/ROCm

AMD GPU의 실제 capability가 부족하면 CPU 경로로 자동 수렴한다.

## 6. NVIDIA 연결

NVIDIA에서는 현재 CUDA backend를 기준으로 유지한다.

향후 구조는:
- CUDA compute backend
- NVDEC video decode backend
- 필요 시 Vulkan compute backend

를 독립적으로 취급한다.

즉 GPU = CUDA가 아니라 CUDA = NVIDIA GPU backend 중 하나라는 의미가 된다.

## 7. CPU/GPU 명칭 정리

제품/문서/빌드 진입점에서는 CPU와 GPU를 사용한다.

변경 예정:
- build-windows-cuda → build-windows-gpu
- build_windows_cuda.ps1 → build_windows_gpu.ps1
- windows-cuda CMake preset → windows-gpu
- windows-cuda-release → windows-gpu-release
- 사용자 문서의 CPU/CUDA → CPU/GPU 또는 실제 NVIDIA CUDA를 직접 말할 때만 CUDA

CMake의 상위 옵션도 목표적으로:
- MSF_ENABLE_CUDA → MSF_ENABLE_GPU
- backend 선택은 별도 backend selector로 분리

예:
- MSF_ENABLE_GPU=ON
- MSF_GPU_BACKEND=AUTO
- 진단 시 MSF_GPU_BACKEND=CUDA

### 구현 이름의 예외

실제 CUDA 코드를 구현하는 파일을 일반적인 GPU라고 부르는 것은 부정확하다.

따라서 다음과 같은 vendor-specific 이름은 허용한다.
- gpu_backend_cuda.cu
- gpu_backend_vulkan.cpp
- gpu_backend_hip.cpp
- gpu_backend_level_zero.cpp

즉 상위 API는 GPU, 하위 구현은 실제 기술 이름을 사용한다.

## 8. Build directory migration

build-windows-cuda에서 build-windows-gpu로 이름을 바꾸는 것은 0.9.4.x 전환 시점에 하는 것이 적절하다.

단, 기존 CMake build tree를 단순히 이름만 바꾸어 재사용하지 않는다.

권장:
1. 기존 build-windows-cuda는 보존
2. build-windows-gpu를 새로운 clean configure로 생성
3. CPU build와 GPU build를 각각 configure/build/test
4. 결과가 검증된 후에만 기존 디렉터리 정리 여부를 결정

스크립트/preset/문서/CI의 모든 참조도 함께 갱신한다.

## 9. Version line

다음 개발선은 0.9.4.x로 전환한다.

첫 빌드 권장 번호:
0.9.4.0

이 버전은 다음 구조적 변화의 시작점을 의미한다.
- CPU/GPU 용어 일반화
- GPU backend abstraction 강화
- Adaptive CPU/GPU Scheduler
- INI performance profile
- multi-vendor backend 연결점
- build naming 정리

이것은 semantic versioning의 major 1.x 전환이 아니라 프로젝트 내부 개발 마이너 라인의 구조적 전환이다.

## 10. 구현 단계

### 0.9.4.0
- GPU backend naming/abstraction 정리
- CPU/GPU Resource Mode 확정
- GPU usage manual control 제거
- Adaptive Scheduler 기본 계층
- INI performance profile schema
- build naming 변경
- CUDA backend를 기존 동작 그대로 연결
- CPU/GPU regression tests

### 0.9.4.x 후속
- runtime telemetry 정밀화
- pipeline scheduling
- adaptive video decode planner
- Vulkan capability/discovery pilot
- NVDEC backend 분리/연결
- 필요 시 HIP/ROCm backend prototype
- 필요 시 Level Zero backend prototype

각 backend는 독립적으로 검증하며, 하나의 backend 추가 때문에 CPU fallback이나 다른 GPU backend가 깨지지 않아야 한다.

## 11. 금지 사항

- CUDA 이름을 제품 전체 GPU의 일반명처럼 사용하지 않는다.
- Vulkan을 모든 GPU에서 자동으로 최적이라고 가정하지 않는다.
- Intel/AMD backend를 실제 장비 검증 없이 지원 완료라고 표시하지 않는다.
- GPU backend 하나의 실패가 전체 검색 실패가 되도록 하지 않는다.
- 상위 engine이 CUDA API를 직접 호출하도록 확장하지 않는다.
- CPU fallback을 삭제하거나 축소하지 않는다.
