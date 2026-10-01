# S6 Data-mining Automation — Implementation Brief

버전 기준선: `0.9.4.43` (`CMakeLists.txt` 단일 source)
선행 brief: `docs/implementation-briefs/S5-console-benchmark-execution.ko.md` / `.en.md`
선행 검증: `docs/build-history/S5-verification.ko.md` / `.en.md`
설계 근거: `docs/architecture/benchmark-telemetry-roadmap.ko.md` §22, §25, §28, §30, `docs/architecture/storage-design.md`

**이 문서는 S6 설계 문서이며 구현 결과 보고서가 아니다.** 이 단계에서 S6 코드는
작성하지 않았다. 아래 "검증됨" 표기는 실제 소스 또는 실제 journal에서 확인한 사실이고,
"결정" 표기는 이 brief가 새로 확정한 설계이며, "보류" 표기는 의도적으로 정하지 않은 것이다.

---

## 1. 목적

S5를 통해 benchmark 결과가 `runs.jsonl` append-only journal로 영구 축적된다.
S6는 이 축적된 journal에서 **반복 가능한 분석과 비교**를 자동화한다.

roadmap이 S6에 이미 부여한 정의(확정된 것, 여기서 새로 정한 것 아님):

```text
benchmark-telemetry-roadmap §28
  "S6 Data-mining automation: suite 자동 실행, dataset fingerprint 검증, 비교 요약"
benchmark-telemetry-roadmap §30
  "S6: Suite 자동 실행, fingerprint 검증, 비교/데이터 마이닝"
```

따라서 roadmap상 S6는 **분석 기능이 아니라 3개 축**이다.

1. **자동 실행(automation)** — suite 자동 실행
2. **검증(validation)** — dataset fingerprint 검증
3. **비교/마이닝(comparison / data-mining)** — 비교 요약

**본 brief가 다루는 범위는 2번과 3번이다.** 1번(suite 자동 실행)은 S6 내부에서
별도 brief로 분리할 것을 권고하며, 이 brief에서는 정의하지 않는다(14장 보류 참조).

`benchmark-telemetry-roadmap` §22가 S6의 데이터 원천 성격을 이미 규정한다.

> "Console은 장기 비교와 데이터 마이닝용이며 결과를 자동 삭제하지 않는다."

즉 Console suite 저장이 곧 S6의 입력 저장소이며, S6는 이를 **읽기만** 한다.

## 2. 문제 정의

현재 journal이 아무리 축적되어도 사람이 매번 수작업으로 해야 하는 반복 작업이 있다.

```text
Benchmark/Console/ 아래 suite 디렉터리 검색
suite 별 runs.jsonl 파일 목록 수집
journal JSONL 파싱 및 truncated tail / commitless case 판정
완료된 run(case_complete + run_finished 보유)만 선별
datasetFingerprint 로 동일 데이터셋 묶기
buildVersion 으로 빌드 구분
requested/effective mode 별 elapsed 집계
media(Image/Video) 별 분리
실행 간 elapsed delta / 비율 비교
```

현재 이 작업을 하는 도구는 **어디에도 없다.** 검증됨:

- `runs.jsonl` / `summary.json` / `auto.json` / `cpu.json` / `gpu-max.json` /
  `Benchmark/` 디렉터리를 읽는 분석 프로그램이 저장소에 **존재하지 않는다**.
- journal 을 읽는 유일한 코드는 `src/benchmark_journal.cpp` 의
  `replayJournal()` + `buildSummaryJson()` 이며, 출력은 **카운트와 `totalElapsedMs`
  합계뿐**이다. median / percentile / trend / regression 판정 로직이 없다.
- Python 이 프로젝트에 **전혀 없다.** `.py` 파일 0개, `requirements.txt` /
  `pyproject.toml` / `Pipfile` / `setup.py` 0개. CI 두 개 모두 `shell: pwsh`.
- 스크립트 관례는 PowerShell 이다(`scripts/` 14개, `scripts/validation/` 7개).
- 가장 가까운 선례는 journal 분석이 아니라 별개 입력용이다.
  `scripts/validation/i3_decode_decomposition.ps1`(버킷별 median),
  `scripts/validation/i3_paired_analysis.ps1`(paired median ratio),
  `tests/dataset_baseline.cpp`(반복 실행 분포 출력).

## 3. 범위 (Scope)

S6가 수행하는 것. 각 단계 책임은 9~13장에 쓴다.

- `Benchmark/Console` 아래 journal **탐색·수집**(read-only)
- journal 파싱 및 **S3 recovery 규칙 준수** 검증 ingestion
- 파생값과 측정값의 **분리된 표현**(7장)
- `datasetFingerprint` 중심 **그룹핑**
- run / mode / case 3개 수준 **집계**
- **교차 run 비교** (동일 데이터셋 + 동일 조건, 다른 빌드)
- **회귀 후보 탐지**(threshold 미확정, 14장)
- 재현 가능한 **분석 산출물** 출력 및 provenance 기록

## 4. 비범위 (Non-scope)

S6는 다음을 **하지 않는다.**

- benchmark **실행 engine**. S2 `BenchmarkRunner` / `ProductionBenchmarkExecutor` 를
  재사용할 뿐 새 실행 경로를 만들지 않는다.
- journal **append / 수정 / 삭제 / 복구**. S6는 journal 을 읽기만 한다.
- S2 / S3 / S5 **코드·계약 변경**. schema 를 bump하거나 record 를 추가하지 않는다.
- journal schema **migration**. 기존 schema 를 이전 버전으로 해석해 재작성하지 않는다.
- legacy `BenchmarkRecorder` schema 변경·재사용·마이그레이션(11장).
- GUI 변경.
- GPU / CUDA / NVDEC 변경.
- **분석 결과의 측정값 사칭**. derived 를 measured 로 저장하거나 표현하지 않는다.
- 통계 threshold 의 임의 확정(14장).
- suite **자동 실행**(1장, 14장).

## 5. 데이터 원천 (Data source)

### 5-1. Authoritative 단일 원천

```text
runs.jsonl  (S3 append-only journal)
```

이것이 유일한 authoritative 입력이다. `benchmark-telemetry-roadmap` §29.2 및
`storage-design.md` "터미널 rendering contract" 와 동일하다.

