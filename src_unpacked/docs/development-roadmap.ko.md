# Development Roadmap — 0.9.4 개발선

## 문서 네이밍 및 구조 규칙

문서의 정식 이름·위치와 역할 분리는 `docs/document-naming.ko.md` / `.en.md`가 기준이다.

핵심 규칙:
- `development-roadmap.ko/.en.md`, `development-progress.ko/.en.md`는 고정 이름을 사용한다.
- Implementation Brief는 `<Node>-<topic>.ko.md` + `.en.md`를 사용한다.
- Build History는 `<version>.ko.md` + `.en.md`를 사용하며 이름을 바꾸지 않는다.
- Work Log는 `docs/worklog/<development-line>.ko.md` + `.en.md`로 개발선 단위로 누적한다. 버전 범위를 파일명에 넣지 않는다. **Performance / Tuning Experiment Index** 를 포함해 실험 계보와 살아 있는/기각된 후보를 연결한다.
- Architecture는 주제 중심 이름을 사용하고, 역사적 snapshot이 필요한 경우에만 버전 suffix를 허용한다.
- 새 문서/이동 시 KO/EN 쌍, 내부 링크, `STRUCTURE.md`, `llms.txt`를 함께 갱신한다.

자세한 규칙: [Document Naming and Structure Rules](document-naming.ko.md)

## 문서 계층

Roadmap과 세부 implementation brief는 역할을 분리합니다.

- **Roadmap**: 전체 방향, 의존관계, Node 경계, 변경 관리 규칙.
- **Progress**: 현재 실제 Node, blocker, 검증 상태, recovery history.
- **Implementation Brief**: 현재 활성 Node를 구현하기 위한 집중된 기술 계약. 단계별 범위, 경계, telemetry, 종료 조건을 기록합니다.
- **Build History**: 실제 버전에서 무엇을 변경했고 어떻게 검증했는지의 증거. 성능 실험의 **상세 수치·실행 조건·기각 근거·향후 재검토 조건**을 보존하는 곳이며, 성공 여부와 무관하게 모든 튜닝/프로파일링 실험을 기록한다.

현재 B/C/D 세부 문서:

- `docs/implementation-briefs/B-adaptive-scheduler.ko.md / .en.md`
- `docs/implementation-briefs/C-calibration-profile.ko.md / .en.md`
- `docs/implementation-briefs/D-pipeline-queue.ko.md / .en.md`

이 문서는 0.9.4 개발선의 상위 개발 방향과 실행 순서를 정의합니다.

핵심 원칙은 개발 단계와 빌드 번호를 분리하는 것입니다.

- A, B, C는 개발 방향과 의존관계입니다.
- B1, B2, B3은 현재 단계에서 발생한 문제의 진단·수정·회귀검증 분기입니다.
- 버전 번호는 단계에 미리 배정하지 않습니다.
- 검증된 코드 상태가 만들어질 때 실제 상황에 맞춰 0.9.4.0 → 0.9.4.1 → 0.9.4.2 → ... 순으로 증가합니다.
- 설계 방향은 Roadmap이, 실제 위치는 Progress가, 실제 코드와 테스트 증거는 build-history가 담당합니다.

## 개발 순서도

START
  |
  v
[A] Foundation / Terminology / Instrumentation
  |
  v
[B] Adaptive Scheduler
  |
  v
[C] Calibration / INI Performance Profile
  |
  v
[D] Pipeline / Queue Optimization
  |
  v
[I] Analyze / Matching Performance        <-- D8b 근거 이후 삽입 (Node I 참조)
  |
  v
[E] Adaptive Video Decode Planner          <-- 종결 (E-1/E-2/E-3 완료, E-3C/E-4 정리)
  |
  v
[F] Hardware Video Decode Backend
  |
  +--> NVIDIA NVDEC (이번 F의 유일한 조사·실험·구현 범위)
  |
  v
[G] Additional GPU Backends
  |
  +--> Vulkan
  +--> AMD HIP/ROCm
  +--> Intel Level Zero
  |
  v
[H] Regression / Stability / Performance Validation
  |
  v
NEXT DEVELOPMENT LINE

각 단계는 이전 단계의 종료 조건을 통과한 뒤 다음 단계로 이동합니다.

## 문제 발생 시 분기

B에서 문제가 발생한 경우의 예:

