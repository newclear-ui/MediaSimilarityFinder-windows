# 제품 acceptance 런북 / 체크리스트 (Search · Index · Comparison)

목적: 0.9.4 개발선의 **최종 제품 acceptance**(Search/Index/Comparison, 현재 `NOT ACCEPTED`)를 동일한 절차·기록 형식으로 수행하기 위한 실행 문서. 결과 기록은 `docs/build-history/<버전>.*`와 `docs/worklog/0.9.4.*`에 남기고, 이 문서는 절차만 정의한다.

## 0. 사전 조건

- 빌드: `--version`이 대상 빌드와 일치(Finder/Backend 동일). 실사용은 portable 또는 `build-windows-gpu\Release` 트리.
- 데이터: 실제 dataset 루트(예: `G:\Downloads\ss_twit`). 인덱스가 이미 있다는 전제와 없다는 전제(첫 스캔)를 구분해 기록한다.
- 도구: `%TEMP%\msf_scan.log`(수행 직전부터 누적), 상세 로그 JSON 저장(완료 팝업 → [상세 로그] → Save), 화면 항목은 스크린샷.
- 원칙: **추측 수정 금지** — crash 발생 시 `crash-response-runbook`으로 dump/PDB/로그를 먼저 수집한다.

## 1. Index (불러오기/인덱스) 체크리스트

| # | 확인 | 기대값 / 판정 신호 |
|---|---|---|
| I-1 | 불러오기(Load) 완료 | 목록이 점진적으로 채워지고 창이 반응(프리즈 없음). `msf_scan.log` `resultsApply`/`groupFill` 수치 |
| I-2 | 검색 시작/종료 시 목록 | **재구축(churn) 없음** — 스크롤·행 유지, 신규 그룹만 하단 추가(0.9.4.118+) |
| I-3 | 유령 그룹 부재 | 삭제 파일이 그룹 멤버로 남지 않음. `diskCleanup missing=N` 로그 존재(0.9.4.116+) |
| I-4 | 증분 fingerprint | 변경/신규 파일만 `analyzed`로 집계(`summary.analyzed` == 실제 변경 수) |
| I-5 | 백엔드 생존 | 킬/재시작 없음(62097 없음), `alive` 하트비트 정상 |
| I-6 | 상세 로그 요약 가독성 | 처리량 기준 라벨 표시(`처리량(분석 단계)`, `전체/신규 files/s`, 0.9.4.119+) |

## 2. Search (검색) 체크리스트

| # | 확인 | 기대값 / 판정 신호 |
|---|---|---|
| S-1 | 진행 표시 정확성 | analyzed/scanned/unchanged, 진행률·ETR이 실제와 모순 없음 |
| S-2 | 중지/재개 | Stop 후 부분 결과 유지(설계), 재시작 시 frontier 동작 |
| S-3 | analysis-failed/unchanged/modified 의미 | 상세 로그·상세정보가 저장 스키마와 일치(0.9.4.46+) |
| S-4 | 검증 모드(A/B/AB) | 상세 로그 `mode` == 요청 모드; AB 최종 패스는 한쪽 변경 쌍만(0.9.4.107+) |
| S-5 | 크래시/중단 내성 | 중단·재시작·백엔드 급사 시 UI 복구(자동 재개 1회, 0.9.4.73+) |
| S-6 | 요약 패널 | 총/읽기/인덱스/확인 쌍 수가 텔레메트리와 일치(monotonic) |

## 3. Comparison (비교/결과) 체크리스트

| # | 확인 | 기대값 / 판정 신호 |
|---|---|---|
| C-1 | 그룹/멤버/쌍 카운트 | `matchescandidates/pairs/groups` ↔ 화면 ↔ 상세 로그 요약 일치 |
| C-2 | 배지 의미 | `identicalContent`는 **전체 내용** SHA-256 동일일 때만(0.9.4.113+); 지각 동일과 구분 표기 |
| C-3 | 파생본/복사본 | 리사이즈·재압축본이 동일 그룹으로, 오탐 없음(0.9.4.106 fixture + 실데이터) |
| C-4 | reduction 표시 | 100.0% 표기 조건이 문서화된 의미와 일치 |
| C-5 | 정렬/필터 | 정렬 변경 시 재구축 정상, 검색 중 잠금 동작 |
| C-6 | 파일 액션 연동 | 삭제/이동 후 `delete/move/prune/forget` 로그와 목록 정합(0.9.4.105+) |

## 4. 판정과 기록

- 각 항목: PASS / FAIL(재현 절차 포함) / N-A(사유). FAIL은 즉시 수정 빌드 후보로 등록한다.
- 요약 수치(예: wall/analyzed/pairs)는 상세 로그 JSON과 1:1 대조하고 build-history에 수치를 남긴다(예: 0.9.4.118 실기 대조 형식).
- 최종 판정은 Search/Index/Comparison 각각에 대해 내리며, `NOT ACCEPTED`→`ACCEPTED` 승격은 세 항목 모두 PASS일 때만 한다(S4/S5 승격 조건과 동일 세션에서 갱신).

## 5. 참고 문서

- `docs/build-history/0.9.4.118.*` — churn 실기 대조 예시, `0.9.4.119.*` — 요약 라벨.
- `docs/architecture/crash-response-runbook.{ko,en}.md` — crash 대응.
- `docs/development-progress.{ko,en}.md` — 현재 큐/판정.
