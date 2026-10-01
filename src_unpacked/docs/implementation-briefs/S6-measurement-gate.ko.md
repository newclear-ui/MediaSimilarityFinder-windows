# S6 Measurement Gate — Controlled A/B Benchmark Measurement

작성: 2026-10-01
상태: **설계 확정 문서. 제품 코드 변경 없음. S6 는 CLOSED 가 아니다.**

이 문서는 S6 의 회귀 판정 계층에 들어가기 전에 **무엇을 측정해야 하고, 어떤 동일 조건과
반복 절차로 측정해야 하는지**를 확정한다. 이미 존재하는 측정 계약은 재사용하고, 새로
필요한 것은 절차와 entry/exit 조건뿐이다.

---

## 1. 왜 이 Gate 가 필요한가

S6 파이프라인(`ingestion → normalization → grouping → aggregation → comparison
candidates → comparison metrics`)은 완성되었으나 **판정 계층 진입 조건이 충족되지 않았다.**

| 조건 | 현재 상태 | 확인 방법 |
| --- | --- | --- |
| Known commit build 2개 이상 | **미충족** — Known 1, Legacy 30 | S6-5 실측 |
| Success case sample | **미충족** — `images` scope case 전부 Skipped | S6-4 실측 |
| controlled measurement environment | **미충족** — 정의 없음 | S2-PERF |
| repeated runs | **미충족** — 동일 조건 반복 없음 | S6 store 실측 |
| measurement variation / error range | **부분 충족** — 과거 측정값 있음 (§3) | 0.9.4.32 |

**이 조건은 S6 코드가 만들어낼 수 없다.** 코드는 journal 을 읽을 뿐이다. 실제 측정이
필요하고, 그 측정 없이는 어떤 threshold 도 정당화되지 않는다.

> **판정 계층이 CLOSED 가 아니다.** 이 문서는 판정 계층을 *시작*하기 위한 선행 조건을
> 정의할 뿐, 판정 계층을 구현하지 않는다.

---

## 2. 이 문서가 실제로 조사한 사실

아래 수치는 모두 이 문서를 작성하며 **직접 측정**한 값이다. 추정하지 않았다.

### 2-1. 측정 환경

| 항목 | 실측값 |
| --- | --- |
| OS | Microsoft Windows 11 Pro, build 26300 |
| CPU | AMD Ryzen 7 5800X3D (8 core / 16 logical) |
| GPU | NVIDIA GeForce RTX 3080 Ti **존재** |
| 전원 모드 | 고성능 (GUID `381b4222-f694-41f0-9685-ff5bb260df2e`) |

### 2-2. 후보 dataset

`C:\project\test_sample_img_vid` — `prepare_dataset.ps1 -FingerprintOnly` (읽기 전용)로 확인:

| 항목 | 실측값 |
| --- | --- |
| 파일 수 | **3347** |
| 총 바이트 | **102,475,315** |
| 디렉터리 수 | 102 |
| 구성 | `bulk` 240, `images` 707, `tree` 2400 |
| 확장자 | `.bmp` 2700, `.jpg` 200, `.png` 200, `.webp` 200, `.gif` 20, `.ico` 12, `.tif` 4, `.tiff` 10, **`.md` 1** |

### 2-3. 현재 benchmark store 의 실제 내용 (중요)

현재 `build-windows-cpu\Release\Benchmark\Console` 의 31 run 은 **실제 dataset 이 아니다.**
case 경로가 `C:\Users\newcl\AppData\Local\Temp\msf_s5_e2e` 이고, 그 fixture 는
**10개 파일**이다. S5 end-to-end 테스트가 남긴 것이다.

따라서 현재 store 의 `caseMedian ≈ 619~645 ms`, `run wall 0 ms` 는
**10 파일 fixture 의 성질**이며 실측 성능의 근거가 될 수 없다.

### 2-4. 현재 CLI 가 제공하는 것

```
MediaSimilarityFinder.exe --benchmark <folder> [--mode <list>]
                        [--suite <id>] [--log-dir <dir>] [--log <file>]
                        [--media images|videos|all]
```