[B]
 |
 +--> [B1] 재현 / 원인 분석
 |       |
 |       v
 |     [B2] 수정 / 보완
 |       |
 |       v
 |     [B3] 회귀검증
 |       |
 |       +---- 실패 ----> [B1]
 |       |
 |       +---- 통과 ----> [B]
 |
 v
[C]

공통적으로 A → A1 → A2 → A3 → A, B → B1 → B2 → B3 → B와 같은 구조를 사용합니다.

전체 방향을 바로 바꾸지 않고 현재 단계 안에서 원인 규명 → 수정 → 검증을 먼저 수행합니다. 구조적 문제가 확인되면 Roadmap과 Progress를 함께 변경합니다.

## Node A — Foundation / Terminology / Instrumentation

상위 GPU 명칭을 vendor-neutral하게 정리하고 GPU ON/OFF 정책, build naming, benchmark instrumentation의 공통 기반을 만듭니다.

주요 내용:
- GPU 수동 사용률 제어 제거
- CPU Resource Mode 유지
- MSF_ENABLE_GPU / MSF_GPU_BACKEND 준비
- build-windows-cpu / build-windows-gpu
- 현재 CUDA를 concrete backend로 유지
- CPU fallback 유지
- measured / not_measured / not_available / partial / failed / fallback 상태
- decodedFrames / sampledFrames 분리
- scheduler / queue / transfer / decoder 계측

종료 조건:
- 상위 GPU와 실제 backend 명칭이 분리됨
- GPU ON/OFF 구조 명확
- benchmark의 미측정=0 오해 제거
- CPU fallback 및 기본 회귀 테스트 통과

## Node B — Adaptive Scheduler

B는 **실효 처리능력 기반의 CPU/GPU 작업 배분 정책**을 담당합니다. 고정 50:50이 아니며 GPU utilization 자체를 최적화 목표로 삼지 않습니다.

세부 구현:
- `docs/implementation-briefs/B-adaptive-scheduler.ko.md`
- B1 최소 배분 → B2 throughput → B3 live load → B4 안정화 → B5 cost → B6 Resource Mode → B7 최종 gate

B의 경계는 "작업을 어디에 얼마나 배분할 것인가"이며 실제 queue/worker/pipeline 구현은 D에 둡니다.

## Node C — Calibration / INI Performance Profile

C는 기존 benchmark의 CalibrationTelemetry 기반을 재사용하면서 Performance Profile과 짧은 calibration lifecycle을 확장합니다.

핵심 경계:

- Profile은 다음 실행의 initial estimate입니다.
- Live runtime measurement가 항상 Profile보다 우선합니다.
- Profile identity가 맞지 않으면 재사용하지 않거나 confidence를 낮추고 재측정합니다.
- C는 B의 Scheduler policy를 바꾸지 않습니다.
- C는 D의 queue/worker topology와 F의 hardware decode 구현을 선행하지 않습니다.
- 아직 측정할 수 없는 항목은 not_measured / not_available / partial / failed / fallback 상태로 명시합니다.

설계 흐름:

```
Profile load
   ↓
identity / freshness check
   ↓
usable ───────────────→ initial estimate
   │                           ↓
   └→ short calibration → B Scheduler
                               ↓
                         live measurement
                               ↓
                    repeated deviation?
                               ↓
                    opportunistic calibration
```

세부 구현 계약:
- `docs/implementation-briefs/C-calibration-profile.ko.md`

단계:
- C1 Profile Foundation — INI model/store, identity, confidence, stale/invalid, atomic persistence, Scheduler initial-estimate interface
- C2 Initial Calibration — CPU/GPU fingerprint·batch, transfer/resize, CPU decode baseline, Profile creation, Scheduler initial estimate
- C3 Opportunistic Recalibration — runtime deviation, repeated trigger, candidate update, confidence
- C4 Calibration Gate — lifecycle, precedence, CPU/GPU parity, failure/partial state, persistence, regression

C1 이전에는 실제 calibration 실행을 구현하지 않습니다.

## Node D — Pipeline / Queue Optimization

D는 **신규 pipeline/queue 설계**입니다. Scheduler가 결정한 작업을 실제로 어떻게 흘려보낼지 담당하며 barrier, worker starvation, queue imbalance, transfer stall, 불필요한 serialization을 줄입니다.

첫 단계에서는 기존 구조를 크게 바꾸지 않고 관측성을 확보한 뒤 단계적으로 최적화합니다.

