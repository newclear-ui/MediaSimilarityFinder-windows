# E-adaptive-sampling-strategy — E-2A Adaptive Sampling Strategy 실측 (Pre-register)

Status: **PRE-REGISTERED** (probe 구현·측정보다 먼저 커밋된다)

```text
Base         v0.9.4.38 / 425def6
Version      v0.9.4.39
Experiment   E-2A
직전          E-1 = PASS (0.9.4.38)
Production change   NONE — measurement + prototype only
연결 brief    E-video-decode-planner.{ko,en}.md
```

## 1. Problem

E-1 이 실측했다.

```text
decoded 12,573 frames → 140 samples = 89.81x 낭비
decode share of sweep = 97.57 %
```

근본 원인은 구조다. `make_sample_plan` 은 duration 만 보고 샘플을 정하지만
`framesAt` 은 **첫 target 으로 한 번만 backward seek 한 뒤 마지막 target 까지
순차 스윕**한다(`src/video_decoder.cpp:107-108`). 필요한 프레임 수와 실제
디코드 프레임 수가 구조적으로 decoupled 다.

E-1 은 산술로 sparse seek 의 잠재 이득을 추정했지만 **그것은 산술이지 실측이
아니라고 스스로 기록했다.** E-2A 의 임무는 그것을 **실측**하는 것이다.

## 2. 이번에 하지 않는 것 (우선순위 원칙)

```text
measurement → prototype → exactness → performance → production decision
```

production sampling policy 는 **교체하지 않는다.** candidate 는 실제
decoder 를 호출하되 production 경로와 완전히 분리된 실험 경로로 둔다.

## 3. Dataset — E-1 기록의 정정

E-1 은 HEVC/AV1/VP9 를 `NOT_AVAILABLE_IN_CURRENT_ENVIRONMENT` 로 기록했다.
**그것은 생성(generation)에 대한 진술이었다.** 조사 결과 이 머신에는 실제
인코딩된 HEVC·AV1 콘텐츠가 존재한다. 따라서 E-2A 는 그것을 사용한다.

AGENTS 규칙에 따라 원 기록을 덮어쓰지 않고 정정 이력을 남긴다.

```text
원 기록(E-1)  : HEVC/AV1/VP9 = NOT_AVAILABLE_IN_CURRENT_ENVIRONMENT
원인          : 번들 FFmpeg 9.0.1 에 해당 codec 의 software ENCODER 가 없다.
                h264_mf(MediaFoundation) 만 실제 동작.
정정          : 생성은 불가하나 실제 인코딩된 콘텐츠는 이 머신에 존재한다.
                E-2A 는 이를 사용한다.
```

추가로 **새로 발견된 제약**이 하나 있다.

```text
AV1 = NOT DECODABLE IN THIS BUILD
  "Your platform doesn't support hardware accelerated AV1 decoding"
  "Error submitting packet to decoder: Function not implemented"
```

`-decoders` 는 `av1` 을 나열하지만 네이티브 구현이 동작하지 않는다.
**이것은 생성이 아니라 디코딩 제약**이며 E-1 이 알 수 없었던 사실이다.

## 4. Dataset 구성

`C:\project\validation\video_dataset_real` (gitignore 대상, 커밋 안 함).
표준 image dataset(`e8f8fa6a..e2640a`)은 **절대로 변경하지 않는다.**

### 4.1 실제 콘텐츠

사용자 미디어 라이브러리에서 **기술적으로 대표하는 짧은 구간만** `-c copy` 로
자른다. stream copy 이므로 native GOP·bitrate·픽셀이 그대로 보존된다.
개인 파일 전체를 복제하지 않고 커밋하지도 않는다. 소스는 **속성으로**
선택하며 (인코딩에 취약한) 파일명으로 선택하지 않는다.

