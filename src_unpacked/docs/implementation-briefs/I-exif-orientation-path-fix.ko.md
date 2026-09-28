# I-exif-orientation-path-fix — EXIF Orientation query path 결함 수정과 post-fix 회귀 측정 (Pre-register)

Status: **PRE-REGISTERED** (production 수정보다 먼저 커밋된다)

```text
Base         v0.9.4.34 / 21d0653
Version      v0.9.4.35
Experiment   production defect correction (EXIF) + post-fix regression
Product change   YES — 단, EXIF query path 결함으로 한정
I-2 통합      금지 (이번 단계에서 I-2 는 production 에 넣지 않는다)
```

이 문서는 수정 계약이다. I-2 production integration 과 분리한다.

---

## 1. 목적

v0.9.4.34 이 발견한 제품 결함을 고친다. EXIF Orientation 태그를 조회하는
WIC metadata path 가 잘못되어 있어, 제품이 회전을 한 번도 적용하지 않았다.

## 2. 확인된 사실 (v0.9.4.34 실측)

```text
제품 사용 경로  /app1/ifd/exif/{ushort=274}  -> 0/8 resolved, 8/8 BADPROPERTYKEY
동작하는 경로    /app1/ifd/{ushort=274}        -> 8/8 resolved, 값 1~8 일치
fixture 유효성  8/8 (방향 값이 의도대로 읽힘)
표준 dataset     orient_applied_files = 0
```

Microsoft WIC 의 `System.Photo.Orientation` 정책:

```text
JPEG  /app1/ifd/{ushort=274}
TIFF  /ifd/{ushort=274}
```

즉 Orientation 은 EXIF IFD(IFD0)에 있고, 제품 문자열의 `exif` 세그먼트가
잘못된 키를 만든다.

## 3. production 사용처 조사 결과

`src/image_decoder.cpp` 에 **3곳**이고 모두 동일한 리터럴·동일한 로직이다.

| 지점 | 함수 | 역할 |
|---|---|---|
| :194 | `decodeWicFile` | fingerprint fixed 경로 |
| :267 | `decodeWicFileAspect` | fingerprint aspect 경로 |
| :321 | `decodeWicFileAspectColor` | 표시(color) 경로 |

세 곳이 복제되어 있다. **행 단위 수정 3번이 아니라 공유 헬퍼 1개로 통합**한다.
표시 경로(3번째)도 반드시 함께 고친다. 회전 사진을 지문과 다르게 표시하면
matching 결과와 화면이 어긋난다.

## 4. 최소 수정 원칙

- 컨테이너(JPEG/TIFF) 분기를 새로 만들지 않는다. 현재 코드에 분기가 없고,
  필요 이상으로 구조를 바꾸지 않는 것이 지시다.
- 대신 ** 알려진 두 경로를 순서대로 시도**하고, 값을 얻은 첫 번째를 쓴다.

```text
/app1/ifd/{ushort=274}   (JPEG)
/ifd/{ushort=274}       (TIFF)
```

- 컨테이너를 몰라도 두 경로 중 하나는 반드시 해결되므로 JPEG/TIFF 를 모두
  처리하며, 새로운 추상화를 도입하지 않는다.
- Orientation 1 은 변환이 아니므로 `orientApplied` 를 증가시키지 않는다
  (기존 의미 유지).
- **XMP fallback 은 추가하지 않는다.** 이번 문제는 잘못된 EXIF path 다.
  XMP 가 제품 요구사항인지는 별도 판단 대상으로 남긴다.

## 5. 회귀 fixture

합성 EXIF JPEG 를 사용한다. 표준 dataset 은 **변경하지 않는다.**

```text
기반        dataset 안 JPEG 한 장(209x248)
삽입 위치   SOI 뒤, 선행 APP0/JFIF 이면 그 뒤
orientation 1~8 전부
```

각 fixture 에서 실제 production baseline 이 아래를 확인해야 한다.

```text
metadata query HRESULT = success
orientation value      = fixture 값과 일치
orientation transform  = 기대 변환과 일치 (1=항등, 3=180, 6=90CW, 8=270CW)
fixed decode           = success
aspect decode          = success
```

## 6. 전체 dataset EXIF coverage 재측정

수정 전 `orient_applied_files = 0` 은 coverage 부족이 아니라 잘못된 path 때문일
가능성이 높다. 수정 후 표준 3,347 파일에서 아래를 기록한다.

```text
total files / orientation metadata present / 값 1~8 분포
orientation applied / orientation query failures
```

## 7. Scan 회귀 (필수, 생략 불가)

v0.9.4.34 의 그룹 결과는 **회전이 적용되지 않은 baseline** 기준이다. EXIF
수정은 scan 결과를 바꿀 수 있으므로 이전 값을 그대로 유지하면 안 된다.

```text
pre-EXIF-fix  (v0.9.4.34)  5,579,470 쌍 / 457,126 그룹 / verdict diff 0
post-EXIF-fix (v0.9.4.35)  다시 측정, 전후 를 구분해 기록
```

## 8. I-2 비교 재실행

baseline 이 바뀌면 I-2 candidate 비교도 다시 수행한다. v0.9.4.34 의
5,579,470 / 457,126 / diff 0 을 기준선으로 보존하고 수정 후 값을 나란히
기록한다.

## 9. dataset 규칙

```text
fingerprint  e8f8fa6a..e2640a 유지
contents     유지 (SOURCES.md 포함)
both_fail 6건 유지 — 제거하지 않음
```

## 10. Acceptance gate

```text
Gate A  기존 production EXIF 정상화 (JPEG / TIFF / fixture 1~8)
Gate B  I-2 EXIF parity
Gate C  scan 회귀 (post-fix baseline / candidate / group consistency)
Gate D  I-2 readiness
```

A·B·C 가 모두 충족되어야 Gate D 를 올릴 수 있다. 하나라도 미완료면
`CONDITIONAL` 을 유지한다. **PASS 를 미리 가정하지 않는다.**

## 11. 이번 단계에서 하지 않는 것

```text
I-2 를 ImageDecoder / ScanPipeline 에 통합        금지
XMP fallback / 새 metadata framework              금지
dataset 변경 / SOURCES.md 제거                     금지
v0.9.4.34 기록 삭제·수정                          금지
I-1 재활성화                                      금지
```

## 12. telemetry

`metaMs` 와 `orientMs` 는 0.9.4.34 에서 이미 분리되었다(그 이전에는
`orientMs` 가 `metaMs` 의 복사본이었다). 이번에 의미를 재확인하고, field
name 과 측정 범위가 일치하는지 검증한다.
