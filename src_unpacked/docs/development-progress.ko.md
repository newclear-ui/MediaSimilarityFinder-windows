# Development Progress — 0.9.4 개발선

## 문서 목적

이 문서는 development-roadmap.ko.md의 **현재 실제 실행 상태**를 기록합니다.

Roadmap은 개발 방향의 뼈대이고, Progress는 실제 위치, 문제, 회복 분기를 기록합니다.

## 현재 상태

| 항목 | 상태 |
| --- | --- |
| 기준 코드 | 0.9.4.43 |
| 공식 보존 기준선 | 0.9.2.32 |
| 개발선 | 0.9.4 |
| 현재 노드 | **S1 Console Entry Foundation 완료 · S2 Run/Suite Benchmark Core 완료 · S3 Benchmark Storage Isolation 완료 · S4 GUI Benchmark Integration 완료(CLOSED)** — Resource Policy 전달과 datasetFingerprint 해결 포함 (Benchmark/Console CLI 교차 트랙) · F — Hardware Video Decode Backend (F-1 CONDITIONAL, NVDEC 미채택) 유지 |
| 현재 단계 | **F-1 완료(0.9.4.43, `CONDITIONAL`).** NVDEC production adoption 은 `NO` 이며 **F-2 production integration 은 금지**다(지시 §41). 핵심 반전: dataset 14개 중 13개가 file-start IDR 인데 **1360x0808 가 20/20 mismatch**였고 **확인된 IDR 에서 재시작해도 20/20** 이었다(1080x1920 도 1/20). 즉 **mid-GOP 은 실패 모드 중 하나일 뿐**이며, NVDEC exactness 는 4K 에서도 IDR-start 에서도 성립하지 않는다. 계약에 `exactnessVerified` 를 `Safe` 의 필수조건으로 도입했고(structure 는 필요조건일 뿐), `Unsafe`/`Unknown` 은 CPU fallback 으로 collapse 된다. 성능도 **frame당 2.1배 느림**(CPU 4.438 s vs NVDEC 9.397 s, 870f)이라 **근거 없음**. 1360x808 의 root cause 는 `INCONCLUSIVE`. 이전 Node E **종결**. E-3B(0.9.4.42) 는 실제 `MediaSearchEngine::scan()` production 경로로 A/B/C 를 구동해 end-to-end exactness 를 판정했고 **판정 `NOT ACCEPTED`**. **핵심 발견: E-2A/E-2B 의 "exact" 수치는 자기참조였다** — 두 실험 모두 seek 기반 구현끼리 비교했고, 둘 다 `av_seek_frame`+`avcodec_flush_buffers` 로 **같은 decoder reference state 손실을 공유**해 틀린 이유로 일치했다. production(from-zero 스윕)을 포함한 **첫 측정**에서 4K H.264 1개가 **실제 불일치**를 보였다(`reference count overflow`/`no frame!`/`concealing`). 같은 실행에서 sparse 는 **end-to-end +17.38% 더 느림**(4K decode 지배) → **성능 논거도 소멸**. 정정 3건: ① executor 가 truncated 결과를 성공 반환 → sample-count contract 추가 ② container-index GOP 을 `Known` 으로 보고 → `Estimated` 하향 ③ 발췌된 `0.5×framesPerSample` threshold 제거(실측과 모순). **결과 `ExactnessPolicy::RefuseAll` 기본값 도입** — production-parity 증명이 있는 codec 이 없어 sparse 는 production 에서 도달 불가하고 전 파일 Sequential. **production 동작은 0.9.4.41 과 동일(13/13 bit-identical, adaptive -0.02% 중립).** **methodology 교훈: exactness 기준선은 반드시 production 경로여야 한다.** 같은 계열 재구현끼리는 공유 결함을 서로 검증하지 못한다. **다음: sparse 는 증거 없이 재개하지 않는다. 재검토 조건은 build history 문서에 명시** |
| 현재 버전 | 0.9.4.43 |
| GPU 구현 기준 | NVIDIA CUDA |
| CPU fallback | 유지 |
| 프로젝트-local vcpkg | 유지, 이전하지 않음 |

### Node E 종결 기록 (0.9.4.42 기준)

사용자 결정으로 Node E 를 종결한다.

- **E-3C**: 별도 roadmap Stage 로 승격하지 않는다. F 로 이관하지 않는다.
  `RefuseAll` 로 sparse production path 가 도달 불가능한 현재 구조를 기준으로
  Node E 종결 범위에서 정리한다. 향후 F architecture 에 참고가 될 수 있다는 사실만
  참고사항으로 기록하며, **작업 항목으로는 이관하지 않는다.**
- **E-4**: production integration + end-to-end validation 역시 Node E 종결 범위에서
  정리한다. `RefuseAll` 하에서는 통합할 "성격의" 경로가 남아 있지 않으므로,
  E-4 를 sparse 재도입 근거로 읽어서는 안 된다.
- `ExactnessPolicy::RefuseAll` 기본값과 production 순차 디코딩 경로는 **유지**한다.
- 결론의 근거와 재검토 조건은 `docs/build-history/0.9.4.42.*`.

**`src/video_sampling_planner.h` 의 E-3C 관련 코드 상태 변경은 이 문서 정리 단계에서
시행하지 않았다.** 필요 여부는 별도로 판단·보고한다.

### Node F 진입 상태

- pre-register brief: `docs/implementation-briefs/F-hardware-video-decode-backend.*`
- 이번 F 의 실제 조사·실험·구현 범위: **NVIDIA NVDEC 단일**
- 다른 hardware decode backend 의 실제 구현·검증은 이번 F 범위 밖
- architecture 는 NVIDIA 전용으로 고정하지 않는다 (추가 backend 배려 방향)
- `tools/` (untracked, 미문서화) 는 **보류**. 삭제·추가·commit·.gitignore 등록 모두 하지 않고
  현 상태를 유지한다. 정식 편입 여부는 별도 작업으로 판단한다.

### I-3 결과 요약 (v0.9.4.37)

```text
BASELINE   v0.9.4.35 / 6c981f7
CANDIDATE  v0.9.4.36 / 4ba2a35
드라이버   msf_dataset_baseline → MediaSearchEngine::scan() (GUI 와 동일 경로)
조건 A (cold process, decode-active, BCBCBCBCBC 교차)
  CPU  10 paired  full scan -10.17 % (9/10)   decode stage -22.42 %
  GPU   5 paired  full scan -11.13 % (5/5, 비겹침)  decode stage -22.29 %
조건 B (warm / repeat scan)  GPU 12행  full scan -26.73 % (24/24 더 빠름)
                           단 process-level slowdown 미해명 → headline 근거로 미사용
정합성    decode share 50.7 % × decode 절감 22.3 % = 11.3 % ≈ 관측 11.13 %
해체      절감의 28~30 %는 계측 전용 D1 reference probe 중복 제거 (product 아님)
          진짜 product 이득은 WIC decoder 생성(open) 25,924 → 12,962회
correctness 39회 실행 전부 groups=156211 / misses=12962 / hits=14524 동일
           exactness 3341/3341 diff_px=0, EXIF 8/8, 전수 5,579,470쌍 무변화
           CPU CTest 81/81, GPU CTest 82/82

판정      I-3 END-TO-END VALIDATION = PASS
          I NODE = COMPLETE, I-1 = DEFERRED, I-2 = PRODUCTION, NEXT = E
```

미해명 항목(I 노드를 막지 않음): 조건 B 의 동일 프로세스 2회째 스캔부터 양 버전
모두 30~60 % 느려지는 현상. v36 은 그 조건에서도 24/24회 더 빠르므로 I-2 가 만든
회귀가 아니다. 원인은 decode `copyMs` 쪽이며 WIC instance 구조 의존성이 **가설
단계다.** `vcpkg.json` 의 `version-string` 은 여전히 `0.9.4.35` 인데, 이를 바꾸면
manifest 해시가 바뀌어 2.2 GB vcpkg 재빌드가 발생하므로 measurement-only 버전에서
의도적으로 건드리지 않았다.

(위 표는 0.9.4.24 시점의 오래된 상태였으며, 0.9.4.32 실제 저장소 상태로
정정했다. 아래 본문의 과거 기록은 삭제하지 않는다.)

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
[I] Analyze / Matching Performance (D8b 근거 이후 개설, Node D 는 측정으로 종료)
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

### C — Calibration / INI Performance Profile (C1~C4 모두 완료)

- C brief를 C1~C4 단계로 구체화했다.
- C1은 Profile Foundation만 다루며 실제 calibration 실행은 넣지 않는다.
- C2는 short initial calibration과 Profile → Scheduler initial estimate 연결을 담당한다.
- C3는 반복 runtime deviation을 기준으로 opportunistic recalibration과 confidence update를 담당한다.
- C4는 Profile lifecycle, live precedence, CPU/GPU parity, failure/partial state, persistence를 최종 검증한다.
- Profile 저장 위치는 portable 기준 Index/PerformanceProfile.ini로 정의한다.
- Profile은 initial estimate이며 live runtime state가 항상 우선한다.
- D queue/worker topology와 F hardware decode는 C에서 선행하지 않는다.
- queue latency와 hardware decode가 아직 측정 불가능한 경우 explicit measurement state로 기록한다.
- **현 상태: C1(0.9.4.9) → C2(0.9.4.10) → C3(0.9.4.11) → C4(0.9.4.13) 모두
  완료·통과.** 위 항목들은 C 를 처음 설계할 때의 단계 정의이며, 각 단계의
  실제 결과는 아래 C4/C3/C2/C1 소제목을 따른다. C4 통과 시점에
  "Node C 완료, 다음 게이트 D" 로 기록되어 있다.

### C4 — Calibration Gate (→ 0.9.4.13, 통과)

- 기능 추가 없음: 프롬프트 17조합을 증거에 연결(매핑은
  `docs/build-history/0.9.4.13.ko.md`).
- 최소 추출 1건: `calibrationUsableForUpdate()` — 엔진이 쓰던 동일식,
  이제 단위 테스트 가능. 엔진 람다는 호출로 교체(동작 동일).
- C4 단언: Exact 10연속 무발화, 실패/부분 불가 판정, 실패 후 파일
  무수정 reload, usable 후보 통과.
- 검증: CPU 66/66, GPU 67/67, `scan_workflow_test`로 UI parity, 양쪽
  `--version`/`--smoke`. 검색 의미 불변. Node C 완료, 다음 게이트 D.

### C3 — Opportunistic Recalibration (→ 0.9.4.11, 검증됨)

- `DeviationTracker`: 스캔당 live-vs-feeding-baseline 검사, 25% 초과
  3연속에만 발화, 발화·교체·무효 입력에 리셋. 고정 정책(25/3/40%/
  ±0.1, 상한 0.95/하한 0.1).
- 후보는 발화 live 관측과 40% 이내 일치해야 교체. 불일치는 metrics
  유지 + confidence 하향. 실패는 파일 무수정. `[lastUpdate]` 영속화
  (부재 = 미갱신).
- 엔진 `finishScan`: 완료·profile-fed·양쪽 live 실측 스캔에만 trigger
  평가, bounded 재측정 + try/catch, 취소 스캔 제외. CPU-only 스캔은
  평가 진입 불가.
- 검증: CPU 66/66, GPU 67/67(tracker·일관성·update/keep reload·
  `[lastUpdate]` round-trip), `scan_workflow_test`로 UI parity, 양쪽
  `--version`/`--smoke`. 검색 의미 불변.

### C2 — Initial Calibration (→ 0.9.4.10, 검증됨)

- Bounded `Calibrator::run`(`src/calibration.h/.cpp`): 합성 CPU 해시,
  웜업 폐기 GPU 배치. resize/decode/transfer/queue/HW-decode는 명시
  상태(사용자 파일 금지, phase budget, 8초 guard). 실패는 스캔에
  전파되지 않는다.
- 엔진: Missing/Hard 판정 시 1회 calibration → 저장 → 당 스캔 적용.
  Exact/Soft 재사용. 정책-off와 무장치를 `videoGpu_`로 구분.
  telemetry 초기 tier는 profile pair 보고.
- schema v2(calibration metric 상태). 스케줄러 우선순위
  live > profile을 `scheduler_test`에 단언.
- 검증: CPU 66/66, GPU 67/67(신규 `calibration_test`, 실기 GPU 측정
  포함), `scan_workflow_test`로 UI parity, 양쪽 `--version`/`--smoke`.
  검색 의미 불변.
- C3 blocker: 대역폭 실측 미해결, resize/decode는 기회관측 설계 필요.

### C1 — Profile Foundation (→ 0.9.4.9, 검증됨)

- `PerformanceProfile` + `ProfileStore`(`src/profile.h/.cpp`, Qt-free):
  FNV-1a 안정 id, Missing/Hard/Stale/Soft/Exact 판정(stale은 호출자
  maxAge로만 발화, C1 기본 age 없음), pair-or-nothing initial estimate,
  수동 INI codec + temp+rename 원자 저장. QSettings 미재사용(비원자).
  CPU-only 머신은 estimate 불가.
