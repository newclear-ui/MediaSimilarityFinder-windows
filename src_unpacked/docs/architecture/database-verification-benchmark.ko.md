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

### 7.1 사용자 dataset 실측 (0.9.4.92 GUI run, 2026-10-12)

사용자 실행 로그 `detailed-log-2026-10-12T162124.json`. dataset `G:\Downloads\ss_twit` 177,392 파일, **인덱스 전체 삭제 후 콜드 스캔**.
비교 기준은 0.9.4.91 로그(177,372 파일). **주의**: 이번 run은 `cudaAvailable=False`(CPU 해싱)라 wall 절대값 비교는 교란된다. 아래 verify 지표는 lookups 수가 동일(14,806)해 직접 비교 가능하다.

| 지표 | 0.9.4.91 | 0.9.4.92 | 변화 |
|---|---|---|---|
| verifyQuickHashReads | 14,806 | **0** | -100% |
| verifyQuickHashBytes | 934.5 MB | **0** | -100% |
| verifyKeyMs | 169,877 ms | **4,582.6 ms** | **-97.3%** |
| verifyCacheHits | 1,353 | 1,960 | hit 9.1% → 13.2% |
| verifyDecodeMisses | 13,453 | 12,846 | -4.5% |
| verifyDecodeMs | 1,095,446 ms | 1,066,381 ms | -2.7% |
| verifyMs | 540,758 ms | 471,820 ms | -12.7% |
| verifyCalls | 19,221 | 19,222 | 동일(≈) |
| groups | 9,054 | 9,055 | +1 (dataset +20 파일) |

- **V-1 확인(핵심)**: 고지연 G: 드라이브에서 캐시키의 64 KiB 읽기 제거로 `verifyKeyMs`가 **170 s → 4.6 s**(-97.3%). 표준 dataset(C:)의 -37.9%보다 훨씬 큰 효과 — 드라이브 지연이 클수록 이득이 크다는 예상을 실측이 확인했다.
- **V-2 부분 확인**: 캐시 히트율 9.1% → 13.2%. 사용자 dataset은 파일당 재등장이 적어(캐시 1024로도 13%) 표준 dataset(96.3%)보다 이득이 작다. 캐시 확대의 상한은 데이터 재사용률에 달려 있다.
- **교란 요인**: `cudaAvailable=False` → CPU 해싱(177,392). CUDA 미가용 원인은 별도 확인 필요(CPU 빌드 실행 여부/드라이버). wall(101m vs 112m)은 이 요인과 인덱스 삭제로 직접 비교 불가.
- dataset fingerprint: 전체 재해시 1,393 s(인덱스 삭제로 content_hash 비어 있음 — 예상된 콜드 비용).
- Status: **PASS** (사용자 dataset, verify key 지표). wall 절대값은 CUDA 가용 상태 재측정 필요.

### 7.2 사용자 dataset 안정형 재스캔 예제 (0.9.4.94 GUI run, 2026-10-13)