> "터미널 표시 내용은 데이터 원본이 아니며 journal/summary가 canonical source다."

S6는 journal 을 **read-only** 로 연다. 파일 lock 을 잡지 않고, 어떤 record 도
append 하지 않으며, suite 디렉터리에 아무것도 쓰지 않는다.

### 5-2. Authoritative 가 아닌 것

다음은 **어떤 경우에도 분석 원본으로 취급하지 않는다.**

| 대상 | 이유 |
| --- | --- |
| `--log` 파일 (renderer text sink) | 표시 산출물. journal 로부터 재생성되지 않으며 journal 의 부분집합이 아니다 |
| Console 표준출력 / TTY 출력 | 동일 |
| `summary.json` | S3 계약상 **journal replay 결과이며 non-authoritative**. 삭제 후 재생성되므로 사용 가능하되 원본 아님 |
| `suite.json` | suite 메타데이터. journal record 와 별개 |
| `runtime/` 트리 | 실행 scratch, 증거 아님 |
| `Benchmark/GUI/*.json` | GUI snapshot. journal 과 **다른 schema**이며 별개 storage |
| legacy `BenchmarkRecorder` JSON | schema 9. journal(schema 1)과 완전히 별개(11장) |
| 사람이 편집한 문서/표 | 근거 없음 |

`summary.json` 은 S6 구현에서 **읽지 않는 것을 기본으로 한다.** journal 을 읽으면
같은 정보를 더 신뢰할 수 있는 원천에서 얻을 수 있기 때문이다. `summary.json` 을
읽어야 하는 경우(예: 재생성 여부 확인)에는 그것이 파생물임을 출력에 명시한다.

## 6. Journal schema — 실제 확인 결과

아래는 **소스와 실제 journal 양쪽에서 확인**한 필드 목록이다. 없는 필드는
** 없다고 적었고**, 추정으로 채우지 않았다.

확인 대상: `src/benchmark_journal.cpp` writer 본문 + 빌드 산출물 journal
**29개 파일 / 862 line** 전체 키 스캔.

### 6-1. 공통 필드 (모든 record)

| 필드 | 출처 | 비고 |
| --- | --- | --- |
| `journalSchemaVersion` | 상수 | `kBenchmarkJournalSchemaVersion = 1` |
| `eventType` | — | `run_started` / `mode_result` / `case_complete` / `run_finished` / `run_cancelled` |
| `recordId` | 결정적 | `case_complete:<runId>:<caseId>` 형식은 멱등 재기록에 무관 |
| `suiteId`, `runId` | S2 run | |
| `timestamp` | `benchmarkNowStamp()` | **표기 문제 존재 — 16장** |

### 6-2. `run_started` (실측 10개 journal에서 확인)

```text
mediaScope, scanImages, scanVideos, sourceRoot, sourceRootLabel, sourceRootId,
datasetFingerprint, buildVersion, startedAt, filesStarted
```

### 6-3. `mode_result` (case 별 mode 수 만큼)

```text
caseId, requestedMode, effectiveMode, status, started, completed, elapsedMs,
summary{ scanned, added, modified, unchanged, removed, analyzed, candidates,
         groups, indexedVideos, videoCandidatePairs },
errorMessage
```

`requestedMode` / `effectiveMode` 값 집합: `AUTO` / `CUDA` / `CPU`.
`status` 값 집합: `SUCCESS` / `FAILED` / `CANCELLED` / `SKIPPED`.

### 6-4. `case_complete` (commit marker)

```text
caseId, path, media, status, elapsedMs, errorMessage
```

`media` 값 집합: `Image` / `Video` / `Unknown`.
S5-3 의 `Scanner` MediaKind 수정 이후 실제로 `Image` / `Video` 가 기록된다.

### 6-5. `run_finished`

```text
status, filesStarted, filesCompleted, filesRemaining, completedAt, completionReason
```

`completionReason` 은 항상 `"completed"`.

### 6-6. `run_cancelled`

```text
status(=CANCELLED 고정), filesStarted, filesCompleted, filesRemaining, completionReason(=사유)
```

- **`completedAt` 필드가 없다.** 소스 확인. `run_finished` 와 다른 shape 다.
- **실제 journal 샘플 0건.** S5 E2E 에서 Ctrl+C 실 triggering 이 `NOT RUN` 이었고
  `benchmark_integration_test` 의 결정론적 cancellation 은 temp journal 을 쓴다.
  따라서 이 record 의 실측 표본은 아직 없다(18장).

## 7. Measured / Derived / Invalid 3분류

이 구분을 S6 전체에 걸쳐 강제한다.

### 7-1. Measured (journal 에 기록된 값)

journal record 에 **실제로 존재하는** 값만 Measued 로 취급한다.

| 값 | record | 필드 |
| --- | --- | --- |
| case elapsed | `case_complete` | `elapsedMs` |
| mode elapsed | `mode_result` | `elapsedMs` |
| mode 결과 | `mode_result` | `requestedMode` / `effectiveMode` / `status` / `started` / `completed` |
| case 결과 | `case_complete` | `status` / `media` / `path` |
| 데이터셋 동일성 | `run_started` | `datasetFingerprint` |
| 빌드 | `run_started` | `buildVersion` |
| 범위 | `run_started` | `mediaScope` / `scanImages` / `scanVideos` |
| 스캔 카운트 | `mode_result.summary` | 10개 카운터 |
| run 진행 | `run_finished` | `filesStarted` / `filesCompleted` / `filesRemaining` |
| 실행 시각 | 양쪽 | `startedAt` / `completedAt` |

### 7-2. Derived (S6 가 계산)

S6 가 계산하는 값이며 **반드시 derived 로 표시**한다.

| 파생값 | 계산 근거 | 표시 규칙 |
| --- | --- | --- |
| run wall duration | `completedAt - startedAt` | 두 값이 동일 formatter(localtime, 무 timezone) 이므로 **서로는 비교 가능**. 단 16장 |
| case/mode elapsed 합 | record `elapsedMs` 합 | 모집단을 명시 |
| mean / median / p95 / min / max | elapsed 모집단 | **어느 모집단인지 명시** (15장) |
| mode 간 delta (GPU-max vs CPU) | 동일 caseId 의 mode 간 elapsed | |
| speedup ratio | 동일 caseId 내 mode 간 | 단일 case 값은 매우 노이즈 → 모집단 명시 |
| regression % | 기준 run 대비 | threshold 미확정 (14장) |
| failure rate | status 카운트 / 전체 | S2 aggregate precedence 따름 (17장) |
| media 별 집계 | `case_complete.media` | |
| build 별 집계 | `buildVersion` | |
| incomplete / discarded 건수 | anomaly 기준 (13장) | |