- 스케줄러 우선순위 live → profile → hardware + `profile_baseline`
  reason, 정책 불변. 엔진 후크는 머신 전역
  `<appDir>/Index/PerformanceProfile.ini` 로드, live 휴면(writer 없음).
- 검증: CPU 65/65, GPU 66/66(신규 `profile_test` 12항),
  `scan_workflow_test`로 UI parity, 양쪽 `--version`/`--smoke`.
  검색 의미 불변. C2/C3 수치는 의도적 미결정.

### B7 — Final Scheduler Gate (→ 0.9.4.8, 통과)

- 외부 리뷰 지적 수정: `const bool schedUseGpu`가 스캔 시작 판단을
  고정시켰다. 이미지 배치·비디오 구간마다 fresh 발표 읽기로 교체
  (3곳, stale const 삭제·grep 검증). worker·queue·barrier 변경 없음.
- 안전: hold/band 승인 발표만 바뀌어 실행 요동 불가. 2초 미만 스캔은
  재평가 미발화로 바이트 동일.
- 커버리지 감사로 B7 항목 전부 증거 연결. 의도적 비게이트 기록:
  저사양 실기·부하 중 Gaming 실행(단위+parity 대체), wall-clock 장기
  실행(합성 틱 대체), 비교 throughput 게이트(관측 증거로 대체).
- `scan_streaming_test`에 엔진 수준 scheduler 기록 단언 추가.
- 검증: CPU 64/64, GPU 65/65, 양쪽 `--version`/`--smoke`. 검색 의미
  불변. Node B 완료, 다음 게이트 C.

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


### C4.1 — Calibration Lifecycle Fix (→ 0.9.4.13, 0.9.4.14에서 검증)
- **GPT Fix:** GPU OFF→ON incomplete-profile lifecycle, default 30-day stale enforcement, CPU model/GPU driver identity completion, and failed-retry preservation.
- Windows CPU/GPU Release 빌드 및 전체 CTest 검증: **0.9.4.14에서 통과** (CPU 66/66, GPU 67/67). `kDefaultMaxAgeDays`를 `PerformanceProfile`으로 이동하고 테스트 참조에 접두 추가. 상세: `docs/build-history/0.9.4.14.ko.md`.

### D1a — Image-Path Observability 게이트 (→ 0.9.4.16, 통과)
- 원격 0.9.4.15 구현을 로컬 검증: packMs는 decode 제외, cpuHashMs는
  실행된 CPU hash 작업 기록(GPU 경로 mirror 포함, 별도 필드),
  batchMaxDepth는 구조상 항상 1(기록, D3에서 카운터 활성화),
  batchItems는 시도 입력 수, transfer는 계속 not_measured.
- 게이트 수정: 6개 키에 schema v2 → v3 (Node A 규칙), 부분적 0.9.4.15
  버전 sweep 완성 (vcpkg/index/GUI-about-titles/package/tests).
- 검증: CPU 66/66, GPU 67/67, 양쪽 `--version`/`--smoke`. 다음 D1b.

### D1b — Walker/Video Observability 게이트 (→ 0.9.4.17, 통과)
- Walker handoff: 정확한 depth 포함 enqueue/dequeue 카운트, maxDepth,
  starved tick (타임아웃 + empty + walker alive만). producer block
  카운터 없음 (unbounded 구조, 문서화).
- Video async 단위: 실행 range마다 ranges/rangeFiles/rangeState
  (타이밍 분해 없음, range wall은 기존 `videoStageMs`에 이미 있음).
- 신규 키에 schema v4 (Node A 규칙). topology 변경 없음 (후크 4개).
- 검증: CPU 66/66, GPU 67/67, 양쪽 `--version`/`--smoke`. 다음 D2
  (D1a/D1b 증거 기반 barrier 검토, 사전 등록 우선).

### D2 — Barrier 검토 (→ 0.9.4.18, 완료)
- 코드 변경 전 pre-register: straggler 대기 후보, (max−min) 이득 가설,
  parity-or-revert 기준.
- video range completion-order harvest (동일 스레드/join, 5ms idle
  bound, 중단 의미 유지). JSON 단독 정량용 `maxRangeFileMs`.
- 비후보 문서화 (image phase join·walker poll·DB 단일 writer·최종
  매칭 경계, 의존성 사유).
- schema v5 (신규 키 규칙). topology 변경 없음.
- 검증: CPU 66/66, GPU 67/67 (parity가 순서 무관 입증),
  양쪽 `--version`/`--smoke`. 다음 D3 (D1b depth 증거 기반 queue).

### D3-Minimal — Bounded Walker Queue (→ 0.9.4.19, 통과)
- `WalkerQueue` (~90줄): capacity + cancel-aware backpressure 전용.
  pause는 scanner 측 유지, 대기는 100ms bound 재확인.
  capacity 4096은 안전 bound (최적 주장 금지).
- 엔진: bounded push + cancel drop 의미, pop notify, join 전 단일
  `shutdown()` (모든 exit 통과). `ScanControl::walkerQueueCapacity`
  테스트 오버라이드 (0 = 기본값).
- telemetry: `walker.capacity` (설정값) + `walker.blockedTicks`
  (full 대기 진입). schema v6.
- 검증: CPU 67/67, GPU 68/68 (unit: capacity/FIFO/block-resume/
  cancel/shutdown, 60파일 cap-16 통합 bound + 1770쌍 parity,
  pause 토글 parity), 양쪽 `--version`/`--smoke`.
- Pre-register 결과: throughput 회귀 없음, 메모리 상한은 구조적 보장,
  롤백 트리거 미발동. Full D3 계속 보류 (실측 imbalance 없음).

### D4a — CUDA 백엔드 내부 타이밍 (→ 0.9.4.20, 통과)
- **계측만 한다.** overlap 없음, double/triple buffering 없음, pinned memory
  없음, 새 stream topology 없음, Scheduler 변경 없음. D4b는 미결.
- 코드 변경 전에 pre-register를 커밋(`ec7cff6`)해 순서를 감사 가능하게 함.
- `cuda_backend.cu`: 경계마다 6개 `cudaEvent_t` 를 같은 stream에 인라인
  기록 (H2D, kernel, D2H). backend 생성 시 1회 생성, 소멸 시 해제.
  event 기록/elapsed 실패는 **계측만** 비활성화 — 해시 결과와 함수 반환값은
  불변 (`measurement failure != GPU processing failure`).
- vendor-neutral `GpuBackend::HashTiming` (기본값 있는 out-param)만 추상화
  경계를 넘음. CUDA 타입이 엔진/파이프라인/recorder에 닿지 않음.
- `syncHostMs` 는 `cudaStreamSynchronize` 내부의 **직접 측정** host wall
  time. `hostTotal - device 합` 으로 계산하지 않음 (그 뺄셈은 enqueue와
  scheduling 오버헤드를 device 값에 섞는다).
- telemetry: `gpuH2dDeviceMs` / `gpuKernelDeviceMs` / `gpuD2hDeviceMs` /
  `gpuSyncHostMs` / `gpuHostTotalMs` / `gpuTimedBatches`, 각각 독립 state.
  schema v7. 기존 `addGpuBatchMs()` 키와 의미 유지.
- 신규 `gpu_timing_test` 를 **양쪽 트리에** 등록 (CUDA가 없는 쪽에서
  recorder 계약 증명): 키 존재, measured/not_measured 전환, 비음수 + 느슨한
  상한 1개, CPU 빌드 `measured == false`, 그리고 계측이 결과를 바꾸지
  않음을 증명하는 hash-vs-CPU parity. host/device 합산 정확 일치는
  단언하지 않는다.
- 검증: CPU 68/68, GPU 69/69; 양쪽 `--version` 0.9.4.20, `--smoke` PASS.
  pre-register 롤백 트리거 미발동.
- 실측(RTX 3080 Ti, 합성, pageable): 16장·256장 모두 kernel device ≈0.24 ms
  (배치에 비례해 증가하지 않음), H2D는 데이터량에 비례, `syncHost` ≈0.013 ms,
  host 측 갭 ≈0.27 ms 일정. **관찰이며 D4b 판정 아님** — 소규모 합성 프로브는
  대표 dataset이 아님.

### D8a — 재현 가능한 Dataset 기반 / Dataset Fingerprint (→ 0.9.4.21, 통과)
- **최적화가 아니라 기반 구축.** 병목은 코드가 아니었다. "동일 조건에서 두 상태를
  비교한다"가 표현 불가능했고, D4b·Full D3·D8이 모두 같은 근거 부재에 막혀 있었다.
- fixture / 코드 변경 전에 pre-register 커밋(`26cef86`).
- 지시대로 코드보다 조사先行. 설계를 결정한 핵심 발견:
  저장소는 **바이너리 asset을 커밋하지 않는다** (트랙 495개 전부 텍스트),
  모든 테스트가 runtime 생성 관례, `msf_core` 에 **암호 해시 없음**
  (Qt `QCryptographicHash` 는 GUI 전용·비노출, openssl 없음),
  모든 video fixture 가 ffmpeg 를 쓰는데 그 출력이 **byte 비재현적**이다.
- 따라서 dataset 은 **커밋하지 않고 생성**한다
  (`scripts/prepare_dataset.ps1`): 손으로 만든 8x8 BMP 60개, 두 구조 —
  `images/exact` 48 (12그룹 x 4 byte 동일) + `images/varied` 12 (시드 전부 다름).
  clock·환경·random 을 읽지 않고 모든 바이트가 `(seed, x, y)` 의 순수 함수.
- **video 는 이번 단계에서 제외**하고 별도 후속 작업으로 문서화:
  ffmpeg 이 encoder/creation-time metadata 를 mix하므로 content hash 가
  실행마다 흔들려 이 단계의 목적을 정면으로 파괴한다.
- `src/dataset_fingerprint.{h,cpp}`: 자체 SHA-256 (NIST 벡터 4종으로 검증 —
  digest 가 조금만 틀려도 모든 fingerprint 가 조용히 흔들린다),
  manifest 는 `canonical relative path + size + SHA-256(content)` 를 byte 순서로
  정렬 후 해시. 절대경로·timestamp·pid·하드웨어명은 없으므로 동일 내용의
  복사본은 다른 root 에서도 동일 fingerprint. 파일 전체를 읽는다 —
  scanner 의 `quick()` 은 첫 64 KiB 만 덮고 파일 동일성 용도다.
- Benchmark JSON: `meta.dataset` 에 `fingerprint` / `fingerprintVersion` /
  `fileCount` / `totalBytes` / `state`. `root` 는 **위치**로 그대로 유지.
  미측정은 `"fingerprint": null` + `not_available`, 절대 0 이 아님. schema v8.
  엔진이 `bench_.start()` 직후 연결.
- 신규: `dataset_fingerprint_test` (A~F, 37 checks, 양쪽 트리) +
  `dataset_e2e_test` (실제 엔진 스캔이 독립 계산과 동일 fingerprint 를 기록,
  `scanned != 0` 도 단언).
- dataset: fingerprint `f01d5c77…d7c`, 60 파일, 14,760 바이트, version 1.
  분리된 실행에서 재현 확인.
- 테스트가 찾아낸 수정 2건 (가정한 것이 아님): 처음 쓴 SHA-256 기대값이
  틀렸음(구현이 정상이었고 별도 벡터 집합으로 교차 확인), 그리고 리포트 파일을
  처음엔 dataset root **안쪽**에 썼는데 다음 manifest walk 에 포함되어
  설명하려는 fingerprint 를 바꿔버리므로 root **옆**에 쓰도록 수정.
- 검증: CPU 70/70, GPU 71/71; 양쪽 `--version` 0.9.4.21, `--smoke` PASS.
  pre-register 롤백 기준 7항목 전부 무발동.
- **D4b overlap 과 Full D3 topology 는 아직 구현하지 않았다.**
  둘 다 이제 이 fingerprint 기준으로 판단 가능하다.

### D8b — 규모 확장 Dataset / Walker Queue 증거 (→ 0.9.4.22, 통과, 계측만)
- fixture 변경 전에 pre-register 커밋(`8f52587`).
- **제품 코드 변경 없음.** 생성기에 `-Scale full` 추가, review probe 에
  stage 분해 리포트 추가만.
- 왜 필요했나: D8a 의 "이득 없다" 판정은 **측정 대상 부재** 에서 왔다 —
  60개의 8×8 을 디렉토리 2개에 두면 파이프라인이 아무 일도 하지 않으므로
  `maxDepth = 1` 은 dataset 크기를 기술한 것이지 구조가 아니었다.
