# I-decode-once-resize-twice — D3 후속 최적화 후보 측정 (Pre-register)

Status: **PRE-REGISTERED** (후보 측정 코드보다 먼저 커밋된다)

```text
Base        v0.9.4.30 / a5fb31f
Experiment  D3 follow-up (Node I)
Product path change    NO
Version change         NO (측정 결과가 나온 뒤 v0.9.4.31 기록 단계에서만)
```

이 문서는 측정 구현 계약이다. 제품 decode 경로 교체가 아니다.

```text
Final status   MEASURED → CANDIDATE DEFERRED (production adoption NO)
Evidence       docs/build-history/0.9.4.31.ko.md   (측정)
               docs/build-history/0.9.4.32.ko.md   (안정성 조사 + flip artifact 정정)
후속 후보      중간 GrayImage 를 만들지 않는 "공유 WIC source + 독립 2개 scaler"
               구조는 별개 후보로 분리했다. I-1 의 대체가 아니라 후속이며,
               pre-register: docs/implementation-briefs/I-shared-wic-source.ko.md
```

아래 내용은 측정 당시의 pre-register 원문이며 소급 변경하지 않는다.

---

## 1. 목적

D3에서 확인된 두 개의 이미지 decode 비용을 줄일 수 있는 후보로서,

```text
공통 해상도 1회 decode
        ↓
   ┌────┴────┐
   ↓         ↓
64×64 f     aspect a
```

구조를 검증한다.

이번 단계는 **최적화 후보의 측정 및 타당성 검증 단계**이며, 제품의 실제 decode 경로를 교체하는 단계가 아니다.

---

## 2. 기존 기준선

현재 검증 경로는 다음과 같다.

```text
decode(path, 64, 64)
    ↓
f

decodePreserveAspect(path, 64)
    ↓
a
```

두 결과 모두 8비트 grayscale `GrayImage`이다 (`src/image_decoder.cpp:350,371`,
WIC 변환 `GUID_WICPixelFormat8bppGray`, `src/image_decoder.cpp:225,297`).

`f`는 정확히 64×64이어야 하며 (`src/image_verify.cpp:90`,
`ssimBuf`의 치수 일치 요구 `src/image_verify.cpp:122-125`), `a`는 기존
`decodePreserveAspect()`의 aspect-preserving semantics를 유지해야 한다.

기준선의 실제 동작과 telemetry 의미는 D3 결과를 기준으로 한다.

---

## 3. 검증 후보

후보 구조:

```text
파일
 ↓
공통 해상도 1회 decode
 ↓
GrayImage
 ├── resize → f (64×64)
 └── resize → a (aspect-preserving)
```

후보는 기존의 두 번째 WIC decode를 제거하고, 이미 메모리에 존재하는 이미지에서 두 결과를 생성할 수 있는지를 검증한다.

이번 단계에서는 생산 코드의 기존 `decode()` / `decodePreserveAspect()` 호출 경로를 교체하지 않는다 (`src/image_verify.cpp:82-83` 유지).

---

## 4. 공통 Decode 해상도

공통 decode 해상도는 하나를 임의로 채택하지 않고 사전에 정의한 후보군을 비교한다.

우선 측정 대상은 다음과 같이 한다.

```text
128
192
256
384
512
```

각 해상도에 대해 동일한 데이터셋과 동일한 측정 조건으로 기준선과 후보를 비교한다.

측정 결과에 따라 사후적으로 후보군에 새로운 해상도를 추가하지 않는다.

### 해상도 선정 근거 (실측 기반, 사후 추가 아님)

아래 분포는 pre-register 작성 시점에 `ffprobe` + 헤더 직접 파싱으로 실측한 값이다.

```text
형식   표본   long edge 범위
BMP    2700   대부분 8, 일부 256
GIF    20     121–1500
ICO    12     ≤256 (대부분 ≤64)
JPEG   200    201–850
PNG    200    224–1281 (IHDR 직접 판독; 사용 ffprobe 빌드는 PNG 치수 미표기)
TIFF   14     1–601
WEBP   200    312–1920
```

- 출력 장축이 64이므로, 128은 출력의 2배에서 시작하는 최소 의미 해상도다.
- 192–512는 실제 네이티브 장축(201–1920)의 하위 구간을 커버한다. 공유 decode
  자체가 이 구간에서 다운샘플이 되므로, 해상도를 올릴수록 공유 decode 비용은
  늘고 resize 정보 손실은 준다. 이 trade-off 곡선을 그리는 것이 측정 목적이다.
- BMP 8px / TIFF 1px 케이스는 업샘플 코너로서, 정보 없이 보간 비용만 드는
  경우를 포맷별 결과에서 분리해 해석한다.

## 5. 데이터셋

D3와 동일한 대표 데이터셋을 사용한다.