세부 구현:
- `docs/implementation-briefs/D-pipeline-queue.ko.md`

B와 D는 역할을 섞지 않습니다. B는 allocation policy, D는 execution pipeline입니다.

**Node D 결과 (0.9.4.22 기록):** D1a/D1b 관측, D2 barrier 검토, D3-Minimal
bounded walker queue, D4a 백엔드 내부 타이밍, D8a/D8b 재현+규모 dataset
완료. 2,700 파일 실측 기준 `walker maxDepth` 964/4096 · `blocked_ticks` 0,
GPU batch 는 엔진 wall 의 0.026 %. **D3+D4 addressable ceiling 은 0.044 %**
이므로 D4b overlap 과 Full D3 topology 는 가정이 아니라 근거로 보류한다.
D 는 구조를 만들었고, 남은 비용은 그 범위 밖에 있다.

## Node I — Analyze / Matching Performance

**변경 관리 기록 (Roadmap 자체 규칙에 따라 기재).**

1. **Progress 에 기록한 문제:** 0.9.4.22 시점 stage 분해에서 `analyze` 가
   엔진 wall 의 **98.62 %** 를 차지했고, walk 1.40 %, image stage 0.59 %,
   GPU batch 0.04 % 였다.
2. **기존 경로와 원인:** D 가 파이프라인 최적화를 계속할 것으로 기대했다.
   실측 결과 D 가 소유한 작업은 벽시계의 0.044 % 뿐이어서 기존 경로에
   여지가 없었다. 원인: 지배적 단계가 **어떤 노드도 소유하지 않았고**,
   그 단계는 최종 매칭/그룹화(`MediaPipeline::analyze`)로 D brief 범위 밖이
   의도된 설계였다.
3. **Roadmap 갱신:** D 와 E 사이에 Node I 삽입.
4. **양쪽(KO/EN)에 기록한 이유:** 병목은 실측된 것이지만 소유자가 없었다.
   소유자를 두지 않으면 100배 이상의 실측 격차가 있어도 0.9.4 라인에
   의미 있는 성능 작업이 남아 있지 않게 된다.
5. **새 경로로 계속.**

목표:
스캔을 지배하는 단계를 관측 가능하게 만든 뒤, 그 비용을 줄인다 —
단, 어떤 search verdict 도 바꾸지 않고.

범위:
- `analyze` 내부 stage 분해 (index build / candidate scan / image SSIM
  검증 / video temporal)
- verify 단계 카운터: 호출, 캐시 적중, 디코드 미스, SSIM 계산 횟수
- 위 분해로 원인이 식별된 **뒤에만** 그 단계 최적화

범위 밖:
- search verdict semantics, threshold, SSIM 알고리즘 정의
- D 가 이미 측정하고 보류한 항목

세부 구현:
- `docs/build-history/0.9.4.23.ko.md` / `.en.md` (D9a pre-register)

## Node E — Adaptive Video Decode Planner — **종결**

필요 이상으로 decode하는 비용을 줄입니다.

- planner: sequential / hybrid / sparse seek
- backend: Software FFmpeg / hardware decoder
- sampled frames / decoded frames
- seek count / latency
- decode throughput
- keyframe/GOP cost
- conversion / resize
- fallback

### 종결 상태 (0.9.4.42 기록 기준)

sparse seek는 **production 채택이 거부**되었고, 그 결과로 실제 효율화 성취는 없습니다.
결과는 "sequential 을 확정했다"입니다. 다만 이 노드에서 얻은 정확한 판정 기준과
검증 구조는 그대로 남습니다.

- E-1 / E-2 / E-3 완료
- **E-3C: 별도 roadmap Stage로 승격하지 않으며, F로 이관하지 않고 Node E 종결 범위에서 정리합니다.**
- **E-4: production integration + end-to-end validation 역시 Node E 종결 범위에서 정리합니다.**
  `ExactnessPolicy::RefuseAll` 로 sparse production path가 도달 불가능하므로,
  통합할 "성격의" 경로가 남아 있지 않습니다. E-4 를 sparse 재도입으로 읽어서는 안 됩니다.

### Node E 완료/종결 조건 (0.9.4.42 기준 충족)