- dataset v2 = D8a 60개를 **바이트 동일하게 유지** (parity anchor) +
  의도적으로 비대칭인 두 축. queue 는 consumer 비용이 producer 비용보다
  클 때만 깊어지므로 walk 비용과 decode 비용을 독립적으로 조절했다:
  `tree/` 2400개 소형 파일을 240 leaf 디렉토리에 분산 (walk 비용),
  `bulk/` 256×192 이미지 240장 (decode + crop + color thumbnail 비용).
  합계 2700 파일 / 36,007,560 B / 95 디렉토리.
  fingerprint `9b113848…4253c`. 알고리즘 변경 없음, content 만 변경.
- 실측(RTX 3080 Ti, 3회, 매 run cold index):
  - `walker maxDepth` **1 → 964 mean / 1076 max** (capacity 23.5 %).
    D8a 의 depth 1 이 dataset 크기 탓이었음이 실증됨
  - `blocked_ticks` **여전히 0** — producer 는 한 번도 block 되지 않음.
    depth 1076 은 capacity 4096 보다 3.8배 낮으므로
  - `gpu_batch share of engine wall` 0.34 % → **0.026 %**
  - `kernel device` 17.1 ms / device 합 20.5 ms 의 84 % — overlap 이 숨길
    대상이 애초에 크지 않음
- 109,737 ms 엔진 wall 의 stage 분해: **`analyze` 98.62 %**, walk 1.40 %,
  image stage 0.59 %, gpu batch 0.04 %.
  **D3+D4 addressable ceiling: 0.044 %.**
- `analyze` 는 `MediaPipeline::analyze()` = 최종 매칭/그룹화 단계
  (`media_search_engine.cpp:702-703`) 로, Node D 가 다우는
  queue/transfer/overlap 범위 **밖**이다.
- 따라서: **D4b 와 Full D3 는 가정이 아니라 근거로 보류**되며, D 를 더
  진행해도 이 workload 에서 의미 있는 이득은 없다.
- 검증: CPU 70/70, GPU 71/71. 제품 코드 무변경.
- 정직한 범위: `bulk` 이미지는 queue 관찰에 *유리하도록* 크기를 정했고,
  그래도 GPU share 는 0.026 % 였다. GPU 경로가 의미를 가지려면 이미지가 훨씬
  크거나(crop/thumbnail 비용 없음) hash batch 가 훨씬 많아야 한다.

### D9a — Analyze 내부 관측 (→ v0.9.4.23, pre-register 커밋 완료, 구현 대기)
- **신규 노드.** D8b 가 병목(`analyze`, 엔진 wall 98.62 %)이 D brief 범위 밖임을
  실측으로 보여줬으므로, D 를 더 진행해도 의미 있는 이득이 나올 수 없어 개설.
- 코드 변경 전에 pre-register 커밋(`0fc3344`).
- **계측만 한다.** 캐시 확대 없음, 병렬화 없음, SSIM 알고리즘 변경 없음,
  후보 생성 변경 없음, threshold 변경 없음.
- 현재 상태: `analyze` 는 **단일 수치**로만 기록된다
  (`media_search_engine.cpp:702-703` 의 `bench_.addAnalyzeMs`).
  benchmark `matches` 섹션은 개수를 주지만 **내부 시간 분해가 없어서**
  "쌍이 많다" / "쌍 하나가 느리다" / "캐시가 안 맞는다" 를 구분할 수 없다.
- 코드 조사로 나온 건 **검증할 가설**이지 행동 근거가 아니다:
  `verifyImagePair`(`image_verify.cpp`)는 gray-zone 쌍마다 두 파일을 재디코드
  하고(쌍당 최대 4회) `frame_ssim` 을 최대 20회 계산하는데,
  `kVerifyCacheMax = 32` 로 verify 캐시가 hard cap 된다. 2,700 파일 기준
  사실상 쌍마다 미스다. **여전히 가설이며, stage 별 시간이 나오기 전까지
  코드는 건드리지 않는다.**
- 예정 telemetry: `analyzeIndexMs` / `analyzeScanMs` / `analyzeVerifyMs` /
  `analyzeVideoMs`, 그리고 `verifyCalls` / `verifyDecodeMisses` /
  `verifyCacheHits` / `ssimEvals` / `frameSsimEvals` / `videoTemporalPairs`.
  판정의 핵심은 파생값 2개 — **`verifyHitRate`** (≈0 이면 캐시 용량 원인 확정)
  와 **`msPerVerifyCall`** ("쌍이 많다" vs "쌍 하나가 느리다" 분리).
- 성공 기준: 계측이 **어떤 병목인지 식별해내는 것**이며, 무엇이든 상관없다.
  deliverable 은 속도향이 아니라 원인 규명이다.
- 현재 버전은 0.9.4.22 유지 — 버전은 *검증된* 코드 상태를 뜻하며 아직
  코드 변경이 없다.

### D9a — Analyze 내부 관측 (→ 0.9.4.23, 통과, 계측만)
- 코드 변경 전에 pre-register 커밋(`0fc3344`).
- **계측만 한다.** verify 캐시 32 그대로, 병렬화 없음, SSIM/index/threshold/
  grouping 변경 없음, Scheduler/CUDA 변경 없음.
- 구조: `src/analyze_telemetry.h` 는 **아무것도에 의존하지 않는** 순수
  Qt-free 데이터 캐리어다. recorder·pipeline·verify 코드 어느 것도
  참조하지 않는다. `image_verify` 와 `scan_pipeline` 이 채우고 엔진이
  복사해 넣는다. **recorder 포인터는 아래로 절대 내려가지 않는다** —
  프롬프트의 강한 결합 금지 요구사항 충족.
- **stage 합이 analyzeMs 를 넘을 수 없는 것은 구조적 보장이다.**
  `flushVideo()` 가 candidate loop 안에서도 호출되므로 중첩 타이머는 겹친다.
  그래서 video 시간을 누적해 제외하고 `scanMs` 을 **나머지로 정의**한다:
  `scanMs = total - index - verify - video` (음수면 0). 4개 슬라이스가 총합과
  정확히 같아지며, scan 은 attribution 안 된 제어 오버헤드를 **정직하게
  흡수**한다 — 측정되지 않은 stage 소속인 것처럼 꾸미지 않는다.
- 카운터는 추정이 아니라 이벤트 개수다. `verifyCalls` 는 video 쌍을 제외
  (작업 없이 반환), misses/hits 는 *조회* 기준(호당 2회), `ssimEvals` 는
  호출 수, `frameSsimEvals` 는 실제 실행 수라 둘 사이 간격 자체가 측정값이다.
  파생값은 분모가 0 이면 `null` + `not_measured` — 0 나눗셈으로 0.0 을
  만들어내지 않는다.
- schema v8 → v9. 신규 `analyze_telemetry_test` **양쪽 트리**, **41 checks**.
- **실측** (RTX 3080 Ti, D8b dataset `9b113848…4253c`, cold index):
  - `index` 3.2 ms (0.00 %), `scan` 350.8 ms (0.32 %), **`verify` 109,142.3 ms
    (99.67 %)**, `video` `not_measured` (dataset 에 video 없음 — 0 위장 아님)
  - substage 합 109,496.3 ≤ analyzeMs 109,501.9 → 규칙 성립
  - `verifyCalls` 158,020 · `misses` 12,949 · `hits` 14,519 · **`verifyHitRate`
    0.5286** · `msPerVerifyCall` 0.69–0.74 ms · `ssimEvals` 137,340 ·
    `frameSsimEvals` 274,680
  - **카운터 내부 정합성**: `ssimEvals/10 == (misses+hits)/2 == 13,734` 이
    정확히 일치 → 13,734 건이 decode+SSIM 전체 경로를 수행
- **가설 판정.** "verifyImagePair 가 지배" → **확인**(99.67 %).
  "kVerifyCacheMax = 32 가 재디코드 원인" → **기각**: 적중률 52.9 % 로
  ≈0 이 아니다. 후보 쌍이 인덱스 순으로 집중 방문되기 때문이다.
  **코드만 읽고 세운 가설이 측정으로 뒤집혔다** — pre-register 가 계측을
  먼저 요구한 이유가 이것이다.
- **실제 지배 구조:** verify 게이트에 도달한 158,020 건 중 ~91.3 % 는
  kFast 단축, ~8.7 %(13,734)만 decode+SSIM 을 각 ~8.5 ms 에 수행해 117 초를
  만든다. 즉 병목은 **verify 게이트에 도달하는 후보 쌍의 수**지, 건당 비용이
  아니다.
- parity: `groups` 156,152 로 0.9.4.22 와 동일, `analyze_telemetry_test` 가
  계측 유무 `verifyImagePair` 반환값이 double 동일함을 단언.
- 검증: CPU 71/71, GPU 72/72; 양쪽 `--version` 0.9.4.23, `--smoke` PASS.
  pre-register 롤백 기준 6항목 무발동.
- 식별되었으나 **구현하지 않은** 다음 후보: 4.3 % 도착률 낮추기, 또는 비싼
  검증 1건의 8.5 ms 낮추기(디코드 4회 + `frame_ssim` 20회, `ssimBuf` 10회 중
  9회가 aspect 조합이며 buffer 재사용 없음). 단 dataset 이 합성
  deterministic fingerprint 이므로 실사용 라이브러리의 후보 비율은 다를 수 있다.

### 0.9.4.24 — QSettings Organization 이름 변경 (통과, identity 만)
- D 계측 순서와 무관한, 포터블 UI identity 한정 변경.
- QSettings organization 이름이 곧 설정 폴더명이므로, 기존 값 `newclear-ui` 는
  GitHub 계정명이 그대로 배포물에 나가고 있었다. `MediaSimilarityFinder-ui`
  로 변경했고 application name·executable·CMake target 은 모두 유지.
  `setDefaultFormat`/`setPath` 는 손대지 않았다.
- 조사 선행: 활성 코드 사용처는 **정확히 1곳**이었고, 나머지는 저장소 owner
  표기와 역사 build-history 문서였다. QuickLook 레지스트리 조회는
  `NativeFormat` + 명시 path 라서 무관하며 의도적으로 건드리지 않았다.
- 단순 rename 은 기존 사용자의 UI 상태를 초기화된 것처럼 보이게 하므로,
  migration 이 이번 작업의 실체다. `MainWindow` 가 곧바로 `ui/ignored` 를
  읽으므로 identity 설정 직후 `initAppSettings()` 내부에서 실행한다.
- 삭제 정책은 엄격한 순서: 복사 → 실제 QSettings 로 새 INI open →
  `status() == NoError` 확인 → `ui/mainGeom`·`ui/splitter` 존재 확인 →
  **그때만** legacy INI 제거, legacy 디렉터리는 비었을 때만 rmdir.
  어떤 실패 경로에서도 legacy 가 보존되므로 사용자 설정이 사라질 수 없다.
  새 위치가 이미 있으면 그쪽이 승리하고 legacy 는 그대로 두므로 idempotent 하다.
- 테스트: `ui_settings_test` 에 cross-process 5단계(plant / verify /
  both / none). 같은 프로세스 read-back 은 디스크 영속성이 깨져도 QSettings
  인메모리 캐시로 통과할 수 있어 프로세스를 분리했다. 기존 cross-process
  round-trip 은 새 identity 아래에서도 계속 통과한다.
- 실환경 검증도 수행: 임시 portable 디렉터리 + 실제 `--smoke` 실행으로
  Case A(신규만), B(legacy 만 — 11개 key 보존·legacy 제거), C(양쪽 — 신규 승리·
  legacy 보존) 확인.
- 검증: CPU 76/76, GPU 77/77; 양쪽 `--version` 0.9.4.24, `--smoke` PASS.
  Engine 1.5.0 / DB 1.0.3 / cache v9 불변.

### D9b — 향후 빌드 비교 기준 고정 및 후보 우선순위 결정

D9b 이후의 모든 관련 빌드에서는 **0.9.4.24 D9a 측정값을 baseline으로 기억하고 비교**한다.

#### 1. 0.9.4.24 D9a baseline

| 항목 | 기준값 |
| --- | ---: |
| dataset | D8b standard, 2,700 files, fingerprint `9b113848…4253c` |
| verifyCalls | 158,020 |
| verifyHitRate | 0.5286 |
| expensive verify | 13,734 |
| expensive verify 평균 | 약 8.5 ms/call |
| expensive verify aggregate | 약 117 s |
| decode | 최대 4회/건 |
| frame_ssim | 최대 20회/건 |
| ssimBuf | 10회/건, 그중 9회가 aspect 조합 |
| groups | 156,152 |

이 값들은 이후 빌드의 **성능 비교와 정확성/parity 비교의 기준선**으로 유지한다.

#### 2. D9b primary candidate — B

**B: expensive verify 1건 비용 감소**를 D9b의 primary 대상으로 한다.

최적화 대상은 불필요한 decode, 중복 buffer 작업, 중복 SSIM 입력 준비/복사, aspect 조합의 반복 buffer 작업이다.

