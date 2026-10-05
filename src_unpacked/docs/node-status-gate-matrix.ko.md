# Node Status / Gate Matrix — 0.9.4 개발선 통합 상태판

기준: `0.9.4.45` / HEAD 동기화 · CPU CTest 102/102 · GPU CTest 103/103
최종 갱신: 2026-10-04

## 이 문서의 역할

**이 문서는 사실의 원본이 아니라 인덱스다.** 상태와 판정만 한 장에 모으고,
근거 수치와 상세 계약은 각 원본 문서로 연결한다.

| 계층 | 원본 문서 | 역할 |
|---|---|---|
| 방향 · 의존관계 | `development-roadmap.{ko,en}.md` | Node 정의, 선행조건, 경계 |
| 현재 실제 위치 · blocker | `development-progress.{ko,en}.md` | 실행 상태, 회복 이력 |
| 실행 계약 | `docs/implementation-briefs/<Node>-*.{ko,en}.md` | 설계/구현 계약 (활성 Node) |
| 기술 구조 | `docs/architecture/*.md` | 아키텍처 |
| 변경 증거 | `docs/build-history/<version>.{ko,en}.md` | 버전별 실제 변경·측정 |
| 판단 계보 | `docs/worklog/0.9.4.{ko,en}.md` | 왜 채택/기각했는가 |

같은 숫자와 판정을 여러 문서에 복사하지 않는다. 아래 표에서 의아한 항목이 있으면
**해당 원본 링크로 내려갈 것.**

## 읽기 순서 (OpenCode / ChatGPT 에이전트 세션 시작 시)

1. **이 문서** — 전체 Node/Gate의 현재 진입점
2. **`development-progress.{ko,en}.md`** — 현재 실행 큐 · 현재 위치 · blocker · 완료 주요 이정표
3. `development-roadmap.{ko,en}.md` — 방향과 선행조건
4. 활성 Node의 `docs/implementation-briefs/<Node>-*.md`
5. 필요한 `docs/architecture/*.md`
6. 관련 `docs/build-history/` + `docs/worklog/`
7. source / test

> **`src_unpacked/AGENTS.md` 작업 규칙**: 소스를 변경하기 전에 위 1~4를 반드시 읽고,
> 현재 Gate가 허용하지 않는 Node를 먼저 시작하지 않는다.
> 특히 `development-progress`의 Active Build Queue가 현재 작업의 실행 우선순위를 결정한다.
> **진척 동기화 규칙**: Node/Build의 구현·검증·판정·Gate·다음 작업이 변경되면 `development-progress.ko.md` / `.en.md`를 즉시 갱신한다. 이 문서는 완료된 상세 증거의 저장소가 아니라 현재 실행 상태의 최신 진입점이어야 한다.

## 판정값 정의 (진행률 %를 쓰지 않는다)

이 프로젝트는 구현 80%인데 정확성 실패, 코드 100%인데 acceptance 미완료인 상태가
실제로 발생한다. 따라서 % 대신 상태값과 Gate로 기록한다.

| 상태값 | 의미 |
|---|---|
| `NOT STARTED` | 착수하지 않음 |
| `DESIGNED` | 설계 확정, 구현 미착수 |
| `IN PROGRESS` | 구현/검증 진행 중 |
| `PASS` | 검증 통과 |
| `CONDITIONAL` | 조건부 채택. 전제 조건 미충족 시 production 채택 불가 |
| `DEFERRED` | 보류. 재개 조건이 별도로 존재 |
| `NOT ACCEPTED` | 현재 조건에서 성공하지 못함. 폐기가 아님 |
| `CLOSED` | 종료. 다음 Node로 진행 가능 |
| `COMPLETE` | 종료. 회귀 가능성은 남음 |

---

## 본선 — Main Development Nodes

선행관계: `A → B → C → D → I → E → F → G → H`

