# 영상 48x48 MSSIM GPU batch 구조

## 현재 구조

`video_similarity()`는 DTW 각 행에서 Hamming gate를 통과한 frame pair만 골라 `GpuBackend::ssimBatch()`로 48x48 MSSIM을 계산한다. 전체 DTW 행렬을 GPU에 만들지 않으므로 메모리 사용은 행 단위 batch에 비례한다.

## 실행 흐름

1. 각 DTW 행에서 Hamming 유사도를 먼저 계산한다.
2. gate 통과 pair의 normal/mirror `thumb48`를 연속 batch 배열에 packing한다.
3. 행 batch 크기가 8 이상이면 CUDA kernel이 pair별 36개 8x8 window MSSIM을 계산하고 평균한다.
4. batch 실패, CUDA 미지원, 8 미만 소규모 행은 `frame_ssim()` CPU 경로로 계산한다.
5. SSIM은 Hamming reject를 승격시키지 않으며, 기존 `0.4*Hamming + 0.6*SSIM` blend 규칙을 그대로 사용한다.

## 동시성 및 의미 보존

- `ssimBatch()` 호출은 `GpuBackend` mutex로 직렬화해 여러 temporal worker가 backend를 공유한다.
- CPU와 동일한 C1/C2 상수·window 평균 규칙을 사용하므로 판정 임계값 의미가 바뀌지 않는다.
- `ScanPipeline` temporal 검증과 `compareFingerprint()` live 경로가 같은 GPU backend를 공유한다.

## 제한

- DTW 행 단위 호출과 host/device 전송 비용 때문에 작은 후보군에서는 이득이 제한된다.
- 엔진 판정 버전은 그대로이며, 저장 쌍 재검증이 필요 없는 additive 가속이다.