기본적으로 다음 의미는 변경하지 않는다.

- verdict semantics
- candidate semantics
- similarity threshold
- grouping semantics
- Engine / DB / cache semantics

단, 판정식을 직접 바꾸지 않는 것만으로 정확성 보존을 가정하지 않는다. **기존/신규 결과 parity를 실제로 측정한다.**

#### 3. 향후 빌드에서 반드시 비교할 항목

D9b 이후에는 0.9.4.24 baseline과 가능한 한 다음을 함께 비교한다.

- verifyCalls
- expensive verify count
- total verify time
- ms per expensive verify
- verifyDecodeMisses / verifyCacheHits
- decode count
- frameSsimEvals
- ssimBuf 관련 비용 또는 동등한 telemetry
- groups
- 최종 match/verdict parity
- CPU/GPU parity

동일 dataset / hardware / 가능한 동일 cold-index 조건이 아니면 직접적인 성능 개선으로 단정하지 않는다.

#### 4. Candidate A는 deferred로 기억한다

**A: candidate pair 도착률 감소**는 158,020건의 gate 도달량을 줄일 수 있는 잠재력이 있지만 candidate/verdict semantics에 영향을 줄 가능성이 있다.

따라서 D9b에서는 구현하지 않는다.

향후 A를 재검토할 경우 별도의 pre-register를 먼저 작성하고 candidate recall, false-positive/false-negative risk, candidate count, verifyCalls, 최종 grouping/verdict parity를 비교한다.

A는 폐기된 것이 아니라 **의도적으로 deferred된 후보**다.

#### 5. 향후 빌드에서도 유지할 관계

```
0.9.4.24 D9a
    ├─ baseline: 158,020 verifyCalls
    ├─ expensive: 13,734 × ~8.5 ms ≈ 117 s
    │
    ├─ D9b primary: B
    │      └─ per-expensive-verify cost reduction
    │
    └─ deferred candidate: A
           └─ candidate-arrival reduction
              (verdict semantics risk)
```

새 빌드를 기록할 때는 가능한 경우 **0.9.4.24 D9a baseline 대비 변화량**도 함께 기록한다.

### D9b — Candidate B (expensive verify 비용 감소) → 0.9.4.25, **NOT ACCEPTED**

- Pre-register 커밋 `b33154b` (코드 변경 전), baseline 은 0.9.4.24 D9a.
- **판정: 실패.** 중단 조건 7(측정 오차 수준) + 8(B 가 실제 병목이 아님) 해당.
- 구현: `verifyScorePlan` 이 `centerCropResize` 를 8→6 회로 줄이고 aspect
  버퍼를 3회 `ssimBuf` 에 재사용. reference 구현은 그대로 보존해
  `verifyImagePairReference` 로 비교 가능하게 함.
- **카운터가 하나도 변하지 않았다** — verdict·grouping·parity 전부 보존
  증명이 동시에, "최적화 대상이 지배 비용이 아니었음"의 증거가 됐다.
  ```
  verifyCalls 158,020 · expensive 13,734 · hits 14,519 · misses 12,949
  ssimEvals 137,340 · frameSsimEvals 274,680 · groups 156,152  (전부 동일)
  total verify ms  109,142 / 117,291  →  110,717   감소 없음
  ```
- **원인 (측정 기반)**: `frame_ssim` 이 64×64 에서 4,096 inner-loop 회
  (호출당 20회 = 81,920 ops) 를 도는데 이것이 비용의 사실상 전부다.
  D9b 는 `centerCropResize` 8,192→6,144 (전체 작업의 2.3%) 만 줄였다.
  **가설이 지목한 "중복" 은 존재하지 않았다** — 원본도 이미 `rA`/`rB` 를
  루프 밖에서 1회만 계산하고 있었다.
- D9a 자체 run 간 variation(7.5%)이 D9b 관측값과 같은 크기여서
  측정 오차 범위 안이다.
- 정확성: `verify_parity_test` 25 checks **double 동일** (5 seed × 5 aspect
  형태). CPU 77/77, GPU 78/78 PASS. Engine/DB/cache/schema 불변.
- **후보 A 는 여전히 deferred.** 이 실패는 A 의 상대 우선순위를
  올리지 않는다 — A 가 접촉하는 것은 verdict semantics 이기 때문이다.
- 남은 질문: 8.5 ms 의 실제 소비처는? `frame_ssim` 자체를 줄일 수 있는가?
  (윈도우/정밀도/early-exit 은 결과에 영향 → 별도 pre-register 필요)

### D9c — Expensive verify 내부 비용 계측 → 0.9.4.26, **PASS**

- Pre-register commit `39d2442` (코드 변경 전), baseline 은 계속 0.9.4.24 D9a.
- **instrumentation 빌드다. 성능 향상이 목표가 아니었다.**
- **핵심 결과: expensive verify 8.564 ms 중 decode 가 94.90 %.**
  `frame_ssim` 은 **0.55 %** — D9b 가 최적화한 그 단계다.
  ```
  decode             111,621.7 ms   94.90 %   8.1274 ms/verify
  key (stat+64KB)      4,421.9 ms    3.76 %   0.3220 ms/verify
  frame_ssim             645.2 ms    0.55 %   0.0470 ms/verify
  crop/aspect             201.9 ms    0.17 %   0.0147 ms/verify
  mirror flip             188.7 ms    0.16 %   0.0137 ms/verify
  cache store+copy         53.9 ms    0.05 %   0.0039 ms/verify
  other (나머지)          481.4 ms    0.41 %   0.0351 ms/verify
  합계                117,614.6 ms  100.00 %   8.564 ms/verify
  ```
  `sum == verifyMs` 정확히 성립 (초과 0.0000 ms).
- **D9b 가 왜 실패했는지 이제 증거로 확인되었다.** 채점 경로 전체가 0.55 %였고,
  decode 는 frame_ssim 의 약 173배였다. decode 를 건드리지 않는 후보는 남은
  5.1 % 보다 큰 것을 다룰 수 없다.
- D9a 에서 유도한 "약 8.5 ms" 가 처음으로 **직접 측정값(8.564 ms)** 이 됐다.
- 카운터에서 나온 구조적 사실 2가지:
  - **miss 1건당 같은 파일을 2번 디코드** (`verifyDecodes` 25,898 = 2×12,949).
    `decode` 와 `decodePreserveAspect` 를 같은 path 에 둘 다 호출한다.
  - **cache hit 에도 64KB quick-hash 를 읽고 해시.** lookup 27,468회 중
    hit 14,519회도 포함. 363.3 MB 를 읽고 해시했다. (비용 3.76 %)
- 미측정(추정하지 않음): `frame_ssim` 내부, decode 내부(4.31 ms 의 분해).
- 정확성: 기존 카운터 전부 D9a 와 동일 — verifyCalls 158,020, expensive 13,734,
  misses 12,949, hits 14,519, ssimEvals 137,340, frameSsimEvals 274,680,
  **groups 156,152**. `verify_parity_test` 25 checks PASS, 새
  `verify_instrumentation` PASS(단계 합 ≤ total, instrumented == uninstrumented).
  CPU 77/77, GPU 78/78. `--smoke` exit 0, `--version` 0.9.4.26.
- Engine/DB/cache/schema 불변, SSIM·threshold·verdict·candidate·grouping 무변경.
- **다음 후보: D (image decode 비용).** 단 먼저 4.31 ms decode 를 더 분해해야 한다.
  후보 A 는 계속 deferred. `frame_ssim` 단독 최적화는 0.55 % 라 가치가 없다.
- D9b 는 **NOT ACCEPTED 로 유지.** 성공으로 재분류하지 않는다.

### D9d — Decode 내부 분해 + cache mutex 계측 → 0.9.4.26, **PASS**

- Pre-register commit `6f0ee90`. **measurement 빌드, 최적화 없음.**
- **decode 내부 89.55 % 가 파일 open + WIC factory 생성 두 호출이다.**
  ```
  open    (CreateDecoderFromFilename)  71,865.2 ms  62.15 %  2.7749 ms/call
  factory (CoCreateInstance WIC)       31,674.5 ms  27.39 %  1.2230 ms/call
  copy    (CopyPixels = 실제 decode)    5,956.9 ms   5.15 %  0.2300 ms/call
  comInit                                492.7 ms   0.43 %
  resize  (Fant)                         369.0 ms   0.32 %
  convert                                311.7 ms   0.27 %
  metadata (EXIF orientation)            194.0 ms   0.17 %
  orient                                 15.4 ms   0.01 %
  합계                               115,628.4 ms  100.00 %  4.4654 ms/call
  ```
  `open + factory` = decode 의 89.55 % = **expensive verify 전체의 84.9 %**.
- **기각된 목표 3가지 (측정 근거 있음)**
  - `WICBitmapInterpolationModeFant` 0.32 % — 이름은 비싸 보이지만 369 ms.
    바꾸면 resize quality 즉 verdict 속성이 바뀐다.
  - EXIF skip 0.17 %.  - `frame_ssim` verify 의 0.53 %.
  - 실제 이미지 압축 해제인 `copy` 는 5.15 %. "decode 가 비싸다"는 직관이
    가리킨 곳은 비용의 20분의 1 이었다.
- **cache mutex: 병목 아님 (증거로 종결)**
  ```
  acquires 40,417 (조회 27,468 + 저장 12,949)
  waitMs   8.4  (0.0002 ms/acquire)   wait share 10.38 %
  holdMs  72.4  (0.0018 ms/acquire)   hold share 89.62 %
  ```
  lock 총시간 80.8 ms = decode 의 **0.07 %**. D9c 가 답하지 못했던 질문이 닫혔고,
  D9c 의 `other`(0.42 %)에 숨은 contention 이 **없음**도 확인된다.
- decode 25,898회 = 12,949 × 2 (같은 파일을 miss 마다 2회 디코드).
  WIC 성공 25,898, PGM fallback 0, 실패 0, orientation 적용 0.
- `sub-sum 110,879.4 vs total 115,628.4` — 4.1 % 갭은 timer 오류가 아니라
  계측 안 된 호출 간 코드(할당, GetSize, HRESULT 검사)다. 단계에 흡수시키지
  않고 그대로 보고.
- 정합성: 전 카운터 D9a 동일, **groups 156,152**, 5회 모두 동일.
  D9c exclusive 합계도 유지(`sum 121,906.2 == verifyMs`, 초과 0.0000).
  parity 25 PASS, CPU 79/79, GPU 80/80, `--smoke` exit 0.
- 미측정(추정하지 않음): decode 시간의 4.1 %, `open` 과 `factory` 의 내부,
  decodeWicFileAspectColor 는 의도적으로 계측하지 않음(UI 스레드).
- wall clock 은 D9a/D9b 범위보다 높지만 **회귀가 아니다** — instrumented 빌드
  이며 D9c 도 이미 범위 상단이었다. 신뢰할 신호는 단계 비중이다.
- noise 6.3 % (5회, 117,392.9–124,839.3 ms). 호출 횟수는 5회 모두 결정적.
- **다음 후보 D1 — image decoder 생성과 file open.** 먼저 2.77 ms `open` 과
  1.22 ms `factory` 를 더 분해해야 한다. 구현하지 않았다.

### D1 — File open / WIC factory 원인 계측 → 0.9.4.27, **PASS**

- Pre-register commit `f4f8ebd` (docs only, 소스 변경 없음).
- **D9d 수치 재검산 (§7 지시)**: `open+factory` = 103,539.7 ms.
  decode 115,628.4 대비 **89.55 %** (D9d 문서는 89.5452 의 절삭 89.54 였음 →
  **89.55 로 정정**), verify 합계 121,906.2 대비 **84.93 %** (D9d 표기 84.9 는
  정확). 두 문서 모두 수정.
- **질문 B — Factory2 는 정상 동작, fallback 은 0회**
  ```
  Factory2 attempts  25,898
  successes          25,898  (100.00 %)
  fallbacks               0  (0.00 %)
  ```
  측정된 factory 비용은 활성화 1회지 2회가 아니다. "실패하는 첫 시도가
  낭비다"는 가설이 **기각**되었다. 이 머신에서 제거할 낭비가 없다. fallback 은
  이식성 가드이므로 코드에 유지한다.
- **질문 A — open 의 97.8 % 는 WIC 고유, OS 파일 I/O 아님**
  ```
  CreateDecoderFromFilename  68,000.8 ms  2.6257 ms/call
  OS CreateFileW 참조        1,490.5 ms  0.0576 ms/call
  WIC 고유 나머지           66,510.3 ms  2.5682 ms/call  = open 의 97.8 %
  ```
  실제 OS 파일 열기 비용은 0.0576 ms(2.2 %)뿐이고 나머지는 WIC 내부 작업이다.
  "file open" 이라는 이름이 오해를 만들었다. 참조 프로브가 없었으면 2.6 ms 의
  COM 작업이 "파일 열기"로 귀속되어 파일 핸들 최적화가 그럴듯해 보였을 것이다.