사용자 실행 로그 `detailed-log-2026-10-13T200015.json`(원본은 사용자 보관, `D:\Temp\OpenCodeWork\`에서 리뷰). dataset `G:\Downloads\ss_twit` 177,581 파일 중 **80개만 신규/변경, 177,501개 무변경**인 증분 재스캔. `analyze.mode = B`(안정형, 기본값) — 이후 §7.3부터 고속형(A)/자동(AB) 측정 기록을 같은 형식으로 추가한다.

| 지표 | 값 | 비고 |
|---|---|---|
| wall / walk / analyze | 980.6 s / 347.0 s / 591.9 s | walk 35% + verify 60%로 결산 (≈976 s) |
| scanned / analyzed / unchanged | 177,581 / 80 / 177,501 | 증분 판정 정상 |
| verifyCalls (= candidates) | 19,234 | 안정형 전역 재검증: 변경 80개와 무관하게 전 쌍 재검증 |
| verifyDecodeMisses / hits | 12,887 / 1,935 | hit 13.1% (§7.1의 13.2%와 일치, 재사용률 상한) |
| verifyQuickHashReads/Bytes | 0 / 0 | DBV-1 유지 |
| verifyKeyMs | 6.6 s | DBV-1 유지 (§7.1의 4.6 s와 동급) |
| verifyMs / msPerVerifyCall | 590.3 s / 30.7 ms | G: 디코드 세금(표준 8.5 ms의 3.6배, `decodeCopy` 87%) |
| dataset fingerprint | 34.4 s (69.1 GB 논리) | 증분 `content_hash` 적중 — 콜드 1,393 s → 34 s |
| revalidateMs | 0 | 엔진 버전 불변이라 생략, 정상 |
| pairs / groups | 15,193 / 9,060 | +5 그룹은 추가 파일분으로 합리적 |
| GPU | hash 80, duty 0.0% | 배치 3.7 ms/980 s — 모순 아님, 매칭은 CPU 작업 |
| decodeOrientApplied | 2 | 느린 파일(`hkdrh69*` 0.4~1.2 s)은 크기/G: latency, 회전 폭주 재발 아님 |
| procIoRead | 46.4 GB | walk quickHash + verify 디코드 + 디코더 재읽기 수준에서 정합 |

- **안정형 비용 확정**: 무변경 177,501개 재스캔도 안정형은 전 쌍을 재검증하므로 592 s가 그대로 든다. 변경 파일만 빠르게 재검사하려면 고속형(A)으로 전환(동일 판정).
- **읽기 주의**: `verifyBreakdownOverMs = 651 s` — 세부 합은 스레드 합산(0.9.4.80 병렬 analyze 이후)이라 wall을 초과한다. D9c 시절 "합계 = verifyMs" 불변식은 다-worker run에서 성립하지 않으므로 비율로 읽는다.
- Status: **PASS** (측정 기록 예제 — 결함 아님, Fast/Auto 측정 대기).

### 7.3 사용자 dataset 고속형(A)·안정형(B)·자동(AB) 풀 리프래쉬 대조 (0.9.4.106 GUI runs, 2026-10-15/16)

사용자 실행 로그: A `detailed-log-2026-10-15T234751.json`, B `detailed-log-2026-10-16T002443.json`, AB `detailed-log-2026-10-16T013202.json`(원본은 사용자 보관, `D:\Temp\OpenCodeWork\`에서 리뷰). dataset `G:\Downloads\ss_twit`, **인덱스 유지 상태의 전체 검색 업데이트**. 같은 빌드(0.9.4.106)에서 A/B/AB를 직접 대조하고 §7.2(B, 0.9.4.94)를 역사 기준으로 병기한다.

| 지표 | §7.2 B(0.9.4.94) | A(0.9.4.106) | **B(0.9.4.106)** | **AB(0.9.4.106)** | 비고 |
|---|---|---|---|---|---|
| wall | 980.6 s | 632.3 s | 1012.3 s | **521.2 s** | walk 변동이 최대 교란 |
| walk | 347.0 s | 399.7 s | 359.1 s | 306.5 s | 드라이브/세션 변동 |
| analyze | 591.9 s | 185.8 s | 605.7 s | **170.2 s** | |
| ├ index | (미기록) | 1.4 s | 1.7 s | 1.2 s | |
| ├ verify | 590.3 s (캡 주의) | **0.0 s / 0회** | 604.0 s (캡·스레드 합, wall ≈ 420 s) | **0.25 s / 1회** | AB: 정확히 한쪽만 변경된 쌍만 검증 |
| └ scan(나머지) | (캡으로 0) | 184.4 s | 0.0 s (캡 아티팩트) | 168.8 s | A/AB의 열거 고정비 실측 |
| candidates | 19,234 | 19,175 | 19,160 | 19,152 | 동일 dataset 계열 |
| verifyCalls | 19,234 | 0 | 19,160 | **1** | AB: 양쪽 변경 쌍은 live 패스가 담당(최종 패스 스킵) |
| verifyDecodeMisses / hit | 12,887 / 13.1% | 0 | 12,859 / 13.1% | 2 / 0% | AB의 1쌍 = 2파일 디코드(254.8 ms) |
| msPerVerifyCall | 30.7 ms | — | 31.5 ms | 254.8 ms | G: 디코드 세금(느린 hkdrh69 파일) |
| pairs / groups (리포트) | 15,193 / 9,060 | 0 / 0 | 15,118 / 9,050 | 0 / 0 | A/AB 리포트 = 최종 패스 신규 판정만 |
| dataset fingerprint | 34.4 s | 42.2 s | 43.3 s | 38.2 s | 증분 `content_hash` 적중 유지 |
| procIoRead | 46.4 GB | 25.0 GB | 43.1 GB | 25.3 GB | verify 디코드분 차이 |
| 메모리 최대 | (미기록) | 1025 MB | 1548 MB | 1030 MB | B의 verify 버퍼 캐시 반영 |
| CPU avg / max | (미기록) | 8.8% / 91.2% | 10.6% / 71.1% | 9.8% / 95.1% | |
| 변경 파일 | 80 | 2 | 0 | **64** | AB는 변경 파일이 있어야 유의미 |
| 이미지 스테이지 | 80 GPU | 0.6 s (2 GPU) | 0 | 2.9 s (64 GPU, cropMs 15.6 s) | 변경 파일 64개 분석(느린 5건 0.84~0.96 s) |
| GPU duty | 0.0% | 0.0% | 0.0% | 0.0% (idle 413.8 s) | 매칭은 CPU 작업 |

- **세 모드 비용 모델(같은 빌드 실측)**: A = "변경 관련 쌍(양쪽 변경 포함) 검증", B = "변경 수 무관 전 쌍 재검증", AB = "**정확히 한쪽만 변경된 쌍만** 최종 검증(양쪽 변경 쌍은 live 패스가 이미 방출)". 이번 AB run(변경 64)에서 최종 패스 verify는 **1회(0.25 s)**뿐이었다 — AB의 검증 비용이 dataset 크기가 아니라 "한쪽만 변경된 후보 쌍 수"에 비례함을 보여준다.
- **AB 리포트 의미**: `pairs/groups 0`은 최종 패스 판정만 집계한 값이다. 양쪽 변경 쌍의 매치는 live 패스가 GUI/저장 집합에 전달하며 최종 리포트 수치에는 잡히지 않는다(설계 의미 — 결함 아님). 이번 run은 64개 변경 파일 중 매치가 성립한 쌍이 없어(검증 1쌍도 임계 미달) 전체 0이다.
- **주의(캡)**: B의 `verifyMs 604.0 s`는 워커 합산값이 `verifyCap`(≈totalMs−index)에 닿은 값이라 scan 버킷이 0.0 s로 눌렸다. A/AB의 verify 0~0.25 s run이 측정한 열거 고정비(184.4/168.8 s)가 B에도 존재하므로 **B의 verify 실 wall ≈ 420 s**로 읽는다(세부 분해는 단정하지 않음).
- **B 전용 오버헤드(구조)**: Sequential(B)은 1000파일 슬라이스마다 `analyzeCheckpoint`(= `persistMatchesSnapshot`)를 호출한다. 무변경 run에서 슬라이스 수만큼(≈177회) 15k쌍 스냅샷이 반복됐다 — A/AB(sliceGroups=0)에는 없다.
- **주의(dataset/교란)**: 파일 수 A 177,572 → B 177,567 → AB 177,627(+60, 변경 64). walk가 306.5~399.7 s로 진동해 wall 비교는 walk 변동을 제거해 읽어야 한다(analyze 기준 A/AB ≈ 170~186 s, B ≈ 606 s).
- **새 발견(고정비)**: A/AB의 analyze 중 verify 0~0.25 s, **scan(열거) 168.8~184.4 s** — 변경 수와 무관하게 전체 index seed + 크롭(4:3/1:1/9:16) 열거가 전량 수행된다(`consumeWindowed`는 순차). A/AB에서 열거를 "변경 파일 대상 질의"로 한정하면 이 고정비를 변경 수 비례로 줄일 수 있다(후보, 미구현 — worklog 색인 `P-0.9.4.106-A-targeted-enum`).
- Status: **PASS** (측정 기록). A/B/AB 세 모드 모두 같은 빌드에서 1회씩 기록됨 — 반복 수 증가와 walk 변동 통제는 후속.
