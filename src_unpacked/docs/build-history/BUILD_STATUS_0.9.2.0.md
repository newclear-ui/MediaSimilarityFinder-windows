# MediaSimilarityFinder 0.9.2.2 — CUDA backend integration status

이번 빌드는 0.9.1.0 CPU 초기 빌드를 폐기하지 않고 그 위에 NVIDIA CUDA backend를 다시 연결하는 GPU 계열 첫 빌드이다.

## 이번 5단계 묶음
1. CUDA runtime GPU detection: 장치명, compute capability, global memory 확인.
2. GPU backend fallback 의미 수정: CUDA가 없을 때 CPU fallback은 MediaPipeline에서 명시적으로 수행.
3. CUDA pHash kernel을 CPU의 32x32 / 8x8 DCT pHash와 동일한 계산으로 유지하고 오류 경로 정리.
4. MediaPipeline imageBatch에 GPU batch size 제한을 적용하고 실제 batch GPU 호출 경로를 연결.
5. CUDA build에서 CPU/GPU pHash 동일성 회귀 테스트 추가.

## 추가 구현
- GPU batch size가 현재 free VRAM의 50% 이하가 되도록 runtime memory budget을 반영.
- CUDA DCT kernel을 precomputed coefficient + separable 2-pass 구조로 개선.
- SearchReport에 CUDA 사용 이미지 수와 CPU fallback 이미지 수 추가.
- Windows CUDA GitHub Actions workflow 추가.

## 검증
- 현재 개발 컨테이너에는 nvcc/CUDA Toolkit이 없으므로 CUDA 컴파일 및 실제 NVIDIA 실행은 검증 불가.
- CPU 구성은 이전 기준대로 계속 유지되어야 한다.
- CUDA가 없는 환경에서는 GPU test가 skip되고 CPU fallback 경로가 유지된다.
- Windows/NVIDIA에서 실제 CUDA build + test가 필요하다.