### 7-3. Invalid / Incomplete (분석 제외)

다음은 **분석에 포함하지 않는다.**

- `case_complete` 없이 `mode_result` 만 있는 case (commit되지 않은 transaction)
- `run_finished` / `run_cancelled` 없는 run (진행 중 또는 crash)
- 마지막 newline 없는 tail line (`TruncatedTail` — S3 규칙대로 폐기)
- 완전한 line 이지만 파싱 실패 (`MidFileCorruption` — S3 규칙대로 **fatal**)
- `recordId` 충돌 + payload 상이 (`DuplicateConflicting` — **anomaly 보고, 첫 record 유지**)
- `datasetFingerprint` 가 빈 문자열인 run (계산 불가 상태. `DatasetFingerprint::state`
  가 `not_available` / `failed` 였을 가능성)
- `status = SKIPPED` 인 mode 의 elapsed ( 측정되지 않음. 0 으로 취급 금지 )

**Invalid 데이터를 "빼고 계산하되, 뺀 개수를 반드시 출력한다."** 조용히 제외하면
분석 결과가 실제보다 좋아 보이게 된다.

## 8. 현재 journal 로 가능한 분석 / 불가능한 분석

이 장이 S6 설계의 핵심이다. **있는 것과 없는 것을 분리해 적는다.**

### 8-1. 지금 가능한 분석 (journal 만으로)

| 분석 | 근거 필드 |
| --- | --- |
| 동일 데이터셋 묶기 | `datasetFingerprint` |
| 동일 데이터셋 + 동일 범위 의 build 간 elapsed 비교 | + `mediaScope` / `scanImages` / `scanVideos` / `buildVersion` |
| 동일 build 내 mode 간 비교 (AUTO / CPU / CUDA) | `requestedMode` / `effectiveMode` |
| requested vs effective 비교 (capability 관찰) | 두 mode 필드 |
| case 단위 elapsed 분포 | `case_complete.elapsedMs` |
| mode 단위 elapsed 분포 | `mode_result.elapsedMs` |
| Image vs Video 분리 집계 | `case_complete.media` |
| success / failed / cancelled / skipped 집계 | status 필드들 |
| 스캔 카운터 비교 (`analyzed`, `groups`, `candidates` 등) | `mode_result.summary` |
| 실행 회귀 후보 탐지 (기준 run 대비 delta) | 위 조합 |
| 불완전/취소 run 식별 | terminal record 존재 여부 |

### 8-2. 지금 **불가능한** 분석 — journal 에 필드가 없다

실측 스캔 결과(29개 journal / 862 line, 키 문자열 전체 검색):

| 원하는 분석 | 필요한 필드 | journal 실측 | 결과 |
| --- | --- | --- | --- |
| **git commit 별 빌드 구분** | `git` / `buildGit` / `commit` | **0건** | **해소됨 (2026-10-01)** — `run_started.gitCommit` 추가 |
| **CPU Resource policy 별 분리** | `resourcePolicy` / `cpuPercent` / `gpuPercent` | **0건** | **불가능** |
| **GPU 사용 여부 구분** | `gpuEnabled` / `gpuBackend` | **0건** | **불가능** |
| **distance 별 비교** | `distance` | **0건** | **불가능** |
| run wall duration | `duration` | **0건** | `startedAt`/`completedAt` 로 **파생 가능**(16장) |
| run 단위 요청 mode 목록 | `requestedModes[]` | 없음 | `mode_result` 전수 스캔으로 **파생 가능** |

**근본 원인은 `MSF_BUILD_GIT` 이 journal 에 기록되지 않는다는 것이다.**
S5-3 에서 `MSF_BUILD_GIT` 를 generated header 에 추가하고 Console 헤더에 표시했지만,
journal 은 S3 schema 이며 변경하지 않았다. 따라서 journal 안에는
`buildVersion`(예 `0.9.4.43`) 만 있고, **같은 버전 번호의 서로 다른 커밋을 구분할 수 없다.**

> **2026-10-01 갱신**: 이 갭은 해소되었다. `run_started.gitCommit` 이 S5 의
> `MSF_BUILD_GIT` 을 additive field 로 기록하며, 실제 실행에서 journal 값과 binary 의
> generated 값이 일치함을 확인했다. 위 표의 0건 수치는 **조사 시점의 실측값**으로
> 그대로 보존한다. 상세는 22장 ①.

이는 `benchmark-telemetry-roadmap` §25 가 "Run must store … `appVersion/gitCommit`"
을 요구하고 §28/§30 이 S6 의 핵심을 "fingerprint 검증 + 비교 요약" 으로 정의한 것과
**직접 충돌한다.** 같은 버전의 두 빌드를 비교할 유일한 키가 지금은 존재하지 않는다.

`distance` 도 동일하다. `BenchmarkRun::distance` 는 S2 에 존재하지만
journal writer 가 기록하지 않는다. `resourcePolicy` 도 S2 `BenchmarkRequest` 에
존재하지만 journal 에 없다.

**결정**: S6 는 이 필드가 없는 상태로 시작할 수 있으나(8-1 범위는 즉시 수행 가능),
8-2 중 `git` 항목 없이는 roadmap이 정의한 "build 간 비교"를 **완수할 수 없다.**
따라서 journal 필드 보강을 S6 선행 조건으로 분류한다(22장).

**이 brief는 journal schema 를 변경하지 않는다.** S6 구현이 schema 를 bump하는 것은
S3 의 영역이며 별도 결정이 필요하다(14장 보류).

## 9. Ingestion 책임

journal 을 분석 가능한 형태로 옮기는 단계. **S3 recovery 규칙을 재구현하지 않는다.**

- journal **파싱/복구**는 `msf::replayJournal()` 을 **재사용**한다.
  S3 가 확정한 의미를 다시 만들지 않는다: truncated tail 폐기, mid-file corruption
  fatal, identical duplicate 무시, conflicting duplicate anomaly 보고(첫 record 유지).
