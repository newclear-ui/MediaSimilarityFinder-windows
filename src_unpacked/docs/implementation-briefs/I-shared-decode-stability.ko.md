# I-shared-decode-stability — D3 후속 후보 안정성·리샘플링 차이 원인 조사 (Pre-register)

Status: **PRE-REGISTERED** (조사 코드보다 먼저 커밋된다)

```text
Base        v0.9.4.31 / a1169b8
Experiment  D3-follow-up stability (Node I)
Product path change    NO
Version change         NO (조사 결과가 나온 뒤 v0.9.4.32 기록 단계에서만)
```

이 문서는 원인 규명 계약이다. 제품 decode 경로 교체도, 후보 채택도 아니다.

---

## 1. 목적

v0.9.4.31에서 DEFERRED된 "공통 decode 1회 + resize 2회" 후보의 정확성 차이
원인을 규명한다. 성능 이점은 이미 관찰됐으므로(기준선 대비 24–46 %),
이번 단계의 유일한 질문은 "왜 출력이 달라지는가"다.

## 2. 답해야 할 질문

```text
Q1. 기준선과 후보의 pixel 차이는 정확히 어디에서 발생하는가?
Q2. 차이가 resize 알고리즘 때문인가, 공통 decode 해상도 때문인가,
    WIC 직접 scaling과 2단계 scaling의 차이 때문인가?
Q3. 왜 R128~R256에서는 sampled verdict flip이 없고 R384~R512에서는 2건인가?
Q4. 두 flip 사례는 특정 이미지의 구조적 특성인가,
    특정 interpolation/resampling 경계인가?
Q5. 후보가 기존 의미를 유지할 수 있는 현실적인 공통 해상도 또는
    리샘플링 구조가 존재하는가?
```

## 3. 기지 사실 (코드·실측 근거, 추측 아님)

- 기준선 WIC 스케일러는 세 decode 경로 전부 Fant다
  (`src/image_decoder.cpp:220,294,344`).
- `centerCropResize`는 정수 nearest-neighbor 샘플링이다
  (`src/crop_fingerprint.cpp:12`, `y*ch/outSize` 정수 연산).
- `a` 치수식은 장축 고정·`lround` 반올림·최소 1
  (`src/image_decoder.cpp:293`, PGM은 `sw>=sh` 변형 `:143`).
- `ssimBuf`/`flipBuf`는 `image_verify.cpp` 파일-로컬이다
  (`:112,122`). probe에서 직접 호출할 수 없으므로, 공개된 `frame_ssim`
  (`src/video_fingerprint.h:77`)으로 동등 로직을 재현하고
  `verifyScorePlan` 총합과 비트 단위 일치를 자체 검증한다.
- flip 2건 실측 스펙 (ffprobe + 파일 크기):

```text
hpredict.tiff            2,163 B   32×32 RGBA
hpredict_packbits.tiff   4,094 B   32×32 RGBA
l1.tiff                  1,558 B   100×100 1-bit bilevel
l1_xmp.tiff              4,430 B   100×100 1-bit bilevel
```

- 두 쌍은 동일 픽셀·다른 컨테이너(압축 변형 / XMP 메타데이터 변형)의
  근사중복이다. 32×32 및 1-bit bilevel이라는 점이 중요하다: 출력 스케일(64)
  근처·이하의 tiny·edge-heavy 이미지에서 2단계 Fant 리샘플이 1단계와 가장
  크게 어긋나는 구조적 조건이다. (가설이 아니라 조사 출발점 — 검증은 측정으로)
- 판정 임계값 87.5 = 스캔 `maxDistance 8` 대응
  (`src/scan_pipeline.cpp:18`, `tests/dataset_baseline.cpp:256`).

## 4. 금지 사항

```text
production decode 경로 / decode() / decodePreserveAspect() / WIC 경로 변경
D2 Path C / CPU 10~90 정책 / ResourcePolicy / Scheduler / GPU 정책 변경
기존 테스트 삭제·완화, threshold 완화
verdict flip을 PASS로 취급
groups 동일만으로 정확성 보장 판단
```

flip을 없애기 위해 threshold·verdict 정책을 임의로 바꾸지 않는다.

## 5. flip 2건 개별 분석 (먼저 재현)

R≥384에서 관측된 2건을 동일 쌍·동일 점수로 재현한다. 수집 항목:

```text
파일 이름·크기·원본 width/height·aspect·EXIF/방향 정보
baseline f/a 크기 · candidate f/a 크기
baseline score · candidate score · absolute delta · threshold 거리
가능하면 비교 계획(10 window)별 점수
```

## 6. score 분해

