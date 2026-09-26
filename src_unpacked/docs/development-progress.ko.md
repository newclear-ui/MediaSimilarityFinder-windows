# Development Progress — 0.9.4 개발선

## 문서 목적

이 문서는 development-roadmap.ko.md의 **현재 실제 실행 상태**를 기록합니다.

Roadmap은 개발 방향의 뼈대이고, Progress는 실제 위치, 문제, 회복 분기를 기록합니다.

## 현재 상태

| 항목 | 상태 |
| --- | --- |
| 기준 코드 | 0.9.4.7 |
| 공식 보존 기준선 | 0.9.2.32 |
| 개발선 | 0.9.4 |
| 현재 노드 | B — Adaptive Scheduler (활성 substep B6, B7 미착수) |
| 현재 단계 | B6 구현·검증 완료 → B7 진입은 brief 검토 후 |
| 현재 버전 | 0.9.4.7 |
| GPU 구현 기준 | NVIDIA CUDA |
| CPU fallback | 유지 |
| 프로젝트-local vcpkg | 유지, 이전하지 않음 |

## 개발 순서 상태

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
[E] Adaptive Video Decode Planner
 |
 v
[F] Hardware Video Decode Backend
 |
 v
[G] Additional GPU Backends
 |
 v
[H] Regression / Stability / Performance Validation

현재 문서 작업은 A의 기준선을 만든 것이며, 0.9.4.0에서 Node A 소스 구현과 검증을 완료했다(상세: docs/build-history/0.9.4.0.ko.md).

## 완료된 준비 작업

### A0 — Development framework

완료:
- Development Roadmap KO/EN
- Development Progress KO/EN
- CPU/GPU Adaptive Resource Scheduling 설계 정리
- GPU Backend Roadmap 정리
- Benchmark / Telemetry Roadmap 정리
- AGENTS.md에 0.9.4 작업 규칙 반영
- STRUCTURE.md / llms.txt / README 계열에서 새 문서 체계 반영

이 변경은 문서 구조 변경이며 0.9.3.19의 scheduler/backend 소스 자체를 변경한 것은 아닙니다.

### A1 — Node A 소스 구현 (→ 0.9.4.0, 검증됨)

- `GpuBackendKind { Auto, Cuda, Cpu }` + `backendName()`. Auto는 CUDA 장치
  존재 시 CUDA, 아니면 CPU fallback으로 귀결. 미구현 backend 없음.
- `MSF_ENABLE_GPU` / `MSF_GPU_BACKEND` 정식화. `MSF_ENABLE_CUDA`는
  deprecated 별칭 유지. `windows-gpu` 프리셋 + `build_windows_gpu.ps1`
  신설, `build-windows-gpu` clean configure(기존 `build-windows-cuda` 보존).
- 툴바 `GPU %` 스핀박스 제거, GPU ON/OFF 체크박스만 잔류. CPU 프리셋
  의미와 deprecated 내부 `gpuPercent` cap은 동결.
- Benchmark `schemaVersion: 1` + `runId`, `MeasureState`
  (measured/not_measured/not_available/partial/failed/fallback),
  `decodedFrames`/`sampledFrames` 분리, `stages`·`scheduler`·
  `calibration`·취소/부분/파일 진행 기록, 디스크 가용성 샘플링 기간
  래치. 기존 키 전부 유지.
- 검증: CPU 62/62, GPU 63/63(clean 트리, CUDA discovery 포함),
  양쪽 `--version`/`--smoke`, 확장된 `benchmark_test`·
  `gpu_backend_policy_test`. 검색 의미 불변(엔진 1.5.0·DB 1.0.3·캐시 v9).
