# MediaSimilarityFinder

Windows x64 미디어 중복/시각적 유사도 검색 프로그램입니다.

MediaSimilarityFinder는 **CPU + GPU 공동 실행**을 기본 아키텍처로 사용합니다. CPU는 항상 안정적인 기준/폴백 경로로 유지하며, GPU는 사용 가능한 작업을 자동으로 가속합니다.

## Resource Management

사용자에게는 다음 자원 모드를 제공합니다.

- Maximum
- High
- Balanced
- Gaming
- Manual

CPU 자원 정책은 사용자가 선택하지만 GPU 사용률을 직접 지정하지는 않습니다.

- GPU ON → Adaptive GPU Scheduler가 자동으로 작업량을 결정
- GPU OFF → CPU 경로 사용
- CPU/GPU 작업 배분은 고정 50:50이 아니라 실제 처리능력, 실시간 부하, queue 상태, 데이터 이동 비용을 기준으로 동적으로 조정
- 초기 하드웨어 성능 프로파일은 INI에 저장하여 다음 검색의 초기값으로 재사용
- 다른 프로그램이 CPU/GPU를 사용하는 경우 실시간 상태를 반영하여 작업량을 조절
- 저사양 iGPU/dGPU에서는 CPU 중심 또는 CPU-only로 자동 전환 가능

## Multi-vendor GPU Direction

현재 GPU compute 기준 구현은 **NVIDIA CUDA**입니다.

하지만 상위 아키텍처에서는 CUDA를 GPU의 일반명으로 사용하지 않습니다.

목표 backend 계층은 NVIDIA CUDA, NVIDIA NVDEC, Vulkan, AMD HIP/ROCm, Intel Level Zero이며, Intel/AMD의 iGPU와 dGPU를 동일한 GPU abstraction으로 연결할 수 있도록 설계합니다.

Vulkan은 vendor-neutral GPU compute 후보이며, Intel은 Level Zero, AMD는 HIP/ROCm을 선택적 vendor-specific backend 후보로 둡니다. 실제 지원 여부와 성능은 장치/드라이버/backend capability 및 실제 workload 측정으로 판단합니다.

## Current / Next Development Line

현재 검증 코드 라인:

**0.9.3.19**

다음 구조적 개발 라인:

**0.9.4.x**

첫 전환 빌드는 **0.9.4.0**을 권장합니다.

0.9.4.x에서는 CPU/GPU 용어 일반화, Adaptive Scheduler, INI 성능 프로파일, multi-vendor GPU 연결점, GPU build naming 정리를 진행합니다.

## Detailed Architecture

- [CPU/GPU Adaptive Resource Scheduling](src_unpacked/docs/architecture/resource-scheduling.ko.md)
- [GPU Backend and Build Naming Roadmap](src_unpacked/docs/architecture/gpu-backend-roadmap.ko.md)
- [English Resource Scheduling](src_unpacked/docs/architecture/resource-scheduling.en.md)
- [English GPU Backend Roadmap](src_unpacked/docs/architecture/gpu-backend-roadmap.en.md)
- [Source Structure](src_unpacked/docs/STRUCTURE.md)
- [Build History](src_unpacked/docs/build-history/)

## Build Naming

상위 빌드 진입점은 CPU/GPU 명칭을 사용합니다.

- CPU build: build-windows-cpu
- GPU build: build-windows-gpu

현재 0.9.3.19의 기존 build-windows-cuda는 전환 기간 동안 보존하고, 새 0.9.4.x GPU build tree는 clean configure로 생성합니다.

실제 NVIDIA 구현은 CUDA라는 기술명을 계속 사용합니다. 즉 **상위 계층은 GPU, 하위 구현은 실제 backend 이름**을 사용합니다.

## Baseline

공식 GPU 보존 기준선은 0.9.2.32이며 수정하거나 덮어쓰지 않습니다.

CPU fallback은 항상 유지합니다.
