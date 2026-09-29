# E-video-decode-planner — E-1 Adaptive Video Decode Planner 사전 조사 / 설계 (Pre-register)

Status: **PRE-REGISTERED** (production 구현·probe 작성보다 먼저 커밋된다)

```text
Base         v0.9.4.37 / 01b4ed1
Version      v0.9.4.38
Experiment   E-1 (Adaptive Video Decode Planner — research / baseline / design)
Product change   NONE expected — measurement + design only
직전 노드     I = COMPLETE (I-1 DEFERRED, I-2 PRODUCTION, I-3 END-TO-END PASS)
```

## 1. Problem

E 노드의 목표(roadmap)는 다음과 같다.

> Reduce workload-specific cost from decoding more frames than necessary.

즉 **요청한 샘플 프레임보다 훨씬 많은 프레임을 실제로 디코드하고 있다**는
전제의 비용을 줄이는 것이다. 현재 코드가 이 전제를 얼마나 강하게 지지하는지를
먼저 측정해야 한다.

## 2. Current pipeline — 실코드 기준

조사 결과(모든 줄번호는 `v0.9.4.37` 기준).

```text
Scanner::isVideoPath            src/scanner.cpp:16   확장자만. 내용 판별 없음
  ↓  (.mp4 .mkv .avi .mov .webm .m4v .wmv — .ts 없음)
MediaSearchEngine::processOne   src/media_search_engine.cpp:569
  ↓
processVideoRange               src/media_search_engine.cpp:518
  ↓  std::async 파일 단위 병렬 (:531)  ← 디코드 스레드가 아니라 파일 레벨 fan-out
VideoFingerprintEngine::build   src/video_fingerprint.cpp:225
  ├─ memoryLookup / loadPersistent   SQLite/LRU 캐시
  ├─ VideoDecoder::open              src/video_decoder.cpp:18   ← FFmpeg 유일 진입점
  ├─ make_sample_plan                src/video_sampling.cpp:2
  ├─ framesAt96Plus32 → framesAt     src/video_decoder.cpp:94   ← 실제 디코드 루프
  │    ├─ av_seek_frame(AVSEEK_FLAG_BACKWARD)  ×1  (:108)
  │    ├─ av_read_frame / send_packet / receive_frame (:123-125)
  │    └─ sws_scale → AV_PIX_FMT_GRAY8 96x96 (:116-118)
  ├─ software 96→32 3x3 box mean    src/video_decoder.cpp:165-169
  └─ processVideoFrames              src/video_fingerprint.cpp:167
       low-variance filter → GPU pHash batch(선택) → CPU pHash
       → 96→48 thumb48 → scene change → cropFingerprints
```

**구조적 사실**

| 사실 | 근거 |
|---|---|
| FFmpeg 심볼 34개가 `src/video_decoder.cpp` 한 파일(182줄)에만 존재 | repo 전체 grep |
| FFmpeg 헤더 include 도 그 한 곳뿐. `video_decoder.h` 는 전부 `void*` 로 숨김 | `src/video_decoder.h:27-30` |
| linking 은 `msf_core` 에 PUBLIC, `MSF_HAS_FFMPEG=1` 상속 | `CMakeLists.txt:331-370` |
| **FFmpeg 9.0.1** — libavcodec 63.1.101 / libavformat 63.1.101 / libavutil 61.1.101 / libswscale 10.1.101 | `vcpkg_installed/.../version.h`, CMakeCache |
| hwaccel 사용 가능: `dxva2` `d3d11va` `d3d12va` | `ffmpeg -hwaccels` |
| 그럼에도 **hw 관련 심볼 0개** — `av_hwdevice_*`, `avcodec_get_hw_config`, `hw_device_ctx`, `hw_frames_ctx` 전부 없음 | repo 전체 grep 0건 |
| 디코드 스레드 없음 — `thread_count` / `thread_type` 미설정, `avcodec_open2` 옵션 nullptr | `src/video_decoder.cpp:30` |
| GPU는 **디코드 이후** pHash batch 와 MSSIM batch 에만 관여. `avformat_*`/`avcodec_*`/`sws_scale` 은 전부 CPU | `src/video_fingerprint.cpp:177-204`, `:454-476` |