- `--mode` 기본값 `auto,cpu,gpu-max`, 실행 순서 `AUTO → CPU → GPU-max`
- `runId` = `run-YYYYMMDD-HHMM-SS` (**1초 해상도**). 코드 주석에 명시적으로 "counter 는
  매 프로세스 초기화되어 같은 suite 의 이전 run 과 충돌한다" 는 이유로 채택된 값이다.
- `suiteId` 기본값도 동일 stamp, `--suite` 로 명시 지정 가능

**CLI 에 `--resource` / `--cpu-percent` 가 없다.** S1/S5 단계에서 거부되었다. 따라서
resource policy 는 CLI 로 지정할 수 없고 journal 에도 기록되지 않는다 (§7).

---

## 3. 이미 존재하는 측정 계약 (재사용, 재작성 금지)

이 development 선에는 이미 측정 규칙이 있으며 **새 기준을 만들지 않는다.**

### 3-1. `AGENTS.md` 9항

- "**측정 오차 범위의 차이는 개선으로 선언하지 않는다.** 실행 횟수(5회 이상 권장)와
  median / min / max / range 를 기록한다."
- 권장 상태값: `BASELINE` / `PASS` / `NOT ACCEPTED` / `REJECTED` / `DEFERRED` /
  `LOW PRIORITY` / `INCONCLUSIVE` / `SUPERSEDED`
- **측정되지 않은 값은 `N/A` 또는 `Not measured` 로 명시한다. 추정해서 채우지 않는다.**

### 3-2. 이미 측정된 변동성 (재측정 불필요)

`0.9.4.32` build history 와 worklog 에 이미 run 간 편차가 실측되어 있다.

| 대상 | 실측 변동폭 | 출처 |
| --- | --- | --- |
| 전체 실행 (R384/R512) | **3.7 ~ 7.5 %** | 0.9.4.32 §측정 |
| 짧은 probe 실행 | **19.4 ~ 45.9 %** | 0.9.4.32 §측정 |

**이 숫자가 이 Gate 의 출발점이다.** 이미 알려진 사실:

> 5회 반복 시 run 간 편차가 3.7~7.5 % 수준이므로, **7.5 % 이하의 차이는 측정 오차 안**이며
> 개선이나 회귀로 선언할 수 없다. 짧은 probe 는 편차가 19~46 % 나므로 반복 횟수를 늘려도
> 그 자체로 판정 근거가 되지 않는다 — **측정 단위(run) 자체가 짧으면 안 된다.**

### 3-3. 이미 발견된 measurement 결함 (반복 적용)

worklog 에서 이미 두 차례 같은 계열의 결함이 발견됐다. Gate 는 이를 전제로 한다.

- **재현성은 정확성을 보장하지 않는다.** 5회 재현은 충분했으나 **재현된 artifact** 였다.
- **판정의 대조 쌍은 반드시 같은 세계여야 한다.** baseline 점수에 이전 파일의 후보 버퍼가
  섞여 있었고, 재현성은 충분하면서도 결과가 artifact였다.
- **측정값이 예상과 잘 맞는 순간 그 서사를 의심한다.** 지우간한 단조 경향(2.63→9.19)은
  defect 신호였다.

> 따라서 Gate 의 성공 조건에 "재현성 확보"만 넣으면 **위 결함을 그대로 통과시킨다.**
> §11 에 대조 쌍 동일성 검증 항목을 별도로 둔다.

---

## 4. Build A / Build B 의 정의

두 build 는 **commit hash 만 다른 임의의 build 가 아니다.** 각 build 의 차이를 명시해야 한다.

| build | 정의 |
| --- | --- |
| **Build A** | 기준이 되는 측정 대상. 어느 코드 상태인지 commit 으로 고정 |
| **Build B** | 비교 대상. **A 와 다른 무엇을 바꿨는지**를 한 문장으로 명시 |

Build B 의 변경 내용이 실행에 영향을 주지 않는 경우(예: 문서만 변경), 측정 결과가 같을
것이 **기대**이지 결함이 아니다. 이 경우에도 비교는 유효하며 "측정 대상이 아닌 것"을
측정했다는 뜻이 된다. 그래서 Build B 의 변경 범위를 **반드시 기록**한다.

