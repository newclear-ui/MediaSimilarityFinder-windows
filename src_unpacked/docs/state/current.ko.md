# 현재 상태 (Current State)

> 새 세션이 **가장 먼저** 읽는 1페이지 요약이다. 80줄 이내를 유지한다.
> 이 문서는 요약/인덱스이며, 판정과 수치의 **원본은 아래 링크된 문서**다.

- 갱신: 2026-11-19
- 버전: **0.9.4.174**
- engine **1.5.2** / cache format **11** (출처: `BUILD_STATUS.md`)
- DB **1.0.6** / benchmark schema **9** (출처: `src/database.h`, `docs/llms.txt`)
- CPU CTest **145/145**, GPU CTest **146/146**

## 지금 하는 일

- 프로세스 분리 **P1-P4 완료**, G4/G6 검증 완료(.111)
- **0.9.4.174**: TemporalAnchorV3 사용자 지정 알고리즘 제품 구현 (실험 opt-in, 기본값 불변, V1/V2 동결)
- V1(.172) / V2(.173) / V3(.174) 순차 구현 완료. 기본 채택은 없음

## 이어갈 작업 (다음 3개)

1. 실제 Windows acceptance 항목 실행·기록 (LocalDumps+PDB, 전체 사용자 데이터셋 재스캔, Test Mode 토글/스크래치 격리, 미리보기/선택, Tiles/ListMode, 대형 데이터셋 순회) — 절차: `docs/architecture/product-acceptance-runbook.ko.md`
2. 최종 Search/Index/Comparison acceptance 재확인
3. GPU MAX share boost gap 정리(문서 기준)

## 블로커

- **실제 Windows acceptance** 미완
- **Sparse production**: `ExactnessPolicy::RefuseAll` 로 차단
- **F-1 CONDITIONAL**: NVDEC production adoption **NO**
- GPU MAX share boost: 문서화된 gap

## 노드/판정 요약

| 영역 | 상태 |
|---|---|
| A / B / C / D / I | CLOSED / COMPLETE |
| E | CLOSED / NOT ACCEPTED (sparse exactness 미입증, RefuseAll) |
| F-1 | CONDITIONAL (NVDEC production NO) |
| S0–S3 | CLOSED |
| S4 | CLOSED (기능 acceptance .112 + 제품 acceptance) |
| S5 | NOT CLOSED (XMP-REAL-001 부분 측정) |
| S6 | NOT STARTED / DEFERRED (실측 미실행) |
| S7 / S8 | NOT STARTED |

## 불변 조건 (건드리면 안 되는 것)

- engine **1.5.2** / cache format **11** — 변경 시 build-history 에 근거를 남긴다
- **V1/V2 동결**, V3는 실험 opt-in이며 기본값이 아니다
- 기존 판정(CLOSED / CONDITIONAL / DEFERRED / NOT ACCEPTED)을 임의로 승격하지 않는다

## 재시도 금지 (측정으로 기각)

D9b · 캐시 용량 32 · scoring-path duplication · `WICBitmapInterpolationModeFant` · EXIF skip · cache mutex contention · Factory2 fallback · raw OS file-open.
근거: `docs/worklog/0.9.4.ko.md` 실험 인덱스.

## 새 세션 읽는 순서

1. **이 문서** (필수)
2. `docs/development-progress.ko.md` 의 **Active Build Queue 절만** (필수)
3. 활성 Node brief — 작업 착수 시
4. matrix / roadmap — 노드 단위 상세가 필요할 때만
5. build-history / architecture / worklog / version-index — 해당 작업 시

## 작업별 필수 참조

- acceptance 실행: `docs/architecture/product-acceptance-runbook.ko.md`
- crash 대응: `docs/architecture/crash-response-runbook.ko.md`
- 문서 작성·이동: `docs/document-naming.ko.md`
- 소스 구조 변경: `docs/STRUCTURE.md`
- 실험 기록·기각 후보: `docs/worklog/0.9.4.ko.md` 실험 인덱스

## 시작 시 읽지 않는 문서 (온디맨드)

- `docs/version-index.md` (77 KB), `docs/worklog/0.9.4.ko.md` (50 KB), `docs/development-progress-detail.ko.md` (167 KB), `docs/build-history/README.ko.md` (117 KB)
- 위 문서는 조회·인용이 필요할 때만 연다. 시작 시 통째로 읽지 않는다.