## 3. Current bottleneck evidence — 코드에서 이미 읽히는 구조적 근거

이 항목은 **코드 근거**이며, Gate B 에서 실측으로 확인한다.

### 3.1 sampling 은 duration 만 본다

`src/video_sampling.cpp:2` 전체가 이렇다.

```cpp
if(d<=10)      interval=1;  else if(d<=60)  interval=2;
else if(d<=240) interval=4;  else if(d<=960) interval=8;
else if(d<=3840)interval=16; else             interval=32;
for(double t=0;t<d;t+=interval) timestamps.push_back(t);
```

- 유일 입력은 `i.duration` (`src/video_fingerprint.cpp:244,264`).
- fps·frame count·해상도·bitrate·codec 은 **입력이 아니다.**
- 상한이 없다 — 32초 간격으로 10시간 영상은 ~1125개, 24시간은 ~2700개 샘플.
- 경계값 10/60/240/960/3840 은 정확히 2×/4×/8×/16× 계단이다.

### 3.2 seek 는 1회, 그 뒤는 순차 스윕

`src/video_decoder.cpp:107-108` 에서 **첫 target 으로 딱 한 번** seek 하고
`while(next<targets.size() && av_read_frame(...)>=0)` 로 끝까지 전진한다.
타임스탬프마다 seek 하는 모드는 존재하지 않는다. 코드 주석도 명시한다
(`src/video_fingerprint.cpp:246-248`):

> Single-sweep decode … each sweep is a single sequential decode, never one
> seek per timestamp.

따라서 **샘플 수와 실제 디코드 프레임 수는 구조적으로 decoupled 된다.**
interval 이 fps 보다 크면 그만큼 적은 수의 프레임만 필요하지만, 구현은
첫 샘플부터 마지막 샘플까지 **연속된 모든 프레임**을 디코드한다.

### 3.3 keyframe / GOP 인식이 전혀 없다

`AV_PKT_FLAG_KEY`, `keyframe`, `GOP`, `has_b_frames`, `AVDiscard`, `skip_frame`
— 전부 **0건**이다. `AVSEEK_FLAG_BACKWARD` 는 target 이전 프레임에 착지한다는
의미로만 쓰이고, 그 keyframe 정렬 비용은 측정되지도 활용되지도 않는다.
roadmap 이 E 노드의 필수 telemetry 로 명시한 **keyframe/GOP cost 를 측정할
수단이 현재 존재하지 않는다.**

### 3.4 fps 는 계산만 하고 읽히지 않는다

`src/video_decoder.cpp:33-34` 에서 `av_guess_frame_rate` 로 계산하지만
`VideoInfo::fps` 를 읽는 코드가 repo 전체에 없다. sampler 는 fps 에 blindness 다.

### 3.5 pixel format / bit depth 처리 없음

`fr->format` 을 그대로 swscale 에 넘긴다(`:74`, `:116`). `av_get_pix_fmt` 0건,
capability probe 0건, fallback pixel format 0건. 10-bit/HDR 은 명시적으로
거부되지 않지만 검증되지도 않았다.

### 3.6 telemetry 공백 (Gate B 를 직접 막는 항목)

| 존재 | 위치 | 문제 |
|---|---|---|
| `stages.videoAnalysis.ms` | `benchmark.cpp:223` | `processVideoRange` 전체 — 파일 spawn/join 포함 lump |
| `videos.buildMs` | `benchmark.cpp:332` | `build()` 전체 lump. 캐시 조회·open·디코드·sws_scale·96→32·pHash·thumb48·crop·**캐시 write** 까지 전부 |
| `videos.decodedFrames` | `benchmark.cpp:325` | **이름과 다릅니다.** `stats->decodedFrames=frames32.size()` (`video_fingerprint.cpp:251`) = sample point 에서 **방출된** 프레임 수. 실제 `av_read_frame` 반복 횟수는 어디에도 세지 않습니다 |
| `videos.sampledFrames` | `benchmark.cpp:329` | cache hit 이 하나라도 있으면 sentinel 로 `null`+`not_measured` |
| roadmap 이 요구한 `requested sample frames` | — | **필드 없음** |
| seek count / latency | — | **필드 없음** |
| decode throughput | — | **필드 없음** |
| keyframe/GOP cost | — | **필드 없음** |
| open / demux / decode / seek / convert 분리 | — | **분리 자체가 없음** |