> 이름은 중립적으로 `Build A` / `Build B` 로 유지한다. `baseline` / `candidate` 라는
> 이름은 우열을 함의하므로 이 Gate 에서 쓰지 않는다. 역할을 부여하는 것은 이후
> 사용자가 명시적으로 선택하는 단계의 몫이다.

### 4-1. Known provenance 조건 (필수, 우회 불가)

각 build 는 다음을 만족해야 한다.

1. configure 시점에 git 이 사용 가능해야 하며, 그 결과가 generated header 에 들어가야 한다.
2. `MSF_BUILD_GIT` 가 실행 중 git 호출로 보완되지 않는다. 값은 **build 시점에 고정**된다.
3. journal 의 `run_started.gitCommit` 과 binary 의 generated 값이 **일치**해야 한다.

현재 측정값 (두 build 모두 동일):

```
MSF_BUILD_VERSION "0.9.4.43"
MSF_BUILD_GIT     "83cced3"
```

**두 build 가 같은 commit 이면 Build A/B 비교는 불가능하다.** Build B 는 반드시 다른
commit 에서 빌드되어야 하며, 그 commit 은 `origin/main` 에 push 되어 복원 가능해야 한다
(AGENTS.md 백업 규칙: 소스 zip 은 `HEAD == origin/main` 이어야 한다).

검증 명령:

```
MediaSimilarityFinder.exe --version          # MSF_BUILD_VERSION 확인
# generated/msf_build_version.h 의 MSF_BUILD_GIT 확인 (journal 과 대조)
```

---

## 5. Dataset 조건

동일 dataset 을 A/B 사이에 사용한다. **dataset 자체를 측정 중 변경하지 않는다.**

보존해야 하는 값:

| 항목 | 현재 실측 |
| --- | --- |
| root | `C:\project\test_sample_img_vid` |
| file count | 3347 |
| total bytes | 102,475,315 |
| `datasetFingerprint` | **Gate 실행 시 제품 자체 보고로 기록한다** (§5-1) |

### 5-1. fingerprint 기록 방법

`prepare_dataset.ps1 -FingerprintOnly` 는 파일 수와 총 바이트를 보고하지만 fingerprint
자체는 `msf_dataset_report` 를 통해 읽으라고 안내한다.

**주의: `-FingerprintOnly` 없이 실행하지 않는다.** dataset 전체를 재생성하면서 픽스처를
삭제하는 사고가 한 차례 있었다(AGENTS.md 10항). Gate 는 dataset 을 **읽기만** 한다.

fingerprint 가 다르면 S6 는 동일-condition 비교에서 그 cohort 를 제외한다. 따라서
fingerprint 는 측정 기록의 필수 항목이며, 사후에 재계산해 비교하는 것이 아니다.

### 5-2. 발견된 문제: `.md` 파일 1개

`format` 하위에 **`.md` 파일 1개**가 있다. `prepare_dataset.ps1` 의 주석은 "root 에는
media 만 있고 다른 것은 없어야 한다" 고 명시한다.

Gate 실행 전 이 파일이 dataset 에 포함되어야 하는지 확인한다. media 가 아니라면
**dataset 변경**이므로 fingerprint 가 바뀌고, 그 경우 A/B 양쪽을 **새 fingerprint 로
재측정**해야 한다. 측정 도중에 dataset 을 고치는 것은 금지한다.

---

## 6. Scope 선택 (조사 결과로 확정)

현재 dataset 은 **비디오 파일이 0개**다. 확장자 실측: `.bmp` `.jpg` `.png` `.webp` `.gif`
`.ico` `.tif` `.tiff` `.md` — `.mp4` / `.mkv` / `.avi` / `.mov` / `.webm` / `.m4v` **0건**,
그리고 `videos` 디렉터리 자체가 없다.

따라서:

| scope | Gate 에서 |
| --- | --- |
| `images` | **사용 가능** (707 + 2400 = 3107 image, `bulk`/`tree` 포함 여부는 확인 필요) |
| `videos` | **불가능** — 파일 0건 |
| `all` | **사용 가능하나 단독 `images` 와 동일 집합** |

