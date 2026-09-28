# I-shared-wic-source — 공유 WIC source/frame + 독립 2개 scaler 후보 (Pre-register)

Status: **PRE-REGISTERED** (후보 probe 구현보다 먼저 커밋된다)

```text
Base         v0.9.4.32 / 69d0350
Experiment   D3 후속 (Node I) — I-2
Product path change    NO
Version change         NO (실측 결과가 나온 뒤 v0.9.4.33 기록 단계에서만)
선행 후보     I-1 (공유 GrayImage + resize 2회) = DEFERRED, 이 후보의 대체가 아님
D2 Path C    혼합 금지 (CreateDecoderFromStream / HandleStream 검토하지 않음)
```

이 문서는 측정 구현 계약이다. 제품 decode 경로 교체가 아니다.

---

## 1. 목적

D3 가 확인한 중복은 두 개의 **완전한 WIC 파이프라인** 이다. I-1 은 이를
`중간 GrayImage 공유` 로 줄이려 했으나, 중간 8-bit GrayImage 를 거치는
2단계 Fant 체인 때문에 f/a byte parity 를 깨뜨려 `DEFERRED` 되었다
(`docs/build-history/0.9.4.32.ko.md` §8, §16).

이번 후보는 **중간 GrayImage 를 아예 만들지 않는다.** 공유하는 것은
factory · decoder · frame · metadata · orientation source 뿐이고,
최종 스케일러/컨버터/`CopyPixels` 는 두 개를 독립적으로 유지한다. 따라서
각 결과물의 리샘플 체인이 baseline 과 동일해질 수 있다.

핵심 질문은 **속도가 아니라 exact output parity** 다.

## 2. Hypothesis (가설이며 사실이 아니다)

- H1: 하나의 `IWICBitmapSource` 를 두 개의 `IWICBitmapScaler` 가 안전하게
  참조할 수 있다.
- H2: 두 스케일러가 같은 소스를 볼 때 f/a 결과가 baseline 과 **바이트 동일**하다.
- H3: WIC 가 실제 decode workload 를 공유하므로 후보 총비용이 감소한다.
- H4 (반대 가설): factory/decoder/frame 생성만 줄어고 `CopyPixels` 는 거의
  그대로 비싸서 총비용 감소가 미미하거나 없다.

**H1~H4 어느 것도 가정으로 사용하지 않는다.** 전부 측정으로 확인한다.

## 3. Baseline

현행 제품 경로 (변경하지 않는다).

```text
ImageDecoder::decode(path, 64, 64)                 -> f
ImageDecoder::decodePreserveAspect(path, 64)        -> a
```

각각 독립적으로 다음 6단계를 수행한다
(`src/image_decoder.cpp:154` `decodeWicFile`, `:238` `decodeWicFileAspect`).

```text
CoInitializeEx
CreateWICFactory2 (fallback: CreateWICFactory)
CreateDecoderFromFilename
GetFrame(0)
GetMetadataQueryReader -> EXIF orientation -> 필요 시 CreateBitmapFlipRotator
CreateBitmapScaler(Fant) -> CreateFormatConverter(8bppGray) -> CopyPixels
CoUninitialize
```

## 4. Candidate structure

```text
파일
 ↓
CoInitializeEx                      1회
 ↓
WIC factory                         1회
 ↓
CreateDecoderFromFilename           1회
 ↓
GetFrame(0)                         1회
 ↓
EXIF orientation (FlipRotator)      1회
 ↓
공유 src (IWICBitmapSource)
   ├── scaler A -> 64x64    -> converter A -> CopyPixels A -> f
   └── scaler B -> aspect64 -> converter B -> CopyPixels B -> a
 ↓
모든 COM object release             (CoUninitialize 보다 먼저)
 ↓
CoUninitialize                      1회
```

핵심: `f`/`a` 용 **scaler 와 converter 를 각각 새로 만든다.** 공유하는 것은
소스까지다. I-1 의 결정적 차이(중간 GrayImage → 두 번째 Fant) 가 없다.

## 5. Baseline 대비 달라지는 것과 달라지지 않는 것

동일해야 하는 것 (바이트 수준으로):

```text
f  : source -> Fant -> 64x64
a  : oriented source -> Fant -> aspectDims(sw,sh,64)
```

후보가 달라도 되는 것: factory/decoder/frame 생성 횟수, CoInitialize 횟수.

## 6. geometry parity 기준

