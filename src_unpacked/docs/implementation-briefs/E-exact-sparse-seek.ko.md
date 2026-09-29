# E-exact-sparse-seek — E-2B Exact Sparse Seek + Adaptive Sampling Planner (Pre-register)

Status: **PRE-REGISTERED** (probe 수정·측정보다 먼저 커밋된다)

```text
Base         v0.9.4.39 / b8a125e
Version      v0.9.4.40
Experiment   E-2B
직전          E-2A = CONDITIONAL (exactness 실패)
Production change   NONE expected — planner 는 test/benchmark 경로에서만 활성화
연결 brief    E-video-decode-planner.{ko,en}.md
              E-adaptive-sampling-strategy.{ko,en}.md
```

## 1. 작업 순서 (지시 §1, 이 순서를 지킨다)

```text
1. exactness 원인 해결
2. exact sparse seek 구현
3. frame-level parity 재검증
4. sparse/sequential 조건별 성능 측정
5. planner rule 설계
6. planner prototype 구현
7. regression 검증
```

**성능 threshold 를 먼저 만들지 않는다. sample parity 가 PASS 되기 전에는
production planner 를 완성으로 판정하지 않는다.**

## 2. E-2A 의 exactness 실패 — 원인 가설

제품 sampling predicate 는 `video_decoder.cpp:127` 의

```text
ft + 0.05 >= target
```

이며, 이는 `pts >= target - 0.05` 인 **첫** 프레임을 고른다.

E-2A 의 sparse prototype 은 `target` 으로 seek 했다. 그 결과 착지 keyframe
`K <= target` 이고, `K > target - 0.05` 이면 제품이 골랐던 프레임이 `K` 보다
**앞에** 있어 도달할 수 없다. 이것이 `tsLater` 의 원인이었다.

**가설: seek 목표를 `target - 0.05` 로 잡으면 착지 keyframe `K'` 이
`K' <= target - 0.05` 를 만족하므로, 제품이 선택하는 프레임은 항상 착지점
이후에 존재하여 도달 가능하다.**

predicate · tolerance · target 은 **어떤 것도 변경하지 않는다.** candidate 의
seek 목표만 바꾼다.

## 3. 절대 변경 금지 항목 (지시 §5)

```text
production predicate 변경             금지
0.05 tolerance 변경                  금지
target timestamp 변경                 금지
sample count 맞추기 위한 임의 frame substitution   금지
pixel mismatch 허용                   금지
"비슷한 frame 이면 동일" 판정         금지
threshold 완화                        금지
final fingerprint 만 같으면 PASS      금지
```

## 4. Exactness 판정 기준 (지시 §14)

```text
sample count     identical
sample order     identical
sample timestamp identical
frame identity   identical
pixel bytes      identical
tsLater          0
```

## 5. 남은 실패에 대한 처리 (지시 §34)

exactness 가 깨지는 조건은 sparse-seek 을 선택하지 않고
`SequentialPreferred` 로 분류한다. **해결을 강요하지 않는다.**
planner 가 그 조건을 런타임에 감지할 수 있어야 한다.

## 6. Seek 후 decoder state 검증 (지시 §8)

product 가 선택하는 프레임은 `pts >= target - 0.05` 를 만족하므로, 정확한
재현을 위해서는 **첫 디코드 프레임이 요청한 seek 지점 이하여야 한다.**
이를 계측한다.

```text
seekRequestPts
firstDecodedPts(landing)
firstSelectedPts
targetPts
landing <= seekRequestPts ?
```

이 위반이 발생하면 해당 파일은 sparse-seek 후보에서 제외한다.
이는 §34 가 요구하는 분류이며, 동시에 planner 의 런타임 신호가 된다.

## 7. Planner / Executor 분리 (지시 §28·29)

```text
VideoSamplePlan
    ↓
AdaptiveSamplingPlanner        <- 어느 전략인가만 판정 (decode 하지 않는다)
    ├── SequentialPreferred
    ├── SparseSeekCandidate
    └── SparseSeekUnavailable
    ↓
ExactSeekExecutor / SequentialExecutor   <- 실행만 담당
```

planner 는 **분류만** 하고 executor 는 **실행만** 한다.

## 8. Planner 상태 (지시 §20)

복잡한 score 가 아니라 세 가지로 충분하다.

```text
SequentialPreferred
SparseSeekCandidate
SparseSeekUnavailable
```

## 9. Hard fallback (지시 §21·22)

correctness 가 performance 보다 먼저다. 아래는 비용 비교 **이전에** 평가한다.

```text
decode unavailable (av1)          -> SparseSeekUnavailable
GopUnavailable                    -> SparseSeekUnavailable
GopEstimated                      -> SequentialPreferred (보수적)
seek landing 위반                 -> SequentialPreferred
tsLater/tsBehind > 0              -> SequentialPreferred
pixel mismatch                    -> SequentialPreferred
GOP 가 샘플 간격에 비해 크다        -> SequentialPreferred
sparse 예상 프레임 >= sequential   -> SequentialPreferred
그 외 + exactness 통과 + 비용 유리   -> SparseSeekCandidate
```

## 10. Production default (지시 §2)

```text
Production default = Sequential   (변경하지 않는다)
```

planner candidate 는 명시적 test/benchmark 경로에서만 활성화한다.

## 11. 측정 (지시 §24·25)

교차 반복 최소 3회, 가능 5회, median 을 headline 으로 쓴다.
telemetry ON/OFF 로 계측 오버헤드를 분리 확인한다.

## 12. Dataset 규칙 (지시 §37·38)

개인 실제 콘텐츠는 커밋·public artifact 금지, 기존 gitignore 디렉터리 유지,
manifest 에 메타데이터만 기록. 표준 image dataset
`e8f8fa6a..e2640a` 은 **절대로 변경하지 않는다.**

## 13. 하지 않는 것 (지시 §30·31·32·33)

```text
hardware decode 실행                금지 (HardwareCandidate 표현만 가능)
CPU software decode + sparse seek 만 먼저 완성
AV1 decoder 추가 / dependency 변경  금지
4K GOP 불일치 해결을 위한 새 codec parser 작성  금지
  -> 원인을 모르면 GopEstimated 로 처리
production sampling policy 변경     금지
```

## 14. 성공 기준 (지시 §44~47)

```text
Gate A  sample count / order / timestamp / frame identity / pixel / tsLater 전부 PASS
Gate B  대표 승리군 개선 + 패배군에서 planner 가 sequential 선택
Gate C  planner 3상태를 재현 가능하게 결정, 사용 입력 기록
Gate D  E/F 경계 유지 (E = sampling/decode 전략, F = hardware backend)
```

## 15. 산출물

```text
tests/video_sampling_strategy_probe.cpp  (exactness + planner + selfcheck)
```

`docs/experiments` 는 만들지 않는다.