D9d 의 `decodeOpenMs`/`decodeCopyMs` 같은 단계 분해는 WIC/`ImageDecoder` 전용이며
FFmpeg 에는 대응 기능이 하나도 없다. E-1 이 만들어야 할 첫 계측이 이것이다.

### 3.7 video fixture 가 프로젝트에 하나도 없다

```text
C:\project 전체 재귀 검색 .mp4 .mkv .mov .avi .webm .ts .m4v  →  0건
표준 dataset test_sample_img_vid (3347 files)  →  video 0건
```

표준 dataset 이 video 를 의도적으로 제외한理由는 문서화되어 있다
(`docs/test_sample_img_vid.md:116-129`): ffmpeg 출력이 **byte 재현가능하지 않아**
content hash 가 실행마다 흔들린다. 기존 test fixture 는 전부
**≤8초, ≤256x192, 2~10fps, mpeg4 또는 libx264, 합성 lavfi** 다
(`tests/video_scan_e2e_test.cpp:29-45`, `video_real_test.cpp:8`,
`video_reencode_test.cpp:12-14`, `video_decode_parity_test.cpp:20-22`,
`color_thumb_test.cpp:42`, `unicode_video_path_test.cpp:26-28`).
long-GOP, 고해상도, 고fps, 10-bit, B-frame 많음, VFR, 멀티스트림, 손상 파일은
**하나도 없다.**

## 4. FFmpeg capability — 실제 확인 결과

`avcodec_get_hw_config()` 계열은 FFmpeg 9.0.1 에 당연히 존재하며 사용 가능하다
(FFmpeg 4.0 이후 API). 이번 버전에서 hardware decode path 를 **연결하지 않는다**,
하지만 F 의 실현 가능성을 평가하기 위해 확인한 사실:

```text
ffmpeg -hwaccels   →  dxva2  d3d11va  d3d12va
```

즉 **이 빌드에서 hardware decode 는 기술적으로 가능하다.** 이것은 F viability 의
사실 확인이지 구현 승인이 아니다.

### 4.1 제약 — software 인코더가 거의 없다 (dataset 설계에 직접 영향)

```text
사용 가능 software encoder : mpeg4, ffv1 (+ rawvideo)
사용 가능 HW encoder       : h264_mf, h264_d3d12va*, hevc_mf(실패), av1_* (전부 실패)
미사용                      : libx264, libx265, libvpx-vp9, libaom-av1  ← 빌드에 없음
(* h264_d3d12va 는 hwupload 경로 필요, 이 머신에서 실패)
```

검증 결과: **`h264_mf` 만 실제로 성공**(320x180, 2초, 50프레임, h264 확인).
`hevc_mf` 는 파일조차 생성하지 않았고, AV1·VP9·libx264·libx265 는 전부 불가.

지시 §8 은 "지원하지 않는 codec 을 benchmark 에 억지로 포함하지 않는다"고
명시한다. 따라서 HEVC/AV1/VP9 는 이번 dataset 에 **포함하지 않고**, 그 부재를
결과가 아니라 **제약으로 기록**한다. 의존성 변경(§31 금지)이 선행되어야
해결 가능하다.

## 5. Representative video dataset — 설계

표준 dataset 은 절대 변경하지 않는다(§4). 별도 reference dataset 을 만든다.

```text
위치      C:\project\validation\video_dataset   (gitignore 대상)
성격      standard dataset 과 분리된 별도 reference
재현성    -fflags +bitexact -map_metadata -1 로 생성
          (기존 문서가 지적한 비재현성 원인의 직접 완화)
```

### 5.1 구성 (생성 가능한 codec 만)