`a` 목표 크기는 **반드시 baseline 이 쓰는 것과 동일한 oriented source 크기**에서
계산한다. 중간 해상도 R 에서 다시 계산하지 않는다.

```text
baseline : src->GetSize() -> (sw,sh) -> aspectDims(sw,sh,64) -> scaler
candidate: 같은 src->GetSize() -> 같은 (sw,sh) -> 같은 aspectDims -> 다른 scaler
```

이 구조에서는 I-1 에서 관찰된 geometry mismatch(R128 93건, R256 54건,
R384 39건, R512 34건)가 발생하지 않아야 한다. 발생하면 원인을 조사한다.

## 7. Exactness criteria (1순위)

후보 채택의 최소 조건. 하나라도 깨지면 즉시 `DEFERRED` 또는 원인별
`NOT ACCEPTED` 를 검토한다.

```text
1. f geometry identical  (baseline 64x64 와 동일)
2. a geometry identical  (baseline 과 동일)
3. f pixel byte identical (전체 dataset)
4. a pixel byte identical (전체 dataset)
```

- **샘플만으로 PASS 처리하지 않는다.** dataset 전체를 검사한다.
- 기준선과 후보는 반드시 같은 파일·같은 시점의 결과끼리 비교한다.
- I-1 에서 발생한 mixed pairing 결함을 재발시키지 않는다:

```text
baseline A + baseline B
candidate A + candidate B
```

절대로 `baseline A + candidate B` 를 섞지 않는다.

## 8. Dataset

```text
경로        C:\project\test_sample_img_vid
파일 수     3,347
bytes       102,475,315
fingerprint e8f8fa6ab0257f1e2b7d839b73ec14b1cc726695dac2efe79b55a13a13e2640a
형식        BMP / GIF / JPEG / PNG / WEBP / TIFF / ICO
```

fingerprint 이 다르면 측정 전부터 중단하고 원인을 확인한다.

## 9. EXIF fixture requirement

현재 dataset 은 `orient_applied_files = 0` 이므로 **EXIF 경로가 측정되지
 않는다.** 이번 후보는 EXIF 를 반드시 검증해야 한다.

```text
필요 최소 orientation: 1, 3, 6, 8 (그 외 2,4,5,7 도 가능하면 포함)
```

추가 전 반드시 확인한다: 기존 fixture 생성 방식 · source/license 기록 ·
dataset fingerprint 영향. 표준 dataset 을 임의로 변경하지 않는다. 필요하면
**별도의 정확성 fixture dataset 으로 분리**하고, 이 경우 그 fingerprint 도
기록한다.

## 10. PGM fixture requirement

현재 `pgm_fallback_files = 0`. PGM 경로도 미검증이다.

후보의 PGM 변형 구조는 다음을 검토한다(이번 단계에서 production 연결 금지).

```text
readPgmFile 1회 -> 원본 byte buffer 공유
                            ├-> scaleGray -> fixed
                            └-> scaleGray -> aspect
```

현행 `readPgmFile` 은 P5 전용이며 `maxv` 정규화가 없다
(`src/image_decoder.cpp:84-92`). 이 동작은 변경하지 않는다.

## 11. 측정 항목

Baseline

```text
decode()                 = f_ms
decodePreserveAspect()   = a_ms
baseline total           = f_ms + a_ms
```

Candidate

```text
shared setup (COM/factory/decoder/frame/orientation) = shared_ms
fixed branch   (scaler+converter+CopyPixels)        = fbranch_ms
aspect branch  (scaler+converter+CopyPixels)        = abranch_ms
candidate total                                    = shared_ms + fbranch + abranch
```

추가로 분리 기록한다: `CreateDecoderFromFilename` 소요, `GetFrame` 소요,
metadata/orientation 소요, `CopyPixels` A/B 소요.

핵심 관찰 대상: **B 브랜치가 WIC 내부 decode 를 재사용하는가.** 즉
`CopyPixels B` 가 `CopyPixels A` 와 비슷한 비용이면 공유가 decode 를
나누지 못한다는 뜻이므로 그대로 기록한다(H4).

## 12. Timing method · run rule

```text
warm-up   1회 (결과에 포함하지 않음)
실측      5회
기록      mean / median / min / max / range
판정      median 기준, run 간 편차 밖의 차이만 유효
```

측정 편차 안의 1~2 % 차이는 개선으로 주장하지 않는다.

## 13. No production change

pre-register 및 probe 단계에서 아래 파일의 production behavior 는 변경하지
않는다.