> **`all` 은 독립 scope 다.** §7-1 처럼 분해하지 않는다. 다만 이 dataset 은 video 가
> 없으므로 `all` 과 `images` 가 **같은 파일 집합**을 가리킨다. 이 사실을 문서에 기록하며,
> 두 scope 의 결과를 서로 다른 scope 비교로 취급하지 않는다.

**Gate 의 primary scope 는 `images` 로 확정한다.** 현재 store 의 `images` scope case 가
전부 Skipped 인 것은 **10 파일 e2e fixture** 의 성질이지 `images` 자체의 문제가 아니다.
실제 dataset 3347 파일로 다시 측정하면 Success sample 이 확보되어야 하며, 확보되지
않으면 그 원인을 dataset 이 아니라 **측정 절차** 문제로 기록한다.

### 6-1. `bulk` / `tree` 포함 여부

`--media images` 의 필터가 어떤 디렉터리 구성을 포함하는지 **Gate 실행 전에 1회 확인**한다.
`bulk`(240)과 `tree`(2400)는 `.bmp` 중심이며, 실제 이미지 분석 비용이 다른 파일일 수
있다. Gate 는 확인된 구성을 그대로 기록하고 임의로 좁히거나 넓히지 않는다.

---

## 7. Mode 선택 (조사 결과로 확정)

### 7-1. 현재 실측

| mode | observed | eligible |
| --- | --- | --- |
| `AUTO/CPU` | 260 | **0** |
| `CPU/CPU` | 110 | 110 |
| `CUDA/CPU` | 60 | **0** |

`CUDA/CPU` + `SKIPPED` + `"mode unavailable in this environment"` 은 **fallback** 이다.

### 7-2. 중요한 발견: GPU 가 없는 것이 아니다

이 머신에는 **RTX 3080 Ti 가 존재한다** (§2-1). 그런데도 CUDA 가 unavailable 로 기록되었다.

원인은 **CPU build** 다. 현재 store 는 `build-windows-cpu\Release` 아래 있고, 그 binary
에는 CUDA 가 링크되어 있지 않다. 따라서 `CUDA → CPU / SKIPPED` 60건은
**하드웨어 부재가 아니라 build 구성의 결과**다.

> 이 사실은 이전 단계까지 "GPU 미지원 환경의 fallback" 으로 기록되어 있었다. 지금
> 측정된 사실로는 **하드웨어는 존재하고 binary 에 capability 가 없었던 것**이므로
> 해석을 수정한다. 기존 문서의 관측값(60건, SKIPPED)은 그대로 두고 해석만 고친다.

### 7-3. Gate 의 mode 결정

**Primary = `cpu` 단독.** `--mode cpu`.

- 현재 실제로 성공 표본이 있는 유일한 mode 이다.
- `auto` 는 매 case 에 대해 추가 부하를 만든다.
- `gpu-max` 는 **CPU build 에서 unavailable 이므로 측정 대상이 아니다.**

`--mode auto,cpu,gpu-max` (기본값)를 그대로 쓰면 260건의 Skipped 가 다시 쌓이고,
성능 표본과 불가능 표본이 같은 cohort 에 섞인다. **Gate 는 `--mode cpu` 를 명시한다.**

### 7-4. GPU 측정은 별도 Gate 다

GPU comparison 이 필요하면 다음 조건을 **별도** 충족시켜야 한다. 이번 Gate 의 범위가 아니다.

- `build-windows-gpu` binary 로 측정 (CUDA linkage 확인은 **pre-register 항목**이며
  가정하지 않는다)
- GPU device 가 점유되지 않은 상태
- driver / VRAM 상태 기록

> 두 build 모두 `--version` 이 `(CUDA/CPU)` 로 표시된다는 점이 **pre-register 대상
> 관측 항목**이다. 표시 문자열만으로 CUDA 사용 가능 여부를 판단하지 않는다.

---

## 8. Resource Policy

`--resource` / `--cpu-percent` 가 CLI 에 없다. journal 에 `resourcePolicy` 도 없다.

결정:

1. **Gate 는 동일 machine · 동일 OS 세션 · 동일 전원 모드에서 실행한다.** 이것이 지금
   확보할 수 있는 최대의 통제다.