| Node | 목적 | 사전 설계 | 선행조건 | 구현 | 빌드/검증 | 현재 판정 | 정체 원인 | 해결/결론 | 다음 Gate | 상세 |
|---|---|---|---|---|---|---|---|---|---|---|
| **A** | Foundation / 용어 / instrumentation | 완료 | — | 완료 | PASS | **CLOSED** | — | GPU 추상화 `GpuBackendKind{Auto,Cuda,Cpu}`, `MSF_ENABLE_GPU`+`MSF_GPU_BACKEND`, build 명명 분리, `MeasureState` 6종, `decodedFrames`/`sampledFrames` 분리, 미측정=0 금지 | B | `implementation-briefs/A-foundation-terminology-instrumentation.*`(참조 계약), `build-history/0.9.4.0.*`, `architecture/resource-scheduling.*`, `architecture/gpu-backend-roadmap.*` |
| **B** | Adaptive Scheduler | 완료 | A | 완료 | PASS | **CLOSED** | 고정 50:50 배분 | capability + calibration + 실시간 부하 + throughput + queue + transfer cost 기반 동적 배분. GPU 사용률 수동 설정 제거 | C | `implementation-briefs/B-adaptive-scheduler.*` |
| **C** | Calibration / INI Performance Profile | 완료 | B | 완료 | PASS | **CLOSED** | calibration 수명주기 경계 | C1 Profile Foundation → C2 Initial → C3 Opportunistic → C4 Gate. live runtime state가 항상 우선 | D | `implementation-briefs/C-calibration-profile.*` |
| **D** | Pipeline / Queue Optimization | 완료 | C | 완료 | PASS | **CLOSED** | 실제 병목 비중이 0.044 %에 불과 | bounded walker queue, transfer stall 해소. D3+D4 addressable ceiling 0.044 % → 추가 최적화 보류 | I | `implementation-briefs/D-pipeline-queue.*` |
| **I** | Analyze / Matching Performance | 완료 | D | 완료 | PASS | **COMPLETE** | 전체 wall time의 98.62 %가 analyze | I-2 production 경로 완료. decode 94.90 %가 실제 병목이며 decode 1/20 (open+factory 89.54 %) | E | `implementation-briefs/I-decode-once-resize-twice.*`, `I-shared-wic-source*.{ko,en}` |
| **E** | Adaptive Video Decode Planner | 완료 | I | 완료 | **정확성 실패** | **CLOSED** | sparse exactness | E-2A/E-2B "exact" 수치가 자기참조였음(같은 `av_seek_frame`+`avcodec_flush_buffers` 손실 공유). production from-zero 포함 첫 측정에서 4K H.264 1개 실제 불일치, sparse는 end-to-end +17.38 % 느림 → **`ExactnessPolicy::RefuseAll` 기본값**, production 전 파일 Sequential | F | `build-history/0.9.4.42.*`, `implementation-briefs/E-*.{ko,en}` |
| **F** | Hardware Video Decode Backend | pre-register | E | NVDEC 조사 | **exactness FAIL** | **CONDITIONAL** / **PRODUCTION ADOPTION = NO** | NVDEC mismatch. dataset 14개 중 13개가 file-start IDR 인데 **1360x0808 가 20/20 mismatch**, 확인된 IDR 에서 재시작해도 **20/20**. 1080x1920 도 1/20. NVDEC 은 frame당 **2.1배 느림**(CPU 4.438 s vs NVDEC 9.397 s, 870f) | `exactnessVerified` 를 `Safe` 의 필수조건으로 도입. `Unsafe`/`Unknown` 은 CPU fallback 으로 collapse. **F-2 production integration 금지.** 1360x808 root cause 는 `INCONCLUSIVE` | G / H | `implementation-briefs/F-random-access-safety.*`, `F-hardware-video-decode-backend.*`, `build-history/0.9.4.43.*` |
| **G** | Additional GPU Backends | pre-register | F | 미착수 | — | **DEFERRED** | F 미해결 | Intel Level Zero / AMD HIP/ROCm / Vulkan 은 독립 backend 후보. **아키텍처는 NVIDIA 전용으로 고정하지 않음** | F 해결 후 | `development-roadmap.ko.md` Node G |
| **H** | Regression / Stability / Performance Validation | — | G | 미착수 | — | **NOT STARTED** | 선행 F 미해결 | — | — | `development-roadmap.ko.md` Node H |