`verifyScorePlan` 총합이 아니라 10개 window 쌍별 점수를 비교한다
(원본 비율 + full 2종 + aspect 3종 × 양방향 — `src/image_verify.cpp:161-165`
구조 그대로). `ssimBuf` 재현 로직의 max가 `verifyScorePlan` 총합과 비트 단위
일치해야 분해가 유효하다. flip이 특정 crop에서 비롯되는지 확인한다.

## 7. 리샘플 경로 대조 (소스 기준)

```text
baseline : WIC decode → 직접 목표 크기 (Fant 1스텝)
candidate: WIC decode → 중간 해상도 (Fant) → 메모리 Fant → 최종 f/a
```

확인 항목: WIC scaler interpolation mode, 기존 resize 함수 보간 방식,
픽셀 중심 좌표, 반올림, 정수 치수 변환식. `centerCropResize`는 nearest-neighbor
이므로 Fant 체인과 원리적으로 다른 출력을 낸다 — 이 차이를 수치로 보인다.

## 8. a geometry 우선 분석

치수식(`lround`, `std::max<…>(1,…)`, 정수 나눗셈, `sw>sh` 분기)을 공유 중간
크기에 적용했을 때 baseline과 어긋나는 경계를 찾는다. 출력값을 맞추기 위한
임의 `+1`/`-1`/`std::max()` 추가 금지.

## 9. Case 분리

```text
Case A: geometry 차이 → pixel 차이
Case B: geometry 동일 → pixel 차이
```

Case B는 리샘플 알고리즘 차이가 원인일 가능성이 높으며, baseline scaling /
candidate 1차 / 2차 scaling 관계를 추적한다.

## 10–11. R stability + 협소 sweep

R∈{128,192,256,384,512}에서 flip 쌍의 baseline/candidate score·delta·verdict·
threshold 거리를 표로 남긴다. R256→R384 경계 규명을 위해 flip 파일에 한해
협소 sweep {256,288,320,352,384}을 수행할 수 있다. 이는 후보 채택용 사후
탐색이 아니라 원인 분석용 실험임을 문서에 명시하고, 추가 이유·목적·결과를
모두 기록한다.

## 12. pixel 통계

f/a 각각에 identical/not-identical을 넘어 다음을 측정한다:

```text
different pixel count / max absolute delta / mean absolute delta
```

차이가 전반적으로 미세한지 특정 영역에 집중되는지 확인한다.

## 13. spatial 분석 (증거만)

flip 2건의 차이 crop에 대해 baseline/candidate crop과 pixel difference를
비교한다. edge·text·미세 질감·압축 아티팩트 중 무엇과 관련되는지 관찰 가능한
증거만 기록하고 단정하지 않는다.

## 14. EXIF / PGM 상태

v0.9.4.31에서 `orient_applied_files=0`, `pgm_fallback_files=0`이었다.
flip 쌍 자체에 EXIF 방향 적용이 없으므로(집계상 0건), 이번 조사도 기존
데이터셋으로는 "미검증" 유지가 기본이다. flip 원인과 무관하므로 PGM용 신규
픽스처는 추가하지 않는다 — 추가한다면 이유·종류·조건·결과를 기록한다.

## 15. Cache

probe는 verify 캐시를 우회한다. hit/miss 분할이 필요하면 공개된
`verifyImagePair`를 동일 파일에 2회 호출해 miss(1회차)와 hit(2회차) 타이밍을
분리 측정한다. hit에서 후보 decode가 실행되지 않으므로 hit 비용을 후보 개선
효과에 포함시키지 않는다. 핵심 비교는 miss다.

## 16. 전체 groups 타당성 조사

생산 경로 변경 없이 전체 비교 구조가 가능한지 조사한다 (기존 테스트 구조
재사용 또는 별도 probe driver). 구현 비용이 크면 flip 원인 분석을 먼저
완료하고 필요성을 판단한다. 결론(가능/불가·사유)을 기록한다.

## 17. Telemetry

D3 telemetry 및 v0.9.4.31 baseline 필드 의미를 변경하지 않는다. 새 조사값은
additive 필드로만 추가한다.

## 18. 성능은 부차적

1순위는 원인 규명이다. "더 빠른 후보 찾기"가 아니라 "왜 달라지는가"를 밝힌다.

## 19. 새 optimization 즉시 등록 금지

새 구조가 발견돼도 순서대로: 원인 분석 완료 → 후보 정의 → 별도
optimization pre-register → 별도 측정.

## 20. 판정 규칙

```text
원인 규명 성공 + 구조적으로 정확성 유지 가능 → 후보 개선 방향 정의
원인 규명 성공 + 구조적으로 유지 어려움 → 기존 후보 폐기 검토
원인 규명 불충분 → 추가 조사 필요
새 유망 구조 발견 → 기존 후보와 분리해 새 pre-register
```

R384 이상 2건 flip이라는 사실 자체는 변경하지 않는다.