- 증거 구분(0.9.4.0 완료 후 점검, 0.9.4.1에서 확장): 자동 빌드/CTest/CLI-smoke
  = PASS, 실제 windowed 실행 = PASS(OS 핸들 + 버전 타이틀 확인),
   슬롯 경로 workflow = 자동 PASS(`scan_workflow_test`: 실제 MainWindow
   슬롯 경로 offscreen 구동, 모달 벤치마크 closer, 양쪽 트리 1그룹 렌더
   단언), 인간 조작(실제 클릭·화면 판독)의 자동화 검증 = NOT_VALIDATED
   (자동화 환경 한계) — 단, 개발 주체가 실제 Windows 세션에서 직접
   실행→검색→리포트 표시가 정상 동작함을 확인했다고 보고함. 자동화
   미검증과 사용자 직접 확인은 구분해서 기록한다.

### B6 — Resource Mode Integration (→ 0.9.4.7, 검증됨)

- `paramsForMode()`: 모드별 (cpuFloor, holdMs, killAt, relieveAbove).
  Maximum 민첩/자기 우선, Gaming 조기 yield/늦은 복귀/긴 hold,
  Balanced·Custom = B4값, 명시 `setHoldMs()` 우선, 미설정(-1)은 모드
  기본값.
- 엔진은 스캔당 `schedHw.mode = policy_.mode` 1줄 연결. Manual CPU
  상한은 upstream 유지(스케줄러 측 Manual == Balanced, 테스트됨).
- 검증: CPU 64/64, GPU 65/65(B6 추가분: 모드표·모드 분기·비대칭
  relief·모드별 hold·Manual==Balanced), `scan_workflow_test`로 UI parity,
  양쪽 `--version`/`--smoke`. 검색 의미 불변. GPU 스위트 초회 빌드
  직후 8건 실패 후 동일 바이너리 2연속 그린(환경 경합, 회귀 아님.
  실패명 미캡처는 운용 교훈).

### B5 — Transfer / Workload Cost (→ 0.9.4.6, 검증됨)

- 관측 경로 전용 total-cost 규칙:
  `effGpu = 1/(1/effGpu + transferSecPerUnit)`. 전송량(1032B)은
  패킹 확정값, 대역폭은 Node C 교정 대상 coarse 12000MB/s.
  workload 편차는 관측 rate 내재, 명시 모델은 Node E 몫.
- `GpuBackend::kTransferBytesPerUnit`으로 VRAM 매직넘버 통일,
  파이프라인 1줄 전달, 스캔당 1회 주입.
- 검증: CPU 64/64, GPU 65/65(`scheduler_test` 내 B5 추가분),
  `scan_workflow_test`로 UI parity, 양쪽 `--version`/`--smoke`.
  검색 의미 불변. 실측 전송(약 86ns)은 오늘 사실상 0 — 구조 우선,
  가중치는 후속.

### B4 — Stability Control (→ 0.9.4.5, 검증됨)

- 관측 rate·부하의 SMA-4(feed-if-known-else-clear, baseline은 정적).
  kill-band hysteresis(kill 0.02 이하·relief 0.05 초과·사이 이전 유지).
  발표 판단에 minimum hold 10초 기본. `decide()` 직후 첫 변경 면제.
  adjustments는 발표 기준.
- 스케줄러 내부 전용: 엔진 게이트가 발표 판단을 읽으므로 실행
  안정화가 엔진 변경 없이 상속된다.
- 검증: CPU 64/64, GPU 65/65(B4 추가분: SMA glide·hold 동결/해제·
  5단계 kill-band·unknown lane clear), `scan_workflow_test`로 UI parity,
  양쪽 `--version`/`--smoke`. 검색 의미 불변.

### B3 — Live Load Awareness (→ 0.9.4.4, 검증됨)

- `SchedulerHardware`에 시스템 부하 입력(cpu/gpu/mem + known 플래그,
  queue depth는 D1 예약으로 동승·판단에서 무시). headroom 규칙: CPU
  하한 0.05, GPU 하한 없음, mem은 기록만. 완전 GPU kill 시
  `external_load_throttle` + edge 카운트.
- 엔진은 스캔당 `SystemLoadMonitor` 1개를 decide/재평가 지점에서
  갱신한다. `externalLoadThrottling`은 edge 카운터로 채우고,
  `throttlingEvents`는 B4+ 몫으로 0 유지.
