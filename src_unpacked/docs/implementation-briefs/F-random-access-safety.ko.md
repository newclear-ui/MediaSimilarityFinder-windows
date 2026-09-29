# Implementation Brief — F-1 Random-Access Safety Contract + NVDEC Exactness Preflight (Pre-register)

Status: **PRE-REGISTERED** — 이 문서는 brief 만 포함하며, probe/test 코드가 이 커밋보다 앞서면 안 된다.
Version: 0.9.4.43
Base: `v0.9.4.42` / `395ae57`

---

## 1. Problem

F preflight에서 NVDEC이 실제로 RTX 3080 Ti에서 동작함은 확인되었다
(`Runtime PASS` / `Decode PASS`). 그런데 4K H.264 fixture에서 **pixel-exact가 깨졌다**
(`Pixel-exact FAIL`).

사용자는 NVDEC production implementation이 아니라, NVDEC을 *쓰기 전에* 만족해야 하는
**안전성 계약**을 먼저 정하고 싶어 한다. 즉 "GPU decode가 가능"과 "GPU decode를 써도
정확"을 분리해, 정확하지 않은 영상은 자동으로 CPU fallback되는 기반을 만드는 것이
이번 단계의 목적이다.

## 2. Current Evidence (기록된 사실)

대상 `real_h264_3840x2160_30fps_020s.mp4`:

```text
codec            = H.264 High, level 6.0
resolution       = 3840x2160, yuv420p
r_frame_rate     = 60/1
avg_frame_rate   = 18045/301   (59.95)
has_b_frames     = 2
duration         = 20.066667 s
nb_frames        = 1203
file start       = decode-order frame 0..22 가 전부 P-frame, key_frame=0
first IDR        = decode-order frame 23
```

CPU software ↔ NVDEC 비교:

```text
file start / mid-GOP   → 12/12 frame mismatch   (maxAbs 148)
confirmed IDR 이후     →  0/12 frame mismatch   (PIXEL-EXACT)
```

보조 control/제거된 가설:

```text
software A vs A                      → byte-identical (deterministic)
software default vs -threads 1       → byte-identical (threading 아님)
양쪽 nv12 정렬 후 비교                 → maxAbs 135 잔존 (format conversion 아님)
1080p H.264 (IDR-start)              → 30/30 pixel-exact
HEVC Main10 (IDR-start)              →  8/8  pixel-exact
```

### 반드시 지킬 표현 규칙 (지시 §5)

좋은 표현:

> 현재 검증된 4K H.264 fixture에서 관찰된 CPU/NVDEC pixel mismatch는 **mid-GOP stream
> start와 강하게 연결**되며, 확인된 IDR 이후 구간에서는 pixel-exact가 확인되었다.

피해야 할 표현:

> 모든 4K H.264에서 NVDEC mismatch가 발생한다.
> 모든 mid-GOP H.264는 NVDEC과 mismatch한다.

**현재 데이터만으로 위 일반화는 검증되지 않았다.** 두 번째 4K fixture가 없고, 지시 §13이
신규 4K fixture 생성을 금지한다. 따라서 "4K에서 반복되는가"는 `INCONCLUSIVE`로 남는다.

### 용어 구분 (지시 §11)

```text
Confirmed-IDR decode start   = 실제로 확인된 IDR 지점에서 디코딩을 시작한 것
File-start IDR fixture      = 파일 자체가 IDR로 시작하는 것
```

`-ss 2.0`로 얻은 exactness는 **첫 번째**이며, 두 번째를 뜻하지 않는다. 두 상태는
별도로 유지한다.

## 3. Random-Access Safety Contract

가장 중요한 산출물. "H.264 supported + GPU available"로 정의하지 않는다.

```text
Video
  ↓
Codec capability
  ↓
Decoder availability
  ↓
Random-access safety
  ↓
Exactness compatibility
  ↓
NVDEC candidate
```

하나라도 확인 불가 → `CPU software fallback`.

### 3-1. 상태 정의 (3개)

```text
RandomAccessSafe
RandomAccessUnsafe
RandomAccessUnknown
```

구체적 enum/문자열 이름은 codebase 관례에 맞춰 F-2에서 확정한다. 이번에는 개념만 고정.

### 3-2. 최소 조사 조건

| 조건 | 내용 |
|---|---|
| **A** 시작/seek 지점이 self-contained인가 | 해당 decode 시작점에 필요한 reference picture가 현재 decode sequence 안에서 확보되는가 |
| **B** 첫 decoded frame이 요청 지점보다 뒤로 가지 않는가 | E-3A의 `firstDecodedPts <= seekRequestPts` 준용 |
| **C** production sampling predicate 재현 가능 | `ft + 0.05 >= target` **그대로** 사용 |
| **D** CPU baseline과 같은 sample identity | timestamp 근접성만으로 PASS하지 않음 |

**파서 금지**: Annex B / SPS / PPS / HEVC parser를 새로 작성하지 않는다. 판단에 필요한
정보는 **FFmpeg가 이미 노출하는 값**(`AVPacket.key_frame`, `pict_type`, 인덱스,
`avformat_index_get_*`)으로 한정한다.

## 4. Exactness Criteria

NVDEC production candidate는 다음을 **모두** 만족해야 한다.

```text
sample count
sample order
sample timestamp / frame identity
pixel output
```

최종 기준:

```text
diffPixelCount = 0
maxAbsDelta    = 0
```

아래는 **PASS 근거가 아니다** (지시 §9, §23):

```text
mean absolute difference가 작다
mean signed difference가 ≈ 0이다
PSNR / SSIM 높다
perceptual hash 일치
시각적으로 같다
"거의 동일"
```

## 5. Fallback Policy

