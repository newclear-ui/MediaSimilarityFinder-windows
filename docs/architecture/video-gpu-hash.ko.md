# 영상 GPU 해시 아키텍처

## 범위

`VideoFingerprintEngine::build()`의 32x32 샘플 프레임 pHash만 CUDA batch로 처리한다. FFmpeg 디코드와 96x96 crop fingerprint는 CPU 경로를 유지한다. temporal 검증의 MSSIM 단계는 0.9.3.14부터 GPU row-batch이며(`video-ssim-gpu.ko.md` 참조), crop temporal 점수 자체는 CPU다. 콘텐츠 중복 판정의 길이별 탐색·시간 정렬 정책은 별도 문서 `video-similarity-search.ko.md`에서 정의하며, 이 문서는 GPU hash 실행 경로만 다룬다.

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

## 하드웨어 디코드 경로 (D3D11VA → CUDA, 1.0 이후 HW 단계용)

- D3D11VA가 현재 vcpkg FFmpeg에서 바로 동작한다(실측: H.264 → `pix_fmt: d3d11`, 90프레임·0에러·26.3x, 디바이스 메모리 유지). h264_d3d11va 래퍼는 없으나 SW 디코더의 get_format hwaccel 경로로 고정기능 디코더가 동작한다. 벤더 중립(Intel/AMD/NVIDIA).
- D3D11 → CUDA zero-copy 경로는 추가 SDK 없이 열려 있다(`cuGraphicsD3D11RegisterResource`, 설치된 툴킷의 `cuda.h`에 존재). D3D11VA 디코드 → D3D11 텍스처 → CUDA 등록 → 기존 pHash/SSIM 커널 직결.
- 디바이스 상주 전처리 수단은 NPP를 사용한다(툴킷 포함: `nppiResizeSqrPixel_8u_C1R`, `nppiNV12ToRGB_*`, `nppiFilterBoxBorder_8u_C1R`). CV-CUDA는 네이티브 Windows 미지원으로 제외한다.
- NV12의 Y plane은 곧 full-res gray이므로, 디바이스 파이프라인에서는 색변환 없이 Y plane → NPP 리사이즈(96→32) → 기존 커널로 직결된다.
- 외부 패턴: mpv `--hwdec=auto` 협상 사다리(네이티브 → copy-back → SW 폴백) + GPU 상주 프레임 렌더러 직결, Chromium 드라이버 blocklist + SW 폴백 + 진단. 우리 scheduler+fallback/capability 게이팅과 동형이다.
- 전제: HW 디코드 픽셀은 SW와 바이트가 다를 수 있어 채택 시 지문 parity 게이트(video_decode_parity_test 패턴) + F-1 exactness 재입증이 필요하다. 순서상 1.0 Vulkan 이후 HW 단계에서 진행한다(로드맵 Node G/F).

## 제한

- 작은 영상에서는 host/device 전송과 batch 준비 비용 때문에 속도 향상이 제한될 수 있다.
- 영상 디코드와 crop/temporal 검증은 아직 CPU이며, NVDEC는 이 설계 범위에 포함하지 않는다.
