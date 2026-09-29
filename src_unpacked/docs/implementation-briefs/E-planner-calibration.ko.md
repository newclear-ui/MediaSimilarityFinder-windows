# E-planner-calibration — E-3B Adaptive Sampling Planner Calibration + End-to-End Validation (Pre-register)

Status: **PRE-REGISTERED** — 이 커밋은 brief **만** 포함하며 코드 변경보다 앞선다.

```text
Base         v0.9.4.41 / 9d5dc26
Version      0.9.4.42
Experiment   E-3B
직전          E-3A = PASS (HEVC_EXACT_SPARSE_SEEK = NOT VERIFIED)
```

## 0. 절차 — v0.9.4.41 에서 지켜진 순서를 유지한다

```text
brief 작성 → brief commit → code → build → test → benchmark
```

**이 커밋은 brief 만 포함한다.** v0.9.4.40 의 절차 이탈을 반복하지 않는다.

## 1. 목적

더 이상 "sparse seek 가 가능한가"를 연구하지 않는다. E-3A 에서 충분히 확인했다.
질문은 다음으로 바뀐다.

> **이 파일에서는 sparse 를 사용해도 안전한가, 그리고 실제로 이득이 있는가?**

## 2. planner 판단 순서 (성능보다 정확성이 항상 먼저)

```text
1. correctness / capability gate
2. confidence gate
3. estimated cost comparison
4. strategy selection
```

**exactness 가능 여부가 성능 score 보다 항상 먼저**다.

## 3. 확정 정책 반영 (E-3A 결과)

```text
HEVC  = SequentialPreferred  (6개 seek 전략 전부 실패, 대체 API 없음)
4K    = SequentialPreferred  (GopEstimated)
AV1   = SparseSeekUnavailable (decode unavailable)
```

이 정책은 **planner 의 초기 정책으로 반영**한다. HEVC 를 다시 sparse candidate 로
승격하지 않는다. 나중에 별도 HEVC seek 연구를 한다면 새 brief/experiment 다.

## 4. planner 구조 — 역할 분리 유지 (§5)

```text
VideoSamplePlan
        ↓
AdaptiveSamplingPlanner        ← 분류만
        ↓
SamplingDecision
        ├── SequentialPreferred
        ├── SparseSeekCandidate
        └── SparseSeekUnavailable
        ↓
SamplingExecutor              ← 실행만
        ├── Sequential
        └── ExactSparseSeek
```

planner 와 executor 를 다시 합치지 않는다.

## 5. planner 입력 (§6)

```text
duration, fps, resolution, codec, GOP / GOP confidence, sampling density
```

`estimated frame count` 와 `sample count` 같은 **파생값을 독립 input 으로 다시
넣지 않는다.** E-1 의 축소 결론을 유지한다.

## 6. 파생 비용 (§7) — raw 입력과 구분

planner 가 계산하는 derived value:

```text
estimated_total_frames
estimated_sample_count
estimated_sequential_decode_work
estimated_sparse_decode_work
estimated_seek_count
estimated_preroll_work
```

## 7. GOP confidence (§8)

```text
GopKnown        → sparse candidate 가능
GopEstimated    → 기본 SequentialPreferred (E-3B 초기 구현)
GopUnavailable  → SparseSeekUnavailable 또는 SequentialPreferred
```

## 8. capability gate (§9·10·11)

가장 먼저 평가한다.

```text
CanExactSparseSeek == false  →  SequentialPreferred   (성능 계산보다 먼저)
```

**검증되지 않은 codec/condition 을 Verified 로 추정하지 않는다.**
특히 `HEVC 1080p` 는 false 다.

## 9. cost model (§12·13)

```text
SequentialCost ≈ 순차 디코드 프레임 작업량
SparseSeekCost ≈ seek + preroll + post-seek decode + sample decode
```

단순 threshold 규칙을 쓰지 않는다.

```text
금지:  duration > 30s → sparse
       GOP > 50 → sequential
       fps > 30 → sparse
       resolution > 1080p → sparse
```

## 10. calibration 데이터 (§14·15)

E-2A/E-2B/E-3A 결과를 다음 형태로 정리한다.

```text
codec, duration, fps, resolution, GOP, GOP confidence, sampling density,
sequential decoded frames, sparse decoded frames,
sequential elapsed, sparse elapsed, exactness
```

4상태를 구별한다.

```text
Fast + Exact      → Sparse        (유일하게 목표)
Slow + Exact      → Sequential    (비용 불리)
Fast + Not Exact  → Sequential    (무조건 거부)
Slow + Not Exact  → Sequential
```

## 11. overfitting 금지 (§17)

benchmark filename 을 rule 에 넣지 않는다.

```text
금지: if filename == "real_h264_270s.mp4"
금지: if path contains "gop225"
허용: codec / duration / fps / resolution / GOP confidence / sampling density /
      estimated cost
```

## 12. evidence 부족 처리 (§18)

```text
EvidenceStrong / EvidenceLimited / EvidenceMissing
```

충분한 evidence 없으면 `SequentialPreferred`. 처음부터 공격적 extrapolation
을 하지 않는다.

## 13. decision reason (§19)

```text
ExactSparseVerified / GOPUnknown / HEVCFallback /
CostNotAdvantageous / SparseUnsupported / ExactnessUnverified
```

디버깅·benchmark 용이며 UI 노출은 불필요.

## 14. production integration (§20·21·22)