- **이 작업이 잡은 버그**: 첫 구현이 `factoryMs = 235,023,895 ms` 보고.
  헬퍼가 누적 합계에 기록하는데 호출 지점이 그 합계를 매번 다시 더해 제곱으로
  증가. pre-register 중단 조건 5번(분해가 factoryMs 재구성 실패)이 정확히
  이를 잡았다. 수정은 호출당 델타. 조용히 고치지 않고 기록함.
- factory 분해: Factory2 attemptMs 17,769.0 (0.6861 ms/attempt), fallback 0.
  `split 17,769.0 == factoryMs 17,769.0` over 0.0000.
- **D9d 대비 factory 감소(31,674.5→17,769.0)는 성능 개선이 아니다.** 같은 이중
  계수 버그의 영향이며 측정 차이로 기록한다(회귀도 개선도 아님).
- 실행 통계(5회): mean 119,608.3 / median 120,346.2 / min 116,839.9 /
  max 121,273.4 / range 4,433.5 ms = median 의 3.68 %.
- 정합성: 전 카운터 D9a 동일, **groups 156,152**, 5회 동일. D9c verify 합계
  유지(`sum 116,676.9 == verifyMs`, over 0.0000). 중복 decode 12,949x2 유지.
- 검증: parity 25 PASS + instrumentation, tr_keys 21 PASS (GUI fix 회귀 없음),
  schema 29 `additive-read-confirmed` (v9 유지), analyze_telemetry 41 PASS,
  CPU 79/79 PASS. GPU 는 core + 전 test target 통과. **GUI exe 는 사용자가
  실행 중인 0.9.4.26 세션이 파일 락을 잡고 있어 relink 불가** — 종료시키지
  않고 그대로 두었다(코드 실패 아님, 빌드 환경 제약).
- **측정으로 제거된 후보 2개**: Factory2 fallback 제거(0회), 파일 핸들/스트림
  관리 최적화(OS 2.2 %).
- **다음**: WIC decoder 생성. 2.5682 ms 의 WIC 고유 부분을 먼저 분해해야 한다.

## 성능 실험 장기 기록 체계 (0.9.4.27 D1 이후)

성능 튜닝/프로파일링 실험은 **성공 여부와 무관하게** 기술 기록으로 보존한다.
새로운 문서 종류는 만들지 않고 다음 역할로 나누어 기록한다.

```
Implementation Brief  무엇을 시험할지 사전 정의
Build History         실제 수치·실행 조건·기각 근거·재검토 조건  ← 상세 기록
Work Log Index        실험 계보와 살아 있는/기각된 후보 연결
Development Progress  현재 상태 요약 (수치 반복 금지)
```

**기각된 후보는 삭제하지 않는다.** `NOT ACCEPTED` / `REJECTED` / `DEFERRED` /
`LOW PRIORITY` 는 현재 조건의 판정이지 영구 폐기가 아니며, 환경이 바뀌면 다시
유효해질 수 있다. 계보와 후보 상태는
`docs/worklog/0.9.4.*.md` 의 **Performance / Tuning Experiment Index** 를
참조한다.

```
D9a BASELINE → D9b NOT ACCEPTED → D9c PASS → D9d PASS → D1 PASS → D2 PASS
측정으로 기각된 것: cache 32 · scoring 중복 · Fant · EXIF
                     cache mutex · Factory2 fallback · raw OS file open
보류된 것: 후보 A(DEFERRED) · 후보 C(Path C, DEFERRED)
```

현재 상태 요약 (상세 수치는 Build History 참조)

```text
D9b = NOT ACCEPTED   (정확성 보존, 성능 목표 미달)
D9c = PASS           (decode 94.90 %)
D9d = PASS           (open 62.15 % + factory 27.39 %)
D1  = PASS           (Factory2 100 % 성공 / fallback 0 회, open 의 97.8 % WIC 고유)
D2  = PASS           (7개 형식 전부 Stream 경로 최단 → 단, 절대 이득 소액 + 유지보수
                       비용 → Path C = DEFERRED, 제품 채택 없음)
D3  = PASS           (두 번째 decode = verifyDecodeMs 49.60 % / verifyMs 39.19 %,
                       단 f/a 가 다른 지오메트리라 "제거" 는 정확성 회귀)

Current active investigation:
후속 optimization pre-register (작성됨, 측정 전)
  후보: 고해상도 decode 1회 + resize 2회로 f(고정 32×32)와 a(aspect)를 모두 생성
  계약: `docs/implementation-briefs/I-decode-once-resize-twice.ko.md`
  D3 은 계측만 했다. 중복 제거·캐시·호출 병합·HandleStream 채택은 하지 않았다.
  이 후보는 accuracy risk 가 높으므로(groups 불변 확인이 수용 조건) 측정 결과를
  먼저 기록한 뒤 별도로 생산 적용을 판단한다.
```

## D2 — WIC decoder 진입 경로 비교 (0.9.4.28, **PASS**)

- 목표: `CreateDecoderFromFilename` vs `CreateDecoderFromFileHandle` vs
  `CreateDecoderFromStream` 의 decoder 생성 비용 비교. **measurement-only.**
- 결과: `CreateDecoderFromStream` 이 7개 형식(bmp/jpg/png/webp/gif/tiff/ico)
  전부에서 decoder 단계 최단, combined 기준 A 대비 약 16~30 % 낮음.
- **제품 채택하지 않음.** 절대 이득이 작음(파일당 ~0.02 ms) + 직접 구현한
  `HandleStream` 유지보수 비용 + production full-path benchmark 미측정.
  → **Path C = `DEFERRED`** (폐기 아님, 재검토 조건 명시).
- **D1 과의 직접 비교 금지.** D1 의 2.6257 ms/call 은 코덱 DLL 최초 로드를
  포함하고 D2 는 warm state 측정이다. **측정 조건이 다르므로 비교 불가하며,
  성능 개선이 아니다.**
- 구현 중 실제 버그 3건 발견 및 영구 기록: Path B handle lifetime
  (use-after-close), Path C 잘못된 stream 연결, probe 의 `CoUninitialize()`
  (COM 수명은 caller 소유).
- dataset 에 TIFF 14 + ICO 12 추가 → 3,347 files / 102,475,315 bytes,
  fingerprint `e8f8fa6a…e2640a`. dataset 이 바뀌었으므로 D2 절대 합계는
  D1 과 직접 비교하지 않는다.
- 검증: CPU 79/79 PASS, GPU 80/80 PASS, `--version` 0.9.4.29.
- **다음**: D3 pre-register (`docs/build-history/0.9.4.29.*.md`) 를 별도로 작성.

## D3 — decode / decodePreserveAspect 중복 비용 계측 (0.9.4.29, **PASS**)

- 목표: verify miss 경로가 같은 파일을 두 번 decode 하면서 발생하는 실제
  wall-clock 비용 계측. **measurement-only.**
- dataset: D2 최종 dataset 고정 (3,347 files, `e8f8fa6a…e2640a`),
  warm-up 1회 제외 + measured 5회.
- 계측 전 코드 확인 결과, 두 호출이 `image_verify.cpp:78` 에서 **같은
  `&tel->decode`** 를 받아 D9c/D9d 의 "decode 94.90 %" 가 두 호출의 합이었다.
  D3 는 이 누산기를 호출별로 분리하고 `mergeDecodeTelemetry` 로 합쳐 D9d 키
  의미를 그대로 보존했다.

| metric | 값 |
|---|---:|
| `decode()` / `decodePreserveAspect()` 호출 | 12,962 / 12,962 (5회 전부 동일) |
| `decode()` per call | 0.8700 ms |
| `decodePreserveAspect()` per call | 0.8568 ms |
| 두 번째 / 첫 번째 | 98.49 % |
| 두 번째 / `verifyDecodeMs` | **49.60 %** |
| 두 번째 / `verifyMs` | 39.19 % |
| 두 번째 / `analyzeMs` | 38.47 % |
| (두 호출 합) / `verifyDecodeMs` | 99.96 % |
| split identity `decodeSplitOverMs` | 0.000000 |

- **"중복" 이라는 표현은 부정확하다.** `f` 는 고정 32×32, `a` 는 aspect 보존
  축소이며 둘 다 `verifyScorePlan` 이 소비한다. 두 번째 decode 를 제거하면
  `a` 가 사라져 similarity/verdict 가 바뀐다.
- 따라서 후속 후보는 "제거" 가 아니라 **"한 번의 고해상도 decode 로 두 벌
  생성"** 이며, **D3 에서 구현하지 않았다**(pre-register §11).
- groups 5회 모두 156,211 (D9a 156,152 와의 차이는 D2 의 dataset 추가분).
- 구현 중 발견한 결함 2건을 영구 기록: pre-register §5.1 의 카운터 오류,
  그리고 `ScanPipeline::analyze` 의 멤버 단위 전사 누락으로 첫 실행이 조용한
  0 을 낸 문제.
- **다음**: 후속 optimization pre-register (작성됨,
  `docs/implementation-briefs/I-decode-once-resize-twice.ko.md`).

기록 의무와 필수 항목은 `AGENTS.md` 9번 항목과
`docs/document-naming.*.md` 2-1절에 정의한다.

## 0.9.4.30 — CPU 사용량 10~90 자동 정규화

- 사용자 CPU 스핀박스, 저장된 `ResourcePolicy.cpuPercent`, 엔진 입력이 항상
  같은 `10~90` 값을 사용한다.
- 프리셋 값, GPU 정책, 스케줄러, 워커 배치 계산식은 변경하지 않았다.
- CPU 정책은 `QSettings`에 저장되지 않으므로 마이그레이션이 필요 없다.
- 검증: CPU 80/80 PASS, GPU 81/81 PASS, `--version` 0.9.4.30.
- D3 후속 최적화 후보와 무관하며 성능 향상을 주장하지 않는다.

## 0.9.4.31 — D3 후속 공유 decode 후보 측정

- pre-register `docs/implementation-briefs/I-decode-once-resize-twice.ko.md`를
  측정 코드보다 먼저 커밋했다.
- `tests/shared_decode_probe.cpp` (측정 전용, 생산 코드 변경 없음)로 기준선
  2회 decode와 후보(공유 decode 1회 + 메모리 Fant 파생 2회)를 849 파일 ×
  5 해상도 × 5회 측정했다.
- 후보는 기준선 대비 24~46 % 저렴하나 바이트 재현이 안 되고 R≥384에서 verdict
  flip 2건(TIFF 근사중복 쌍, 5회 재현) — 후보 DEFERRED, 생산 미채택.
- 검증: CPU 80/80 PASS, GPU 81/81 PASS, `--version` 0.9.4.31,
  probe `--selfcheck` 14 checks.
- **0.9.4.32에서 일부 정정됨**: 위 "verdict flip 2건"과 "R 증가 → delta 증가
  (2.63→9.19)"는 probe artifact다 (기준선 버퍼에 후보 버퍼가 섞임). 성능 수치는
  그대로 유효하다(측정 전용 probe 값이며 제품 성능 아님).

## 0.9.4.32 — D3 후속 후보 안정성 · 리샘플 차이 원인

- pre-register `docs/implementation-briefs/I-shared-decode-stability.ko.md`를
  조사 코드보다 먼저 커밋했다.
- 측정 결함 발견: 전체 측정의 기준선 점수에 현재 파일의 기준선 버퍼와 이전
  파일의 **후보** 버퍼가 함께 들어갔다. "기준선"이 어느 쪽 세계도 아니었다.
- pure pairing(양쪽 기준선 vs 양쪽 후보)으로 재측정 — 조사한 8개 R
  (128·192·256·288·320·352·384·512) 전부 verdict flip **0건**, max abs delta
  **1.972205~2.557407**(R 에 대해 단조 증가 아님).
- 원인 규명: 2단계 Fant 체인 + 중간 8-bit GrayImage 양자화 + 서로 다른
  resampling chain. scoring 단계의 `centerCropResize`(정수 nearest-neighbor)는
  그 차이를 crop 으로 전달할 뿐 최초 원인이 아니다.
- 조사한 R 범위(R128~512)에서는 byte parity 가 복원되지 않았다. 이 범위를
  넘어선 불가능성은 증명되지 않았다.
- verdict 비교는 해상도당 **1691 sampled pairs** 에서 수행했고 **전스캔 groups
  비교는 수행하지 않았다.** EXIF 방향·PGM fallback fixture 가 없어 해당 경로는
  미검증이다.
- 후보 `DEFERRED` 유지, production adoption NO. revisit 우선순위는 ① full-scan
  groups 비교 ② near-threshold fixture ③ EXIF fixture ④ PGM fixture
  ⑤ 비변경 정확성 검증 구조. 정정 기록은
  `docs/build-history/0.9.4.32.ko.md` §2.