> **Node G/H 관련 유지 규칙**: 다른 hardware decode backend 를 추가하려면 codec /
> profile / pixel-format / bit-depth / capability 맵, 시간축 seek 와 frame mapping,
> decode 오류 fallback 이 필요하다. **단계적 기능 추가가 아니라 착수 조건이며,
> NVDEC 문제로 인해 이 조건을 충족한 backend 는 아직 없다.**

---

## S 트랙 — Validation / Benchmark Track

본선 A~H 와 **병렬 트랙**이다. 서로의 Gate 에 의존하지 않으며, 검증/측정 계층만
담당한다. 트랙 통합 계약은 `docs/implementation-briefs/S-validation-benchmark-track.{ko,en}.md`,
설계 권위는 `docs/architecture/benchmark-telemetry-roadmap.{ko,en}.md` 다.

선행관계: `S0 → S1 → S2 → S3 → S4 → S5 → S6 → S7 → S8`

| Node | 목적 | 선행조건 | 구현 | 빌드/검증 | 현재 판정 | 정체 원인 | 해결/결론 | 다음 Gate | 상세 |
|---|---|---|---|---|---|---|---|---|---|
| **S0** | 설계 확정 / pre-register | — | 완료 | PASS | **CLOSED** | — | Run=한 mode 의 한 측정, Suite=같은 dataset/media scope 의 mode 묶음. Search Index 와 Benchmark index/cache 분리 | S1 | `architecture/benchmark-telemetry-roadmap.*` |
| **S1** | Console entry foundation | S0 | 완료 | PASS | **CLOSED** | — | CLI 파싱 계층. production 검색 엔진과 별도 엔진 없음 | S2 | `development-progress.*` |
| **S2** | Run / Suite benchmark core | S1 | 완료 | PASS | **CLOSED** | — | ordering/status 집계/취소/결과 보존. injected `BenchmarkExecutor` 로 production scan 경로 재사용 | S3 | `architecture/benchmark-telemetry-roadmap.*` |
| **S3** | Benchmark storage isolation | S2 | 완료 | PASS | **CLOSED** | — | `runs.jsonl` append-only journal. `summary.json` 은 파생이며 권위 아님. `kBenchmarkSchemaVersion` 9 | S4 | `implementation-briefs/S4-*`, `S5-*` |
| **S4** | GUI Detailed Logging | S3 | 구현 완료 | **acceptance 미완료** | **IN PROGRESS** | product acceptance 미완료 | S4 semantic reset 반영. `TelemetryPurpose{UserDiagnostic,Benchmark}` 로 GUI/CLI 분리. GUI 문자열 `[Benchmark]`→`[Detailed Logs]`. **CLOSED 로 올리지 않음** | S5 | `implementation-briefs/S4-gui-diagnostic-logging.*`, `architecture/gui-diagnostic-logging.*` |
| **S5** | Console benchmark execution | S4 계약 | infra 완료 | **product benchmark 미실행** | **NOT CLOSED** | product acceptance 의존 | `runConsoleBenchmark()` 가 S3 `BenchmarkSession` + S2 `BenchmarkRunner` 재사용. `QCoreApplication` 사용으로 platform plugin 불필요. **실측 benchmark 미실행** | S6 | `implementation-briefs/S5-console-benchmark-execution.*` |
| **S6** | Data-mining automation / measurement gate | S5 | gate 준비 | **실측 미실행** | **NOT STARTED** | 실측 dataset 필요 | threshold 는 **여전히 미정**. 보고된 조건만 만족할 뿐 임계 미정의 | S7 | `implementation-briefs/S6-data-mining-automation.*`, `S6-measurement-gate.*` |
| **S7** | Help / usability | S6 | 미착수 | — | **NOT STARTED** | — | — | S8 | — |
| **S8** | Full verification / release gate | S7 | 미착수 | — | **NOT STARTED** | — | — | — | — |