| # | codec | 생성 방식 | 의도 |
|---|---|---|---|
| 1 | `mpeg4` | software | 기존 fixture 와 동일한 baseline |
| 2 | `h264` (H.264/AVC) | `h264_mf` | 현대 inter-frame. F 의 실제 대상 codec |
| 3 | `ffv1` | software, intra-only | **전 프레임이 keyframe** → GOP 가설의 극단 대비군 |

codec 3종이 "GOP 구조가 비용을 바꾸는가"를 Answersmeasurable하게 만든다.
mpeg4/h264 은 inter-frame, ffv1 은 intra-only 다.

### 5.2 가변축

지시 §9에 따라 codec 이름 하나로 planner 를 만들지 않는다. 다음 축을 교차한다.

```text
duration   짧음 / 중간 / 길음  (sampling ladder 의 7개 버킷을 모두 건드린다)
resolution 낮은 / 보통 / 높음
fps        낮음 / 보통 / 높음   (sampling count 대비 decoded count 비율을 바꾼다)
container  mp4 / mkv
```

### 5.3 축 개수를 늘리기 전에

§10이 요구한다: "실제로 decode time 또는 end-to-end time 을 설명하는 변수만
남긴다." 따라서 **전수 교차 행렬을 먼저 만들지 않고**, 소수의 대표 조합으로
샘플-vs-디코드 격차가 실제로 나타나는지 먼저 확인한 뒤 축을 확정한다.
격차가 없으면 Node E 의 전제 자체가 검토 대상이 된다.

## 6. Planner goal

```text
"이 video 를 어떤 조건에서 어떻게 decode 하는 것이 적절한가"
```

E 는 **판단**을, F 는 **구현**을 담당한다. E-1 은 판단을 위한 입력·비용
모델·fallback 정책을 **설계하고 측정 기반 근거를 마련**한다.

## 7. Planner inputs — 후보와 축소 원칙

§10 때문에 전부 넣지 않는다. 후보 전부(§9 목록)와 실제 측정 후 남길 것을
함께 기록한다.

```text
후보     codec, container, width, height, fps, duration, estimated frame count,
         pixel format, bit depth, file size, sampling count, seek distance,
         keyframe interval, CPU mode, CPU live load, GPU ON/OFF,
         GPU capability, expected transfer cost
```

축소 기준: **decode time 또는 end-to-end time 을 설명하는가.**
설명하지 못하는 변수는 제거한다. 특히 `CPU live load` / `GPU capability` /
`expected transfer cost` 는 E-1 에서는 측정 기반이 없으므로 **추측으로 넣지
않고**, F 구현 시점에 측정 가능해지면 그때 추가한다.

## 8. Planner outputs — 설계 (구현하지 않음)

지시 §15 때문에 hardware backend 가 없는 현재 production 에서 곧바로 실행 가능한
`HardwareDecode` 를 반환해서는 안 된다. 계획 상태로만 표현한다.

```text
VideoDecodePlan
{
    samplingMode    SequentialSweep | SparseSeek | Hybrid   (E-1 은 순차만 존재)
    backend         Software | HardwareCandidate
    expectedCost    ms 추정
    fallbackPolicy  FallbackToSoftware
    reason          enum (아래)
}
```

**실제 enum/구조체 이름은 구현 시 코드를 확인하고 최소화한다.** 위는 설계
방향이며 채택 mandate 가 아니다.

## 9. reason 코드 (§14)

잘못된 선택을 나중에 디버깱할 수 있어야 한다.

```text
GPU_OFF              사용자가 GPU OFF
NO_HW_CAPABILITY     capability 부재
SHORT_VIDEO          짧아서 hardware setup 이 회수되지 않음
LOW_SAMPLING_DENSITY 샘플이 드물어 seek 가 더 비쌀 수 있음
SEQUENTIAL_SUFFICIENT 순차 스윕이 이미 충분
HIGH_TRANSFER_COST   transfer 가 이득을 상쇄
CPU_SOFTWARE_PREFERRED
HARDWARE_CANDIDATE
```

UI 노출은 불필요. 개발 telemetry 수준이면 충분하다.

## 10. Cost model (E-1 설계)

§12에 따라 hardware 가 무조건 빠르다고 가정하지 않는다.