- 검증: CPU 80/80 PASS, GPU 81/81 PASS, `--version` 0.9.4.32,
  probe `--selfcheck` 14 checks, `decomp_mismatch=0`.
- 버전 문자열 갱신만으로 커밋 16e1240, 태그 v0.9.4.32. 생산 알고리즘 변경 없음.

## 0.9.4.33 — I-2: 공유 WIC source + 독립 2개 scaler 후보

- pre-register `docs/implementation-briefs/I-shared-wic-source.ko.md`를 probe
  구현보다 먼저 커밋했다.
- 후보 구조: factory·decoder·frame·EXIF orientation source 만 1회 공유하고,
  scaler/converter/`CopyPixels` 는 f 용과 a 용으로 **각각 독립 생성**한다.
  **중간 `GrayImage` 이 없다** — I-1 의 2단계 Fant 체인이 구조적으로 제거된다.
- **정확성 (전체 dataset, CPU 5회 + GPU 5회 전부 동일)**
  - f geometry 849/849, f byte 849/849
  - a geometry 849/849, a byte 849/849
  - geometry·pixel 불일치 **0건** (I-1 은 R128 에서 geometry 93건)
  - baseline/후보가 **동일한 5개** 파일에서 둘 다 실패
    (`WINCODEC_ERR_FRAMEMISSING`). **후보만 실패하는 파일 0건.**
- **성능 (measurement-only probe)**: CPU 40.8~42.2 %, GPU 41.5~42.6 % 감소.
  두 번째 `CopyPixels` 가 평균 3.88→1.09 ms — WIC 가 실제 decode 를 공유한다.
- **검증 중 발견한 실제 결함 2건** (자기 정합성 검사로는 잡히지 않음):
  ① 살아 있는 WIC 객체가 있는 상태에서 `CoUninitialize` 호출 → 접근 위반.
     제품 소스가 경고하는 실패 그대로였고, WIC 객체를 헬퍼가 소유하도록
     구조를 바꿔 해결. ② 루프의 `if (!okCand) continue` 가 baseline 실패를
     상쇄해 `baseFail=0` 이라는 잘못된 결론을 만들 뻔했다. 양쪽 독립 기록 +
     `candOnlyFail` 카운트로 수정.
- **EXIF 종단간 검증은 `not_measured`**: 합성 fixture 8개가 byte 동일은
  하나, 그러나 제품이 orientation 을 적용한 횟수가 0이라 EXIF 분기가
  실행되지 않았다. WIC 가 4종 query path 전부에서 `PROPERTYNOTFOUND` 를
  반환하며, 이는 후보만의 문제가 아니라 **제품 EXIF 경로가 dataset 에서
  실행된 적이 없을 가능성**도 시사한다. 별도 과제로 등록.
- full-scan groups 비교는 수행하지 않았다. EXIF 미검증을 감안하고 별도
  production implementation brief 작성과 함께 판단한다.
- **판정: 정확성 PASS / production adoption NO.** threshold 조정이나 score
  tolerance 완화는 하지 않았고 필요하지도 않았다.
- 검증: CPU 80/80 PASS, GPU 81/81 PASS, 양쪽 `--version`/`--smoke` exit 0,
  새   probe `--selfcheck` 11 checks, 기존 probe selfcheck 14 checks 회귀 확인.
  **생산 코드 변경 없음.** 상세: `docs/build-history/0.9.4.33.ko.md`

## 0.9.4.34 — I-2 검증 보완 (전체 corpus · EXIF 정정 · full-scan groups)

- **정정 1 — "전체 dataset" 표현 오류.** 0.9.4.33 의 849 파일은 표준
  dataset 3,347 개가 아니라 `images/format` + BMP stride 샘플로 구성된
  **probe corpus** 였다. 표준 dataset 전체로 재측정:
  `both_success=3341, baseline_only_fail=0, candidate_only_fail=0, both_fail=6`
  (추가된 1건은 `images/format/SOURCES.md` — 문서 파일). f/a
  geometry·pixel 모두 **3,341/3,341**.
- **정정 2 — EXIF 진단 결함 2건.**
  ① query path 를 `const char*` 로 보관하고 `LPCWSTR` 캐스트로 조회 →
     **허위 음성**(PROPERTYNOTFOUND). 올바른 wide 리터럴로 재측정.
  ② applied counter 가 절대 증가하지 않는 변수를 출력 → 항상 0.
  정정 후 **EXIF fixture 는 8/8 유효**했고 값이 1~8 로 일치했다.
- **부수 발견 (제품 결함).** 제품이 쓰는
  `/app1/ifd/exif/{ushort=274}` 는 WIC 가 `WINCODEC_ERR_BADPROPERTYKEY` 로
  거부한다(8/8). 동작하는 경로는 `/app1/ifd/{ushort=274}` 다.
  I-2 와 무관한 **제품 EXIF 경로 결함**이며, 이번에 production 코드를
  변경하지 않았으므로 아직 수정되지 않았다. 0.9.4.33 의 "제품 EXIF 가 동작하지
  않는다" 는 관측이 아니었고 근거도 없었다.
  → **0.9.4.35 에서 수정됨** (아래 절 참조).
- **I-2 구조는 회전 하에서 통과.** fixture 가 선언한 변환을 강제 적용해
  rotation 을 실제로 수행한 뒤, 공유 source + 독립 scaler 2개와 완전 독립
  pipeline 2개를 f/a 모두 byte 비교 → **7/7 동일**.
- **full-scan groups 전수 비교.** 양쪽 모두 제품 판정 함수 `verifyScorePlan`
  사용, prefilter 없이 전수 열거. `pairs_compared=5,579,470`,
  `baseline_groups=candidate_groups=457,126`, `verdict_diffs=0`,
  `max_abs_score_diff=0.000000000`.
- **성능 (full corpus 3,341 기준, probe corpus 와 비교 불가)**
  CPU raw 47.6~48.2 % / adjusted 32.0~32.6 %, GPU raw 47.2~48.9 % /
  adjusted 31.5~32.2 %. 두 번째 `CopyPixels` 가 평균 0.8354→0.2834 ms 로
  감소하는 것은 **이 환경·이 데이터셋에서의 관측**이며 일반화하지 않는다.
- **판정: `READY FOR PRODUCTION IMPLEMENTATION REVIEW`, 반영 미수행.**
  I-1 은 `DEFERRED` 유지.
- 검증: CPU 80/80 PASS, GPU 81/81 PASS, 양쪽 `--version`/`--smoke` exit 0,
  새 probe selfcheck 11, 기존 probe 14, telemetry 25/29/71,
  `git diff --check` 통과. **생산 코드 변경 없음.**
  상세: `docs/build-history/0.9.4.34.ko.md`

## 0.9.4.35 — EXIF Orientation query path 결함 수정과 post-fix 회귀

- pre-register `docs/implementation-briefs/I-exif-orientation-path-fix.ko.md`를
  production 수정보다 먼저 커밋했다.
- **조사 결과**: 잘못된 리터럴은 `src/image_decoder.cpp` 의 3곳(고정 지문,
  aspect 지문, 표시 color)이고 모두 동일 로직 복제본이었다. 행 단위 수정 3번이
  아니라 **공유 헬퍼 1개**로 통합했다.
- **최소 수정**: `/app1/ifd/{ushort=274}`(JPEG) → `/ifd/{ushort=274}`(TIFF)
  순서로 시도하고 **값이 나오면 즉시 반환**. 컨테이너 분기 없음, XMP 없음,
  새 framework 없음. 표시 경로도 함께 수정(회전 사진이 지문과 다르게 보이면
  화면과 매칭이 어긋난다).
- **Gate A PASS** — fixture orientation 1~8: metadata 8/8, 값 8/8 일치,
  baseline 적용 7/7(1은 항등이라 정상적으로 미적용), decode 성공, 90/270 에서
  소스 209x248→248x209 로 실제 회전 확인.
- **Gate B PASS** — f/a byte parity 8/8. production 과 candidate 가 같은
  orientation 을 읽고 같은 변환을 수행.
- **Gate C PASS** — groups 전수 5,579,470쌍 결과가 수정 전후 **동일**
  (457,126 / verdict diff 0 / max score diff 0.000000000).
  - 표준 dataset EXIF 재측정: metadata 보유 8건, **전부 orientation 1**,
    적용 대상 0, query 실패 0. **dataset 에 회전 이미지가 없다**는 것이
    scan 이 변하지 않는 이유이며, 이는 측정 결과다.
  - 그 7개 TIFF 파일이 `/ifd/` 경로가 실제로 동작함을 증명한다.
- **비용**: 수정 자체 비용을 분리 측정 — one_path 0.01678 ms vs two_paths
  0.04547 ms, **파일당 0.02870 ms**. 값 나오면 즉시 반환하므로 JPEG 는 이
  비용을 지불하지 않는다. corpus 비용은 CPU raw 46.8~49.4 %,
  GPU raw 46.9~50.4 % 감소로 v0.9.4.34 수준 유지.
- **회귀 테스트 추가**: selfcheck 에 EXIF 회귀를 넣고 **CTest 에 등록** —
  selfcheck 11→16 checks, CPU 80/80→**81/81**, GPU 81/81→**82/82**.
  orientation 1 미적용 / orientation 6 적용 / 회전이 픽셀을 실제로 바꾸는지 /
  회전 파일 baseline-candidate parity 를 검증한다. 이 테스트가 없으면 결함이
  조용히 돌아올 수 있었다.
- **Gate D PASS — I-2 = `READY FOR PRODUCTION IMPLEMENTATION`.**
  단 **production I-2 통합은 수행하지 않았다**(`NOT PERFORMED`,
  adoption NO). 다음 단계다.
- **남긴 과제**: dataset 에 회전 이미지 없음(fingerprint 변경 수반),
  XMP fallback 미구현.
- dataset 규칙 준수: fingerprint `e8f8fa6a..e2640a` 유지, `SOURCES.md` 유지,
  `both_fail` 6건 유지.
- 상세: `docs/build-history/0.9.4.35.ko.md`


## 2026-09-30 — Benchmark / Console CLI 추가 설계 결정

현재 F-1은 NVDEC production adoption이 허용되지 않는 상태이므로 Benchmark / Console CLI 인프라는 독립적인 cross-cutting track으로 정리할 수 있다. 이 작업은 F-2 NVDEC production integration 허용을 의미하지 않는다.

### 확정 사항

1. Benchmark mode는 AUTO / CPU 단독 / GPU 최대화의 세 가지로 고정한다.
2. 기존 GUI의 이미지/동영상 선택은 mode와 별도의 축으로 유지한다.
3. Console은 동일한 media scope를 제공한다.
   - --media images
   - --media videos
   - --media all
   - 기본값 all
4. GUI는 source folder마다 AUTO / CPU / GPU-max 최신 결과 최대 3개만 자동 유지한다.
5. Console은 장기 데이터 마이닝 저장소이며 자동 삭제하지 않는다.
6. Normal Search Index와 Benchmark index/cache는 분리한다.
7. Run = 한 mode의 한 측정, Suite = 같은 dataset/media scope의 mode 묶음으로 정의한다.
8. 각 Run은 sourceRoot, dataset fingerprint, mode, mediaScope, CPU/GPU/scheduler 설정, 환경 정보, 완료/실패/fallback 상태를 저장한다.
9. GPU 최대화는 GPU-only가 아니며 필수 CPU 작업과 CPU fallback을 유지한다.
10. Benchmark Run은 독립 프로세스를 우선 검토하고, OS filesystem cache는 uncontrolled 상태로 기록한다.

### 빌드 / 구현 스케줄

버전 번호는 미리 배정하지 않는다.

~~~text
S0  설계 / pre-register
 ↓
S1  Console entry foundation
 ↓
S2  Run / Suite benchmark core
 ↓
S3  Benchmark storage isolation
 ↓
S4  GUI benchmark integration
 ↓
S5  Console benchmark execution
 ↓
S6  Data-mining automation
 ↓
S7  Help / usability
 ↓
S8  Full verification / release gate
~~~

각 단계는 기존 운영 규칙에 따라 다음 순서로 닫은 뒤 다음 단계로 이동한다.

소스 변경 → CPU/GPU 빌드 → CTest → 실행 검증 → 문서 갱신 → 필요 시 Build History → commit

이 기록은 구현 완료나 성능 측정 결과가 아니라 **설계 확정과 구현 순서 등록**이다. 상세 계약은 docs/architecture/benchmark-telemetry-roadmap.*에 기록한다.

## 2026-09-30 — Benchmark Console 설계 최종 확정

Benchmark/Console 설계를 구현 전에 최종 확정했다.

