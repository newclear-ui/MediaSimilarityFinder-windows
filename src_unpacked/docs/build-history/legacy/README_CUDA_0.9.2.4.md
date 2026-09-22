# MediaSimilarityFinder 0.9.2.4

## 이번 빌드
- Persistent CUDA backend 유지: device buffer/stream/DCT table 재사용
- CPU/GPU 리소스 정책을 분리 유지
- GPU 사용 On/Off 런타임 설정 추가
- GPU Off 시 이미지 분석은 동일한 CPU pHash 경로로 처리
- 변경 이미지 배치 처리에서 불필요한 경로 맵 제거
- 버전 번호를 0.9.2.4으로 일관화

## 리소스 정책
- CPU 사용률: worker 수를 통해 제어
- GPU 사용률: GPU batch budget을 통해 제어
- GPU Enable: CUDA 사용 자체를 끄면 CPU fallback 경로 사용

## 검증 한계
현재 개발 컨테이너에는 NVIDIA GPU/CUDA Toolkit(nvcc)과 Qt6가 없으므로 실제 CUDA/Windows GUI 실행 검증은 수행할 수 없습니다. Windows/NVIDIA 환경에서는 GitHub Actions CUDA 빌드를 통해 최종 검증합니다.


## Portable index storage

Indexes are stored under the executable directory in `Index\<stable-folder-id>\`. Scanned media folders are never used to store index files.
