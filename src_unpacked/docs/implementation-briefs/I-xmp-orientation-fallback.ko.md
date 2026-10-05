# Implementation Brief — I-XMP Orientation Fallback (Pre-register)

Status: **IMPLEMENTED / PRODUCTION ACCEPTANCE CONDITIONAL** — original pre-registration contract is retained; implementation is complete, while real-dataset/full-scan acceptance remains deferred.

```text
Implementation baseline  v0.9.4.44 / 1564b87
Current product baseline  v0.9.4.45 / 619f74a
Experiment   I-XMP orientation fallback
Product change   YES — ImageDecoder orientation source에 XMP fallback 추가
```

`I-exif-orientation-path-fix.*`는 XMP fallback을 명시적으로 금지했으며, 이 계약을
소급 변경하지 않는다. 본 문서는 별도 요구사항으로 승격된 XMP fallback의 독립 계약이다.

## 1. 목적

EXIF Orientation이 없거나 EXIF 경로로 해결되지 않는 파일에 대해 XMP Orientation을
fallback으로 사용하는 production 경로를 정의한다. EXIF 기존 경로는 그대로 둔다.

## 2. Precedence

```text
EXIF 조회 → VT_UI2 값 발견
  ├─ 값 1..8 (valid) → EXIF 사용, XMP 조회 금지
  └─ 값 범위 밖 (invalid) → 미해결로 취급 → XMP 시도
EXIF 미발견 / 조회 실패 / VT_UI2 아님 → 미해결로 취급 → XMP 시도
XMP 유효 → XMP 사용
XMP 무효/없음 → 변환 없음 (Rotate0)
```

EXIF 값이 정상인데 XMP가 다른 값을 가지면 EXIF가 이긴다. EXIF query failure와
EXIF value invalid는 "사용 가능한 값이 없음"이라는 점에서 동일하게 미해결로 취급한다.

## 3. Supported representation (단 하나)

XMP `tiff:Orientation` 정수 하나만 지원한다 (`http://ns.adobe.com/tiff/1.0/`).
WIC metadata query 경로로 읽으며, VT_UI2/VT_UI4/VT_I4 정수 또는 파싱 가능한
정수 문자열을 정규화한다. 그 외의 XMP 표현(다른 property, 다른 namespace,
구조체 탐색)은 `unsupported`로 취급하고 "XMP 전체 지원"이라 표현하지 않는다.

실측 (Windows WIC): `/xmp/tiff:Orientation` 경로가 resolves되며 값은
`VT_LPWSTR` 문자열이다 (예: `"6"`). `/xmp/exif:Orientation` 등은
`WINCODEC_ERR_PROPERTYNOTFOUND`이므로 지원하지 않는다.

## 4. Value normalization

EXIF path와 동일한 의미로 정규화한다.

```text
1 = identity (transform 미적용, orientApplied 증가 없음)
3 = 180°
6 = 90° CW
8 = 270° CW
2/4/5/7 = EXIF 매핑과 동일 (flip 계열)
```

범위 밖 값·파싱 불가 값은 추측 보정하지 않고 미해결로 취급한다.

## 5. Telemetry

새 field 없음. 기존 bucket 의미 유지:

- XMP 조회는 EXIF 미해결 때만 수행하므로 corpus 전체 비용 구조 불변.
  추가 조회 비용은 `metadataMs`에 포함된다.
- `orientApplied`는 transform이 실제 적용될 때만 증가한다 (source 무관).
- `orientMs`는 FlipRotator 생성+초기화 비용 그대로다.

## 6. Fixture contract (표준 dataset 변경 금지, 독립 synthetic)

| Case | EXIF | XMP | 기대 |
|------|------|-----|------|
| A | absent | 1 | identity, orientApplied 미증가 |
| B | absent | 6 | 90° CW 적용 |
| C | absent | 3 | 180° 적용 |
| D | absent | 8 | 270° CW 적용 |
| E | 6 | 8 | EXIF 6 적용 (EXIF wins) |
| F | absent | invalid | 변환 없음 |
| G | absent | absent | 기존 기본 semantics |

## 7. Scan regression contract

표준 dataset에서 pre-XMP baseline과 post-XMP candidate를 분리 기록한다.
비교 항목: scanned/analyzed, orientation 적용 수, groups, match/pair 수,
verdict 차이. XMP coverage가 거의 없으면 implementation correctness와
real dataset coverage를 분리 기록하고 "효과 없음"으로 단정하지 않는다.

## 8. 금지

- EXIF correction contract 소급 수정
- EXIF보다 XMP 우선
- invalid XMP 추측 보정
- orientation/threshold/SSIM/CandidateIndex/grouping semantics 변경
- SearchReport/DB schema 변경