| 파일 | codec | 해상도 | fps | duration |
|---|---|---|---|---|
| real_h264_1920x1080_030s | h264 | 1920x1080 | 30 | 29.0 s |
| real_h264_1920x1080_300s | h264 | 1920x1080 | 30 | 270.5 s |
| real_h264_1080x1920_030s | h264 | 1080x1920 | 30 | 29.9 s |
| real_h264_3840x2160_020s | h264 | 3840x2160 | 60 | 20.1 s |
| real_h264_0648x1080_030s | h264 | 648x1080 | 30 | 30.0 s |
| real_h264_1360x0808_030s | h264 | 1360x808 | 30 | 8.9 s |
| real_hevc_1920x1080_030s | hevc | 1920x1080 | 30 | 30.1 s |
| real_hevc_1360x0808_030s | hevc | 1360x808 | 30 | 8.9 s |
| real_av1_1920x1080_030s | av1 | 1920x1080 | 30 | 30.0 s → **디코드 불가** |

### 4.2 제어된 GOP 합성 세트 (E-1 세트 확장)

실제 콘텐츠만으로는 GOP 길이를 **통제**할 수 없다. 지시 §6 이 요구하는
"짧은/중간/긴 GOP/intra-only" 를 위해 `-g` 로 제어점을 만든다.

```text
synth_mpeg4_640x360_25fps_030s_gop5 / gop15 / gop60 / gop250
synth_ffv1_640x360_25fps_030s_intra   (-g 1, 전 프레임 key)
```

### 4.3 Dataset 은 두 층위로 보고한다

```text
Synthetic dataset  : 알고리즘 sanity check, GOP 제어점
Real-content dataset: sparse seek 가 실제 파일에서 어떤 특성을 갖는지
```

## 5. E-1 사전 측정 (probe 재사용, baseline 확보)

E-1 probe 를 그대로 재사용해 실제 콘텐츠 baseline 을 먼저 확보했다.

```text
files_measured                  = 14
probe_matches_production_frames = NO (13/14 byte identical)
  unfaithful: av1 (production framesAt96Plus32 failed — 디코드 불가)
decoded / emitted / ratio       = 17,164 / 208 / 82.52x
decode share of open+decode     = 99.90 %
```

### 5.1 실제 codec 별 GOP (E-1 은 데이터점이 2개뿐이었다)

| 파일 | decoded | key packets | GOP 추정 |
|---|---|---|---|
| real_h264_1080x1920_030s | 902 | **4** | **≈ 225** ← 긴 GOP |
| real_h264_3840x2160_020s | 1199 | 11 | ≈ 109 |
| real_h264_1920x1080_300s | 8115 | 271 | ≈ 30 |
| real_h264_0648x1080_030s | 900 | 30 | = 30 |
| real_hevc_1920x1080_030s | 900 | 33 | ≈ 27 |
| real_h264_1920x1080_030s | 868 | 37 | ≈ 23 |
| real_h264_1360x0808_030s | 265 | 9 | ≈ 29 |

실제 GOP 은 **약 23 ~ 225** 로 10배 범위다. E-1 은 mpeg4 11.9 / h264 50.0
두 점만 있었다. 특히 **GOP 225 파일은 sparse seek 의 최악 조건 후보**다.

### 5.2 실제 codec 별 낭비율

```text
29.4x (1360x808 8.9s)  ~  53~58x (30s급)  ~  109x (4K)  ~  231.9x (270.5s)
```

270.5초 실제 h264 1편에서 **8,115프레임을 디코드해 35개 샘플**을 얻는다.

### 5.3 E-1 H2 의 정정 — per-frame cost 에 콘텐츠 복잡도도 관여한다

```text
h264 1920x1080 30s : 3036 ms / 868 frames =  3.50 ms/frame
hevc 1920x1080 30s : 10050 ms / 900 frames = 11.17 ms/frame   (3.2x h264)
h264 1080x1920 30s  : 9057 ms / 902 frames = 10.04 ms/frame   (2.9x, 동일 픽셀수!)
h264 3840x2160 20s  : 17661 ms / 1199 frames = 14.73 ms/frame
```

`1920x1080` 과 `1080x1920` 은 **픽셀 수가 같지만** 프레임당 비용이 2.9배
달라진다. E-1 의 H2 ("per-frame cost = f(codec, resolution)") 는 **완전하지
않다.** bitrate·GOP·콘텐츠 복잡도가 추가 요인이다. E-2A 는 이를 기록하되
planner 입력에 추가하지는 않는다(§10 축소 원칙, 측정 기반 없음).