2. resource policy 를 **문서에 명시하되, 측정값으로 기록하지 않는다.** S6 analytics 가
   나중에 이를 provenance 로 복원할 수 있다고 가정하지 않는다.
3. 향후 S3 schema 확장 후보로 분리한다. **이번 Gate 에서 journal schema 를 변경하지 않는다.**

---

## 9. 반복 횟수와 절차

### 9-1. 횟수

`AGENTS.md` 9항의 권장 기준 **5회 이상**을 Gate 의 최소 반복 횟수로 채택한다.

```
Build A × 5, Build B × 5   = 총 10 실행
```

**5회 미만을 제안해야 하는 경우**에는 그 이유를 명시하고, 통계적 충분성을 선언하지
않는다. 그 경우 결과는 `INSUFFICIENT` 로 기록한다 (§13).

### 9-2. 반복 실행의 독립성 — `runId` 충돌 규칙

`runId` 는 **1초 해상도** stamp 다(`run-YYYYMMDD-HHMM-SS`). 따라서:

> **반복 실행은 서로 다른 suite 로 시작하고, 시작 시각이 2초 이상 떨어지도록 한다.**

실제 규칙:

| 항목 | 규칙 |
| --- | --- |
| suite | 반복마다 **명시적** `--suite <label>` 사용 (예: `gateA-r1`, `gateB-r1`) |
| journal | suite 마다 별도 `runs.jsonl` 이 생성된다 |
| `RunReference` | **`(sourceJournalPath, runId)`** 를 계속 사용한다 |
| 충돌 확인 | 실행 전 `consoleSuiteIdIsSafe` 통과 여부 확인 |

명시적 suite 를 쓰는 이유: 자동 stamp 는 1초 해상도라 동시 시작 시 충돌한다. 명시 지정은
사용자가 조작할 수 있어 절차가 재현 가능해진다.

### 9-3. 반복마다 확인 · 기록

| 항목 | 기록 위치 |
| --- | --- |
| suite id | `--suite` 값 (journal `suiteId`) |
| run id | journal `runId` (자동 stamp) |
| gitCommit | journal `run_started.gitCommit` |
| datasetFingerprint | journal `run_started` |
| mediaScope | journal `run_started` |
| 시작·종료 시각 | journal `startedAt` / `completedAt` |
| 종료 상태 | journal terminal record |

---

## 10. A/B 실행 순서

### 10-1. 결정: 교차 순서 `A B B A B B A ...`

```text
Build A r1
Build B r1
Build B r2
Build A r2
Build A r3
Build B r3
Build B r4
Build A r4
Build A r5
Build B r5
```

이유:

- 선형 drift (시간에 따른 머신 상태 변화)를 상쇄한다.
- 각 build 의 실행이 시간 축에서 **앞/뒤로 번갈아** 배치되므로, drift 가 어느 한쪽에
  몰릴 수 없다.
- 단순 `A B A B A B` 보다 drift 영향이 작다. `A B B A` 는 인접한 동일 build 실행이
  서로 영향을 주어 cache 상태를 공유한다는 부작용이 있는데, 3347 파일 규모에서는
  직전 실행의 OS cache 영향이 작으므로 **drift 상쇄를 우선**한다.

### 10-2. 순서 기록

각 실행의 순서는 **suite id 로 복원 가능**해야 한다. 실행 시 위 표의 순서를 그대로
`--suite` 라벨에 반영한다 (예: `gateA-r1`, `gateB-r1`, `gateB-r2`, `gateA-r2`).

> 실행 순서를 바꾸더라도 journal schema 는 변경하지 않는다. 순서는 suite id 라벨
> Convention 으로만 남긴다.

---

## 11. Cache 정책 (warm / cold)

### 11-1. 결정: 방법 A — 통제하지 않고 양쪽에 동일 절차를 적용한다

OS filesystem cache 를 이 환경에서 완전히 통제할 수 있다고 **주장하지 않는다.**

S2-PERF 가 이미 OS filesystem cache 와 process isolation 을 **uncontrolled** 로 accepted
두었다. 이 결정을 Gate 가 뒤집지 않는다.