```text
planner enabled + decision telemetry + actual executor + fallback
```

compile-time/test-time switch 로 제어하며, **production default 는
Sequential 로 유지한다.**

fallback 은 정상 planner decision 의 일부다.

```text
SparseSeek → failure → SequentialDecode     (telemetry 기록)
```

**단, pixel mismatch 를 일으킨 뒤 결과를 candidate 로 수용한 뒤 fallback 했다고
처리하지 않는다.** 정확성 판단은 sample output **이전** 의 capability gate 와
executor contract 로 보장한다.

## 15. end-to-end validation (§24·25·26·36)

세 조건을 비교한다.

```text
A. Sequential baseline
B. Forced Sparse executor   (진단 전용)
C. Adaptive planner
```

**§36 이 가장 중요하다: 실제 `MediaSearchEngine::scan()` production 경로를
사용한다. probe 의 별도 algorithm 으로 결과를 만들지 않는다.**
I-2 에서 배운 `probe ≠ product` 원칙을 그대로 적용한다.

## 16. planner confusion matrix (§27)

```text
| Condition | Actual best | Planner | Exact | Result |
```

```text
Best + Exact        → PASS
Safe but slower     → acceptable
Unsafe selected     → FAIL
Unsafe avoided      → PASS
Unsupported avoided → PASS
```

## 17. 평가 기준 (§28)

planner 성공을 GPU 사용률이나 sparse 선택 횟수로 평가하지 않는다.

> **전체 end-to-end video analysis time 을 줄이면서 exactness 를 유지하는 것**

## 18. false positive / negative (§29·30)

```text
False positive (sparse 선택 → 실제 느림) : calibration data 추가, 반복 시 rule 수정
False negative (sequential 선택 → sparse 가 더 빠름) : 즉시 완화하지 않고 evidence 누적
```

기본 철학은 `correctness > unnecessary optimization`.

## 19. E-2B 성과 보존 (§34·35)

```text
production predicate 변경 없음
target 변경 없음
tolerance 변경 없음
target - 0.05 를 사용한 exact sparse seek 개념 유지
ft + 0.05 >= target 기준 유지
```

## 20. 금지 항목 (§4)

```text
HEVC parser / HEVC CRA parser      금지
FFmpeg source 수정 / version upgrade 금지
hardware decode / NVDEC / CUDA video decode / D3D11VA / QSV / Vulkan  금지
codec-specific decoder 교체         금지
production sampling tolerance 변경 금지
ft + 0.05 >= target 변경           금지
target timestamp 정의 변경          금지
"유사 frame" 허용 완화              금지
특정 파일명 workaround              금지
개인 미디어 path 하드코딩            금지
```

## 21. production default 변경 조건 (§44)

```text
exactness PASS + planner regression PASS + end-to-end PASS + fallback PASS
```

하나라도 실패하면 `PRODUCTION DEFAULT = Sequential` 을 유지한다.

## 22. selfcheck (§52)

```text
HEVC → Sequential
AV1 → Unavailable
GopEstimated → Sequential
GopKnown + exact sparse + cost beneficial → Sparse
exactness unsafe → Sequential
fallback → Sequential
```

기존 E-2B/E-3A test 는 모두 유지한다.

## 23. E COMPLETE 조건 (§45) — **종결 처리됨 (0.9.4.42 이후 사용자 결정)**

```text
E-1 pipeline 이해 + baseline
E-2 adaptive sampling candidate 구현/검증
E-3 planner calibration          E-3B 완료 / NOT ACCEPTED
E-3C                             Node E 종결 범위에서 정리 (별도 Stage 승격 없음, F 이관 없음)
E-4 성격의 production integration + end-to-end validation
                                  Node E 종결 범위에서 정리
```

그리고 CPU path PASS / GPU build PASS / video exactness PASS / fallback PASS /
no unexplained mismatch.

### 23-1. 종결 사유

E-3B 결과 sparse seek 는 **production 채택이 거부**되었다
(`EXACTNESS = DISPROVEN`, `docs/build-history/0.9.4.42.*`).
`ExactnessPolicy::RefuseAll` 로 sparse production path 가 도달 불가능하므로
E-4 가 통합할 "성격의" 경로가 남아 있지 않다.

따라서 **E-4 를 sparse 재도입의 근거로 읽어서는 안 된다.**

- **E-3C**: 별도 roadmap Stage 로 승격하지 않고, F 로 이관하지 않고, Node E 종결
  범위에서 정리한다. 향후 F architecture 에 참고가 될 수 있다는 사실만 참고사항으로
  기록한다. **작업 항목으로는 이관하지 않는다.**
- **E-4**: Node E 종결 범위에서 정리한다.

`ExactnessPolicy::RefuseAll` 기본값과 production 순차 디코딩 경로는 유지한다.

**F 는 Node E 종결 확인 이후 착수한다.** (원 문장: "F 는 E 가 완전히 끝나기 전에는
시작하지 않는다." — E 종결이 확정되었으므로 조건은 충족됨)

## 24. 산출물

```text
src/video_sampling_planner.{h,cpp}   planner (분류만)
src/video_decoder.{h,cpp}            exact sparse seek executor 추가
benchmark JSON                       decision telemetry
docs/build-history/0.9.4.42.{ko,en}.md
docs/worklog/0.9.4.{ko,en}.md
```

`docs/experiments` 는 만들지 않는다.