```text
SoftwareDecodeCost
  = open + demux + seek + Σ(decodePerFrame) + convert + fingerprint
HardwareEstimatedCost
  = setup + Σ(hwDecodePerFrame) + transferPerFrame×frames + surface管理
FallbackCost
  = setup 낭비 + software 재실행
```

hardware capability·device·frame configuration 별 판단이 필요하다. FFmpeg
공식 API 구조상 `avcodec_get_hw_config()` 결과를 조회해야 결정 가능하므로,
**"GPU 가능 = GPU 선택" 규칙을 만들지 않는다.**

## 11. Fallback model (§15, §23)

```text
HardwareCandidate
  ↓  실제 hardware backend 없음 (현재 production)
FallbackRequired
  ↓
SoftwareDecode          ← 항상 가능해야 한다
```

CPU fallback 은 어떤 조건에서도 제거하지 않는다. F 에서도 동일 계약을 유지한다.

## 12. E / F boundary (§13, §4)

```text
E = decision / planning     어떤 video 를 어떤 조건에서 어떻게 decode 할 것인가
F = implementation          선택된 hardware decode 를 실제로 어떻게 붙일 것인가
```

E-1 은 `av_hwdevice_*` 를 **호출하지 않는다.** hardware capability 를 *조회*하는
것과 hardware decode 를 *사용*하는 것은 다르며, 전자는 F 의 설계를 위해
필요한 사실 확인이고 후자는 F 의 일이다.

## 13. Measurement plan (Gate B)

`tests/` 에 measurement-only probe 를 추가한다. production behavior 를 바꾸지
않고 `docs/experiments/` 는 만들지 않는다. video sample 별 수집 항목(§18):

```text
file, codec, container, width, height, fps, duration,
requested sample count, emitted/sampled frames,
ACTUALLY DECODED frame count (av_read_frame 반복 — 현재 전무),
seek count, seek latency,
open time, decode time, convert time, total analysis time
```

**hardware 값은 F 이전이므로 만들어내지 않는다.** software decode baseline
만 기록한다.

핵심 측정 항목:

```text
decodedFrames / sampledFrames 격차
```
이것이 Node E 전제의 실측 근거이며, 격차가 없으면 planner 설계의 근거가
없어진다.

## 14. Success criteria (§33)

```text
Gate A  실제 production video decode path 를 코드 기준으로 설명 가능
        sampling policy / decode·seek·conversion 구조 확인
Gate B  video sample 별 주요 시간 구성 측정 가능 + software baseline 확보
Gate C  실제 측정 가능한 planner input 목록 확정, 불필요한 변수 제거
Gate D  설명 가능한 초기 cost model + software/hardware candidate 판단 구조
        + fallback 정의
Gate E  E=decision / F=implementation 경계 명확
Gate F  image path / CPU·GPU build / CTest regression 없음
```

## 15. Non-goals (§2, §20, §34)

```text
NVDEC / CUDA video decode / D3D11VA / QSV / Vulkan / AMD / Intel 구현  금지
AVHWDeviceContext / hw_frames_ctx production 연결                      금지
GPU frame surface production path / GPU→CPU transfer 최적화            금지
decoder library 교체 / FFmpeg 버전 변경 / codec 지원 범위 확장         금지
sampling 정책 자체 변경 / similarity 변경 / UI 변경 / resource mode 변경
GPU ON/OFF user semantics 변경                                         금지
하드코딩 rule 근거 없이 도입 (duration>30s→GPU 류)                      금지
```

hardware decode 가 빠르다 / HEVC는 항상 GPU / 4K는 항상 GPU 류 결론은
**F 와 실 hardware benchmark 이후에만** 판단한다.

## 16. 절대 원칙 (AGENTS 규칙 준수)

- vcpkg/dependency 구조 유지. FFmpeg 버전 변경 금지.
- UTF-8 파일을 `Get-Content | Set-Content` 으로 round-trip 하지 않는다.
  수정 후 `git diff --check` / `git diff --numstat` 확인.
- 삭제는 `scripts/safe_remove.ps1` (휴지통) 만.
- 백업은 src/portable 각 최대 3개.
