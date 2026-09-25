# 영상 48x48 MSSIM GPU 확장 검토

## 현재 병목

`video_similarity()`는 Hamming gate를 통과한 frame pair에 대해 `frame_ssim()`을 CPU에서 실행한다. `thumb48`은 이미 프레임별 연속 배열로 저장되어 있으므로 후보 pair가 수만 개가 되면 이 단계가 CPU 병목이 될 수 있다.

## 권장 확장 구조

- `GpuBackend::ssimBatch()`를 추가해 48x48 grayscale pair 배열과 결과 배열을 받는다.
- CUDA kernel은 pair별 8x8 window MSSIM을 계산하고 CPU 구현과 동일한 C1/C2 및 평균 규칙을 사용한다.
- DTW의 행 의존성을 유지하기 위해 전체 행렬을 GPU에 저장하지 않고, 각 DTW row에서 Hamming gate를 통과한 pair만 packing해 batch 처리한다.
- batch 실패, CUDA 미지원, 작은 batch는 현재 `frame_ssim()`으로 fallback한다.
- normal/mirror 두 방향을 하나의 batch에 함께 넣어 GPU 호출 횟수와 전송 비용을 줄인다.

## 검증 조건

- GPU와 CPU 결과 차이에 대한 허용 오차 및 threshold 경계 회귀 테스트가 필요하다.
- 동일 영상, re-encode 영상, 저대비/평탄 프레임, mirror 영상, no-thumb legacy cache를 비교해야 한다.
- 후보 수별 CPU/GPU wall time, host/device 전송 시간, VRAM 사용량을 측정해야 한다.

## 판단

구조적으로 구현 가능하지만, `10배 이상`은 아직 보장할 수 없다. DTW row별 호출과 전송 비용이 작은 후보군의 이득을 상쇄할 수 있으므로 대규모 실제 corpus benchmark 뒤에 활성화한다.