## 6. GOP 측정 — 독립 이중 측정 (지시 §9)

`AV_PKT_FLAG_KEY` 만으로 GOP 를 정의하지 않는다. E-1 에서 ffv1 이
intra-only 인데 63/750 로 측정되어 **플래그 단독 신뢰성이 이미 깨졌다**는
사실이 출발점이다.

세 값을 모두 수집하고 불일치를 **숨기지 않는다.**

```text
A. packet key flag    : AV_PKT_FLAG_KEY 위치 → key distance
B. decoded I-picture  : AVFrame::pict_type == AV_PICTURE_TYPE_I 위치 → I distance
C. mismatch           : A 와 B 가 다른 지점의 수
```

### 6.1 신뢰도 상태 (지시 §10)

```text
GopKnown        A 와 B 가 일치하고 분포가 규칙적
GopEstimated    A·B 중 하나만 사용 가능, 상호 검증 불가
GopUnavailable  어느 것도 신뢰할 수 없음
```

**`GopUnavailable` 인 경우 planner 가 GOP 에 의존해 잘못된 seek 비용을
계산하지 않아야 한다.** sparse seek 후보는 이 상태에서 생성하지 않는다.

### 6.2 추정을 만들어내지 않는다 (지시 §11)

`mpeg4 ≈ 12`, `h264 ≈ 50` 같은 값을 codec 고정값으로 만들지 않는다.
데이터가 적으면 `insufficient evidence` 로 남긴다.

## 7. Sampling baseline 고정 (지시 §12)

```text
plan  = msf::make_sample_plan(duration)     ← 실제 production 함수 호출
frame = msf::VideoDecoder::framesAt96Plus32 ← 실제 production 함수 호출
```

probe 는 sample plan 을 재구현하지 않는다. E-1 probe 와 동일하게
byte 단위 검증으로 faithfulness 를 확인한다.

## 8. Sparse-seek candidate (지시 §13·14)

**오직 두 개만 존재한다.** planner threshold 는 만들지 않는다.

```text
Baseline Sequential  : production framesAt96Plus32 (변경 없음)
Sparse Seek Candidate: prototype, production 과 완전 분리
```

candidate 개념:

```text
각 target 마다:
  target 이전의 가장 가까운 keyframe 으로 seek (AVSEEK_FLAG_BACKWARD)
  avcodec_flush_buffers 후 target 까지만 decode
  sample emit 후 다음 target
```

여기서 **정확성이 최우선**이다. sparse seek 는 "근접 keyframe" 를
`AVSEEK_FLAG_BACKWARD` 로 찾으므로, baseline 이 forward 스윕 중 선택한 프레임과
**다른 프레임을 emit 할 수 있다.** 이 경우 pixel parity 가 깨지므로
보고한다.

## 9. Exactness 기준 (지시 §16·17·18)

fingerprint 결과만 비교하지 않는다. sample frame 자체를 비교한다.

```text
sample count parity
sample order parity
sample timestamp parity   (time_base 기준, 임의 tolerance 금지)
sample pixel parity      (32x32 gray byte 단위)
```

### 9.1 Floating timestamp 처리 (지시 §18)

PTS/time_base 때문에 target_pts 와 actual_pts 가 정확히 같은 정수가 되지
않는다. **먼저 time_base 에서 의미 있는 허용 범위를 조사한 뒤** 결정한다.

```text
측정: target_pts, actual_pts, |target_pts - actual_pts| 를 time_base tick 단위로 기록
판정: video_decoder.cpp:127 이 이미 사용하는 0.05 s tolerance 와
      time_base tick 단위값을 함께 기록해 어느 쪽인지 명시한다.
      새 tolerance 를 발명하지 않는다.
```

## 10. 측정 항목 (지시 §19~25)

```text
seekCalls  seekTime  preTargetDecodedFrames  sampleDecodedFrames
totalDecodedFrames  sampleEmittedFrames
```

