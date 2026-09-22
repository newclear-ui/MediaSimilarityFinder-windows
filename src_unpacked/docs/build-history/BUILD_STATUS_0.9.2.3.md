# Build Status — 0.9.2.3

## 완료
1. 버전 번호를 0.9.2.3으로 일관화.
2. ResourcePolicy에 GPU Enable/Disable 상태 추가.
3. GUI에 Enable GPU 설정 추가 및 스캔 중 잠금.
4. MediaSearchEngine이 GPU 정책을 MediaPipeline에 전달하도록 수정.
5. 불필요한 imageOrder 임시 맵 제거 및 GPU Enable 회귀 테스트 보강.

## 기존 0.9.2.2 기반 유지
- Stateful/Persistent CUDA backend
- persistent device buffers
- dedicated CUDA stream
- cached DCT tables
- CPU fallback
- GPU batch memory safety

## 검증
- Linux CPU fallback 구성에서 CMake/build/test 수행.
- 실제 CUDA/nvcc 및 Windows Qt GUI는 현재 환경에서 검증 불가.