- 검증: CPU 64/64, GPU 65/65(`scheduler_test` 내 B3 추가분),
  `scan_workflow_test`로 UI parity, 양쪽 `--version`/`--smoke`.
  검색 의미 불변. 스무딩·hysteresis 없음(B4), transfer/workload 없음(B5).

### B2 — Runtime Throughput Feedback (→ 0.9.4.3, 검증됨)

- `ThroughputWindow`(30초 recent-window, 2표본 미만·만료 시 unknown,
  smoothing 없음). `SchedulerHardware`에 관측 이미지 경로 rate 추가.
  양쪽 실측·양수면 `observed_throughput` 배분, 아니면 baseline 경로.
  한쪽 미상은 0이 아니라 폴백한다.
- 엔진은 배치마다 완료 이미지를 해시 backend별로 귀속하고 재평가 전
  rate를 갱신한다. 비디오는 제외(디코드 측은 C/E). `currentCapacities()`
  는 실측 rate 또는 baseline을 보고한다.
- 검증: CPU 64/64, GPU 65/65(`scheduler_test` 내 확장, 수량 불변),
  `scan_workflow_test`로 UI parity, 양쪽 `--version`/`--smoke`.
  검색 의미 불변.

### B1 — Minimal Adaptive Allocation (→ 0.9.4.2, 검증됨)

- `CpuGpuScheduler`(`src/scheduler.h/.cpp`): baseline 전용 입력(CPU
  스레드·GPU ON/OFF·가용성·SM 수), 비례 배분, reason 코드, 기존 단계
  지점에서 2000ms 재평가 주기. moving average·hysteresis·transfer·
  workload·외부 부하 모델 없음. pipeline·worker·queue 변경 없음.
- 엔진 배선: 스캔당 1회 `decide()`, 이미지/비디오 게이트가 판단값을
  읽는다(기존 플래그와 동작 동일). `finishScan`에서 scheduler 섹션을
  `measured`로 기록한다(shares·backend·조정 횟수·이미지+비디오
  fallback 합).
- 검증: CPU 64/64, GPU 65/65(신규 `scheduler_test` 포함: B1 판단표·
  주기·telemetry JSON 양쪽 통과), `scan_workflow_test`로 UI parity,
  양쪽 `--version`/`--smoke`. 검색 의미 불변(엔진 1.5.0·DB 1.0.3·캐시 v9).

## Node B/C/D 상세 설계 상태

Node B와 D는 **신규 설계**, Node C는 **기존 profile/benchmark 개념의 부분 재사용 + 확장**으로 분류한다.

상세 구현 계약은 `docs/implementation-briefs/`에 둔다. Roadmap에는 방향과 경계만 두고 세부 구현 단계를 반복하지 않는다.

현재 B/C/D 문서:
- `docs/implementation-briefs/B-adaptive-scheduler.ko.md / .en.md`
- `docs/implementation-briefs/C-calibration-profile.ko.md / .en.md`
- `docs/implementation-briefs/D-pipeline-queue.ko.md / .en.md`

현재 구현 대상은 B이며 B1부터 작은 검증 단위로 진입한다. D의 pipeline 내부 구조를 B에서 선행 구현하지 않는다.

## Node A — Foundation / Terminology / Instrumentation

### 현재 목표

- 상위 GPU 명칭 일반화
- GPU ON/OFF만 UI에 남기고 수동 GPU 퍼센트 제어 제거
- Resource Mode CPU 정책 보존
- MSF_ENABLE_GPU / MSF_GPU_BACKEND 구조 준비
- build-windows-gpu 명칭 정리
- 기존 CUDA backend를 concrete backend로 유지
- benchmark schemaVersion
- measurement states
- scheduler / queue / transfer / decoder telemetry
- decodedFrames / sampledFrames 분리
- cancellation / partial 상태
- 기존 검색 정합성 보존

### Node A 종료 조건 (0.9.4.0에서 전부 검증 → 다음 게이트 B)