지표:

```text
decoded / emitted ratio                    (E-1: 89.81x → 실제 감소량 측정)
extra_decoded_per_sample
  = (candidate decoded - emitted) / emitted
```

§22 의 sampling spacing(96/128/192/256/384/512)은 **production plan 이 쓰는
값이 아니므로** 명시적으로 exploratory benchmark 로 표시한다. §23 duration
(짧음/중간/길음 — 270.5초가 반드시 포함), §24 fps, §25 resolution,
§26 codec 별로 측정한다.

## 11. 불리한 조건을 반드시 찾는다 (지시 §27)

candidate 가 항상 빠를 것으로 가정하지 않는다. 실제로 느린 조건을 찾아야
planner 가 의미를 가진다. 탐색 대상:

```text
sample interval 이 매우 짧음 / GOP 가 매우 짧음 / seek overhead 가 큼 /
target 이 keyframe 과 거의 매번 인접 / GOP 225 같은 긴 GOP
```

**단, 이 측정 전까지 어떤 threshold 도 만들지 않는다** (지시 §28).

## 12. Performance 측정 (지시 §33·34)

```text
baseline → candidate → baseline → candidate ...  (교차 실행)
각 조건 최소 3회, median 기록
```

분리해서 기록한다 (지시 §34):

```text
seek elapsed / decode elapsed / frame decode count / sample conversion / total elapsed
```

seek 이 빨라도 그 뒤 decode 가 많으면 전체가 느릴 수 있음을 반드시 보여준다.

## 13. 계측 오버헤드 분리 (지시 §35)

v0.9.4.37 이 `probeOsFileOpen` 계측 오버헤드가 benchmark 에 섞인 것을
발견했다. 이번에도 같은 오류가 나지 않도록:

```text
telemetry 없는 경로와 있는 경로를 별도 측정해 계측 비용을 분리한다
headline 은 계측 영향이 최소화된 값으로 잡는다
```

## 14. E / F 경계 (지시 §36·37)

```text
E = 어떤 frame 에 어떤 방식으로 접근할 것인가   ← 이번 작업
F = 선택된 decode backend 를 CPU/GPU HW 에서 어떻게 실행할 것인가  ← 구현 금지
```

- I 노드는 COMPLETE 다. **I-2 코드를 다시 수정하지 않는다.**
- sparse seek 는 software decoder 에서도 의미가 있으므로 E 에서 검증한다.
- NVDEC·CUDA video decode·D3D11VA·QSV·Vulkan·hw frame path 를 구현하지 않는다.
- `av_hwdevice_*` 를 호출하지 않는다.

## 15. 성공 기준 (지시 §44)

```text
Gate A  실제 콘텐츠 dataset + manifest + fingerprint + codec/resolution/duration 분류
Gate B  packet key flag / decoded I-picture / 불일치 / 신뢰도 상태
Gate C  실제 seek + 동일 sample plan + count/order/timestamp/pixel parity
Gate D  decoded count / ratio / seek count / seek elapsed / decode elapsed / total
Gate E  CPU CTest PASS / GPU CTest PASS / 기존 probe PASS / production 회귀 없음
```

## 16. Non-goals (지시 §45, §48)

```text
production sampling policy 교체          금지 (E-2B)
planner threshold 확정                   금지
adaptive algorithm 구현                  금지 (두 가지 전략만 비교)
production decode path 변경              금지
NVDEC / CUDA video decode / D3D11VA / QSV / Vulkan / hw frame   금지
dependency 업그레이드                    금지
dataset 변경 (표준 image dataset)         금지
```

E-2A 가 PASS 여도 **production 통합하지 않을 수 있다** (지시 §45). sparse seek
가 모든 codec/조건에서 일관되게 빠르지 않으면 E-2B 에 추가 모델이 필요하다.

## 17. 산출물

```text
tests/video_sampling_strategy_probe.cpp   (baseline / sparse / compare / manifest / selfcheck)
scripts/validation/e2a_make_real_dataset.ps1
```

`docs/experiments` 는 만들지 않는다.