따라서:

| 상태 | 정책 |
| --- | --- |
| **warm** (기본) | 첫 실행 후 OS cache 가 데워진 상태. A/B 양쪽 동일 |
| **cold** | 시도만 하고 강제하지 않는다. OS cache flush 는 시스템 전체에 영향을 주므로 **하지 않는다** |

Gate 는 **warm 상태를 표준 조건으로 채택**하고, 첫 실행을 별도로 warming run 으로
분리해 기록에서 제외하거나 표시한다.

### 11-2. 실제로 통제 가능한 것 / 없는 것

| 요소 | 통제 가능 | 방법 |
| --- | --- | --- |
| 전원 모드 | **가능** | 고성능 고정 (현재 적용됨) |
| background workload | **부분** | 측정 중 다른 빌드/테스트를 실행하지 않음 |
| process 격리 | **부분** | 실행마다 새 프로세스 (CLI 는 이미 그럼) |
| application 내부 cache | **가능** | suite 별 runtime index 가 분리됨 (§3-2) |
| **OS filesystem cache** | **불가** | warm 조건으로 고정하고 제한을 명시 |
| 스케줄러 / CPU frequency | **불가** | uncontrolled 로 기록 |
| turbo / thermal state | **불가** | uncontrolled 로 기록 |

> process 격리와 OS cache 는 **통제된 것처럼 표현하지 않는다.** S2-PERF 와 동일한
> uncontrolled 표시를 유지한다.

---

## 12. Metric 선택

### 12-1. Primary = `ModeElapsed`

| 후보 | 판단 |
| --- | --- |
| **`ModeElapsed`** | **Primary.** 단일 mode 실행 시간. 실행 단위를 가장 잘 표현하고, 짧은 probe 편차(19~46 %)의 영향을 상대적으로 가장 적게 받음. `--mode cpu` 단독이므로 이 cohort 는 순수 CPU 측정이다 |
| `CaseElapsed` | **Secondary 확인용.** S2 정의상 case 의 mode 합이므로 mode elapsed 와 거의 같은 정보를 담음. 차이는 파일 준비 비용을 포함한다는 점뿐 |
| `RunWallDuration` | **Primary 로 부적합.** 해상도 **1초** 이므로 세밀한 regression 판단에 한계가 있다. S6-2 에서 계약으로 고정된 제약이므로 **보조 확인에만** 쓴다 |

### 12-2. `RunWallDuration` 를 primary 로 쓰지 않는 이유

S3 timestamp 는 `localtime_s` + literal `Z` 로 초 단위만 기록한다. 따라서 run wall
duration 은 **항상 1000 ms 의 배수**이고, 1초 미만 실행은 정확히 `0` 이다.

이 저장소의 실측이 그 결과다: 21 run cohort 의 wall median 이 `0.0` 이었다.
**0 ms 는 "1초 안에 끝났다"는 실측값이지 missing 이 아니다** (S6-2 계약).

따라서 wall duration 은 "(1초 안에 끝났는가)" 라는 coarsened 정보만 준다. ms 단위
regression 판단의 근거로 쓰면 분해능 손실을 즉시 얻는다.

> `durationResolution = OneSecond` 가 결과 metadata 로 항상 따라간다. median/mean 을
> 다른 metric 과 같은 축에 놓지 않는다.

### 12-3. 측정 결과에서 확보할 값

반복 실행 결과에서 다음을 확보한다. **이번 Gate 에서는 코드에 새 통계값을 추가하지
않는다.** S6-4 가 이미 `count / min / max / mean / median / p95` 를 계산하므로 그것을
소비한다.

추가로 **문서로만** 기록할 값:

| 값 | 용도 |
| --- | --- |
| run 간 spread (max − min) | 변동폭의 직접 표현 |
| coefficient of variation | 상대 변동폭 |
| `AGENTS.md` 9항 권장 실행 횟수 대비 | 5회 채달 여부 |

---

## 13. Measurement variation 과 measurement error 의 구분

이 둘을 같은 말로 쓰지 않는다.