- CPU-only 정상 — Release 빌드 PASS, CTest 62/62 PASS
- GPU OFF 정상 — CPU fallback 경로 무변경, 모니터/CPU 스위트 통과
- GPU ON에서 기존 CUDA 경로가 backend abstraction을 통해 실행 —
  clean `build-windows-gpu` 트리에서 `cuda_backend_test` 통과
- 수동 GPU utilization UI 제거 — 툴바 `GPU %` 스핀박스 삭제
- benchmark의 unmeasured=0 문제 제거 — `MeasureState`, `null` + 상태, 래치
- benchmark instrumentation이 검색 결과를 변경하지 않음 — 판정 경로
  무변경, 양쪽 트리에서 parity 스위트 통과
- 기본 regression tests 통과 — CPU 62/62, GPU 63/63
- build tree / CMake naming 정리 — `MSF_ENABLE_GPU`/`MSF_GPU_BACKEND`,
  `windows-gpu` 프리셋, `build_windows_gpu.ps1`, clean 트리
- 문서/코드/테스트 버전 일치 — 전부 0.9.4.0

## Recovery branch 기록

문제가 생기면 버전 번호가 아니라 현재 노드의 하위 작업으로 기록합니다.

예:

A
|
+-- A1: CMake migration error
|
+-- A2: option compatibility fix
|
+-- A3: CPU build regression
|
+-- A4: GPU smoke
|
+-- A5: final A gate
|
+---- fail --> A1/A2/...
+---- pass --> B

각 하위 작업은 다음을 기록합니다.

- 증상
- 재현 조건
- 원인
- 해결책
- 변경 파일
- 테스트
- 실패했던 시도
- 해결 후 결과
- 다음 gate 영향

### B6 구현 과정의 기록

- B6 (명칭): "Manual"은 UI 라벨, enum은 `ResourceMode::Custom`
  (C2838). 테스트 수정, 향후 UI 작업용으로 기록.
- B6 (접합 파손): 테스트 삽입 중 B5 xfer 블록이 함수 밖 고아 상태가
  됨(C2447/C2059). `b4checks()` 안으로 복구, 중복행 삭제, read-back
  검증.
- B6 (SMA 상호작용): 모드 hold 테스트가 첫 관측 전 rate를 뒤집어
  SMA가 decide 시점 값을 섞음(50/50). B4 패턴(rateless decide 후
  관측)으로 재작성. 테스트 설계는 SMA 메모리를 존중해야 함.
- B6 (스위트): 새 GPU 빌드 직후 8건 실패 후 동일 바이너리 2연속
  65/65. 환경 경합으로 기록, 실패명 미캡처는 정직하게 남김.

### B5 구현 과정의 기록

- 코드 이슈 없음. 도구 메모 1건: 버전 문자열 편집 1회가 spurious
  "identical"로 보고되어 넓은 컨텍스트로 재적용, 이후 grep sweep으로
  검증.

### B4 구현 과정의 기록

- B4 (테스트 셋업): kill-band 테스트가 `decide()` 시점에
  `gpuLoadKnown=true` + 값 0으로 SMA 레인에 유령 0을 주입해 밴드에
  닿지 않았다. known 플래그는 실제 판독과 함께 무장하도록 수정 —
  Node A telemetry의 unknown-vs-zero 원칙과 동일. 테스트 전용.
- B3 kill 테스트는 `setHoldMs(0)` 명시로 hold와 kill 규칙의 독립성 입증.

### B3 구현 과정의 기록

- B3 (멤버 중복): 헤더 편집 중 `last_`/`decided_`/`lastHw_` 중복
  선언(C2086). 중복 블록 삭제, 로직 변경 없음.
- B3 기지 동작: 부하 기반 share jitter는 그대로 기록된다(adjustment
  카운터가 시스템을 따라 움직임). damp는 B4 범위.

### B2 구현 과정의 기록