F-1의 보수적 초기 정책:

```text
RandomAccessUnsafe  → CPU software fallback
RandomAccessUnknown → CPU software fallback
```

검증되지 않은 codec/stream 구조를 추측해 NVDEC으로 보내지 않는다. fallback 후 결과는
기존 CPU baseline과 **동일**해야 한다(sample count / order / frame identity / pixel /
fingerprint). **fallback 때문에 결과가 달라져서는 안 된다.**

## 6. E와 F의 경계 (지시 §26, §27, §28)

E와 F의 policy를 섞지 않는다.

```text
E = sampling strategy        (Sequential / Sparse)
F = decode backend capability & safety
```

따라서 E가 `Sparse`를 골라도 F가 반드시 NVDEC을 쓸 필요는 없다. F는
`NVDEC safe?`를 **다시** 판단한다.

목표 구조 (이중 안전장치):

```text
E Planner
    ↓
Sparse / Sequential
    ↓
F Backend Selector
    ↓
┌──────────────────────┐
│ Random-Access Safety │
└──────────┬───────────┘
           │
      ┌────┴────┐
      │         │
    SAFE      UNSAFE/UNKNOWN
      │         │
    NVDEC      CPU
      │         │
      └────┬────┘
           ↓
    Exact sample output
```

## 7. HardwareCapability와 RandomAccessSafety 분리 (지시 §29)

```text
HardwareCapability   = 이 GPU/FFmpeg가 codec을 하드웨어로 나눌 수 있는가
RandomAccessSafety   = 이 특정 stream을 그 경로로 처리해도 결과가 CPU와 같은가
```

예:

```text
H.264
HardwareCapability  = YES
RandomAccessSafety  = NO  (mid-GOP 시작)
→ NVDEC 사용 금지
→ CPU fallback
```

이 구분은 HEVC/AV1/다른 backend에도 동일하게 적용 가능해야 한다.

## 8. Validation Matrix (지시 §10)

```text
| Input condition        | Software | NVDEC | Exact | Policy        |
|------------------------|----------|-------|-------|---------------|
| Mid-GOP start          | baseline | differ| NO    | CPU fallback  |
| Confirmed IDR 이후     | baseline | same  | YES   | NVDEC candidate|
| File-start IDR fixture | baseline | ?     | ?     | NVDEC candidate|
| Unknown safety         | baseline | n/a   | n/a   | CPU fallback  |
```

`File-start IDR fixture` 행은 **새로 채워야 한다**(지시 §12: 기존 확보 자료만 사용,
신규 fixture 생성 금지). dataset 13개 중 실제 file-start가 self-contained인지 조사한다.

## 9. Performance Preflight (지시 §18, §20, §21)

**정상 random-access 조건이 확인된 경우에만** 측정한다. mid-GOP mismatch 조건은 정상
NVDEC 성능 근거로 쓰지 않는다.

```text
CPU software decode + required frame output
        vs
NVDEC + GPU→CPU transfer + required frame output
```

가능하면 분리 측정:

```text
decode elapsed
frame output
GPU→CPU transfer
end-to-end
```

없는 telemetry를 억지로 만들지 않는다. GPU decode가 빨라도 transfer가 크면
end-to-end 이득이 줄어들 수 있음을 함께 기록한다.

## 10. Selfcheck (지시 §30)

최소 자동화 항목:

```text
H264 mid-GOP            → RandomAccessUnsafe → CPU fallback
Confirmed-IDR decode    → RandomAccessSafe   → NVDEC candidate
pixel exact             → PASS
pixel mismatch          → FAIL
unknown safety          → CPU fallback
```

기존 E selfcheck는 제거하지 않는다. production decode decision은 변경하지 않는다
(`tests/`, `probe/`, `scratch/`, `documentation/`만 허용).

## 11. Non-Goals (지시 §2)

이번 F-1에서 **하지 않는다**:

- production NVDEC backend 구현, production video decoder 변경
- E planner production default / sampling predicate / `ft + 0.05` / target 변경
- pixel tolerance 추가, "거의 동일"을 PASS로 인정
- 새 H.264 / Annex B / SPS / PPS / HEVC parser
- FFmpeg source 수정, version upgrade, dependency 변경
- CUDA kernel, GPU→CPU copy 최적화, 새 GPU backend
- 4K fixture 임의 생성, 개인 media 커밋, 표준 image dataset 변경
- **PRODUCTION ADOPTION = NO**

## 12. Exit Criteria (지시 §39)

| Gate | 내용 |
|---|---|
| **A** Safety Contract | `Safe` / `Unsafe` / `Unknown` 3상태 정의 완료 |
| **B** Exactness | Confirmed-IDR에서 CPU==NVDEC pixel exact **재현**, mid-GOP에서 mismatch **재현** |
| **C** Fallback | mid-GOP에서 NVDEC denied → CPU fallback → CPU baseline exact |
| **D** Performance | 정상 exact 조건에서 CPU vs NVDEC benchmark |
| **E** Regression | CPU/GPU CTest + 기존 E selfcheck PASS |

## 13. Next

```text
F-1 PASS/CONDITIONAL  → F-2 NVDEC backend architecture / implementation brief
F-1 에서 정상 조건에서도 exactness 실패 → F-2 production integration 금지, 원인 조사 먼저
```

## 14. Related

- `docs/build-history/0.9.4.42.*` — E-3B, sparse 기각, exactness 기준선 원칙
- `docs/worklog/0.9.4.*` — `E-3B-REF`, `E-CLOSE`
- `docs/implementation-briefs/F-hardware-video-decode-backend.*` — F preflight (NVDEC 단일 범위)
- `docs/development-roadmap.*` — Node E/F 범위와 이중 안전장치 구조