| 구분 | 뜻 | 이 Gate 에서 |
| --- | --- | --- |
| **실행 변동성** | 동일 build · 동일 조건 반복 실행 간 차이 | §14 로 **측정한다** |
| **측정 오차** | timer 해상도, 시스템 스케줄링, cache 등 측정 장치 자체의 불확실성 | 분리해서 기록하되 **정량하지 않는다** |

기존에 이미 분리해야 한다는 근거가 있다.

- 짧은 probe 의 변동폭(19.4~45.9 %)은 timer 해상도 문제가 아니라 **실행이 짧아 상대
  변동이 커진 것**이다. 측정 단위를 늘리면 줄어든다.
- 전체 실행의 3.7~7.5 %는 그보다 안정적이다.

> **"측정 오차"라고 부르지 않는다.** 관측 가능한 것은 run 간 변동성이며, 측정 장치의
> 절대 오차는 이 절차로 정량하지 않는다. 정량하지 않은 값은 `Not measured` 로 남긴다.

### 13-1. Gate 가 실제로 얻는 것

```
ModeElapsed 의 run 간 변동폭 (5회)
→ 이것이 후속 threshold 설계의 유일한 근거
→ threshold 는 이 값이 나온 뒤 별도 decision 단계에서 정한다
```

---

## 14. Gate 의 성공 조건 (exit condition)

**regression 을 발견하는 것이 성공 조건이 아니다.** 다음 조건의 **측정 데이터가 생성**되는
것이 성공 조건이다.

```
[ ] Build A   — Known gitCommit, run_started.gitCommit == MSF_BUILD_GIT
[ ] Build B   — Known gitCommit, A 와 다른 commit, origin/main 에 존재
[ ] datasetFingerprint 동일, A/B 간 dataset 무변경
[ ] mediaScope 동일 (primary: images)
[ ] mode semantics 동일 (--mode cpu)
[ ] 실제 SUCCESS case sample 존재 (eligible > 0)
[ ] 각 build 5회 반복
[ ] mode-elapsed 기준 run 간 변동폭 산출 (min/max/mean/median/p95)
[ ] 실행 순서 기록 (A B B A 교차)
[ ] 환경 요소 기록 (통제 가능/불가 구분 포함)
[ ] A/B 모두 S6 파이프라인을 통과하여 comparison candidate > 0
```

위 조건이 모두 충족되면 **S6 판정 계층 진입 가능**으로 표시한다. 그때도 threshold 는
미정 상태이며, 별도 결정 단계가 남는다.

### 14-1. 검증 방법 (S6 자체 도구)

Gate 는 제품 코드를 바꾸지 않고 **S6 의 read-only diagnostic** 으로 검증한다.

```
# ingestion + grouping + aggregation + candidate + metric
build-windows-cpu\Release\msf_benchmark_data_mining_test.exe       <storageRoot>
build-windows-cpu\Release\msf_benchmark_data_aggregation_test.exe  <storageRoot>
build-windows-cpu\Release\msf_benchmark_data_comparison_test.exe   <storageRoot>
build-windows-cpu\Release\msf_benchmark_data_comparison_metrics_test.exe <storageRoot>
```

진입 판정 기준 (기계적으로 확인 가능):

| 확인 | 기준 |
| --- | --- |
| Known build cohort | **2개 이상** |
| provenance | `known=2` 이상, `unknown`/`legacy` 의존성 없음 |
| candidate | `eligibleCandidates > 0` |
| comparison metrics | `absolute deltas available > 0` |
| 실패/제외 | `missing-provenance = 0`, `no-comparable-samples = 0` |

---

## 15. Invalid measurement 조건

아래는 비교용 측정으로 **자동 포함하지 않는다.** 각 항목의 판정 근거를 함께 적는다.

| 조건 | 판정 근거 |
| --- | --- |
| dataset fingerprint mismatch | journal `run_started.datasetFingerprint` 값 비교 |
| scope mismatch | journal `run_started.mediaScope` 값 비교 |
| mode mismatch | `mode_result.requestedMode` / `effectiveMode` 비교 |
| build provenance missing | `run_started.gitCommit` 부재 또는 `Legacy`/`Unknown` |
| 전 sample skipped | mode cohort `eligible = 0` |
| 전 sample failed | mode cohort `eligible = 0` |
| cancelled / incomplete run | terminal record 없음 또는 `run_cancelled` |
| dataset 변경 | fingerprint 변경 (측정 도중 dataset 수정 금지) |
| 환경 중단 | **아래 기준** |