- suite 탐색은 `<storageRoot>/Benchmark/Console/suite-*/runs.jsonl` 패턴만 읽는다.
  `runtime/` , GUI storage, legacy 산출물은 읽지 않는다.
- 여러 run 이 한 journal 에 누적될 수 있으므로 `(runId, caseId)` 단위로 분리한다.
  S3 가 이미 `replayJournal(path, runIdFilter)` 로 제공하며, 이는 `S3-BUG` 에서
  실제 결함으로 수정된 항목이다(run 미만을 caseId 만으로 keying 해 서로 다른 run 의
  mode record 가 합쳐지던 문제). S6 는 이 규칙을 그대로 따른다.
- ingestion 결과마다 다음 카운트를 유지한다.
  `journalFiles` / `runs` / `committedCases` / `incompleteCases` / `anomalies` / `fatal`

**결정**: S6 는 journal 파서를 **새로 작성하지 않는다.** PowerShell 이나 Python 으로
journal 을 다시 파싱하는 것은 S3 recovery 규칙의 중복 구현이 되며 10장에서 금지된다.

## 10. Normalization 책임

- journal 값 → 내부 표현. `status` / `mode` / `media` 는 **S3 의 이름 문자열을 그대로** 쓴다.
  S6 전용 enum 으로 매핑하면서 의미를 바꾸지 않는다.
- 경로: `case_complete.path` 는 전체 절대 경로다. **분석 출력에서 basename 만 쓴다.**
  (journal 은 원본을 그대로 보존해야 하고, S6 는 원본을 바꾸지 않는다)
- 부재 값과 0 을 구분한다. `elapsedMs` 가 없는/측정 안 된 경우는 0 이 아니라
  **분석 제외**로 표시한다. `AGENTS.md` 9항 "측정하지 않은 값은 `N/A` 또는
  `Not measured` 로 명시한다"를 따른다.
- 단위: journal `elapsedMs` 는 millisecond. S6 출력 단위를 명시적으로 표시한다.

## 11. Legacy benchmark 분리

`legacy benchmark ≠ new durable journal analytics` 를 유지한다. **검증됨.**

| 항목 | legacy | journal |
| --- | --- | --- |
| 주체 | `BenchmarkRecorder` (`src/benchmark.{h,cpp}`) | `BenchmarkJournalWriter` (`src/benchmark_journal.{h,cpp}`) |
| schema version | `kBenchmarkSchemaVersion = 9` | `kBenchmarkJournalSchemaVersion = 1` |
| 저장 | **파일을 쓰지 않는다.** `toJson()` 가 문자열 반환 | `runs.jsonl` append |
| 파싱读者 | 없음. GUI dialog 가 in-memory 로만 읽음 | `replayJournal()` |
| schema 변경 | 금지 | 금지 |

- 두 schema 번호는 **의도적으로 독립**이며 함께 bump 하지 않는다
  (`benchmark_store.h`, `benchmark_gui_store.h` 주석에 명시).
- 공유 코드는 JSON escape 함수 **하나뿐**이며, 이는 함수 재사용일 뿐 schema/의미
  재사용이 아니다(`benchmark_store.h` 주석이 그렇게 명시한다).
- GUI snapshot schema 는 또 다른 독립 번호(1) 이며 journal 과 다르다.
- S6 는 legacy JSON 을 **읽지도 않고 쓰지도 않는다.** migration 도 하지 않는다.

## 12. Grouping 책임

grouping key 는 **존재하는 필드만** 사용한다. 없는 키를 쓰면 안 된다.

### 12-1. 1순위: `datasetFingerprint`

가장 강한 동일성 키다. `worklog 0.9.4` D8a 결정에 따라 dataset identity 에는
**절대경로 · timestamp · 하드웨어명이 포함되지 않으므로**, 동일 내용물은 root 가 달라도
같은 fingerprint 를 갖는다. 이 성질이 S6 비교의 근거다.

단, **fingerprint 동일만으로 모든 조건 동일이라고 가정하지 않는다.** 반드시
아래 조건을 함께 비교 조건으로 둔다.

### 12-2. 함께 비교해야 하는 조건 (fingerprint 와 별개 축)

| 축 | 필드 | 비고 |
| --- | --- | --- |
| media 범위 | `mediaScope` / `scanImages` / `scanVideos` | `all` 은 images+videos 로 분해하지 않는다 |
| 빌드 | `buildVersion` | provenance 상태(`Known`/`Unknown`/`Legacy`) 를 함께 쓴다 |
| 요청 mode | `mode_result.requestedMode` | run 단위 목록은 파생 |
| 실효 mode | `mode_result.effectiveMode` | capability 관찰. 요청과 **합치지 않는다** |
| case 동일성 | `caseId` | 파일 단위 비교의 키. dataset 과 짝을 이룬다 |

### 12-3. grouping 키로 **쓸 수 없는** 것 (8-2)

```text
resourcePolicy      — journal 에 없음
gpuEnabled          — journal 에 없음
gpuBackend          — journal 에 없음
distance            — journal 에 없음
```

이 키를 쓰면 "같은 조건" 이라는 주장이 거짓이 된다. **사용하지 않으며,
출력에도 "해당 조건 미기록" 이라고 명시한다.**

> **2026-10-01 갱신**: 위 목록에서 `git commit` 은 **제거**되었다. S3 가
> `run_started.gitCommit` 을 추가하여 이제 journal 에 존재한다(8-2 참조).
> 다만 journal 에 값이 있다는 것과 commit 으로 비교할 수 있다는 것은 다르므로,
> provenance 3상태(`Known`/`Unknown`/`Legacy`) 를 함께 유지한다. `Legacy` 는
> 필드 자체가 없어 commit 수준 비교 대상이 아니며 현재 HEAD 로 채우지 않는다.

### 12-4. 단일 composite key 를 쓰지 않는 이유 (S6-3 결정)

`datasetFingerprint + mediaScope + buildVersion + gitCommit + requestedMode +
effectiveMode` 을 하나로 묶은 key 를 쓰면 모든 run 이 자기 고유 그룹에 들어가
**build 간 비교가 구조적으로 불가능**해진다. 겉으로는 단순해 보이지만 비교를
삭제하는 설계이므로 채택하지 않았다.