핵심 결정:
- 파일 단위 AUTO → CPU → GPU-max 실행
- 파일별 완료 결과 즉시 append-only journal 저장
- Ctrl+C 취소 및 부분 Suite 보존
- 기존 CPU Resource Policy 재사용, 권장 기본 Balanced 55%
- Maximum 결과는 실제 측정값으로만 취급하며 단순 선형 추정 금지
- Interactive 상단 고정영역은 3줄이며 절대 자동 줄바꿈하지 않음
- Target 경로는 화면에서 middle ellipsis 가능, JSON에는 원문 저장
- 상단은 실행 조건과 IMG/VID 진행률, 하단은 CURRENT FILE 상세와 compact 완료 이력으로 역할 분리
- TTY/non-interactive는 서로 다른 표시 방법을 사용하지만 동일 journal/JSON을 기록
- 기존 benchmark source/schema 및 v0.9.4.43 baseline을 legacy로 영구 보존

다음 구현 단계는 S1 Console entry foundation이며, benchmark core는 이후 S2에서 시작한다. 이번 결정은 source implementation 변경이 아닌 문서상의 설계 게이트다.
### Console Benchmark 목업 추가

최종 Console UI 계약을 검토하기 위한 정적 목업을 추가했다.

- HTML: `project/uimock/benchmark-console-mockup.html`
- JPG preview: `benchmark-console-mockup.jpg` (현재 작업 환경의 검토용 artifact)
- 기존의 GUI와 유사하게 보이던 초기 SVG console 시안은 최종 Console 목업에서 제거했다.

목업은 실제 실행 기능을 의미하지 않으며 S1/S2/S5 구현 시 참조용이다.

## 2026-10-01 — S5 Console benchmark execution 구현 및 검증

S5 를 S5-1(CLI) / S5-2(renderer) / S5-3(실행 연결) 세 커밋으로 나누어 구현하고
실제 `--benchmark` 를 실행해 검증했다. 상세 기록은
`docs/build-history/S5-verification.ko.md` / `.en.md` 에 있다.

기준 커밋:

```text
f4c3fdd  S5: add benchmark CLI parsing
fcace68  S5: add console benchmark renderer
842ba01  S5: connect console benchmark execution
```

구현 범위:

- **S5-1**: `--benchmark <folder>` + `--mode/--suite/--log-dir/--log` 파싱.
  `--mode` 는 단일 쉼표 목록이고 canonical order 로 정규화한다. 잘못된 입력은
  조용히 무시하지 않고 모두 인자 오류로 거절한다. 40 → 95 checks.
- **S5-2**: Qt-free 표시 전용 renderer `src/benchmark_console_renderer.*`.
  presentation model 은 S2 의 `GpuBackendKind` / `BenchmarkStatus` / `MediaKind` 를
  복사하지 않고 재사용하므로 S2 가 생산하지 않은 상태를 표현할 수 없다.
  모든 관측 값은 `std::optional` 이며 부재한 값은 생략한다(0 이나 `-` 로 대체하지 않음).
  100 checks.
- **S5-3**: `runConsoleBenchmark()` 가 S3 `BenchmarkSession` → S2 `BenchmarkRunner`
  → 제품 scan 경로를 그대로 재사용한다. 새 engine 이 없다. suite id 자동 생성,
  `--log-dir` storage root override, `--log` renderer sink, Ctrl+C 연결을 포함한다.
  32 checks.

E2E 중 발견하고 수정한 결함:

- `src/scanner.cpp` 의 `Scanner::scan_stream()` 이 `FileState.kind` 를 설정하지 않아
  `Unknown` 이 남았고, 그 결과 benchmark 의 media filter 가 한 번도 발동하지 않았다.
  `--media images` / `videos` / `all` 이 모두 동일한 10개 파일을 처리했다.
- 제품에 이미 있던 `isVideoPath()` 규칙을 그대로 재사용해 1줄로 복구했다.
  새 classifier 를 만들지 않았고 `toMediaKind()` 도 변경하지 않았다.
- 영향 범위는 코드 추적으로 확인했다. Scanner 의 `kind` 를 읽는 곳은
  `benchmark_core.cpp` 의 `toMediaKind` 호출 **한 곳뿐**이고,
  `MediaSearchEngine` 은 `kindOf(path)` 로 자체 재계산하므로
  **production indexing/search 영향 없음**이다.
  (초기 "제품 전체 image/video 구분 손상" 추정은 오류였으며 정정한다.)
- 회귀 테스트는 수정을 비활성화하면 exit=3 으로 실패하고 복원하면 exit=0 으로
  통과함을 확인해 실제로 결함을 잡는다.

실측 결과:

- `--media`: images 8 case(`Image=8`), videos 2 case(`Video=2`), all 10 case(`Image=8, Video=2`)
  — 실제 dataset 구성(Image 8, Video 2, Total 10)과 일치. 옵션 순서 무관성도 확인.
- 기본 3-mode: exit 0, Cases 10, Records 42, journal sequence 정상.
- suite: explicit 값이 suite.json / runs.jsonl / summary.json 3곳 일치.
  자동 생성은 `YYYYMMDD-HHMM-SS`(UTC). `..\..\evil` 형식은 exit 2 로 거부.
- `--log-dir` 는 기본 저장소를 오염시키지 않고, `--log` 는 ANSI 없는 text 산출물이다.
- CPU 빌드는 AUTO / GPU-max 가 `SKIPPED`, GPU 빌드는 `effectiveMode=CUDA` 로 `SUCCESS`.
- CPU CTest 96/96, GPU CTest 97/97, 양쪽 build exit 0.

미실행 검증( PASS 로 기록하지 않는다):

- 실제 TTY ANSI repaint: **NOT RUN** — 검증 환경에 Windows console 이 없었음.
- 실제 Windows Ctrl+C trigger: **NOT RUN** — 같은 사유. 프로세스 kill 로 대체하지 않음.

S5 상태: **기능 구현 완료, 자동/비대화형 E2E 검증 완료.** 위 두 항목만 미실행으로 보존한다.

별도 부채로 기록한 항목: S3 `benchmarkNowStamp()` 가 `localtime_s` 결과에 literal `Z` 를
붙여 journal timestamp 가 UTC 를 표기하면서 로컬 시각을 담는다. S5 suite id 는 진짜 UTC 라
약 9 시간 차이가 난다. 이번 S5 에서는 `benchmark_store.cpp` 를 변경하지 않았다.
## 2026-10-01 — S6 Data-mining Automation 설계 조사 및 brief 작성

S6 의 설계와 implementation brief 를 작성했다. **이번 단계에서 S6 코드는 작성하지 않았고,
S6 가 CLOSED 도 아니다.** 상세는 `docs/implementation-briefs/S6-data-mining-automation.ko.md` /
`.en.md` 에 있다.

설계 조사로 확인한 사실:

- **journal 을 읽는 분석 도구가 저장소에 존재하지 않는다.** journal 을 읽는 유일한 코드는
  `replayJournal()` + `buildSummaryJson()` 이며 출력은 카운트와 `totalElapsedMs` 합계뿐이다.
- **Python 이 프로젝트에 없다.** `.py` 0개, `requirements.txt` / `pyproject.toml` /
  `Pipfile` / `setup.py` 0개, CI 두 개 모두 `shell: pwsh`. 스크립트 관례는 PowerShell 이다.
- **journal schema 실측**(29개 journal / 862 line 전수 키 스캔). `run_started` /
  `mode_result` / `case_complete` / `run_finished` / `run_cancelled` 의 실제 필드를
  brief 6장에 전부 기록했다.
- **최대 제약**: `git` / `resourcePolicy` / `cpuPercent` / `gpuPercent` / `gpuEnabled` /
  `distance` 가 **모두 0건**이다. `MSF_BUILD_GIT` 가 generated header 에만 있고 journal 에는
  기록되지 않기 때문이다. 따라서 `buildVersion` 만으로는 같은 버전의 다른 커밋을 구분할 수 없고,
  roadmap §25 저장 요구와 §28/§30 의 S6 핵심 정의(비교 요약)를 현 상태로는 완수할 수 없다.
  brief 22장 진입 조건 ① 에 최대 리스크로 명시했다.
- `run_cancelled` 에는 `completedAt` 필드가 없고, **실측 표본이 0건**이다.
- journal `timestamp` 는 `localtime_s` 값에 literal `Z` 를 붙인다. 저장소의 timestamp 는
  네 갈래로 갈라져 있으며(실측 확인), S6 는 이를 숨기지 않고 §16-2 규칙(`suiteId` 정렬,
  `completedAt - startedAt` 만 duration)으로 우회한다. S3 코드는 변경하지 않는다.
- legacy `BenchmarkRecorder`(schema 9)는 journal(schema 1)과 완전히 별개이며 파일도 쓰지
  않고 읽는 코드도 없다. S6 는 읽지도 쓰지도 않는다.

설계 결정:

- journal 파서를 새로 만들지 않고 `replayJournal()` 을 재사용한다(S3 recovery 규칙 이중화 방지).
- **measured / derived / invalid** 3분류를 강제하고, invalid 제외 건수를 반드시 출력한다.
- 회귀 판정 threshold 는 **정하지 않았다**. `AGENTS.md` 9항이 이미 기준이고,
  `worklog` `E-3B-BUG` 에 임의 threshold 제거 선례가 있다.
- `S2-PERF` 의 uncontrolled cache/부하 때문에 회귀 **판정** 이 아니라 회귀 **후보 + 조건 경고** 를
  제공한다.
- 산출물은 machine-readable + human-readable 2계층이며, 두 출력은 같은 계산 결과에서 나온다.
- 기본 출력에는 analysis timestamp 를 넣지 않는다(재현성). `--provenance` 로만 켠다.

S6 내부 작업 분해안(공식 roadmap node 가 아님): S6-1 schema 계약 → S6-2 ingestion/normalization →
S6-3 grouping → S6-4 aggregation → S6-5 cross-run comparison → S6-6 regression 후보 → S6-7 전체 검증.

DEFERRED 로 기록한 항목: 회귀 threshold, journal 에 `git` / `distance` / `resourcePolicy`
추가 여부, 출력 포맷 최종 선택(JSON vs CSV), suite 자동 실행, p95 알고리즘, `run_cancelled`
분석 세분화. S3 timestamp 수정은 별도 S3 follow-up.
## 2026-10-01 — S3 journal git provenance 추가 (S6 진입 조건 ① 해소)

S6 brief 가 진입 조건으로이했던 `git provenance 없음` 을 해소했다. **S6 구현이 아니다.**

변경:

- `BenchmarkRun::gitCommit` / `BenchmarkRequest::gitCommit` 추가 (S2 run metadata,
  `buildVersion` 과 같은 자리). runner 는 이를 **복사만** 하며 git 을 호출하지 않는다.
- `run_started` 에 `gitCommit` 을 `buildVersion` 바로 뒤에 additive field 로 기록.
  값은 S5 의 `MSF_BUILD_GIT` 을 그대로 사용한다.
- provenance 가 없으면 **빈 문자열이 아니라 literal `"unknown"`** 을 기록한다.
- `JournalReplay::gitCommit` 추가 및 replay 가 해당 필드를 읽는다. 필드가 없는
  기존 journal 은 **빈 값으로 남고 값을 만들어내지 않는다.**

**schema version 은 1 로 유지했다(무조건 bump 하지 않았다).** 근거 3가지:

1. `kBenchmarkJournalSchemaVersion` 은 7곳에서 **쓰기만** 하고 읽는 곳이 없다.
   어떤 코드도 이 값으로 분기하지 않는다.
2. replay parser 는 알려진 키만 추출하며, 미존재 필드는 무시하고 알 수 없는 필드는
   거부하지 않는다.
3. 저장소 기존 선례: `benchmark_schema_test` 의 "meta.schemaVersion is still 9
   (**additive fields did not force a bump**)".

실측 검증:

- 실제 Console benchmark 실행의 `run_started.gitCommit` = `fcace68`,
  해당 binary 의 generated `MSF_BUILD_GIT` = `fcace68` → **완전 일치**.
  git command 를 다시 실행해 확인한 것이 아니라 **generated 값과 journal 값을 비교**했다.
- 같은 실행의 Console 헤더도 `Git : fcace68` 로 동일한 값을 표시한다(중복 로직 없음).
- 구 journal 과 신 journal 의 `run_started` 필드 집합 비교: **추가 `gitCommit` 1개,
  제거 0개**. 순수 additive.
- provenance 없는 journal 3건, summary regeneration, 회귀 테스트 전부 PASS.
- 기존 journal **29개는 수정하지 않았고** 필드가 없으며 정상 replay 대상이다.

변경하지 않은 것: `benchmark_store.cpp` (timestamp `benchmarkNowStamp` 그대로),
`journalSchemaVersion`, S3 recovery semantics 전체, S2 실행 semantics, GUI, Console
renderer, MediaKind/Scanner, NVDEC, legacy benchmark schema.

