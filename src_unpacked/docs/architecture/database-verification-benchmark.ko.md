# Database / Pair-Verification Benchmark (실측 기록)

> 이 문서는 **쌍매칭 검증 시간 개선(V-1~V-7)** 의 실측 기록이다. 계획·조건·원시값·판정을 여기에 남기고,
> DB 구조와 개선 예정 내역은 `docs/architecture/database-structure.ko.md`에 둔다.
> 실험 간 관계와 판단 흐름은 `docs/worklog/0.9.4.ko.md`의 **Performance / Tuning Experiment Index**에 연결한다.
> 측정하지 않은 값은 `Not measured`로 명시하고 추정으로 채우지 않는다.

## 0. 상태

- 현재 상태: **DBV-1 / DBV-2 실측 완료(0.9.4.92) / DBV-3~7 대기**
- 대상 개선: V-1~V-7 (`database-structure` 7절)
- Baseline: 0.9.4.91 사용자 실행 로그(아래 2절) + 0.9.4.92 표준 dataset before/after(7절)

## 1. 실험 정의

| 항목 | 내용 |
|---|---|
| Experiment ID | DBV-1 (V-1: 캐시키 quickHash 읽기 제거) |
| Version | (측정 시 기록) |
| Date | (측정 시 기록) |
| Purpose | 검증 캐시키의 64 KiB 파일 읽기(verifyKeyMs) 제거 효과 측정 |
| Hypothesis | DB 저장 quickHash 또는 (size,modified)만으로 키를 만들면 verifyKeyMs가 사라지고 판정이 불변이다 |
| Baseline | 0.9.4.91: verifyKeyMs = 170 s(합산), verify의 ≈13% |
| Target | verifyKeyMs → 0 (파일 읽기 0회), 판정 parity 100% |
| Implementation | (구현 시 기록) |
| Dataset | 사용자 dataset `G:\Downloads\ss_twit` (또는 표준 dataset) |
| Dataset fingerprint | (측정 시 기록) |
| Hardware | RTX 3080 Ti / (CPU, RAM 기록) |
| OS | (측정 시 기록) |
| Build configuration | CPU Release + GPU(CUDA) Release |
| Run count | ≥3 (median/min/max 기록) |
| Raw measured values | Not measured |
| Mean / Median / Min / Max / Range | Not measured |
| Derived ratios | Not measured |
| Accuracy result | Not measured |
| Verdict parity (CPU/GPU) | Not measured |
| Compatibility impact | Not measured |
| Performance result | Not measured |
| Status | **PLANNED** |
| Reason | — |
| Known limitation | — |
| Future revisit condition | — |
| Related experiments | DBV-2..7, D9c/D9d(decode 분해) |

## 2. Baseline (0.9.4.91 로그, 177,372 파일 / 이미지 154,041 분석)

워커 합산값(주의: `src/scan_pipeline.cpp:477` — 병렬 verify 시간은 워커 합산).

| 항목 | 값 |
|---|---|
| verifyMs | 540,758 ms |
| verifyCalls | 19,221 |
| verifyDecodeMisses / verifyCacheHits | 13,453 / 1,353 (hit rate 9.1%) |
| verifyKeyMs | 169,877 ms |
| verifyDecodeMs | 1,095,446 ms (full 756,290 + aspect 339,114) |
| verifyFrameSsimMs | 476.6 ms |
| verifyFlipMs / verifyCropMs | 187.1 / 151.7 ms |
| verifySumMs / verifyBreakdownOverMs | 1,266,314 / 725,556 ms |
| msPerVerifyCall | 28.134 ms |
| meanDecodeMs(인덱스) | 83.18 ms |

비율(합산 기준): decode 86.5% · key 13.4% · SSIM 0.04% · flip/crop 0.03%.

## 3. 실험 계획 (V별)

각 V는 1절 표를 복제해 채운다. 공통 규칙:
- 판정 parity: 기존(변경 전) 결과와 쌍/그룹 수, 개별 percent가 동일해야 한다(`image_verify_test`, `verify_parity_test`).
- GPU SSIM(V-7)을 포함할 경우 CPU/GPU 결과 수치 일치를 확인한다.
- 회귀: 전체 CTest CPU/GPU, 사용자 dataset 재스캔.

### DBV-1 (V-1) 캐시키 quickHash 읽기 제거
- 상태: **PASS (0.9.4.92)** — 키를 `(path,size,modified)`로 축소(파일 읽기 0회).
- 결과: `verifyQuickHashReads`/`Bytes` 27,486 / 364.3 MB → 0 / 0, `key` 시간 5,453.8 → 3,386.6 ms(-37.9%).
- 판정 parity: groups 156,211 동일.

