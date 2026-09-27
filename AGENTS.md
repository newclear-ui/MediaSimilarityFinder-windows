# AGENTS.md - Workspace Root

실제 소스는 `src_unpacked/`에 있음. 모든 작업은 `src_unpacked/` 기준으로 수행.

## 중요 기억 사항
1. **각 빌드의 내용을 한글과 영문으로 DOCS에 저장할 것**
   - 경로: `src_unpacked/docs/build-history/<버전>.ko.md` + `<버전>.en.md`
   - ko/en 쌍으로 저장, 한쪽만 작성 금지
   - 중요 변경은 `변경 필요성 → 기존 구조 → 변경 구조 → 해결된 상황 → 검증 → 향후 영향` 형식
   - `README.ko.md` / `README.en.md` 버전 목록도 양쪽 갱신
   - 상세 규칙은 `src_unpacked/AGENTS.md` 참조

2. **소스 백업 zip — 세션/에이전트가 바뀌어도 항상 유지 (예외 없음)**
   - 버전 변경 시점에 `src_unpacked/scripts/backup_src.ps1`을 반드시 실행해 zip 백업을 남긴다.
     - `powershell -ExecutionPolicy Bypass -File scripts/backup_src.ps1` (cwd = `src_unpacked`)
   - 파일명 `MediaSimilarityFinder-v<버전>-src.zip`, 위치는 저장소 루트 `backup/`.
   - **내용물은 GitHub과 동일해야 한다.** zip은 `git archive`로만 만든다(워킹트리 복사 금지). 코드와 문서를 모두 포함한다.
   - 실행 전 `HEAD == origin/main`, 미커밋 tracked 변경 없음을 스크립트가 검증하고 위반 시 중단한다.
   - **회전: 백업은 최대 3개 보존.** 신규 zip 생성 시 가장 오래된 1개를 휴지통으로 보낸다. 3개 초과분은 다음 빌드에서 회전 대상.
   - 생성 후 `git ls-tree` + `git hash-object`로 파일 목록과 내용의 byte 단위 일치를 검증해 보고한다.
   - 상세 규칙과 근거는 `src_unpacked/AGENTS.md` 4번 항목 참조.