1. production sequential decode 를 기준선으로 확정
2. sparse sampling 의 production exactness 확보 여부 검증
3. production baseline 과 실제 결과가 달라질 수 있음을 확인
4. sparse production adoption 거부
5. `ExactnessPolicy::RefuseAll` 유지
6. 관련 correctness 문제 수정 및 regression 검증 완료
7. 추가 sparse adoption 을 위한 증거가 없는 상태에서는 재도입하지 않음
8. 향후 재검토는 새로운 production-parity 증거가 확보된 경우에만 허용

**E-4**: sparse production path 가 존재하지 않으므로 **별도의 sparse integration 단계로
수행하지 않고 Node E 종결에 흡수한다.**

**참고 사항**: E-3C 는 향후 F 의 architecture 설계에 참고가 될 수 있으나(참고만),
F 의 작업 항목으로는 이관하지 않습니다.

종결 판단의 근거와 재검토 조건은 `docs/build-history/0.9.4.42.*` 에 있습니다.
기존 E-2A/E-2B 의 exactness 수치는 **자기참조**였으므로 production exactness 근거로
재사용하지 않습니다.

## Node F — Hardware Video Decode Backend

우선 NVIDIA NVDEC을 실제 backend 후보로 연결합니다.

Software FFmpeg은 기준/폴백 경로로 유지하며 codec/profile/pixel-format/bit-depth/capability를 확인합니다. 초기화·seek·frame mapping·decode 실패는 파일 단위 fallback으로 처리합니다.

**이번 F 의 실제 조사·실험·구현 범위는 NVDEC 단일입니다.** 다른 hardware decode
backend 의 실제 조사와 검증은 이번 F 범위에 포함하지 않습니다.

다만 **architecture 는 NVIDIA 전용으로 고정하지 않습니다.** backend abstraction 은
향후 Intel/AMD 등 hardware decode backend 를 추가할 수 있는 방향을 고려합니다.
그 구현·검증은 **이번 F 의 범위가 아닙니다.**

### F 진행 상태 (0.9.4.43)

- **F-1 Random-Access Safety Contract: `CONDITIONAL` / `PRODUCTION ADOPTION = NO`**
- **F-2 production integration 은 금지.** F-1 에서 **정상 조건(IDR-start fixture)에서도
  exactness 가 깨졌다**(IDR-start H.264 4개 중 2개 mismatch). 정확성이 확보되지 않은 상태에서
  production 통합으로 넘어가지 않는다.
- 확립된 계약:
  ```text
  RandomAccessSafe / RandomAccessUnsafe / RandomAccessUnknown
  Unsafe → CPU fallback,  Unknown → CPU fallback
  ```
- **핵심 규칙: structure(키프레임 시작)는 필요조건일 뿐 충분조건이 아니다.** 기록된
  exactness 증명이 있어야 `Safe` 이다. capability 와 safety 는 별도 축이다.
- 미확정 사항: 1360x808 mismatch 의 root cause(`INCONCLUSIVE`). 4K IDR-start H.264
  fixture 부재.
- 상세: `docs/build-history/0.9.4.43.*`, `docs/implementation-briefs/F-random-access-safety.*`

## Node G — Additional GPU Backends

abstraction이 안정화된 뒤 독립적으로 검토합니다.

- Vulkan
- AMD HIP/ROCm
- Intel Level Zero

실제 장비 검증 전에는 지원 완료로 표시하지 않습니다.

## Node H — Regression / Stability / Performance Validation

CPU-only, GPU OFF, GPU ON/AUTO, low-end simulation, acceleration-not-beneficial, external CPU/GPU load, hardware decode success/fallback, mixed workload, cancellation/partial, accuracy parity, stability, cache compatibility를 함께 검증합니다.

최종 기준은 GPU utilization 하나가 아니라 정확성 + end-to-end throughput + fallback correctness + 안정성 + observability입니다.

## 버전 번호 규칙

Roadmap Node와 버전 번호는 같은 개념이 아닙니다.

Roadmap: A → B → C → D → I → E → F → G → H
Version: 0.9.4.0 → 0.9.4.1 → 0.9.4.2 → 0.9.4.3 → ...

예를 들어 B 내부에서 B1/B2/B3 문제 해결을 거쳐 하나의 검증 상태가 만들어질 때 다음 버전으로 증가할 수 있습니다.

버전은 결과를 나타내고, Roadmap Node는 방향을 나타냅니다.

## 문서 역할

