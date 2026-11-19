# 현재 상태 (Current State)

> 새 세션이 **가장 먼저** 읽는 1페이지 요약이다. 80줄 이내를 유지한다.
> 이 문서는 요약/인덱스이며, 판정과 수치의 **원본은 아래 링크된 문서**다.

- 갱신: 2026-11-19
- 버전: **0.9.4.174**
- engine **1.5.2** / cache format **11** (출처: `BUILD_STATUS.md`)
- DB **1.0.6** / benchmark schema **9** (출처: `src/database.h`, `docs/llms.txt`)
- CPU CTest **145/145**, GPU CTest **146/146** (출처: `docs/node-status-gate-matrix.ko.md`)

## 지금 하는 일

- 프로세스 분리 **P1-P4 완료**, G4/G6 검증 완료(.111)
- **0.9.4.174**: TemporalAnchorV3 사용자 지정 알고리즘 제품 구현 (실험 opt-in, 기본값 불변, V1/V2 동결)
- V1(.172) / V2(.173) / V3(.174) 순차 구현 완료. 기본 채택은 없음

## 블로커

- **실제 Windows acceptance** 미완: LocalDumps+PDB 캡처, 전체 사용자 데이터셋 재스캔,
  Test Mode 토글/스크래치 격리, 미리보기/선택, Tiles/ListMode, 대형 데이터셋 순회
- **Sparse production**: `ExactnessPolicy::RefuseAll` 로 차단
- **F-1 CONDITIONAL**: NVDEC production adoption **NO**
- GPU MAX share boost: 문서화된 gap

## 다음 3개 행동

1. 실제 Windows acceptance 항목 실행·기록
2. 최종 Search/Index/Comparison acceptance
3. GPU MAX share boost gap 정리(문서 기준)

## 불변 조건 (건드리면 안 되는 것)

- engine **1.5.2** / cache format **11** — 변경 시 build-history 에 근거를 남긴다
- **V1/V2 동결**, V3는 실험 opt-in이며 기본값이 아니다
- 기존 판정(CLOSED / CONDITIONAL / DEFERRED / NOT ACCEPTED)을 임의로 승격하지 않는다

## 재시도 금지 (측정으로 기각)

| 후보 | 판정 |
|---|---|
| D9b | NOT ACCEPTED |
| 캐시 용량 32 | 기각 |
| scoring-path duplication | 기각 |
| WICBitmapInterpolationModeFant | 기각 |
| EXIF skip | 기각 |
| cache mutex contention | 기각 |
| Factory2 fallback | 기각 |
| raw OS file-open handling | 기각 |

근거: `BUILD_STATUS.md`, `docs/worklog/0.9.4.ko.md`

## 새 세션 읽는 순서

1. **이 문서**
2. `docs/node-status-gate-matrix.ko.md` — Node/Gate 상태판
3. `docs/development-progress.ko.md` — 현재 실행 큐·blocker
4. `docs/development-roadmap.ko.md` — 방향·선행조건
5. 활성 Node brief (`docs/implementation-briefs/`)
6. 필요 시 `docs/build-history/`, `docs/worklog/`, `docs/version-index.md`
7. 완료된 버전별 상세가 필요할 때만 `docs/development-progress-detail.ko.md` (현재 상태 아님)