### 15-1. "명백한 환경 중단" 의 객관적 기준

`obvious` 라는 주관적 판단을 쓰지 않는다. 다음 중 하나라도 해당하면 해당 run 을
제외하고 그 사실을 기록한다.

- 실행 중 다른 빌드 / 테스트 실행이 시작됨 (작업 이력으로 확인)
- 전원 모드가 변경됨
- 실행 시간이 동일 조건의 다른 run 의 **중앙값 3배** 를 초과
- `run_wall` 이 `Not measured` (completedAt 부재)
- OS 가 업데이트 / 재시작됨

> 기준에 걸리지 않았더라도 **의심되는 run 은 제외하지 않고 포함하되 표시**한다. 제외는
> 근거가 있을 때만 한다.

---

## 16. 실행 절차 (Gate run sheet)

각 반복에 대해 순서대로 실행한다.

```
1. 실행 전: 다른 빌드 / 테스트가 동작 중이 아닌지 확인
2. 실행 후: journal 을 read-only 로 열어
   - runId / suiteId / gitCommit / fingerprint / mediaScope 확인
   - eligible case sample > 0 확인
   - 두 sample 이상이면 후보에 넣고, 아니면 "이유" 를 기록하고 다음 반복 진행
3. 전 반복 완료 후: S6 diagnostic 4종 실행 (read-only)
4. mode-elapsed 기준 run 간 변동폭 산출
5. 문서에 실제 수치 기록
```

### 16-1. 명령 형태

```
build-windows-cpu\Release\MediaSimilarityFinder.exe ^
    --benchmark C:\project\test_sample_img_vid ^
    --media images ^
    --mode cpu ^
    --suite <gateA-r1 | gateB-r1 | ...> ^
    --log-dir <Gate 전용 log 디렉터리>
```

> `--log-dir` 로 Gate 전용 저장소를 지정하여 기존 store 와 섞지 않는다. 이 Gate 의
> 산출물은 실측 증거이므로 별도 경로에 보존한다.

---

## 17. 경계 — 이번 단계에서 변경하지 않는 것

- S2 benchmark execution
- S3 journal schema / recovery / summary
- S6 ingestion / normalization / grouping / aggregation / comparison / comparison metrics
- Scanner / GUI / Console renderer / CLI
- legacy benchmark

필요한 변경이 발견되면 **별도 implementation task 로 분리**한다. 이번 Gate 는 문서
작업이며 제품 코드 변경이 없다.

### 17-1. 이번 단계에서 구현하지 않는 것

- benchmark 실행 자동화
- 새로운 측정 engine
- threshold code / regression code / anomaly code
- report formatter
- GUI / CLI
- journal schema change

---

## 18. 기존 제약 (계속 유지)

| 제약 | 상태 |
| --- | --- |
| `distance` unavailable | 유지 — journal 에 없음 |
| `resourcePolicy` unavailable | 유지 — CLI/journal 양쪽에 없음 (§8) |
| `gpuBackend` unavailable | 유지 — journal 에 없음 |
| controlled environment 미정의 | **이 Gate 가 부분 정의**하되 완전 통제는 아님 (§11) |
| repeated runs 부족 | **이 Gate 가 해소** |
| real `run_cancelled` sample 없음 | 유지 |
| run wall 1초 해상도 | 유지 — primary metric 에서 제외 (§12) |
| OS filesystem cache / process isolation uncontrolled | 유지 (§11) |

---

## 19. 문서 변경 이력

| 날짜 | 내용 |
| --- | --- |
| 2026-10-01 | 최초 작성. 환경 · dataset · CLI · store 실측값 반영. 기존 0.9.4.32 변동성 재사용. `CUDA→CPU SKIPPED` 해석을 "build 구성" 으로 수정. 비디오 파일 0건 확인으로 `videos` scope 제외. |
