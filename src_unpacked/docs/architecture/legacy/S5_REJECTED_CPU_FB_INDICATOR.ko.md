# S5 rejected design record — Console `CPU FB` indicator

Status: **REJECTED** — 이 항목은 제품 코드에 구현되지 않았고, 구현 후 삭제한 것이 아니다.

---

## 1. 무엇이었나

`CPU FB` 는 Console benchmark mockup
(`uimock/benchmark-console-mockup.html`) 이 CURRENT FILE 영역의 mode 결과 행마다
두던 표시 항목이다. 의도는 각 mode 실행에서 **CPU fallback 이 발생했는지**를
눈에 보이게 하는 것이었다.

mockup 원문 예시:

```text
AUTO     DONE    Time 12.41 ms  Policy Adaptive        Backend Scheduler  CPU FB NO
CPU      DONE    Time 18.08 ms  Policy Balanced        Backend Software   Workers 10
GPU-MAX  RUNNING Time  7.32 ms  Policy GPU Preferred  Backend CUDA       CPU FB NO
```

## 2. 채택하지 않은 이유

S2 benchmark 실행·저장 계약에는 이 지표가 **존재하지 않는다**.

- `BenchmarkModeResult` 는 `requestedMode` / `effectiveMode` / `status` /
  `started` / `completed` / `elapsedMs` / `summary` / `errorMessage` 를 기록한다.
- `errorMessage` 는 사람이 읽는 메시지이며 구조화된 fallback 판정 필드가 아니다.
- `BenchmarkScanSummary` 도 CPU fallback 여부를 담지 않는다.

따라서 현재 측정 데이터만으로 `CPU FB` 값을 계산하거나 판정하면,
**benchmark 결과의 의미를 임의로 확장**하는 것이 된다.

## 3. 기각 대상이 되었던 잘못된 대안들 (구현하지 않음)

| 대안 | 왜 안 되는가 |
| --- | --- |
| `effectiveMode == Cpu` 를 `CPU FB YES` 로 재해석 | `effectiveMode` 는 **요청된 mode 의 backend 해석 결과**이며, "fallback 이 발생했는지" 라는 별개의 판정이다. GPU-max 요청이 CPU 로 풀렸다는 사실과 GPU 처리 중 CPU 보조가 발생했다는 사실은 같은 것이 아니다. 재해석하면 두 의미가 섞인다. |
| `gpuEnabled == false` 로 유추 | `gpuEnabled` 는 **요청된 mode** 에서 파생된다(S2 규칙). 실행 중 실제 fallback 발생 여부와 무관하다. |
| `errorMessage` 문자열 검색으로 판정 | 메시지 문자열에 의존하면 계약이 아니라 문자열 포맷에 결합된다. |
| renderer 에 임시 구현 후 제거 | 지시에서 금지. **제품 코드에 한 번도 구현하지 않는다.** |

## 4. 제품 코드에 적용된 결정

- S5 Console renderer 는 `CPU FB` 열을 **표시하지 않는다**.
- 새로운 측정이나 추정 로직을 추가하지 않는다.
- benchmark 결과 모델과 journal schema 를 변경하지 않는다.

## 5. 재검토 조건 (Future revisit condition)

아래 중 하나가 충족되면 이 결정을 다시 검토한다. 그전까지는 `REJECTED` 를 유지한다.

1. **S2/S3 계약에 fallback 판정 필드가 정식 추가된 경우.**
   `BenchmarkModeResult` 또는 `SearchReport` 에 구조화된 CPU fallback 지표가
   contract 수준으로 생기고 journal schema 가 함께 확장되면, 그 값을 그대로
   표시할 수 있다. 표시가 아니라 **계약 확장**이 먼저다.
2. **GPU fallback 이 계측 가능한 지표로 승격된 경우.**
   Node F / Adaptive Scheduler 쪽에서 fallback 발생 횟수·비율이 확정 지표가 되면
   그때 `CPU FB` 를 "표시 이름"으로 재채택할 수 있다. 표시 이름만 먼저 쓰는 것은
   여전히 의미 확장이므로 금지한다.
3. **console renderer 가 측정값이 아닌 사용자 입력(선택 모드)을 표시하는 항목으로
   재정의되는 경우.** 이는 같은 이름의 다른 지표이므로, 이 기록의 대상이 아니다.

재검토 시에도 위 2-3절의 부재 근거를 다시 확인해야 하며,
근거 없이 "이번엔 되겠다"로 통과시키지 않는다.

## 6. 연결

- 구현 대상 지시: `C:\project\지시\S5 A2 CPU FB 결정 정정.md`
- 원본 목업: `uimock/benchmark-console-mockup.html`
- 관련 계약: `docs/architecture/benchmark-telemetry-roadmap.ko.md` / `.en.md`
  (mode 정의 — GPU 최대화가 CPU fallback 을 유지하는 이유)
- worklog 색인: `docs/worklog/0.9.4.ko.md` / `.en.md` 의
  Performance / Tuning Experiment Index 항목 `S5-CPUFB`
- S5 구현 브리프: `docs/implementation-briefs/S5-console-benchmark-execution.ko.md`
  (생성 예정) — `CPU FB = REJECTED` 명시
