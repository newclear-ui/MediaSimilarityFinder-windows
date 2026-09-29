# E-hevc-seek-landing — E-3A HEVC Seek Landing Characterization (Pre-register)

Status: **PRE-REGISTERED** — 이 커밋은 brief **만** 포함하며 probe 수정보다 앞선다.

```text
Base         v0.9.4.40 / 57d447b
Version      v0.9.4.41
Experiment   E-3A
직전          E-2B = CONDITIONAL (pixel parity 13/14, HEVC 1080p 잔여 실패)
Production change   NONE
```

## 0. 절차 원칙 — 이번에는 순서를 지킨다

v0.9.4.40 에서 pre-register brief 와 probe 수정이 동일 커밋에 들어간
절차 이탈이 있었다. 이번에는 순서를 강제한다.

```text
1. brief 작성
2. brief commit          ← 이 문서만
3. probe 수정
4. 측정
```

**이 커밋은 2 단계에 해당하며 brief 만 포함한다.**

## 1. 목적

단 하나의 질문에 답한다.

> **HEVC 에서 발생하는 seek landing violation 이 현재 seek API / decoder
> interaction 으로 해결 가능한 문제인가?**

해결 가능하면 Exact Sparse Seek 을 HEVC 에 승격할 근거를 얻고,
해결되지 않으면 **HEVC = SequentialPreferred 를 최종 정책으로 확정한다.**

**HEVC 를 반드시 sparse-seek 으로 살내는 것이 목표가 아니다.**
기술적으로 안전한지 증명하거나, 안전하지 않다면 경계를 명확히 확정하는 것이
목표다.

## 2. 현재 관측된 사실 (v0.9.4.40)

```text
pixel parity = 13/14        실패 파일 1건
tsLater 발생 파일 = 1
실패 파일 = real_hevc_1920x1080_030s.mp4
필요조건 위반 = firstDecodedPts > seekRequestPts
planner 는 이를 감지해 SequentialPreferred 로 분류했다 (탐지 성공, 해결 안 함)
```

## 3. 가설 (측정 대상이지 결론이 아니다)

```text
HEVC CRA / random-access point / decoder state
+ av_seek_frame landing semantics
```

## 4. 조사할 API

현재 FFmpeg 9.0.1 dependency 에서 실제 사용 가능한지 확인하고 실측한다.

```text
av_seek_frame()            keyframe 기준 seek
avformat_seek_file()       min_ts / ts / max_ts 범위로 seek 지점 결정
AVSEEK_FLAG_BACKWARD       backward 방향
AVSEEK_FLAG_ANY            non-keyframe 을 keyframe 처럼 요청 (진단 전용)
avformat_flush()           demuxer 내부 버퍼 폐기
avcodec_flush_buffers()    decoder 내부 버퍼 폐기
```

## 5. Candidate 정의

### Candidate A — 현재 방식 (baseline)

```text
av_seek_frame(stream, pts(target - 0.05), AVSEEK_FLAG_BACKWARD)
+ avcodec_flush_buffers(cc)     (현재 production 와 동일)
```

### Candidate B — avformat_seek_file

```text
avformat_seek_file(stream, min_ts, target, max_ts, flags)
```

처음부터 복잡한 timestamp window 를 가정하지 않는다. 최소한
`target - ε`, `target`, `target + ε` 세 경우를 비교하며 ε 은
**실제 time_base 에 맞춰** 정한다.

### Candidate C — AVSEEK_FLAG_ANY (진단 전용, production 후보 아님)

목적은 단 하나다.

> non-keyframe seek 가 landing violation 을 없애는지 확인

exactness 가 깨지거나 decoder state 가 불안정하면 **즉시 production candidate
에서 제외**한다.

### Candidate D — seek 후 flush/state 초기화

`avformat_flush()` 사용 여부를 비교한다. probe 에서만 비교하며
**production 에 임의로 넣지 않는다.**

## 6. 측정값 (지시 §11, 가장 중요)

seek 마다 아래를 전부 기록한다.

```text
seekRequestPts
firstDecodedPts        ← landing
firstEligiblePts       ← predicate 가 처음 만족하는 프레임
selectedPts            ← 실제로 고른 프레임
targetPts
```

필수 조건

```text
firstDecodedPts <= seekRequestPts
```