테스트: `benchmark_journal_test` 51 → **66**, `benchmark_integration_test` 137 → **144**.
CPU build exit 0 / CTest **96/96**, GPU build exit 0 / CTest **97/97**.

남는 진입 조건: `distance` 와 `resourcePolicy` 는 여전히 journal 에 없고(DEFERRED),
통제된 측정 환경 정의와 반복 실행 데이터, `run_cancelled` 실측 표본도 미해소다.
S6 는 여전히 CLOSED 가 아니다.
## 2026-10-01 — S6-1 Data Ingestion / Journal Normalization 구현

S6 brief 의 `S6-1` 단계(데이터 수집/정규화)만 구현했다. **비교·집계·회귀·통계 판정은
구현하지 않았고 S6 는 여전히 CLOSED 가 아니다.**

위치: `src/benchmark_data_mining.{h,cpp}` (msf_core 소속). 테스트는
`tests/benchmark_data_mining_test.cpp` (69 checks).

### 핵심: recovery 규칙을 재구현하지 않는다

journal 파싱은 **전혀 하지 않는다.** `msf::replayJournal()` 이 모든 무결성·멱등성·anomaly
판정을 내리고, S6 는 그것을 호출하고 결과를 투영할 뿐이다. S6 가 자체적으로 만든 부분은
딱 하나이며 그것도 판단이 아니다:

- `run_started` / terminal record 를 찾아 **어떤 run 이 존재하는지** 열거하기 위해
  S6 자신의 최소 flat field reader(`jsonFieldString`)를 쓴다. suite journal 은 여러 run 이
  누적되므로 per-run replay 전에 run 목록이 필요하고, replay 는 한 번에 한 run 만 돌려준다.
  이 단계에서는 runId·datasetFingerprint·mediaScope·completedAt 네 문자열만 복사한다.

`completedAt` 은 S3 의 replay 가 **채우지 않는다** (`JournalReplay::completedAt` 이 항상
빈 상태). `run_finished` 가 해당 필드를 쓰기는 하지만 replay 는 `completionReason` 만
읽는다. 그래서 S6 가 같은 최소 reader 로 직접 읽는다.

### 표준화 모델

- `IngestRunClass`(Complete / Cancelled / Incomplete / Corrupt / Unavailable) 는 **S6 분석용
  분류이며 benchmark status 가 아니다.** `BenchmarkStatus` 는 S2 정의를 그대로 유지한다.
- `IngestExclusion` 10종으로 제외 사유를 이름으로 기록한다. 아무것도 조용히 버리지 않는다.
- `GitCommitState` 3상태(Known / Unknown / **Legacy**)를 구분한다. Legacy(필드 자체가 없는
  journal)에 현재 git 값을 채워 넣지 않는다.
- 모든 값이 journal 에 없는 필드(`distance` / `resourcePolicy` / `gpuBackend`)는
  `std::optional` 로 **부재를 보존**하며 0 / false / `"unknown"` 으로 치환하지 않는다.
- mode `elapsedMs` 는 **실행된 경우에만** 값을 갖는다. 미실행 모드의 0.0 은 "매우 빠름"으로
  읽힐 수 있는 주장이므로 저장하지 않는다.

### 실측 발견 2건

1. **wall duration 의 해상도는 1초다.** S3 의 `startedAt` / `completedAt` 은
   `%Y-%m-%dT%H:%M:%S` 로 소수부가 없다. 따라서 파생 duration 은 항상 1000 ms 의 배수이며,
   **1초 미만 run 은 정확히 0 으로 나온다.** 저장된 0 은 "같은 초 안에 끝났다"는 뜻이지
   "시간이 없다"가 아니다. 값이 없는 상태(취소 run 등)와 **구분 가능해야 하며** 테스트로 고정했다.
2. **실제 저장 트리에서 multi-run journal 이 확인되었다.** `suite-TEST-SUITE-001` 은
   `run_started` 가 2개다. 30개 journal 에서 31개 run 이 나오며 각각의 datasetFingerprint 와
   case 가 뒤섞이지 않는다(S3-BUG 회귀 방지).

### 실제 저장소 대상 검증

테스트 binary 에 읽기 전용 진단 경로를 두어 실제 산출물로 확인했다.

`	ext
journals=30  unreadable=0  withoutRuns=0
runsFound=31  accepted=31  excluded=0  commitless=0  anyFatal=0
provenance: legacy=30  known=1  unknown=0   withDuration=31
determinism: IDENTICAL
`

`legacy=30 / known=1` 은 실제 상황(기존 29 + provenance 1개 journal)과 일치한다.

테스트: ingestion **69 checks**(신규). journal 66 / store 51 / integration 144 / core 63 /
gui_store 110 / worker 35 / ui 34 / ui_e2e 40 / cli 95 / renderer 100 / orchestrator 32 전부
PASS. CPU build exit 0 / CTest **97/97**, GPU build exit 0 / CTest **98/98**.

변경하지 않은 것: S2 BenchmarkRunner/Executor/Request, S3 journal schema·recovery·summary,
Console renderer, GUI, Scanner/MediaKind, NVDEC, legacy benchmark. journal 필드 추가 없음.
CLI 도 추가하지 않았다.
## 2026-10-01 — S6-2 Normalization 보강 / 분석 데이터 계약 고정

S6 brief 의 `S6-2` 단계(정규화 데이터 계약 확정)를 구현했다. **grouping 실행·집계·
통계·회귀 판정은 구현하지 않았고 S6 는 여전히 CLOSED 가 아니다.**

위치: 기존 `src/benchmark_data_mining.{h,cpp}` 보강 + 신규
`tests/benchmark_data_contract_test.cpp` (67 checks).

### S6-1 감사에서 발견한 실제 갭 5건

1. **run 의 benchmark status 가 저장되지 않았다.** `BenchmarkStatus` 가 mode/case 에만
   있었고 run 은 `IngestRunClass`(S6 분류)만 갖고 있었다. terminal record 의 status 를
   읽어 `std::optional<BenchmarkStatus> runStatus` 로 추가했다. S3 replay 가 이 필드도
   채우지 않으므로(`completedAt` 와 동일) 같은 최소 reader 로 읽는다.
2. **fingerprint 의 empty 와 missing 이 구분되지 않았다.** writer 는 항상 필드를 쓰므로
   빈 문자열은 "source 를 측정할 수 없음"이라는 **실제 의미**인데 S6-1 은 이를 absent 로
   접어버렸다. `DatasetIdentityState{Missing,Empty,Valid}` 로 3상태를 고정했다.
3. **집계 타입이 없었다.** `NormalizedBenchmarkData` 를 도입했다. S6-1 의 이름은
   `using IngestResult = NormalizedBenchmarkData;` 로 유지해 기존 테스트가 그대로 컴파일된다.
4. **duration 해상도가 문서에만 있었다.** `TimestampResolution::OneSecond` 로 실행 가능한
   계약으로 만들고 `benchmarkTimestampResolution()` 로 노출했다. run 별 metadata 를
   늘리지 않기 위해 값 하나로 충분하다(저장 방식 자체의 속성이라 모든 run 에서 동일).
5. **exclusion 에 journal/run provenance 가 없었다.** `IngestExclusionRecord` 를 추가해
   `sourceJournalPath` / `runId` / `suiteId` / `reason` 을 담은 평탄 목록을 제공했다.

### Measured / Derived / Missing 를 실행 가능한 계약으로

`valueOrigin(run, RunField)` 가 run 수준 필드마다 Measured / Derived / Missing 를
반환한다. Case/Mode 수준은 record 에서 그대로 복사되므로 구조상 전부 Measured 다.
`distance` / `resourcePolicy` / `gpuBackend` 는 **구조적으로 항상 Missing** 이며
`0` / `false` / `"unknown"` 으로 치환되지 않는다. resourcePolicy 를 preset 에서
추론하지 않고, gpuBackend 를 effectiveMode 에서 추론하지 않는다.

### Run identity 와 provenance 분리

`gitCommit` 은 identity 가 아니라 provenance 다. `datasetFingerprint + gitCommit` 을
새 runId 로 합성하지 않으며 S3 runId 를 그대로 보존한다. 후속 S6-3 이 조합을 고를 수 있도록
`groupingKey(run)` 가 비교 가능 필드를 한 곳에 모으지만, **어떤 조합을 group key 로 쓸지는
S6-3 의 결정**이며 이 단계에서 고정하지 않는다.

### Legacy provenance 는 채워 넣지 않는다

dataset 전체를 훑어 `GitCommitState::Legacy` 인 run 중 `gitCommit` 값을 가진 것이
하나도 없음을 테스트로 검증한다. 현재 HEAD 를 legacy journal 에 채워 넣지 않는다.

테스트: contract **67 checks**(신규), S6-1 ingestion **71 checks**(회귀), journal 66 /
store 51 / integration 144 / core 63 / gui_store 110 / worker 35 / ui 34 / ui_e2e 40 /
cli 95 / renderer 100 / orchestrator 32 전부 PASS. CPU build exit 0 / CTest **98/98**,
GPU build exit 0 / CTest **99/99**.

실제 저장소 재확인: journals 30 / runsFound 31 / accepted 31 / excluded 0 /
provenance legacy 30 · known 1 / determinism IDENTICAL.

변경하지 않은 것: BenchmarkRunner/Executor/Request, journal schema·recovery·summary,
Console renderer, GUI, Scanner/MediaKind, NVDEC, legacy benchmark. journal 필드 추가 없음.
CLI 추가 없음. grouping/aggregation/통계 없음.
## 2026-10-01 — S6-3 Grouping / 분석 view 분리

S6 brief §12 계약을 실제 구현과 맞췄다. 위치: 신규 `src/benchmark_data_grouping.{h,cpp}`
+ `tests/benchmark_data_grouping_test.cpp` (58 checks).

### 가장 중요한 결정 — 단일 composite key 를 쓰지 않았다

`fingerprint + mediaScope + buildVersion + gitCommit + requestedMode + effectiveMode`
을 하나로 묶으면 모든 run 이 자기 고유 cohort 에 들어가 **build 간 비교가 구조적으로
불가능**해진다. 구현은 가장 단순해 보이지만 비교를 삭제하는 설계이므로 기각했다.
대신 분석 축별 typed key 5종(`DatasetCohortKey` / `ScopeCohortKey` / `BuildCohortKey` /
`ModeCohortKey` / `CaseCohortKey`)을 분리했고, 축을 합치는 것은 호출부의 몫이다.
근거는 `docs/worklog/0.9.4.*.md` 에 기록했다.

### `caseId` 는 run-scoped 가 아님 (실측 정정)

S2 는 `caseId = IndexManager::folderId(file.path)` (canonical path FNV-1a) 이므로
**같은 파일을 다른 run 이 실행하면 동일 `caseId`** 다. brief 의 run-scoped 가정을
실측으로 정정했고, 새 hash 를 만들지 않고 dataset fingerprint 와 짝을 이루어 key 로
썼다.

### 결정적 정렬과 상태 분리

mode canonical 순서 `AUTO → CPU → GPU-MAX` 는 `GpuBackendKind` 선언 순서와 다르므로
명시적 rank 를 쓴다(정렬은 표시·그룹 용이며 execution 순서를 재정의하지 않는다).
Known/Unknown/Legacy cohort 는 구조적으로 분리되고 `commitComparable` 은 Known 에서만
true 다. provenance 품질 표시이지 comparability 판정이 아니다.
empty/missing fingerprint 는 cohort 가 되지 않고 개수만 집계된다. excluded run 은
어떤 cohort 에도 들어가지 않는다.

테스트: grouping **58 checks**(신규), S6-2 contract 67, S6-1 ingestion 71, journal 66 /
store 51 / integration 144 / core 63 전부 PASS. CPU build exit 0 / CTest **99/99**,
GPU build exit 0 / CTest **100/100**. `git diff --check` clean.

실제 저장소(30 journal / 31 run, read-only): dataset cohort **2**, scope cohort **4**
(`all` 22, `images` 5, `videos` 3), build cohort **2** (Known 1 commit-comparable /
Legacy 28), mode cohort **3**, case cohort **70** (10개가 2개 이상 run 에 걸쳐 재등장,
최대 26 run). determinism IDENTICAL. build diversity 부족은 brief §23 에 따라 정상으로
기록했고 다양성을 만들어내지 않았다.

미구현: aggregation, mean/median/p95, delta, speedup, regression, threshold, anomaly,
comparability 최종 판정, distance/resourcePolicy/gpuBackend 추론, CLI, GUI,
journal schema 변경.

### 실제 journal 에서 확인한 capability 관찰

`requested=CUDA` 인데 `effective=CPU` 인 record **60건**이 실측됨
(`status=SKIPPED`, `mode unavailable in this environment`). requested/effective 를
합쳤다면 60건의 CUDA 관찰이 CPU 실행으로 위장되어 capability 관찰이 사라진다.
brief §9 의 근거가 실제 데이터로 확인된 사례다.