```text
files       = 3,347
bytes       = 102,475,315
fingerprint = e8f8fa6ab0257f1e2b7d839b73ec14b1cc726695dac2efe79b55a13a13e2640a
```

동일한 데이터셋 fingerprint를 사용하여 기준선과 후보 측정이 동일한 입력 집합임을 보장한다.

데이터셋을 변경하는 경우에는 변경 사실과 새로운 fingerprint를 별도로 기록한다.

## 6. 정확성 검증

각 공통 decode 해상도에 대해 다음을 확인한다.

### f

```text
width  = 64
height = 64
pixel count = 4096
```

### a

기존 `decodePreserveAspect()`와 동일한 aspect-preserving semantics를 유지하는지 확인한다.

특히:

```text
원본 종횡비
장축 기준 크기
width × height
```

를 비교한다. `a`의 치수 계산식은 `decodePgmAspect` /
`decodeWicFileAspect`의 장축 고정·단축 `lround` 반올림과 동일해야 하며
(`src/image_decoder.cpp:143,293`), 반올림 경계가 어긋나면 geometry-different로
기록한다.

## 7. Pixel Parity

가능한 범위에서 기준선과 후보의 출력 버퍼를 직접 비교한다.

대상:

```text
f baseline ↔ f candidate
a baseline ↔ a candidate
```

각 결과를 다음 중 하나로 기록한다.

```text
byte-identical
pixel-different
geometry-different
```

차이가 발생하면 단순히 허용 오차라고 판단하지 않고 원인을 기록한다.

### 사전 고지 (추측이 아닌 코드 근거)

`centerCropResize`는 정수 nearest-neighbor 샘플링이다
(`src/crop_fingerprint.cpp:12`, `y*ch/outSize` 정수 연산). 출력이 소스 해상도에
의존하므로, WIC Fant 스케일러로 만든 기준선과 바이트 일치는 기대하지 않는다.
따라서 byte parity는 참고 지표이며, 판정의 주축은 §8의 groups/verdict다.
이 기대를 pre-register에 먼저 적어 두어, 사후에 기준을 바꾸지 않는다.

## 8. Verification 결과 비교

기준선과 후보에서 다음을 비교한다.

```text
groups
verdict
```

가능한 경우 개별 비교 결과까지 확인하여 단순한 전체 그룹 수 동일 여부만으로 결과를 판단하지 않는다.

최소 요구사항:

```text
groups 변화 여부 확인
verdict 변화 여부 확인
```

`groups`가 동일하더라도 `verdict`가 달라지는 경우 별도 문제로 기록한다.

## 9. 성능 측정

기준선:

```text
fixed decode
+
aspect decode
```

후보:

```text
shared decode
+
resize for f
+
resize for a
```

각 단계의 시간을 별도로 측정한다.

최소 측정 항목:

```text
baseline fixed decode
baseline aspect decode
baseline total

candidate shared decode
candidate f resize
candidate a resize
candidate total
```

기준선과 후보의 총 비용을 동일한 측정 조건에서 비교한다.

## 10. 측정 조건

측정 조건은 D3와 동일하게 유지한다.

```text
warm-up 1회 제외 + measured 5회 (D3와 동일)
mean / median / min / max / range
```

다음 항목을 결과에 명시한다.

```text
warm-up 횟수
실측 반복 횟수
평균 또는 중앙값
측정 대상 파일 수
데이터셋 fingerprint
```

기존 benchmark framework를 재사용한다
(`msf_dataset_baseline <root> <app-dir> [runs]`, `docs/STRUCTURE.md` §빌드/테스트).

## 11. 포맷 범위

기존 D2/D3 검증 범위에 포함된 포맷을 사용한다.

```text
BMP
GIF
JPEG
PNG
WEBP
TIFF
ICO
```

PGM fallback이 해당 테스트 경로에 포함되는 경우 별도로 기록한다
(`readPgmFile`은 P5 전용이며 `maxv`를 정규화하지 않는다,
`src/image_decoder.cpp:88-91`; EXIF 방향 처리도 없다).

## 12. EXIF Orientation

기존 `decodePreserveAspect()`의 EXIF orientation 처리를 후보에서도 유지해야 한다.

양쪽 decode 경로는 EXIF orientation 태그 274를 읽는다
(`src/image_decoder.cpp:194,267`). 후보의 공유 decode도 동일 매핑을 적용해야
하며, 적용 지점(공유 decode 1회)이 기준선(2회 호출)과 달라지는 만큼 방향 처리
비용의 귀속이 바뀌는 점을 telemetry에 반영한다.

회전 정보를 가진 입력을 포함하여 기준선과 후보의 `a` 결과를 비교한다.

방향 처리 차이가 발생하면 성능보다 먼저 원인을 분석한다.

## 13. Cache

기존 검증 캐시의 동작은 변경하지 않는다.

캐시 키는 `{path, size, mtime, quickHash}` 이며 LRU 최대 32개,
miss 시 `{f, a}` 쌍을 저장한다
(`src/image_verify.cpp:22-28,60-66,101-105`).