대신 분석 축을 **typed key 구조체**로 분리하고, 호출부가 질문에 맞는 view 를
선택하게 한다(`src/benchmark_data_grouping.h`).

| view | key | 쓰이는 질문 |
| --- | --- | --- |
| `DatasetCohortKey` | `datasetFingerprint` | 같은 dataset 을 여러 build/mode 로 관찰 |
| `ScopeCohortKey` | `datasetFingerprint` + `mediaScope` | build 간 관찰의 기준 모집단 |
| `BuildCohortKey` | `buildVersion` + `gitCommitState` + `gitCommit` | 같은 build 의 run 묶기 |
| `ModeCohortKey` | `requestedMode` + `effectiveMode` | fallback/capability 관찰 |
| `CaseCohortKey` | `datasetFingerprint` + `caseId` | 파일 단위 비교 |

### 12-5. `caseId` 의 실제 의미 (S6-3 실측)

S2 는 `caseId = IndexManager::folderId(file.path)` 로 canonical path 를 해시한다.
따라서 **run-scoped 가 아니라 run 간 동일 파일이면 동일 `caseId`** 다(실측 확인:
같은 파일을 실행한 두 run 이 동일 `caseId` 를 냈다). 새 hash 를 만들지 않고
기존 `caseId` 를 쓰되, dataset fingerprint 와 **짝을 이루어** key 로 삼는다.
절대경로 해시이므로 단독 신뢰하지 않는다.

### 12-6. grouping 은 비교 가능성을 판정하지 않는다

S6-3 은 같은 cohort 를 **구성만** 하고 `comparable = true` 를 판정하지 않는다.
`BuildCohort::commitComparable` 은 commit **provenance 품질** 만 나타내며
(`Known` 에서만 true), 데이터의 비교 가능성 판정이 아니다. resourcePolicy ·
distance · GPU backend 부재, controlled env 미정의, 반복 run 부족은 그대로
후속 단계로 넘긴다.

mode 정렬은 `AUTO → CPU → GPU-MAX` canonical 순서를 쓴다. 이는
`GpuBackendKind` 의 선언 순서(`Auto, Cuda, Cpu`)와 다르므로 명시적 rank 를 쓴다.
이 정렬은 **표시·그룹 결과** 용이며 execution 순서를 재정의하지 않는다.

## 13. Aggregation 책임

3개 모집단을 **명시적으로 구분**한다. 서로 섞지 않는다.

| 수준 | 단위 | 측정값 | 주의 |
| --- | --- | --- | --- |
| case-level | `case_complete` 1건 | `elapsedMs` | 파일 1개 전체(모든 mode 합) |
| mode-level | `mode_result` 1건 | `elapsedMs` | mode 1회 실행 |
| run-level | run 1건 | `completedAt - startedAt` | 파생. wall clock |

- `case_complete.elapsedMs` 는 S2 정의상 **그 case 의 mode 결과 elapsed 합**이며
  discovery 비용을 포함하지 않는다. 따라서 case-level 값은 "파일 처리 시간"이지
  "run 전체 시간" 이 아니다.
- `run-level` wall duration 과 `case-level` 합은 **같지 않다.** 두 값을 같은 축으로
  비교하지 않는다. (§25 구현 원칙 "benchmark overhead 기록" 과 연결된다)

집계 결과에는 반드시 다음을 붙인다.

```text
n            — 모집단 크기
mean, median, p95, min, max, range
sourceRunIds — 근거가 된 run
```

## 14. Comparison / Regression — threshold 미확정

### 14-1. 지금 가능한 비교

동일 `datasetFingerprint` + 동일 `mediaScope` + 동일 `buildVersion` 안에서,
동일 `caseId` 의 mode 간 elapsed 비교. 그리고 기준 run 대비 delta / 비율.

### 14-2. 회귀 판정 threshold — **보류, 이 brief에서 정하지 않는다**

지시는 5% / 10% / 20% 같은 기준을 임의로 확정하지 말라고 했다. 그 지시를 따른다.
추가로, **이 저장소에는 이미 관련 규칙이 있으며 새 기준을 만들지 않고 그 것을 쓴다.**

- `AGENTS.md` 9항: "**측정 오차 범위의 차이는 개선으로 선언하지 않는다.**
  실행 횟수(5회 이상 권장)와 median/min/max/range 를 기록한다."
- `AGENTS.md` 9항 권장 상태값: `BASELINE` / `PASS` / `NOT ACCEPTED` / `REJECTED` /
  `DEFERRED` / `LOW PRIORITY` / `INCONCLUSIVE` / `SUPERSEDED`.
- `worklog` 의 `E-3B-BUG` ③ 항목은 **내가 임의로 넣은 `0.5 × framesPerSample`
  같은 단순 threshold 를 brief 금지 사안으로 제거한 실제 사례**다. 선례가 있다.

**결정**: S6-1 은 회귀 **후보**를 수치 delta + 모집단 통계로 나열하되
"회귀" 라고 판정하지 않는다. 판정 threshold 는 실제 반복 실행으로
측정 오차 범위를 산출한 뒤 별도 결정한다(22장 진입 조건).

### 14-3. 회귀 분석의 근본 한계 (S2-PERF 에서 승계)

`worklog` `S2-PERF` 가 다음을 이미 기록하고 **accepted** 로 두었다.

> "`scan()` 이 호출마다 폴더를 walk 하므로, N 개 파일은 N 번 walk = O(N²)."
> "Process isolation 과 OS filesystem cache 는 **uncontrolled** 로 기록한다."

**따라서 동일 machine · 동일 dataset 이라도 OS filesystem cache 상태, 외부 부하,
동시 실행 여부에 따라 elapsed 가 달라진다.** 이를 무시하고 코드 변경 전후 delta 를
"회귀"로 읽으면 측정 대상이 아닌 것을 측정하게 된다.

**결정**: S6 산출물은 회귀 판정이 아니라 **회귀 후보 + 측정 조건 경고**를 제공한다.
cache / 부하 통제 여부를 함께 출력하며, 통제되지 않은 조건임을 명시한다.

## 15. Cancellation / Failure 처리

- S2 aggregate precedence 를 **재정의하지 않는다.**
  `Cancelled > Failed > Success > Skipped` (`benchmark_core.h` 계약).
