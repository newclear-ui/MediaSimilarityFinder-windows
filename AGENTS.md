# AGENTS.md - Workspace Root

실제 소스는 `src_unpacked/`에 있음. 모든 작업은 `src_unpacked/` 기준으로 수행.

## 중요 기억 사항
1. **각 빌드의 내용을 한글과 영문으로 DOCS에 저장할 것**
   - 경로: `src_unpacked/docs/build-history/<버전>.ko.md` + `<버전>.en.md`
   - ko/en 쌍으로 저장, 한쪽만 작성 금지
   - 중요 변경은 `변경 필요성 → 기존 구조 → 변경 구조 → 해결된 상황 → 검증 → 향후 영향` 형식
   - `README.ko.md` / `README.en.md` 버전 목록도 양쪽 갱신
   - 상세 규칙은 `src_unpacked/AGENTS.md` 참조

2. **소스/컴파일 백업 zip — 세션/에이전트가 바뀌어도 항상 유지 (예외 없음)**
   - 버전 변경 시점에 **두 종류**의 zip 백업을 남긴다. 둘 다 저장소 루트 `backup/` 에 모은다.
     1. **소스 백업** — `powershell -ExecutionPolicy Bypass -File scripts/backup_src.ps1` (cwd = `src_unpacked`)
        - 파일명 `MediaSimilarityFinder-v<버전>-src.zip`
     2. **컴파일(포터블) 백업** — `powershell -ExecutionPolicy Bypass -File scripts/package_portable.ps1` (cwd = `src_unpacked`)
        - 파일명 `MediaSimilarityFinder-v<버전>-Portable-Windows-x64.zip`
        - 스크립트가 exe/DLL/ffmpeg를 모아 smoke 테스트를 통과시킨 뒤 zip 으로 만든다.
   - **소스 zip은 GitHub과 동일해야 한다.** `git archive`로만 만든다(워킹트리 복사 금지). 코드와 문서를 모두 포함한다.
     - 실행 전 `HEAD == origin/main`, 미커밋 tracked 변경 없음을 스크립트가 검증하고 위반 시 중단한다.
   - **포터블 zip은 빌드 산출물이다.** Git 대상이 아니며, smoke 테스트를 통과한 exe 를 담는다.
   - **회전: 종류별로 최대 3개 보존.** `-src` 와 `-Portable` 는 서로 독립적으로 회전한다.
     신규 zip 생성 시 초과분을 **휴지통으로 보낸다**(삭제 금지).
   - 생성 후 소스 zip은 `git ls-tree` + `git hash-object`로 파일 목록과 내용의 byte 단위 일치를 검증해 보고한다.
   - 포터블 zip은 `Expand` 후 엔트리 수와 exe 존재를 확인한다.
   - **포터블 zip을 `src_unpacked/` 에 만들지 않는다.** 항상 `backup/` 으로 바로 쓴다(임시 파일 경유).
   - 상세 규칙과 근거는 `src_unpacked/AGENTS.md` 4번 항목 참조.

3. **사용자 지시 폴더 `지시/` — 사용자가 직접 관리 (커밋 금지)**
   - 저장소 루트 `지시/` 폴더의 `.md` 문서는 **사용자가 나에게 남기는 지시문**이다. 사용자가 직접 보관·삭제한다.
   - **절대 규칙**
     1. 이 폴더는 **git에 커밋하지 않는다** (`.gitignore`에 등록됨).
     2. **내가 내용을 수정하거나 삭제하지 않는다.** 읽기만 한다.
     3. 임시파일 정리/휴지통 처리/리셋 시 이 폴더를 **대상으로 삼지 않는다.**
   - 사용자가 지시하면 내부 문서를 백업하거나 휴지통으로 보낼 수 있다. 명시가 없으면 손대지 않는다.
   - 이 폴더의 지시는 `AGENTS.md` 및 지시문本身보다 우선하는 사용자 지시로 취급한다. 지시문마다 작업 순서/금지 항목이 적혀 있으면 그대로 따른다.
   - 새 지시문이 오면 먼저 이 폴더를 읽고, 기존 문서와 충돌하면 사용자에게 확인한다.
   - **실수 주의 (실发生过 사례)**: 임시파일 정리를 위해 저장소 루트의 `*.md` 를 대상으로 삼았다가
     `D9b 방향 결정 지시.md` 를 삭제했다(휴지통에서 복구함). 정리·회전·리셋 작업의 대상 경로는
     **반드시 `지시/` 와 저장소 루트 `*.md` 를 제외**한 뒤 확정한다. 애매하면 삭제보다 먼저 사용자에게 확인한다.