그리고 production predicate 에 의해

```text
selectedPts >= target - 0.05
```

를 만족하는 동일 프레임을 선택할 수 있어야 한다.

## 7. HEVC CRA 관련 수집 항목 (지시 §9)

```text
packet key flag
decoded picture type
PTS
DTS
first decoded after seek
first output after seek
seek landing packet timestamp
```

CRA / IDR / I-picture 관계가 해당 파일에서 실제로 어떻게 나타나는지 기록한다.

## 8. FFmpeg keyframe flag 를 절대 ground truth 로 쓰지 않는다 (지시 §10)

```text
packet KEY  ≠  무조건 random-access-safe decode point
```

이 가능성을 열어둔다. **그러나 이것만으로 "HEVC 가 잘못됐다"고 결론내리지
않는다.** 실제 해당 파일의 decoded picture / PTS / landing 결과를 확인한다.

## 9. Exactness 기준 (지시 §12)

각 candidate 에 대해 비교한다.

```text
sample count
sample order
sample PTS
frame identity
pixel parity
tsLater
```

목표

```text
pixel parity = 14/14
tsLater      = 0
```

**HEVC 하나를 해결하려다 다른 codec 의 exactness 가 깨지면 그 candidate 는
거부한다.**

## 10. 범위 — HEVC 한정 (지시 §15)

주 대상은 HEVC 이다. regression 확인을 위해 H.264 와 FFV1 에 대해 최소 smoke
test 를 유지한다. 범위를 다른 codec 으로 확장하지 않는다.

## 11. production planner 변경 금지 (지시 §16)

현재 `HEVC → SequentialPreferred` 를 **그대로 유지**한다. Candidate B/C/D 가
좋게 나와도 즉시 planner 에 연결하지 않는다. **먼저 결과를 기록한다.**

## 12. production 변경 금지 항목 (지시 §2·23)

```text
production sampling policy 변경          금지
production default 변경                  금지
HEVC exactness 용 tolerance 변경         금지
target timestamp 변경                    금지
pixel tolerance 추가                     금지
frame substitution                       금지
새 codec parser / HEVC bitstream parser  금지
NVDEC / CUDA video decode / HW decoder   금지
FFmpeg dependency upgrade / patch        금지
decoder library 교체                      금지
sampling algorithm 전체 재작성            금지
```

허용 범위는 test/probe, telemetry, documentation 뿐이다.

## 13. 종료 조건 (지시 §19, 무한 최적화 방지)

다음 중 하나로 **반드시** 종료한다.

```text
A. HEVC Exact Sparse Seek = VERIFIED
   → E-3B planner calibration 후보로 승격

B. HEVC Exact Sparse Seek = NOT VERIFIED
   → HEVC = SequentialPreferred 최종 확정
```

모든 safe seek API 가 동일 landing 을 보장하지 않거나, decoder state 특성상
exactness 를 안정적으로 보장할 수 없다면 **문제 해결을 억지로 계속하지
않는다.** B 는 E 실패가 아니다. 정확성과 성능을 함께 고려해 안전한 fallback 을
선택한 결과다.

## 14. 이번 버전에서 하지 않는 것 (지시 §20·21·22)

- **4K GOP mismatch 11** 은 주 대상이 아니다. `GopEstimated → SequentialPreferred`
  정책을 그대로 유지하며, 원인이 확정되지 않은 상태에서 GOP parser 를 만들지 않는다.
- **AV1** 은 `SparseSeekUnavailable` 을 유지한다. dependency 를 변경하지 않는다.
- **planner calibration 은 하지 않는다.** duration/fps/GOP/resolution threshold 를
  확정하지 않는다. HEVC landing 문제를 먼저 닫는다. calibration 은 E-3B 다.

## 15. Selfcheck (지시 §24)

추가하되 **기존 29개 selfcheck 를 삭제하지 않는다.**

```text
Candidate A landing
Candidate B landing
Candidate C landing
Candidate D landing
HEVC exactness
HEVC tsLater
HEVC pixel parity
```

## 16. 산출물

```text
tests/video_sampling_strategy_probe.cpp  (candidate A~D 추가)
docs/build-history/0.9.4.41.{ko,en}.md
docs/worklog/0.9.4.{ko,en}.md
```

`docs/experiments` 는 만들지 않는다.
