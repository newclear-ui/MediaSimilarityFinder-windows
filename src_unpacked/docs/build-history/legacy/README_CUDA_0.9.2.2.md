# Media Similarity Finder 0.9.2.2 — CUDA/CPU

## 이번 버전

- CUDA GPU backend를 persistent state로 전환
- CUDA device input/output buffer 재사용
- 전용 non-blocking CUDA stream 사용
- DCT constant table을 backend 수명 동안 캐시
- MediaPipeline이 검색 전체에서 동일한 GPU backend를 재사용
- 이미지 검색 경로의 반복 선형 탐색을 path map으로 개선
- CUDA 실패/미지원 환경에서는 동일한 CPU pHash reference 경로로 fallback

## 배포 목표

최종 Windows 릴리스는 사용자가 별도로 Qt, FFmpeg, CUDA runtime, MSVC runtime 등의 개발/실행 라이브러리를 설치하지 않아도 되도록 필요한 런타임 DLL을 패키지에 포함하는 self-contained ZIP을 목표로 한다.

단, NVIDIA GPU를 사용할 경우 호환되는 NVIDIA 그래픽 드라이버 자체는 운영체제/하드웨어 환경에 필요한 기본 전제이며 프로그램 ZIP에 GPU 드라이버를 포함하지 않는다.

## 현재 검증 한계

현재 개발 환경에는 CUDA Toolkit/nvcc와 NVIDIA GPU가 없으므로 실제 CUDA kernel 컴파일/실행은 검증하지 못했다. CPU 빌드와 전체 회귀 테스트를 검증하며 CUDA 코드는 Windows CUDA CI에서 실제 검증하도록 구성한다.