### DBV-2 (V-2) 검증 버퍼 캐시 확대
- 상태: **PASS (0.9.4.92)** — `kVerifyCacheMax` 32 → 1024.
- 결과: 히트율 50.4% → 96.3%, `verifyDecodes` 27,240 → 1,028(-96.2%), `decode` 13,596.4 → 931.6 ms(-93.1%).
- 측정 지표: hit rate, decode miss, verifyDecodeMs.

### DBV-3 (V-3) 디코드 비용 축소
- 상태: PLANNED (CONDITIONAL)
- 전제: 판정 parity 통과 후 채택.

### DBV-4 (V-4) 인덱스 디코드 재사용
- 상태: PLANNED (검토)

### DBV-5 (V-5) 후보 쌍 감소
- 상태: PLANNED (CONDITIONAL)

### DBV-6 (V-6) 디코드 I/O 겹침
- 상태: PLANNED (검토)

### DBV-7 (V-7) 이미지 GPU SSIM
- 상태: PLANNED (LOW PRIORITY)

## 4. 결과 요약 (측정 시 갱신)

| ID | Status | 핵심 결과 | 판정 근거 |
|---|---|---|---|
| DBV-1 | **PASS** (0.9.4.92) | reads/bytes → 0; key -37.9% | 표준 dataset before/after(7절) |
| DBV-2 | **PASS** (0.9.4.92) | hit 50.4%→96.3%; decode -93.1% | 표준 dataset before/after(7절) |
| DBV-3 | PLANNED | Not measured | — |
| DBV-4 | PLANNED | Not measured | — |
| DBV-5 | PLANNED | Not measured | — |
| DBV-6 | PLANNED | Not measured | — |
| DBV-7 | PLANNED | Not measured | — |

## 5. 기각/보류 후보 보존

- (측정 중 기각/보류된 후보를 사유·재검토 조건과 함께 기록. 삭제 금지.)

## 6. 관련 문서

- `docs/architecture/database-structure.ko.md` — DB 구조 + 개선 예정 내역.
- `docs/worklog/0.9.4.ko.md` — Performance / Tuning Experiment Index.
- `docs/build-history/0.9.4.92.*` — DBV-1/DBV-2 구현·검증.
- `docs/build-history/0.9.4.29.*`(D3 decode 중복), `0.9.4.26.*`(D9c/D9d verify 분해).

## 7. DBV-1/DBV-2 실측 결과 (0.9.4.92, 표준 dataset)

빌드: 0.9.4.92 (engine 1.5.1). dataset: `test_sample_img_vid`(3,347 파일). 임시 appdir. run=1.
**주의**: 표준 dataset은 C:(저지연)에 있어 V-1의 절대 효과는 사용자 G:(고지연)보다 작다. 여기서는 메커니즘과 상대 효과를 측정한다.

| 지표 | before(0.9.4.91 로직) | after(0.9.4.92) | 변화 |
|---|---|---|---|
| wall_ms | 14,847.3 | 6,974.4 | -53.0% |
| verifyMs | 10,492.8 | 3,043.3 | -71.0% |
| key (stat+64KB) | 5,453.8 | 3,386.6 | -37.9% |
| decode | 13,596.4 | 931.6 | -93.1% |
| verifyQuickHashReads | 27,486 | 0 | -100% |
| verifyQuickHashBytes | 364.3 MB | 0 | -100% |
| verifyDecodes | 27,240 | 1,028 | -96.2% |
| verifyCacheCopies | 13,866 | 26,972 | hit 50.4%→96.3% |
| groups | 156,211 | 156,211 | 동일(parity) |
| scanned / analyzed | 3,334 / 3,333 | 3,334 / 3,333 | 동일 |

- 귀속: **V-1** = key의 64 KiB 읽기 제거(reads/bytes → 0, key -2,067 ms). **V-2** = 캐시 확대(hit↑, decode -12,665 ms).
- 검증: CPU CTest 122/122, GPU CTest 123/123(`image_verify_test`, `verify_parity_test` 포함). groups 동일 → 판정 불변.
- **미측정**: 사용자 dataset(G:)에서의 절대값(고지연 드라이브에서 V-1 key 효과가 더 큼), DBV-3~7.
- Status: **PASS** (표준 dataset). 사용자 dataset 재측정 대기.
