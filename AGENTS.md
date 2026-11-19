# AGENTS.md - Workspace Root

실제 소스는 `src_unpacked/`에 있음. 모든 작업은 `src_unpacked/` 기준으로 수행.

## 중요 기억 사항

0. **사용자 리포트 언어는 한글/영어만**
   - 최종 리포트·설명·요약·질답은 **KO 또는 EN만** 쓴다. 중국어·기타 언어 혼입 금지.
   - 같은 내용을 두 언어로 제시할 때도 KO/EN 쌍으로만. 코드 식별자·파일명은 원문 유지.
   - `지시/` 폴더와 모든 사용자 대면에 동일 적용.

1. **빌드 내용은 한글+영문으로 DOCS에 저장**
   - `src_unpacked/docs/build-history/<버전>.ko.md` + `.en.md` 쌍. 한쪽만 작성 금지.
   - 중요 변경은 `변경 필요성 → 기존 구조 → 변경 구조 → 해결된 상황 → 검증 → 향후 영향`.
   - `build-history/README.ko/en.md` 버전 목록도 양쪽 갱신. 상세: `src_unpacked/AGENTS.md`.

2. **소스/컴파일 백업 zip — 항상 유지 (예외 없음)**
   - 버전 변경 시 저장소 루트 `backup/` 에 두 종류를 남긴다(cwd = `src_unpacked`).
     1. `scripts/backup_src.ps1` → `MediaSimilarityFinder-v<버전>-src.zip`
     2. `scripts/package_portable.ps1` → `MediaSimilarityFinder-v<버전>-Portable-Windows-x64.zip`
   - 소스 zip은 GitHub과 동일해야 한다. `git archive`만 사용(워킹트리 복사 금지). 스크립트가 `HEAD == origin/main`·미커밋 tracked 변경 없음을 검증하고 위반 시 중단.
   - 포터블은 빌드 산출물(Git 대상 아님). exe/DLL/ffmpeg + smoke 통과 후 생성하며 `src_unpacked/`에 만들지 않는다.
   - 회전: 종류별 최대 5개, `-src`·`-Portable` 독립. 초과분은 **휴지통**.
   - 검증 보고: 소스는 `git ls-tree` + `git hash-object` byte 일치, 포터블은 엔트리 수 + exe 존재.
   - 상세·근거: `src_unpacked/AGENTS.md` 4항.

3. **`지시/` 폴더 — 사용자 관리 (커밋 금지)**
   - 이 폴더의 `.md` 는 사용자 지시문이다. **읽기만 하고 수정·삭제하지 않는다.** git 커밋 금지.
   - 정리·휴지통·리셋 작업에서 `지시/` 와 저장소 루트 `*.md` 는 **대상에서 제외**한다.
   - 지시문이 `AGENTS.md` 와 충돌하면 지시문이 우선이며, 모호하면 사용자에게 확인한다.
   - 실수 사례: 루트 `*.md` 정리 중 `D9b 방향 결정 지시.md` 를 삭제했다(휴지통 복구). 애매하면 삭제보다 확인.

4. **반복 실행은 창 1개 유지 — 콘솔 개방/폐쇄 반복 금지 (예외 없음)**
   - 반복 작업은 **오케스트레이터 1개를 한 번만 띄우고** 그 안에서 시작~종료를 수행한다. 단계마다 새 프로세스/창 금지.
   - 반복 횟수가 2회 이상이면 도구 호출을 반복하지 말고 오케스트레이터로 통합한다.
   - 콘솔은 보이지 않게 한다. `-WindowStyle Hidden` 은 콘솔 할당을 막지 못하므로 `CREATE_NO_WINDOW` / `ProcessStartInfo.CreateNoWindow = $true` 를 쓴다.
   - 원인 4가지(2026-10-01 확인): ① 도구 호출당 powershell+conhost 1개 ② 제품의 `src/monitor.cpp` `_popen("nvidia-smi")` 가 run 중 약 3000개 창 생성 ③ 부모 콘솔이 없으면 자식이 새 보이는 콘솔 할당 ④ 해결은 이미 있는 `src/proc_capture.h::captureSilent`(CREATE_NO_WINDOW) 사용.
   - 제품 코드 변경은 사용자 승인 사항(2026-10-01 승인, `6cc19ba`). 미수정 `WINDOWS_GUI` 바이너리는 어떤 숨김 실행으로도 팝업을 막을 수 없다.
   - 폴링/감시는 무창 방식 + 긴 간격(45분 이상). 상태 확인은 `.done` 마커와 로그로 한다.
   - 상세: `src_unpacked/AGENTS.md` 11항.

5. **파일 삭제 — 직접 삭제 절대 금지, 항상 휴지통 (예외 없음)**
   - `Remove-Item`, `rm`, `del`, `rd`, `git clean`, `git rm`, API 하드 삭제 등 어떤 우회도 금지.
   - 삭제 대상은 `src_unpacked/scripts/safe_remove.ps1` 로만 휴지통 이동하고, **경로·건수를 보고**한다.
   - 적용 범위: 소스·문서·dataset·픽스처·빌드 산출물·백업 zip·임시파일 전부. "재생성 가능"은 예외가 아니다.
   - 빌드 디렉터리 전체 재생성처럼 대상 경로 자체를 다시 만드는 작업은 사전 보고 + **사용자 승인**.
   - 실수 사례: `-FingerprintOnly` 없이 `prepare_dataset.ps1` 를 실행해 픽스처가 하드 삭제됐다(복구). **지문만 갱신할 때는 반드시 `-FingerprintOnly`.**

6. **외부 프로세스 안전 — hot-path 반복 생성 금지 (예외 없음)**
   - Benchmark hot path/monitor/frame loop의 `_popen`·`system`·`CreateProcess`·`ShellExecute` 반복 금지.
     Run-level isolation(측정 run마다 exe 1개)은 허용한다.
   - 필요 시 `in-process API > persistent helper > 단발성 프로세스` 순으로 검토한다.
   - Windows child는 `src/proc_capture.h::captureSilent` 만 사용한다. timeout은 호출별 명시
     (telemetry 10000ms / metadata 30000ms / decode 120000ms), `INFINITE` 대기 금지.
   - Timeout은 실패다: 종료 요청 → 강제 종료 → handle 정리 → 명시적 실패. 필요 시 Job Object.
   - Runaway(프로세스/콘솔/orphan/재시도 폭증) 시 자동 체인 중단(circuit breaker).
   - Release Gate: raw 반복 생성 0 / 콘솔 팝업 0 / 무한 대기 0 / orphan 0 / FFmpeg 폴백 경로 확인.
   - 실측: `monitor.cpp` `_popen` 3000창 → `captureSilent` 교체(`6cc19ba`). 상세는 `src_unpacked/AGENTS.md` 11항.