> **Benchmark 와 Telemetry 는 동의어가 아니다.** GUI `[상세 로그]` 는
> `TelemetryRecorder/UserDiagnostic`, CLI `--benchmark` 는
> `TelemetryRecorder/Benchmark` 다. 서로 자동 주입하지 않는다.

---

## 현재 초점 / Current Focus

```text
활성 Node        S4  (GUI Detailed Logging 실제 화면 acceptance)
다음             color_thumb R1 (0.9.4.47) → XMP coverage validation → S5 benchmark → S6 gate
직접 원인        제품 acceptance 결함 DEFECT-A/B 는 0.9.4.46 에서 수정 완료.
                 남은 조건은 실제 GUI 화면 검증뿐이며 이는 headless 로 종결 불가
```

**S4 acceptance 가 막히지 않는 이유**: 이 환경은 headless 이며, 결과 다이얼로그의
실제 화면 렌더링은 시각적으로 검증하지 못했다. 이는 "하지 않음"으로 기록하며
"통과"로 주장하지 않는다.

## 보류 / 금지 / 재개 조건

```text
sparse seek production 재개          DEFERRED — production-parity 증명이 있는 codec 없음
NVDEC production adoption            PRODUCTION ADOPTION = NO — F-1 CONDITIONAL
F-2 production integration           금지 — IDR-start fixture 가 없음을 확인
1360x808 mismatch root cause          INCONCLUSIVE — video cohort 로 재검토
Intel/AMD/Vulkan backend              착수 조건 미충족
color_thumb production correction    NOT PERFORMED — audit 만 완료 (R1 fixture + skip/pass 가 다음)
XMP production acceptance            CONDITIONAL — fixture PASS, coverage/regression 없음
vcpkg 이동                            하지 않음 (project-local 유지)
```

## 상태값이 다른 살아 있는 후보

이 표에는 **판정 상태만** 적는다. 수치·조건·재검토 시점은 원본에 있다.

| 후보 | 상태 | 원본 |
|---|---|---|
| `ExactnessPolicy::RefuseAll` (E) | 채택 — production 전 파일 Sequential | `build-history/0.9.4.42.*` |
| sparse seek | `NOT ACCEPTED` — 재검토 조건 명시됨 | `build-history/0.9.4.42.*` |
| `exactnessVerified` 필수조건 (F-1) | `CONDITIONAL` | `implementation-briefs/F-random-access-safety.*` |
| XMP Orientation Fallback | `CONDITIONAL` | `build-history/0.9.4.45.*`, `implementation-briefs/I-xmp-orientation-fallback.*` |
| 제품 acceptance (Search/Index/Comparison) | `NOT ACCEPTED` → **결함 수정 완료 (0.9.4.46)** | `build-history/0.9.4.46.*`, `worklog/0.9.4.*` acceptance audit 항목 |
| `color_thumb` no-FFmpeg classification | `DESIGNED` (audit 완료) | `implementation-briefs/I-color-thumb-no-ffmpeg-classification.*` |
| nvcc 경고 release gate 등록 | 미결정 | `worklog/0.9.4.*` |
| 콘솔 출력 전용 회귀 테스트 | 미추가 | `worklog/0.9.4.*` |

## 버전 불변 확인

`kEngineVersion 1.5.0` · `kDatabaseVersion 1.0.3` · `kBenchmarkSchemaVersion 9` ·
`kCacheFormatVersion 9` — 0.9.4.45 기준 변경 없음.