- development-roadmap.*: 전체 개발 방향과 순서도
- development-progress.*: 현재 단계와 문제 해결 상태
- build-history/*: 실제 버전의 코드 변경과 검증 결과
- architecture/*: 기술 영역별 상세 설계
- AGENTS.md: OpenCode 작업 규칙

OpenCode는 Roadmap과 Progress를 먼저 확인한 후 현재 단계의 상세 프롬프트를 적용합니다.

## 변경 관리

방향을 바꾸어야 할 정도의 문제가 생기면 Progress에 원인을 기록하고 Roadmap과 KO/EN 문서를 함께 수정합니다.


## Benchmark / Console CLI 교차 인프라 트랙

Benchmark/Telemetry는 새로운 Roadmap Node를 추가하지 않고 공통 인프라 트랙으로 관리한다. 특히 현재 F-1에서 NVDEC production adoption이 금지된 상태이므로, Benchmark/Console CLI의 정리는 F-2 통합을 의미하지 않는다.

확정 계약:

- benchmark mode: AUTO / CPU 단독 / GPU 최대화
- media scope: images / videos / all
- Console canonical selector: --media images|videos|all
- Run / Suite 분리
- GUI는 source folder당 최신 mode 3개만 보존
- Console은 장기 누적 보존
- normal Search Index와 benchmark index/cache 분리
- source folder는 human-readable label + short stable id로 저장
- Run은 source identity, dataset fingerprint, media scope, scheduler 설정, 환경, 실패/fallback 상태를 저장
- GPU 최대화는 GPU-only가 아니며 필수 CPU 작업과 fallback을 유지

### 빌드 / 구현 스케줄 정책

버전 번호는 미리 배정하지 않는다.

S0 설계/pre-register
→ S1 Console entry foundation

**S2 구현 상태 (0.9.4.43)**: Run/Suite benchmark core **완료**. Case=파일 1개, modeResults[]에 requested/effective 분리, aggregate precedence Cancelled > Failed > Success > Skipped. 실행 주입 경계(BenchmarkExecutor)로 production scan 경로를 재사용하며 별도 검색 엔진 없음. 단위 테스트 59 checks, CPU CTest 87/87, GPU CTest 88/88.
per-file 측정은 ignoredPaths 로 구현하며 scan() 이 매 호출 폴더를 walk 하므로 **O(N²)** 이다(S2 는 correctness 우선으로 허용, 대규모 최적화는 후속). process 격리·OS filesystem cache 는 **통제 불가**.
아직 미구현: terminal renderer(S5), public benchmark CLI 옵션, AUTO/CPU/GPU-max 최종 정책, NVDEC 통합.
(JSONL durable journal 과 storage isolation 은 S3 에서 구현되어 S2 runner 에 실제 연결되었다.)
**S3 구현 상태 (0.9.4.43)**: Benchmark storage isolation **완료 (runner 연결 + recovery/summary E2E 검증)**. journal schema 1 은 legacy `kBenchmarkSchemaVersion`(9) 와 **분리**되어 있고, 둘은 함께 버전이 올라가지 않는다.
배선: `onRunStarted → run_started`, `onCaseComplete → mode_result xN + case_complete(commit marker)`, `onRunFinished → run_finished / run_cancelled`, 그리고 summary 재생성. `BenchmarkRequest::runId` 로 run 을 **실행 전에** 식별할 수 있게 하여 suite lock → runtime 준비 → journal open → run_started 순서를 지킨다.
검증: journal 51 checks, store 51 checks, 통합 137 checks(E2E 정상/취소/실패/recovery/lock/격리 + 실제 엔진 1회). CPU CTest 90/90, GPU CTest 91/91.
확인된 사실: 취소된 case 도 commit marker 를 남긴다(S2 가 그 case 를 callback 으로 전달하고 실제 commit 지점에 도달했으므로). 취소 전에 시작되지 않은 파일은 case 자체가 없어 journal 에 기록도 없다.
recovery: 마지막 개행 없는 tail 은 폐기, commit 없는 mode 기록은 incomplete 로 분류, 동일 recordId 중복은 무시, payload 불일치 중복은 anomaly 로 보고 **첫 record 유지**, **중간 record 손상은 fatal 이며 이후를 추측 복구하지 않는다**. `summary.json` 은 journal replay 결과일 뿐 authoritative 가 아니며 삭제 후 journal 에서 재생성된다.
**통제 불가**: durability 는 append+flush 이며 fsync/power-loss 보장은 아니다. process 격리·OS filesystem cache·O(N²) scan 은 그대로다.
**S4 구현 상태 (0.9.4.43)**: GUI benchmark integration **구현 완료 / 검증 완료, 단 CLOSED 아님**.
Phase 3-1 저장 계층(`src/benchmark_gui_store.*`), Phase 3-2 worker(`gui/benchmark_worker.*`),
Phase 3-3 MainWindow 배선(mode checkbox 3개 + 실행 버튼 + 중지 + 상태 표시 + 직렬 실행 게이트) 구현.
**모드 checkbox 는 실행 선택**이며 유효 조합 7개, 최소 1개 필수. `BenchmarkRunner::run(request, selectedModes)` 를 **단일 호출**하고 mode 별 분리 실행은 하지 않는다. 파일별 mode 순서 유지.
**`benchTgl_` 은 기존 legacy telemetry checkbox 로 유지**되었고 mode selector 로 재사용되지 않았으며, benchmark 실행은 별도 실행 버튼으로 제공된다(스캔 중 benchmark / benchmark 중 스캔 상호 배적, §4-13). Pause/Resume 은 benchmark 에 없고 Cancel/Stop 만 존재한다.
저장: `Benchmark/GUI/<label>_<shortid>/{auto.json,cpu.json,gpu-max.json}` + `runtime/run-<id>/<mode>/`. atomic replace, **실행된 mode 만 갱신**하고 미선택 mode snapshot 은 보존한다. mode 별 aggregate 는 `modeResults[]` 로 재계산하며 Case aggregate 를 복사하지 않는다.
인스턴스 간 상호 배제는 **S3 `BenchmarkSuiteLock` 재사용**이며 별도 locking system 을 만들지 않는다. 이미 실행 중이면 실행을 시작하지 않고 기존 snapshot 을 변경하지 않는다.
진행 표시는 **"완료 k/N · 마지막 <파일>"** 이다. S2 가 case 완료를hook 으로만 주므로 **진행 중인 파일/mode 는 알 수 없어 추측 표시하지 않는다.**
검증: storage 110, worker 35, UI 34, **실제 엔진 GUI E2E 40** checks. CPU CTest 94/94, GPU CTest 95/95.
실제 E2E 관측값: CPU 빌드 `gpu-max` = **SKIPPED**, GPU 빌드 `gpu-max` = **SUCCESS**. production Index 오염 0건(전후 내용 비교), 스캔 폴더 오염 0건.
**Resource Policy 전달 해결**: `BenchmarkRequest::resourcePolicy`(optional, additive)를 추가하고 executor 가 전달된 policy 에서 출발한다. 미지정 시 기존 S2 동작(엔진 기본 policy + mode 별 gpuEnabled) 그대로 유지되어 기존 호출자 무영향. GUI 는 MainWindow 가 `make_policy()` 로 이미 해석한 `policy_` 를 그대로 전달한다. 실제 E2E 에서 toolbar preset "Maximum 90%" → snapshot `cpuPercent: 90` 기록 확인. **남는 제약**: `gpuEnabled` 는 mode 가 결정하므로 GUI `gpuEnabled_` 는 AUTO/GPU-max 실행에 영향 없음(S2 규칙 유지).
**datasetFingerprint 해결**: worker 가 fingerprint 미지정 시 기존 `msf::computeDatasetFingerprint(root).fingerprint` 를 verbatim 사용. 새 해시·새 직렬화 형식 없음, `DatasetFingerprint` 가 공개 멤버 구조체라 accessor 추가 불필요. worker 스레드에서 계산해 UI 비차단. 실제 E2E 에서 64자 hex 값이 기록되고 `computeDatasetFingerprint(root).fingerprint` 와 완전히 동일함을 확인(`3793e510…`).
**S4 상태: CLOSED.** 남는 것은 구현 결함이 아닌 제품 결정 3가지다: ① O(N²) walk 감수 여부 ② CPU 빌드 `SKIPPED` 표현의 UX 적정성 ③ GUI GPU 토글을 benchmark 에 반영할지 여부. 그 밖에 `selectedBenchModes()` 는 private 유지(간접 검증).
자세한 판정과 S5 진입 조건: `docs/build-history/S4-phase3-4-verification.ko.md` / `.en.md`

**S1 구현 상태 (0.9.4.43)**: Console Entry Foundation **완료**. 파서 단위 테스트 40 checks, CPU CTest 86/86, GPU CTest 87/87.
CLI 는 --help / --version / --smoke / --scan <folder> [--media images|videos|all] 만 지원하며, 인자 없음 실행은 기존 GUI를 그대로 연다. CLI 경로는 MainWindow 를 만들지 않는다.

**S5 구현 상태 (0.9.4.43)**: Console benchmark execution **기능 구현 완료, 자동/비대화형 E2E 검증 완료**.
- S5-1 `f4c3fdd`: `--benchmark <folder>` + `--mode/--suite/--log-dir/--log` 파싱, canonical 정규화(`AUTO → CPU → GPU-max`). `command_line_test` 40 → **95** checks.
- S5-2 `fcace68`: Qt-free 표시 전용 renderer. presentation model, S2 enum 재사용, optional 관측 필드, TTY/non-TTY 분리, 폭 처리(no-wrap). **100** checks.
- S5-3 `842ba01`: `runConsoleBenchmark()` orchestration → S3 `BenchmarkSession` → S2 `BenchmarkRunner` → 제품 scan 경로. `MSF_BUILD_GIT` 추가. `SetConsoleCtrlHandler` → atomic flag → `BenchmarkRequest::isCancelled`. **32** checks.
- **E2E 중 발견한 결함 수정**: `src/scanner.cpp` 의 `Scanner::scan_stream()` 이 `FileState.kind` 를 설정하지 않아 `Unknown` 으로 남았고, benchmark media filter 가 무력화되었다. 기존 `isVideoPath()` 규칙을 그대로 재사용해 1줄로 복구(새 classifier 없음). 영향 추적 결과 **production indexing/search 영향 없음**(`MediaSearchEngine` 이 `kindOf(path)` 로 자체 재계산). 초기 "제품 전체 구분 손상" 추정은 오류였으며 정정함.
- 실제 `--media` (dataset Image 8 / Video 2 / Total 10): images → **8** case `Image=8`, videos → **2** case `Video=2`, all → **10** case `Image=8, Video=2`. 옵션 순서 독립성 확인.
- 실제 E2E: 기본 3-mode exit 0 / Cases 10 / Records 42, journal sequence `run_started → (mode_result+case_complete)×N → run_finished`, suite.json·journal·summary 3곳 suiteId 일치, `--log-dir` 격리, `--log` text 산출물(ANSI 없음), non-TTY ESC 없음, `--suite ..\..\evil` exit 2 거부.
- CPU CTest **96/96**, GPU CTest **97/97**, 양쪽 build exit 0, stale object 없음.
- **NOT RUN**: 실제 TTY ANSI repaint, 실제 Windows Ctrl+C trigger. 검증 환경에 Windows console 이 없었음(`GetConsoleWindow() == NULL`). 프로세스 kill 로 대체하지 않음. `SetConsoleCtrlHandler` 등록·atomic flag wiring·`isCancelled` 연결과 deterministic cancellation 테스트(137 checks)는 확인됨.
- A1 보존(live callback 미추가, CURRENT FILE = 완료 case 만, ETA 미사용), A2 보존(CPU FB 미구현).
- S3 `benchmarkNowStamp()` 가 `localtime_s` 결과에 literal `Z` 를 붙이는 기존 timestamp 표기 불일치는 **이번 S5 에서 수정하지 않음** → S3 후속 부채.
자세한 판정과 실제 측정값: `docs/build-history/S5-verification.ko.md` / `.en.md`

**S6 설계 상태 (0.9.4.43)**: Data-mining Automation brief **작성 완료, 구현 착수 전**. CLOSED 가 아니다.
- brief: `docs/implementation-briefs/S6-data-mining-automation.ko.md` / `.en.md`
- roadmap 이 S6 에 부여한 정의(§28/§30)는 3축이다: suite 자동 실행 / fingerprint 검증 / 비교 요약. 이 brief 는 **2·3번(검증·비교/마이닝)** 만 다루고, 1번(자동 실행)은 별도 brief 분리 를 권고하며 정의하지 않는다.
- **설계 조사에서 확인된 최대 제약**: journal 에 `git` / `resourcePolicy` / `distance` 가 **실측 0건**이다(29개 journal / 862 line 전수 키 스캔). 원인은 S5-3 가 `MSF_BUILD_GIT` 를 generated header 에만 추가하고 journal(S3 schema)에는 쓰지 않았기 때문이다. 결과적으로 `buildVersion` 만으로는 **같은 버전의 다른 커밋을 구분할 수 없으며**, roadmap §25(저장 요구)와 §28/§30(S6 핵심 정의)이 요구하는 "build 간 비교" 를 현 상태로는 완수할 수 없다. 이를 S6 최대 리스크로 entry condition ① 에 명시했다.
- 지금 가능한 분석(8-1)과 **불가능한** 분석(8-2)을 분리해 기록했다. 없는 필드를 추정으로 채우지 않았다.
- S3 journal 파싱을 다시 만들지 않고 `replayJournal()` 을 재사용하기로 했다. PowerShell/Python 으로 재파싱하면 S3 recovery 규칙이 이중화되기 때문이다.
- **Python 은 이 프로젝트에 존재하지 않으므로**(`.py` 0개, packaging 파일 0개, CI `pwsh`) 분석 도구 후보에서 제외했다. 스크립트 관례는 PowerShell 이나 journal 파싱에는 사용할 수 없다.
- 회귀 판정 threshold 는 **정하지 않았다**(DEFERRED). `AGENTS.md` 9항(측정 오차 범위 밖의 차이는 개선 선언 금지, 5회 이상 권장)이 이미 기준이며, `worklog` `E-3B-BUG` 에 임의 threshold 를 제거한 선례가 있다.
- 회귀 분석의 근본 한계: `S2-PERF` 가 OS filesystem cache 와 process isolation 을 **uncontrolled** 로 accepted 했다. S6 산출물은 회귀 판정이 아니라 회귀 후보 + 측정 조건 경고다.
- S3 timestamp(`localtime_s` + literal `Z`) 문제는 S6 prerequisite 가 아니라 **별도 S3 follow-up** 으로 분류했다. 정렬 키를 `suiteId`(진짜 UTC)로 바꾸고 duration 은 `completedAt - startedAt` 만 쓰면 우회 가능하기 때문이다.
- suite 자동 실행, 출력 포맷 최종 선택(JSON vs CSV), p95 알고리즘, `run_cancelled` 분석 세분화도 **DEFERRED** 로 기록했다.

→ S2 Run/Suite benchmark core
→ S3 Benchmark storage isolation
→ S4 GUI benchmark integration
→ S5 Console benchmark execution
→ S6 Data-mining automation
→ S7 Help/usability
→ S8 Full verification/release gate

각 단계는 소스 변경 → CPU/GPU 빌드 → CTest → CLI/GUI 실행 검증 → 문서 갱신 → 필요 시 Build History → commit 순으로 닫는다.

세부 계약과 저장 레이아웃은 docs/architecture/benchmark-telemetry-roadmap.*를 기준으로 한다.

## Benchmark Console 최종 설계 보완 — 2026-09-30

기존 benchmark S0~S8 일정을 다음 최종 계약으로 보완한다.

- 파일 단위로 AUTO → CPU → GPU-max를 실행하고 파일별 결과를 즉시 저장한다.
- 분석용 중간 결과를 benchmark mode 사이에 공유하지 않는다.
- Ctrl+C 취소와 부분 결과 보존을 기본 지원한다.
- Console CPU resource는 기존 Maximum / High / Balanced / Gaming / Manual(10–90%) 정책을 재사용한다. 기본값은 Balanced(55%)로 권장한다.
- Balanced 결과를 선형 보간한 Maximum 값은 실제 benchmark 결과로 간주하지 않는다.
- Interactive Console 상단은 3줄 고정 정보 영역으로 압축하며 자동 줄바꿈하지 않는다.
- Target 경로가 길면 화면에서 middle ellipsis로 축약하고 JSON에는 원본을 저장한다.
- 상단에는 Target / Scope / IMG·VID 진행률 / Mode / CPU Resource / GPU / Distance / Suite ID / Build·Git 식별자를 우선 배치한다.
- 하단에는 CURRENT FILE의 파일명과 AUTO/CPU/GPU-max 상세 결과를 충분히 표시한다.
- 완료 이력은 파일당 한 줄의 compact form으로 누적한다.
- TTY와 non-interactive 출력은 분리하되 동일 journal/JSON을 사용한다.
- 기존 benchmark source/schema는 legacy baseline으로 영구 보존한다.

이 결정으로 S2는 benchmark execution + per-file journal contract, S5는 Console renderer + CLI execution을 담당한다.

