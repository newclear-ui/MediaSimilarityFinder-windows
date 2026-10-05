# S — Validation / Benchmark Track (S0~S8 통합)

**이 문서는 S 트랙 전체의 통합 계약이다.** 단계별 상세 계약은 기존 문서를
그대로 유지한다. 이 문서는 단계 문서를 대체하지 않고, 통합 관점에서 지금 어디에
있는지와 단계 간 규칙을 한 번에 확인하기 위한 진입점이다.

| 계층 | 문서 |
|---|---|
| 트랙 전체 설계(권위) | `docs/architecture/benchmark-telemetry-roadmap.{ko,en}.md` |
| 단계별 실행 계약(권위) | `implementation-briefs/S4-*`, `S5-*`, `S6-*` |
| 현재 상태 1페이지 | `docs/node-status-gate-matrix.{ko,en}.md` |
| 실제 변경 증거 | `docs/build-history/<version>.{ko,en}.md` |
| 판단 계보 | `docs/worklog/0.9.4.{ko,en}.md` |

## 1. S 트랙의 위치

S 트랙은 **본선 A~H 와 병렬 트랙**이다. 본선 Gate 에 의존하지 않으며,
검증/측정 계층만 담당한다. `S0 → S1 → S2 → S3 → S4 → S5 → S6 → S7 → S8` 순으로
각 단계를 닫은 뒤 다음 단계로 이동한다.

각 단계 종료 순서: 소스 변경 → CPU/GPU 빌드 → CTest → 실행 검증 → 문서 갱신 →
필요 시 Build History → commit

## 2. 단계 정의와 현재 상태

| 단계 | 설계 정의 (roadmap 30절) | 선행 | 구현 | 검증 | 현재 판정 |
|---|---|---|---|---|---|
| **S0** | Run/Suite, mode, media scope, Resource Budget, journal, isolation, cancellation, terminal 계약 확정 | — | 완료 | PASS | `CLOSED` |
| **S1** | Console entry, help/version, headless scan, media/resource 옵션 | S0 | 완료 | PASS | `CLOSED` |
| **S2** | 파일 단위 AUTO→CPU→GPU-max 실행 core, 공유 mode context, per-file 결과 event/journal 계약 | S1 | 완료 | PASS | `CLOSED` |
| **S3** | benchmark 실행 산출물 분리, append-only journal, crash-safe/partial persistence, summary 파생 | S2 | 완료 | PASS | `CLOSED` |
| **S4** | GUI benchmark 용도 고정(3줄 요약) → 이후 semantic reset 으로 `Detailed Logs` | S3 | 구현 완료 | acceptance 미완료 | `IN PROGRESS` |
| **S5** | Console benchmark CLI 및 interactive/non-interactive terminal renderer | S4 계약 | infra 완료 | product benchmark 미실행 | `NOT CLOSED` |
| **S6** | Suite 내 반복 측정, fingerprint 변경 감지, 병렬/순차 비교 정책 | S5 | gate 준비 | 실측 미실행 | `NOT STARTED` |
| **S7** | help / usability / exit code / verbose | S6 | 미착수 | — | `NOT STARTED` |
| **S8** | CPU/GPU 빌드, 테스트, CLI/GUI 실행, JSON/journal 정합, 최종 release gate | S7 | 미착수 | — | `NOT STARTED` |

S0~S3 을 한 번에 닫은 근거와 각 단계 상세는 `docs/development-progress.{ko,en}.md`
의 S 트랙 일정 항목을 참조한다.

## 3. 단계 간 유지 규칙 (전 단계 공통)

- **두 번째 검색 엔진을 만들지 않는다.** S 계층은 S2 `BenchmarkRunner` +
  injected `BenchmarkExecutor` 로 production 검색 경로를 그대로 재사용한다.
- **미측정 값을 0 으로 기록하지 않는다.** `MeasureState`(measured / not_measured /
  not_available / partial / failed / fallback) 로 구분한다.
- **Normal Search Index 와 Benchmark index/cache 를 분리한다.** 실행 대상 폴더에
  benchmark 산출물을 쓰지 않는다.
- **벤치마크 실행을 자동화한다.** 실행 결과는 실제 측정값으로만 취급하며 단순 선형
  추정을 금지한다. Cancellation 후 부분 Suite 결과를 보존한다.
- **threshold 는 정의되기 전까지 미정으로 남긴다.** 조건 충족만으로 통과로 쓰지 않는다.

## 4. 의미 경계 (가장 자주 오해되는 지점)

```text
GUI  Search/Update -> [상세 로그] -> TelemetryRecorder / UserDiagnostic
CLI  --benchmark                   -> BenchmarkSession / BenchmarkRunner
                                     -> TelemetryRecorder / Benchmark
```

- **Benchmark 와 Telemetry 는 동의어가 아니다.**
- GUI 는 Console benchmark 이력을 자동으로 읽지 않는다.
- Console 은 GUI 상세 로그를 benchmark 이력으로 자동 주입하지 않는다.
- 진단 수집만을 위해 GUI 에 Benchmark Run/Stop/Pause UI 를 새로 만들지 않는다.
- `runs.jsonl` 이 복구 원본이며 `summary.json` 은 파생값이라 권위가 없다.

## 5. 현재 초점과 블로커

```text
활성 단계   S4  (GUI Detailed Logging acceptance) + 본선 product acceptance audit
직접 원인   구현된 Search/Index/Comparison semantics 의 제품 acceptance 미완료
다음        S5 실제 dataset product benchmark -> S6 controlled measurement gate
미해결      S6 threshold 미정의. 실측 dataset 필요
```

S4 가 CLOSED 로 올라가지 않는 이유는 이 환경이 headless 여서 결과 다이얼로그의
실제 화면 렌더링을 시각 검증하지 못했기 때문이다. 이는 **하지 않음**으로 기록하며
**통과**로 주장하지 않는다.

## 6. 트랙 경계

```text
S4 최종 GUI visual/save acceptance   DEFERRED
S5 product benchmark 실행            DEFERRED (S4 acceptance 의존)
S6 measurement gate 실측             DEFERRED (실측 dataset 필요)
S7 / S8                               NOT STARTED
본선 F/G/H                            본선 규칙에 따름 (본선과 독립)
```

진행률 % 를 쓰지 않는다. 이 트랙은 구현이 끝나도 acceptance 가 남거나,
acceptance 가 남아도 실측이 없으면 다음 단계로 못 간다.