- B2 (링키지): B2 테스트 초안이 `b2checks()`를 익명 네임스페이스에
  선언하고 전역에 정의해 LNK2019. 전방 선언을 파일 스코프로 옮겨
  해결. 테스트 전용, 게이트 영향 없음.

### B1 구현 과정의 기록

- B1 (테스트 전제): `scheduler_test`가 무장 전 `decide()`의 주기
  유지를 기대했으나, 첫 `maybeReevaluate`가 시계를 무장하는 게
  설계다. 코드가 아니라 테스트를 수정(명시적 무장 단계). 게이트 영향 없음.
- B1 (flake): 전체 CPU 스위트 안에서 `reveal_window_test` 1회 실패,
  단독·스위트 재실행 통과 — Explorer 포그라운드 경합, 스케줄러 경로와
  무관. 기록, 게이트 영향 없음.

### Node A 구현 과정의 A1 기록

- A1 (빌드 깨짐): 신규 `gpuExecState` JSON 행에서
  `C2001: 문자열 리터럴 내 줄 바꿈` — 편집 중 닫는 따옴표 누락.
  `<< "\""` 종결 복원으로 해결, 이후 CPU 빌드 통과.
- A1 (테스트 환경): `benchmark_test`가 `"diskState":"measured"`를
  기대했으나 이 머신에 PDH LogicalDisk 카운터가 없어 `not_available`이
  정답이다. 테스트를 단일 환경값 단언에서 상태/플래그 정합 단언으로
  변경. 게이트 영향 없음.
- A1 (인코딩): PowerShell 일괄 버전 치환이
  `gui/main.cpp`/`mainwindow.cpp`의 UTF-8 BOM/한글 리터럴을 깨뜨림.
  되돌린 뒤 수술식 편집으로 재적용하고 diff 최소화를 확인. 교훈:
  비ASCII 소스에 plain `Set-Content` 일괄 쓰기 금지.
- A1 (스크립트 기본값): 신규 `build_windows_gpu.ps1`이 `VCPKG_ROOT`를
  요구한 반면 `build_windows_cpu.ps1`은 `C:\src\vcpkg` 기본값을 둠.
  기존 관례에 맞춤. 게이트 영향 없음.

## 버전 진행 규칙

버전은 **통과한 코드 상태**를 기록합니다.

예:
- 0.9.4.0 = A 초기 구현 기준점
- 0.9.4.1 = A 수정/검증 완료
- 0.9.4.2 = B 진입 가능한 검증 상태
- 0.9.4.3 = B 내부 다음 안정화 지점

위 숫자는 예시일 뿐이며 실제 의미는 build-history 문서가 최종 증거입니다.

## Roadmap / Progress / Build History

Development Roadmap
        |
        v
Development Progress
        |
        v
Build / Test
        |
        v
docs/build-history/<version>.ko.md
docs/build-history/<version>.en.md

즉 Roadmap → Progress → Build History 순서로 보면 설계 의도 → 현재 위치 → 실제 코드/테스트 결과를 확인할 수 있습니다.

## OpenCode 작업 원칙

OpenCode는 새 작업을 시작할 때 다음을 먼저 읽습니다.

1. AGENTS.md
2. development-roadmap.ko.md 또는 .en.md
3. development-progress.ko.md 또는 .en.md
4. 현재 노드에 해당하는 architecture 문서
5. 필요한 기존 소스와 테스트

활성 Node에 implementation brief가 있으면 해당 KO/EN brief도 architecture 문서와 함께 읽습니다.

현재 노드의 종료 조건을 먼저 확인한 뒤 구현합니다.

문제가 발생하면 A1/B1/C1 같은 하위 작업으로 기록하고 해결 후 같은 노드의 gate로 복귀합니다.

## 다음 상태 갱신

소스 구현이 시작되면 다음 항목을 실제 값으로 갱신합니다.

- Current Node
- Current Version
- Active Substep
- Blocker
- Validation Result
- Next Gate

**이 문서는 미래 계획을 예측하는 문서가 아니라 현재 개발 위치를 잃지 않기 위한 상태 기록입니다.**