```text
src/image_decoder.cpp
src/image_verify.cpp
src/media_pipeline.cpp
src/scan_pipeline.cpp
src/crop_fingerprint.cpp
```

측정 전용 probe 를 추가한다. probe 는 자체 WIC 파이프라인을 직접 구현하며
제품 decode 함수를 대체하거나 호출 순서를 바꾸지 않는다.

## 14. D2 Path C separation

이번 후보는 `CreateDecoderFromFilename` 을 유지하고 decoder/frame/source
공유만 다룬다. `CreateDecoderFromStream` · `HandleStream` · Path C 생산
적용은 하지 않는다. D2 Path C 는 `DEFERRED` 로 유지된다.

## 15. API / COM lifetime 검증 항목 (추측 금지)

구현 전에 아래를 **실제 컴파일·실행으로** 확인하고 결과를 기록한다.

```text
A. factory / decoder / frame 생성
B. 하나의 IWICBitmapSource 를 두 scaler 가 참조 (H1)
C. orientation FlipRotator 를 거친 source 를 두 scaler 가 공유
D. scaler Initialize 후 source 를 outlive 하는가 (lifetime 순서)
E. 두 converter 를 각각 scaler 에 Initialize
F. CopyPixels 2회 호출 시 두 번째가 첫 번째를 재사용하는가
G. 모든 ComPtr 가 CoUninitialize 보다 먼저 해제되는가
H. f/a 출력 geometry
I. byte compare
J. EXIF 분기 / PGM 분기
```

현행 소스가 이미 명시한 제약: `CoUninitialize()` 이 살아 있는 WIC object 보다
먼저 실행되면 fault 한다 (`src/image_decoder.cpp:150-153`). probe 는 이
순서를 반드시 지킨다.

## 16. Acceptance / Defer criteria

채택(`ACCEPT`) 전제 — **모두** 만족해야 한다.

```text
A1. 전체 dataset 에서 f geometry identical
A2. 전체 dataset 에서 a geometry identical
A3. 전체 dataset 에서 f byte identical
A4. 전체 dataset 에서 a byte identical
A5. EXIF fixture parity PASS
A6. PGM fixture parity PASS
A7. 비용 감소가 run 간 편차를 명백히 초과
A8. CPU/GPU 양쪽에서 동일 결과
```

하나라도 깨지면:

```text
- A1~A4 파괴 -> DEFERRED 또는 원인별 NOT ACCEPTED
- A5/A6 미검증 -> DEFERRED (fixture 확보 전까지)
- A7 실패 -> NO MEASURABLE GAIN (정확성은 통과했을 수 있음, 별도 기록)
```

**threshold 를 변경해 맞추지 않는다. score tolerance 를 임의로 늘리지 않는다.**

## 17. 측정 순서

```text
1. API 구조 검증 (§15)
2. probe 구현 + selfcheck
3. f/a geometry parity
4. f/a byte parity
5. EXIF / PGM parity
6. 성능 측정
7. parity PASS 이면 -> full-scan groups 비교 검토
8. 그 다음에야 별도 production implementation brief
```

pixel divergence 가 있는 상태에서 full-scan 을 먼저 하는 방식은 반복하지
않는다.

## 18. Full-scan groups

I-1 은 pixel divergence 가 확정된 상태였는데도 revisit 조건으로
full-scan 을 두었다. 이번에는 순서를 바꾼다. **parity(§7)가 깨지는 동안에는
full-scan 을 진행하지 않는다.** I-1 의 full-scan 기록은 `DEFERRED` 상태로
보존하되 즉시 실행하지 않는다. 그 자체가 남은 문제의 질문에 더 직접적인
답을 주지 않기 때문이다.

## 19. Future revisit condition

이후 재검토 조건.

```text
- A1~A4 가 깨지는 원인이 규명되면, 후보 구조 자체를 재설계
- EXIF/PGM fixture 가 dataset 에 추가되면 즉시 재측정
- Windows/WIC 런타임이 바뀌면 재측정
- WIC 가 frame 전체를 캐시하지 않는 것으로 확인되면, 후보의 가치 자체가
  재검토 대상이 된다 (H4 가 사실로 확정되는 경우)
```

## 20. Related

```text
I-1  docs/implementation-briefs/I-decode-once-resize-twice.*   DEFERRED
I-1S docs/implementation-briefs/I-shared-decode-stability.*   원인 규명 완료
D2   docs/build-history/0.9.4.28.*                            Path C DEFERRED
D3   docs/build-history/0.9.4.29.*                            PASS
```
