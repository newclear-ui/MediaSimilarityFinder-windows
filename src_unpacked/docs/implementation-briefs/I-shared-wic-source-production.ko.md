# I-shared-wic-source-production — I-2 Shared WIC Source 구조의 production 통합 (Pre-register)

Status: **PRE-REGISTERED** (production 통합 구현보다 먼저 커밋된다)

```text
Base         v0.9.4.35 / 6c981f7
Version      v0.9.4.36
Experiment   I-2 production integration
Product change   YES — I-2 구조에 한정
이전 단계      I-2 = READY FOR PRODUCTION IMPLEMENTATION (v0.9.4.35 Gate A~D PASS)
```

## 1. 목적

probe 에서 검증하고 0.9.4.35 에서 모든 gate 를 통과한 I-2 구조를 실제
production `ImageDecoder` 에 넣고, 기존 동작과 완전한 parity 를 확인한다.

**이 버전은 EXIF 기능 추가 버전이 아니다.** 0.9.4.35 의 EXIF semantics 를
그대로 유지하면서 구조적 최적화만 승격한다.

## 2. 조사 결과 — 중복은 어디에 있는가

```text
src/image_verify.cpp:82-83  verifyBuffersFor()
  dec.decode(path, kDim, kDim, f, &tel->decodeFull)        <- WIC 파이프라인 1회
  dec.decodePreserveAspect(path, kDim, a, &tel->decodeAspect) <- WIC 파이프라인 1회
```

파일 1개당 factory · decoder · frame · metadata · orientation source 를
**두 번** 만든다. 이것이 D3 가 계측한 중복의 실체다.

다른 단독 호출자는 두 결과를 함께 쓰지 않는다 (따라서 이득이 없다):

```text
src/media_pipeline.cpp:33,45   decode() 만          (32x32 perceptual hash)
src/media_pipeline.cpp:110     decodePreserveAspect() 만 (128 crop fingerprint)
src/monitor.cpp:307            decodePreserveAspect() 만
```

즉 **두 결과를 함께 요청하는 유일한 production 경로는 `verifyBuffersFor`
하나**이며, 통합 이득은 전적으로 그 경로에서 발생한다.

## 3. production 적용 구조

`single source of truth` 하나를 만들고 public API 3개가 모두 그것을 호출한다.

```text
decodeWicBranches(path, fw, fh, maxDim, wantFixed, wantAspect, f, a, telFixed, telAspect)
  CoInitializeEx                1회
  WIC factory                   1회
  CreateDecoderFromFilename     1회
  GetFrame(0)                   1회
  EXIF orientation source       1회   (exifOrientationToTransform 재사용)
  shared IWICBitmapSource
    ├─ scaler -> converter -> CopyPixels -> f
    └─ scaler -> converter -> CopyPixels -> a
  모든 COM object release -> CoUninitialize

decode()                 -> wantFixed=true,  wantAspect=false
decodePreserveAspect()   -> wantFixed=false, wantAspect=true
decodeBoth()             -> wantFixed=true,  wantAspect=true    <- 신규
```

- `decode()` / `decodePreserveAspect()` 는 **시그니처·반환값·semantics 를
  그대로 유지**하고 내부만 공유 헬퍼로 바꿉니다. 단독 호출자는 영향받지 않는다.
- `decodeBoth()` 는 두 결과를 함께 요구하는 경로(`verifyBuffersFor`) 전용.
- **중간 `GrayImage` 를 만들지 않는다.** I-1 의 2단계 resample 구조를
  재도입하지 않는다.

## 4. telemetry 배분 (의미 보존)

D3/v0.9.4.29 부터 유지된 불변식:

```text
decodeFull.calls        == 파일당 1
decodeAspect.aspectCalls == 파일당 1
decodeFullTotalMs + decodeAspectOnlyTotalMs == 전체 호출 시간  (split identity)
```

통합 후에도 이 불변식을 유지한다.

```text
공통 비용 (COM/factory/open/metadata/orient)  -> telFixed (없으면 telAspect)
fixed branch (scaler/convert/copy)            -> telFixed
aspect branch (scaler/convert/copy)           -> telAspect
telFixed.totalMs     = 공통 + fixed branch
telAspect.totalMs    = aspect branch
```