- **취소된 run 을 정상 success 표본에 포함하지 않는다.** `run_cancelled` 가 있는
  run 은 별도 분류하며, 그 안의 `SUCCESS` case 도 "완전 실행 조건" 표본임을 출력에
  명시한다. run 이 취소되었다면 case 가 commit 되었더라도 그 run 은
  정상 비교 기준 run 으로 **사용하지 않는다**(선행 조건 검사 사항).
- `SKIPPED` 는 측정값이 아니다. 분포 통계에 넣지 않고 건수만 보고한다.
- `FAILED` 는 `errorMessage` 을 함께 보존한다. 단, S3 계약을 따라 **`errorMessage`
  문자열을 fallback(cpu/gpu 우회) 추론에 사용하지 않는다.** S5 `A2` CPU FB
  REJECTED 결정이 이 규칙을 이미 확립했다
  (`docs/architecture/legacy/S5_REJECTED_CPU_FB_INDICATOR.*`).
- `errorMessage` 는 사람이 읽는 진단 문자열이다. 집계·그룹 키로 **사용하지 않는다.**

## 16. Timestamp 처리

### 16-1. 문제 (S5 에서 발견, S3 코드는 미변경)

`src/benchmark_store.cpp` 의 `benchmarkNowStamp()` 는 `localtime_s` 결과를
`strftime("%Y-%m-%dT%H:%M:%SZ")` 로 출력한다. 즉 **로컬 시각 값에 literal `Z` 를
붙인다.** UTC 가 아니다.

이 저장소의 timestamp 는 **네 갈래로 갈라져 있다.** 전부 실측 확인:

| 위치 | 형식 | 원본 |
| --- | --- | --- |
| `benchmark.cpp` `localTimeStr()` | `%Y-%m-%dT%H:%M:%S` | `localtime_s`, 무 timezone |
| `benchmark_core.cpp` `isoNow()` → `run.startedAt` | `%Y-%m-%dT%H:%M:%S` | `localtime_s`, 무 timezone |
| `benchmark_store.cpp` `benchmarkNowStamp()` → journal `timestamp` | `%Y-%m-%dT%H:%M:%SZ` | `localtime_s` + literal `Z` |
| `console_benchmark_cli.cpp` suite id | `YYYYMMDD-HHMM-SS` | **진짜 UTC** (`gmtime`) |

실측 동일 run 값:

```text
record timestamp      2026-10-01T05:02:38Z
run_started.startedAt 2026-10-01T05:02:38
run_finished.completedAt 2026-10-01T05:02:45
```

### 16-2. S6 처리 규칙

S6 는 이 문제를 **숨기지 않거야 자동으로 UTC 로 재해석하지도 않는다.**

1. **journal record `timestamp` 로 시간순 정렬하지 않는다.** `Z` 가 거짓이므로
   UTC 기준 비교가 되지 않는다. 대신 정렬 키는 다음을 쓴다.
   - 1순위 `suiteId`(진짜 UTC `YYYYMMDD-HHMM-SS`, 사전순 = 시간순)
   - 2순위 `runId`(`run-YYYYMMDD-HHMM-SS`)
   - 3순위 journal 내 물리적 line 순서 (append-onlyarante)
2. **`run wall duration` 은 `completedAt - startedAt` 로만 계산한다.**
   두 값은 **동일 formatter(localtime, 무 timezone)** 이므로 서로 비교 가능하다.
   단, 이 값은 **로컬 시간 단조 증가량**이지 절대 시각이 아니다. DST 경계를 넘는
   구간에서는 부정확할 수 있으며, 산출물에 그 사실을 표시한다.
3. **절대 시각을 사람에게 제시할 때 timezone 을 명시하지 않는다.**
   출력을 UTC 라고 부르지 않고, `startedAt`/`completedAt` 원본을 그대로 보여주며
   "로컬 시각, timezone 미기록" 이라고 함께 적는다.
4. journal `timestamp` 를 출력에 포함할 경우 **항상 timezone 표기 문제를 함께 출력**한다.

### 16-3. 선행/후속 분류

`docs/build-history/S5-verification.ko.md` 8장에 이미 S3 후속 부채로 기록되어 있다.
S6 는 이를 **S6 prerequisite 가 아니라 별도 S3 follow-up** 으로 분류한다.
이유: 위 16-2 규칙만으로 S6 분석은 **수행 가능**하기 때문이다(정렬 키와 duration
파생이 모두 다른 필드에서 얻어진다). timestamp 를 고쳐야 analysis 가 가능해지는 것은 아니다.
다만 추세/시계열 분석을 확장할 때마다 이 부채를 다시 검토한다.

## 17. 산출물 형식

### 17-1. 후보와 선택 근거

| 후보 | 평가 |
| --- | --- |
| **C++ 분석 엔진 + CLI** (`msf_core` + CLI 명령) | **채택.** `replayJournal()` 재사용으로 S3 recovery 규칙 중복 구현이 없다(9·10장). journal 파서를 PowerShell/Python 으로 다시 만들면 S3 규칙을 이중화한다. 배포가 exe 로 기존과 동일 |
| standalone exe | 부적합. 제품 빌드에 이미 포함되므로 별도 배포 이득이 없다 |
| PowerShell helper | `replayJournal()` 를 쓸 수 없다. journal 파서를 **재구현**하게 된다 → 10장 위반. 단, **CSV 후처리 전용 보조**로는 기존 `scripts/validation/` 관례와 맞으므로 허용 |
| Python | **부적합.** 저장소에 `.py` 0개, packaging 파일 0개, CI 가 `pwsh` 뿐. 새 언어·배포 의존성을 추가하는 것은 이 프로젝트에 대한 비용이 크고 이득이 없다 |
| Markdown 리포트 | 보관용으로 부적합. 사람용 요약 출력에 한정 |
| JSON / CSV export | **채택 (machine-readable)**. 다른 도구가 재사용할 수 있고, `scripts/validation/i3_*` 관례가 CSV 소비에 익숙하다 |

### 17-2. 결정

S6 는 **두 계층 출력**을 제공한다.

1. **machine-readable (필수)** — JSON 또는 CSV. 항상 `derived` 항목이 `derived`
   로 표시되고, provenance 가 포함된다.
