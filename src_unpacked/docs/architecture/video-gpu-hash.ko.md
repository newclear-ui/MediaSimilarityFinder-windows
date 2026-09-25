# 영상 GPU 해시 아키텍처

## 범위

`VideoFingerprintEngine::build()`의 32x32 샘플 프레임 pHash만 CUDA batch로 처리한다. FFmpeg 디코드와 96x96 crop fingerprint는 CPU 경로를 유지한다. temporal 검증의 MSSIM 단계는 0.9.3.14부터 GPU row-batch이며(`video-ssim-gpu.ko.md` 참조), crop temporal 점수 자체는 CPU다.

## 실행 흐름

1. 기존 샘플 계획으로 FFmpeg가 32x32 프레임을 디코드한다.
2. 저분산 필터를 통과한 프레임을 normal/mirror 두 입력 batch로 packing한다.
3. 공유 `GpuBackend`의 `hashBatch()`로 두 pHash batch를 계산한다.
4. CUDA 미지원, runtime 실패, 부분 batch 실패 시 해당 프레임은 기존 CPU `perceptual_hash_pair()`로 계산한다.
5. GPU 사용 영상 수, fallback 영상 수, GPU hash 시간은 Benchmark JSON에 기록한다.

## 동시성 및 의미 보존

- 여러 video worker가 하나의 backend를 공유하므로 `hashBatch()` 호출을 mutex로 직렬화한다.
- GPU 호출은 hash 계산만 대체하며 샘플 timestamp, frame filter, mirror hash, crop hash, temporal 판정 규칙은 변경하지 않는다.
- GPU 경로가 실패해도 결과를 버리지 않고 CPU fallback으로 완료한다.

## 제한

- 작은 영상에서는 host/device 전송과 batch 준비 비용 때문에 속도 향상이 제한될 수 있다.
- 영상 디코드와 crop/temporal 검증은 아직 CPU이며, NVDEC는 이 설계 범위에 포함하지 않는다.