따라서 merge 후 모든 D9d/D3 key 의 합은 동일하게 재구성된다. benchmark JSON
schema 를 바꾸지 않는다. D3 의 "두 번째 decode = 49.60 %" 라는 **관측값**은
통합으로 낮아지는 것이 목적이며, schema/의미 훼손이 아니다.

`metaMs` 와 `orientMs` 는 0.9.4.34/35 에서 분리된 상태를 그대로 유지한다.

## 5. EXIF 보존

`exifOrientationToTransform` 을 재사용한다. query path 를 다시 설계하지 않는다.

```text
/app1/ifd/{ushort=274}  (JPEG)
/ifd/{ushort=274}      (TIFF)
```

orientation 1~8 fixture 결과가 그대로 유지되어야 한다.

## 6. 표시(color) 경로

`decodeWicFileAspectColor` 는 color 출력을 만들므로 gray 공유 source 와는
출력 포맷이 다르다. 다만 **orientation semantics 는 동일한
`exifOrientationToTransform` 을 이미 공유**하고 있으므로 분석 경로와 표시
경로의 방향이 갈라질 수 없다. UI 를 개편하지 않는다.

## 7. 실패 동작 보존

- orientation metadata 없음 = 정상 이미지. decode 실패가 아니다.
- WIC 실패 시 기존 fallback 을 **그대로 호출**한다 (`decodePgm` /
  `decodePgmAspect`). 두 번째 fallback 을 새로 만들지 않아 실패 경로의
  동작을 byte 단위로 보존한다.
- `decodeBoth` 는 두 결과 중 하나라도 실패하면 두 API 와 동일하게 실패한다.

## 8. Exactness 기준

```text
dataset fingerprint e8f8fa6a..e2640a 유지 (변경 금지)
both_success       = 3341
candidate_only_fail = 0
both_fail          = 6 유지
fixed geometry/pixel  = 3341/3341
aspect geometry/pixel = 3341/3341
```

## 9. Scan 회귀 기준

```text
pairs_compared = 5,579,470
baseline_groups = candidate_groups = 457,126
verdict_diffs = 0
max_abs_score_diff = 0
group_parity = identical
```

score · verdict · grouping · failure classification 네 가지가 모두 일치해야 한다.

## 10. 성능 측정

이번 버전부터 **probe 가 아니라 production end-to-end** 를 측정한다.

```text
A. decoder 자체  v0.9.4.35 production vs v0.9.4.36 I-2 production
B. full scan elapsed
```

warm-up 1회 + 실측 5회, median 사용. decoder microbenchmark · full scan ·
filesystem/cache 상태 · process startup 을 섞지 않고 각각 따로 기록한다.
성능이 개선되지 않아도 exactness 가 유지되면 그대로 기록한다.
**예상 수치를 미리 쓰지 않는다.**

## 11. Rollback 기준

아래 중 하나라도 발생하면 통합을 완료로 선언하지 않고, 원인 · 영향 범위 ·
재현 조건 · 기존/변경 동작 · rollback 필요 여부를 기록한다.

```text
pixel mismatch / geometry mismatch / score mismatch / grouping mismatch
EXIF 회귀 / failure classification 변화 / CPU·GPU 빌드 실패 / CTest 실패
caller 회귀 / telemetry 의미 훼손
```

코드를 임의로 완화하거나 테스트를 삭제하지 않는다.

## 12. 적용하지 않는 범위

```text
XMP fallback / 새 EXIF framework / 새 컨테이너 분기
새 포맷 / GPU backend / E 단계 Video Decode Planner / NVDEC
D 노드 수정 / I-1 재도입 / dataset 변경 / fingerprint 변경
similarity threshold / grouping 정책 / verify 정책 / UI 동작 변경
exactness 완화를 근거로 한 성능 주장
```

## 13. 성공 판정

```text
Gate A  production 구조 (shared source, 중복 제거, intermediate 없음)
Gate B  exactness  (3341/3341, failure parity)
Gate C  scan 회귀  (전수 5,579,470쌍 무변화)
Gate D  EXIF       (orientation 1~8 PASS)
Gate E  CPU/GPU    (build + 전체 CTest + smoke)
Gate F  performance (production 기준 측정 완료)
```

모두 충족하면 `PRODUCTION ADOPTION = YES`. 하나라도 아니면 `NO` 또는 구체적
blocking reason 을 명시한다.