2. **human-readable (필수)** — console 표. 두 출력은 **같은 계산 결과**에서 나오며
   값이 달라서는 안 된다(19장).

`summary.json` 을 덮어쓰거나 suite 디렉터리에 쓰지 않는다(5-2).

## 18. 재현성 (Reproducibility)

같은 입력 → 같은 출력이 기본 방향이다.

- **동일 journal 집합 + 동일 옵션 = 동일 바이트 출력**이어야 한다.
- 분석 결과에 **분석 시각을 포함하지 않는다** (출력이 실행마다 달라지면 안 된다).
  단, provenance 요구사항 때문에 실행 환경 정보가 필요하면 §21 의
  `analysisTimestamp` 처럼 **출력 스코프 밖 메타데이터**로 분리하고, 기본 출력에는
  넣지 않는다. 기본 출력에 넣을 경우 `SOURCE_DATE_EPOCH` 류의 고정값만 허용하지 않는다.
  → **결정: 기본 출력에 timestamp 를 넣지 않고, `--provenance` 로만 켠다.**
- 정렬은 **전부 결정적**(사전순 또는 명시적 키 순서). 해시 기반 container 순서
  (`std::unordered_map`) 순회 결과를 그대로 출력하지 않는다.
- 부동소수 출력은 고정 소수 자리. 반올림 규칙을 명시한다.
- 실행 시각·호스트명·사용자名 같은 환경 의존 값은 **결과에 섞지 않는다.**

## 19. Provenance

분석 결과마다 가능한 한 출처를 남긴다.

```text
sourceJournalRoot     — 읽은 storage root
sourceJournalFiles    — 읽은 runs.jsonl 목록 (경로 + SHA-256 또는 크기/line 수)
runIds                — 포함한 run
suiteIds              — 포함한 suite
datasetFingerprints   — 포함한 데이터셋
buildVersions         — 포함한 빌드
analysisToolVersion   — 분석 도구 버전 (CMake project VERSION)
analysisCommand       — 실행 옵션
```

- **`analysis timestamp` 와 `benchmark timestamp` 를 혼동하지 않는다.**
  전자는 `--provenance` 출력에만, 후자는 journal 원본값으로 별도 라벨을 붙인다.
- `runIds` / `datasetFingerprints` 를 생략하면 "어떤 데이터로 이 결론이 나왔는지"
  재현할 수 없으므로 **필수**다.

## 20. 테스트 전략

실제 구현 전에 정의한다. 케이스는 실제 journal contract 에 맞춘다.

### 20-1. Ingestion

- 정상 journal (run_started / mode_result×N / case_complete×N / run_finished)
- **incomplete tail** — 마지막 line 에 newline 없음 → 폐기, 이전까지 사용
- **commitless case** — `mode_result` 만 있고 `case_complete` 없음 → `incompleteCases`
- **cancelled run** — `run_cancelled` 존재, `completedAt` 없음
- **duplicate identical** — 무시
- **duplicate conflicting** — anomaly 보고, 첫 record 유지
- **mid-file corruption** — fatal, 자동 복구 금지
- **빈 journal / 없는 파일** — 오류가 아니라 "no data"
- 여러 run 이 한 journal 에 누적된 경우 `(runId, caseId)` 분리 (`S3-BUG` 재발 방지)
- `fatal` 발생 시 부분 결과를 정상 통계로 내지 않는지

### 20-2. Grouping

- 동일 `datasetFingerprint` + 동일 범위 → 같은 그룹
- 동일 fingerprint + 다른 `mediaScope` → **다른 그룹**
- 동일 fingerprint + 다른 `buildVersion` → 다른 그룹
- (git 가 없으므로) 동일 `buildVersion` 인 서로 다른 커밋을 구분하지 **못함**을
  **출력에 드러내는지** — 이 테스트가 있어야 8-2 갭이 사용자에게 보인다
- `datasetFingerprint` 빈 값 run → 그룹에서 제외 + 건수 보고

### 20-3. Aggregation

- case-level / mode-level / run-level 가 **섞이지 않는지** (모집단 라벨 검증)
- `n = 1` 인 그룹에서 통계가 억지로 계산되지 않는지
- `SKIPPED` 가 elapsed 통계에 들어가지 않는지
- 회귀 대상이 아닌 조건(다른 fingerprint / 다른 scope) 이 비교에서 제외되는지
- 부재 값이 0 으로 표시되지 않는지

### 20-4. Comparison

- 동일 dataset / 다른 build 비교
- 동일 build / 다른 mode 비교
- 동일 caseId 내 mode 간 delta
- 비교 가능한 run 이 0개 / 1개일 때의 동작
- **regression percentage 계산** (판정 아님, 수치 산출)

### 20-5. Reproducibility

- 동일 journal 집합 2회 실행 → **출력 바이트 동일**
- 파일 탐색 순서 / `unordered_map` 순서와 무관하게 동일한 출력
- 소수점 반올림 결정성

### 20-6. Empty / partial

- journal 0개 → "no runs", 오류 아님
- 비교 가능한 run 0개 → 명시적 no-comparable 상태
- 부분 run 만 존재 → 부분으로 표시
- 모든 case 가 incomplete → 집계 0 + 사유

## 21. 구현 순서 (S6 내부 작업 분해안)

**공식 roadmap node 를 새로 만드는 것이 아니라**, S6 하나의 내부 분해안이다.
roadmap 의 S0~S8 순서는 바꾸지 않는다.

```text
S6-1  Data source / schema contract
      journal 필드 인벤토리 고정, 8-2 갭의 journal 보강 여부 결정
      ingestion + provenance, 통계·출력 상태값 정의

S6-2  Ingestion / normalization
      replayJournal 재사용 wrapper, 경로/부재값/단위 정규화

S6-3  Grouping
      datasetFingerprint + scope + build + mode 축

S6-4  Aggregation
      case / mode / run 3개 모집단, n + mean/median/p95/min/max/range

S6-5  Cross-run comparison
      동일 caseId 비교, 조건 불일치 제외 규칙

S6-6  Regression 후보 / anomaly 보고
      수치 delta + 측정 조건 경고. 판정 threshold 는 S6-1 결정 이후

S6-7  전체 검증
      CPU/GPU CTest, 실제 journal 대상 CLI E2E, 재현성 검증
```

