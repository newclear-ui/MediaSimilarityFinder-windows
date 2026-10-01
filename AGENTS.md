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

4. **파일 삭제 — 직접 삭제 절대 금지, 항상 휴지통으로 (예외 없음)**
   - **어떤 파일·폴더도 직접 삭제하지 않는다.** `Remove-Item`, `rm`, `del`, `rd`, `git clean`,
     `git rm`, 파일시스템 API 하드 삭제 등 우회 경로도 금지한다.
   - 삭제해야 하는 상황이면 **반드시 휴지통(Recycle Bin)으로 보낸다.** 사용자가 직접 비운다.
   - 준삭제로 끝내지 않는다. 휴지통 이동 후 **경로·건수를 보고**하고, 무엇을 버렸는지 사용자에게 알린다.
   - **적용 범위**: 소스·문서·dataset·테스트 픽스처·빌드 산출물·백업 zip·임시파일을 **전부 포함**한다.
     "재생성 가능", "파일이므로 하드 삭제해도 된다" 는 이유로 예외를 만들지 않는다.
     단, 빌드 디렉터리 전체 재생성(`prepare_dataset.ps1 -Root`)처럼 **대상 경로 자체가 재생성되는 작업**은
     그 전에 대상·파일 수를 보고하고 **사용자 승인을 받아** 진행한다.
   - **도구**: `src_unpacked/scripts/safe_remove.ps1` 를 사용한다. 하드 삭제 기능이 없고 휴지통으로만 보낸다.
   - **실수 사례 (2026-09-28)**: dataset fingerprint 갱신 목적으로 `prepare_dataset.ps1` 를 실행하면서
     `-FingerprintOnly` 스위치를 쓰지 않아 dataset을 통째로 재생성하면서
     `test_sample_img_vid/images/format` 의 수백 개 픽스처가 하드 삭제되었다.
     원본 풀(`G:\Downloads\ss_twit`)에서 재선택하여 복구했으나, **지문만 갱신할 때는 반드시 `-FingerprintOnly`.**


5. **벤치마크 자동화 안전 규칙 — 반복 외부 프로세스 생성 금지**
   - 제품 benchmark hot path와 benchmark 자동화 harness에서는 **짧게 실행되고 곧바로 종료되는 외부 프로세스를 반복 생성하지 않는다.** 특히 telemetry/monitoring에서 `_popen`, `system`, 반복 `CreateProcess`, `ShellExecute` 등을 샘플마다 호출하는 구조를 금지한다.
   - 외부 도구가 꼭 필요하면 우선순위를 **in-process API → run/session 수명 동안 유지하는 persistent helper 1개 → 불가피한 단발 호출** 순으로 검토한다. 반복 telemetry에서는 매 sample마다 child process를 새로 만들지 않는다.
   - Windows child process는 **console window가 생성되지 않는 방식**과 stdout/stderr capture를 기본으로 한다. 프로젝트의 검증된 process-capture wrapper가 있으면 이를 재사용하고, 각 호출부가 임의의 숨김 실행 방식을 새로 만들지 않는다.
   - 외부 process wrapper는 무한 대기를 기본 계약으로 두지 않는다. timeout/lifecycle/실패 시 child 종료 및 부모 프로세스 보호를 고려하며, 장시간 백그라운드 helper를 쓰는 경우 소유권과 종료 경계를 명확히 한다.
   - benchmark/test harness도 동일한 규칙을 따른다. `Start-Job`, 반복 `Start-Process`, `cmd`, `powershell` 등을 조합하여 console/child process를 폭증시키는 자동화는 금지한다.
   - 자동 실행 체인은 **runaway child process, 예상치 못한 console window, orphan process, 반복 비정상 종료**가 감지되면 자동 retry를 계속하지 않고 즉시 stop하는 circuit-breaker 원칙을 따른다.
   - 해당 규칙은 측정 오염 여부와 별개인 **PC 부담/안정성 규칙**이다. 한 run이 S6에서 제외되어 측정값을 오염시키지 않았더라도 반복 process spawn 자체는 개선 대상이다.
   - GPU telemetry는 가능하면 NVML 등 in-process API를 우선 검토한다. `nvidia-smi`를 반복 spawn하는 구조는 금지하며, persistent sampler가 필요하면 별도 승인/설계 후 적용한다.
   - 새 release gate에서는 소스/스크립트의 raw process-creation 경로를 점검하고, 승인된 wrapper 외의 반복 spawn이 없는지 확인한다.