캐시 hit 시에는 기존처럼 `f` / `a` 쌍을 그대로 사용하는 구조를 유지한다.

후보 측정에서도 캐시 자체를 제거하거나 우회해서 제품 동작을 바꾸지 않는다.

다만 후보 효과 분석을 위해 필요하다면:

```text
cache hit
cache miss
```

상태별 결과를 구분해서 측정한다.

## 14. Telemetry

기존 D3 telemetry 필드의 의미를 변경하지 않는다.

실측 확인된 기존 baseline 키 (`src/benchmark.cpp:725-799`):

```text
decodeTotalMs / decodeCalls / decodeAspectCalls
decodeFullCalls / decodeFullTotalMs / decodeFullState
decodeAspectOnlyCalls / decodeAspectOnlyTotalMs / decodeAspectOnlyState
decodeFullOpenMs / decodeAspectOpenMs
decodeFullCopyMs / decodeAspectCopyMs
decodeFullFactoryMs / decodeAspectFactoryMs
decodeSplitOverMs / decodeSplitState
verifyDecodeMs
```

각 `*Ms` 키에는 `*State`(`measured` / `not_measured`)가 붙는 것이 이 프로젝트의
naming convention이다. 후보 측정값은 이 convention을 따라 별도 telemetry
항목으로 기록한다. 후보 필드명은 구현 시점에 convention 확인 후 확정하며,
본 pre-register는 이름이 아니라 **규칙**(분리 기록 + state 동봉 + 기존 의미
불변)을 고정한다.

D3에서 유지한 `mergeDecodeTelemetry`의 combined 의미를 훼손하지 않는다.

## 15. 제품 코드 적용 금지

이번 단계에서는 다음을 변경하지 않는다.

```text
production decode path
decode()
decodePreserveAspect()
기존 검증 캐시
D2 Path C 생산 경로
Adaptive Scheduler
resource_policy
CPU 10~90 정책
GPU 정책
```

후보는 benchmark 또는 test 전용 경로에서 측정한다.

## 16. 판정 목적

이번 pre-register의 목적은 다음 세 가지 질문에 답하는 것이다.

### 질문 1

공통 decode 1회만으로 기존의 `f`와 `a` 의미를 재현할 수 있는가?

### 질문 2

그 과정에서 `groups`와 `verdict`를 유지할 수 있는가?

### 질문 3

실제 총 비용이 기존의 두 decode보다 낮은가?

세 질문의 답을 실제 측정 결과로 기록한다.

## 17. 이번 단계에서 제품 최적화 채택을 미리 결정하지 않는다

측정 결과에 따라 다음 단계가 달라질 수 있다.

```text
정확성 유지 + 성능 개선
→ 생산 적용 후보로 검토

정확성 유지 + 성능 차이 미미
→ 채택 필요성 재검토

성능 개선 + groups/verdict 변화
→ 정확성 원인 추가 조사

byte parity 또는 geometry 차이
→ 차이 원인 분석 후 추가 판단

성능 개선 없음
→ 후보 보류 또는 폐기
```

이번 pre-register 자체에서는 제품 적용을 확정하지 않는다.

## 18. 변경 금지 원칙

이번 작업과 관계없는 다음 영역은 수정하지 않는다.

```text
D2 Path C
CPU 10~90 사용자 정책
ResourcePolicy 계산식
Scheduler
GPU Adaptive 정책
기존 benchmark 의미
기존 D3 telemetry 의미
```

기존 테스트를 삭제하거나 완화해서 후보 결과를 통과시키지 않는다.

## 19. 사전 등록 기준

이 문서를 먼저 커밋한 후 후보 측정 구현을 시작한다.

pre-register 이후에 측정 결과에 맞춰:

```text
해상도 후보 추가
측정 조건 변경
판정 기준 변경
불리한 결과 제외
```

를 하지 않는다.

변경이 꼭 필요한 경우 변경 이유와 변경 시점을 별도로 기록한다.

## 20. 다음 단계

Pre-register 커밋 후 다음 측정 빌드에서:

```text
기준선
vs
128
vs
192
vs
256
vs
384
vs
512
```

후보를 동일한 데이터셋과 조건으로 측정한다.

최종 결과는 다음 항목을 포함한다.

```text
공통 decode 비용
f resize 비용
a resize 비용
총 비용
f parity
a parity
groups
verdict
포맷별 결과
EXIF 결과
PGM fallback 결과
f byte parity
a byte parity
groups 비교
verdict 비교
cache hit/miss 영향
CPU CTest 결과
GPU CTest 결과
verify_parity 결과
telemetry/schema 검증 결과
production path가 변경되지 않았는지 확인
측정 중 발견된 문제
다음 단계 제안
```

측정 결과를 바탕으로 후속 생산 적용 여부를 별도로 결정한다.