각 단계는 이전 단계가 실제 검증된 뒤 시작한다(23장).

## 22. 진입 조건 (Entry conditions)

S6-1 착수에 필요한 조건. **아래 ① 은 2026-10-01 에 해소되었다.**

1. ~~**`git` (또는 동등한 build identity) 이 journal 에 기록될 것.**~~ → **해소됨**
   - 해소 방법: S3 journal 의 `run_started` 에 `gitCommit` 을 additive field 로 추가했다.
     값은 S5 의 `MSF_BUILD_GIT` 을 그대로 사용하며, benchmark 실행 중 git command 를
     다시 호출하지 않는다. 따라서 journal 값과 실행된 binary 의 build provenance 가
     항상 일치한다.
   - **schema version 은 1 로 유지했다.** 근거는 세 가지이며 모두 실측 확인했다.
     (1) `kBenchmarkJournalSchemaVersion` 은 7개 위치에서 **쓰기만** 하고 읽는 곳이 없다.
     어떤 코드도 이 값으로 분기하지 않는다. (2) replay parser 는 알려진 키만 추출하며
     미존재 필드는 무시하고, 알 수 없는 필드를 거부하지 않는다. (3) 저장소의 기존 선례
     `benchmark_schema_test` "meta.schemaVersion is still 9 (**additive fields did not
     force a bump**)" 이 같은 판단을 이미 확정했다.
   - **실측 검증**: 실제 Console benchmark 실행의 `run_started.gitCommit` 과 해당
     binary 의 generated `MSF_BUILD_GIT` 이 **완전히 일치**했다. journal  run_started
     필드 집합을 비교한 결과 추가된 필드는 `gitCommit` 하나뿐이고 제거된 필드는 0개였다.
     provenance 없는 journal 은 `"unknown"` 으로 기록되며, 필드가 아예 없는 기존 29개
     journal 은 정상 replay 된다(기존 journal 을 수정하지 않음).
   - 남는 제약: `distance` 와 `resourcePolicy` 는 여전히 journal 에 없다(아래 ②).
     commit 수준 비교는 가능해졌으나 조건 동일성 축은 여전히 이 두 项이 비어 있다.
2. **`distance` 와 `resourcePolicy` 기록 여부 결정.** 8-2 참조. **미해소(DEFERRED)** —
   journal schema 를 더 확장하는 것은 별도 결정이 필요하며 이번 범위 밖이다.
3. **통제된 측정 환경 정의.** `S2-PERF` 가 OS filesystem cache 와 process
   isolation 을 **uncontrolled** 로 accepted 했다. 회귀 해석은 이 통제 조건 없이
   신뢰할 수 없다(14-3). **미해소** — 제품 결정이 필요하다.
4. **반복 실행 데이터.** `AGENTS.md` 9항은 실행 5회 이상을 권장한다. 현재 실제
   journal 은 S5 E2E 산출물이며 반복 실행 통계가 없다. threshold 결정은
   실측 반복 데이터 이후에야 가능하다(14-2). **미해소.**
5. **`run_cancelled` 실측 표본.** 현재 journal 에 0건(6-6). 취소 run 분석 규칙은
   소스 계약으로만 검증되어 있다. **미해소.**

## 23. 종료 조건 (Exit conditions)

S6 종료 판정 기준.

- `S6-1` ~ `S6-7` 각 단계가 실제 CPU/GPU build + CTest 로 검증되었다.
- 20장 테스트 범주가 구현되어 통과했다.
- 8-1 의 "지금 가능한 분석" 전 항목이 실제 journal 로 end-to-end 산출된다.
- 8-2 의 불가능 항목이 **출력에 명시적으로 드러난다**(조용히 빠지지 않는다).
- 재현성 검증(동일 입력 → 동일 출력)이 실제 journal 로 확인되었다.
- derived 값이 measured 로 표시된 사례가 없다.
- S2/S3/S5 계약과 journal schema 가 변경되지 않았다.
- legacy benchmark 가 변경되지 않았다.
- S3 timestamp 부채 상태가 문서에 남아 있다.

## 24. 보류 / 미결정 사항 (Deferred)

의도적으로 확정하지 않은 것. 임의로 정하지 않고 사유를 남긴다.

| 항목 | 상태 | 사유 |
| --- | --- | --- |
| 회귀 판정 threshold | **DEFERRED** | `AGENTS.md` 9항이 측정 오차 범위 밖의 차이를 개선으로 선언하지 않게 한다. 임의 수치 금지 선례가 `E-3B-BUG` 에 있음(14-2) |
| journal 에 `git` / `distance` / `resourcePolicy` 추가 | **DEFERRED** | S3 schema 영역. S6 가 변경하지 않는다. 대안 3가지 22장 ① 참조 |
| S6 산출물 포맷 최종 선택 (JSON vs CSV) | **DEFERRED** | §17-2 는 "machine-readable + human-readable 2계층"만 확정했고 JSON/CSV 선택은 구현 시 실제 소비자를 확인한 뒤 한다 |
| suite 자동 실행 | **DEFERRED** | roadmap §28/§30 이 S6 에 포함하지만(1장), 실행은 S2 경로를 써야 하므로 별도 brief 분리 권고 |
| 통계 분포 함수 (p95 알고리즘) | **DEFERRED** | 근사/정확 중 선택은 모집단이 작을 때 결과가 다르다. 실제 데이터 분포 확인 후 결정 |
| `run_cancelled` 분석 규칙 세분화 | **DEFERRED** | 실측 표본 0건(6-6) |
| timestamp S3 수정 | **별도 S3 follow-up** | S5 검증 8장에 이미 기록. S6 는 16-2 규칙으로 우회 가능 |

## 25. 구현 착수 금지 확인

이번 단계는 **설계 문서만** 산출한다. 다음은 하지 않는다.

- S6 분석 CLI / 엔진 구현
- Python 또는 PowerShell 분석 스크립트 작성
- journal schema 변경 및 bump
- S2 / S3 / S5 / GUI / benchmark 실행 코드 변경
- performance tuning, GPU/CUDA 변경
- 테스트 구현

이번 단계 산출물:

```text
S6 설계 조사 (이 문서 6·8·11·16장)
+
S6 implementation brief (KO/EN)
+
roadmap / progress 의 S6 brief 준비 상태 기록
```
