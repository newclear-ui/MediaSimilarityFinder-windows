# AGENTS.md - MediaSimilarityFinder 작업 기억사항

## 중요 기억 사항
1. **각 빌드의 내용을 한글과 영문으로 DOCS에 저장할 것**
   - 모든 버전 변경은 `docs/build-history/<버전>.ko.md` + `<버전>.en.md` 2개 파일로 저장
   - 한쪽만 작성 금지. ko/en 내용 동일하게 유지
   - 중요 설계 변경은 두 파일 모두에 `변경 필요성 → 기존 구조 → 변경 구조 → 해결된 상황 → 검증 → 향후 영향` 형식으로 기록
   - 일반 변경은 요약만 기록
   - `docs/build-history/README.ko.md` + `README.en.md` 버전 목록 테이블에도 양쪽 모두 추가
   - 숫자 동기화: `kCacheFormatVersion`·테스트 수·엔진/DB 버전이 바뀌면 `docs/STRUCTURE.md`와 `docs/llms.txt`의 해당 숫자도 같은 커밋에서 갱신
   - 교체된 구현은 `docs/architecture/legacy/`에 비빌드용 snapshot으로 보존. 활성 소스에 주석 죽은 코드 금지
   - 아키텍처 변경 시 `docs/architecture/*.ko.md` + `.en.md`도 양쪽 갱신
   - 소스 구조·진입점·문서/릴리즈 절차가 바뀌면 `docs/STRUCTURE.md`(구조)와 `docs/llms.txt`(LLM 인덱스+raw URL 규칙)도 함께 갱신

2. 빌드 기준: Visual Studio 18 2026 x64, project-local `vcpkg_installed`, `vcpkg.json`이 유일 의존성 기준
3. 릴리즈 zip 규칙: 최근 2개 빌드(bin/src/portable 각 1개)만 유지. src.zip은 `git archive <태그> -- src_unpacked ':!*.zip'`으로 만들어 zip 중첩 금지
4. **소스/컴파일 백업(zip) 규칙 — 세션·에이전트가 바뀌어도 항상 유지**
   - 목적: 최근 소스+문서와 컴파일 산출물을 zip 백업으로 항상 남긴다. **이 규칙은 어떤 세션/에이전트에서도 예외 없이 지켜야 한다.**
   - **두 종류의 zip을 모두 저장소 루트 `backup/` 에 모은다.**
     1. 소스 백업 — `powershell -ExecutionPolicy Bypass -File scripts/backup_src.ps1`
        - 파일명 `MediaSimilarityFinder-v<버전>-src.zip`
     2. 컴파일(포터블) 백업 — `powershell -ExecutionPolicy Bypass -File scripts/package_portable.ps1`
        - 파일명 `MediaSimilarityFinder-v<버전>-Portable-Windows-x64.zip`
   - **소스 zip은 반드시 GitHub과 동일해야 한다.** zip은 `git archive`로만 만든다. 워킹트리 복사 금지. 코드와 문서를 모두 포함한다.
     - 스크립트가 다음을 사전 검증한다. 위반 시 refuse 한다.
       1. `HEAD == origin/main` (미push 커밋이 있으면 중단)
       2. tracked 파일에 미커밋 변경이 있으면 중단
       3. `.zip`은 archive 대상에서 제외 (zip 중첩 금지)
   - **포터블 zip은 빌드 산출물이다.** Git 대상이 아니며, exe/DLL/ffmpeg 를 모아 smoke 테스트를 통과시킨 뒤 만든다. `portable.json` 과 zip 파일명은 `CMakeLists.txt` 의 VERSION 을 읽어 정하므로 하드코딩되지 않는다.
   - **회전(rotation)**: **종류별로** 최대 **3개**를 보존한다. `-src` 와 `-Portable` 는 서로 독립적으로 회전한다. 신규 zip 생성 시 초과분을 **휴지통으로 보낸다**(삭제하지 않는다).
   - **포터블 zip을 `src_unpacked/` 에 만들지 않는다.** 항상 `backup/` 으로 바로 쓴다(임시 파일 경유).
   - 검증 후 보고:
     - 소스 zip: `git ls-tree` + `git hash-object` 로 **파일 목록과 내용의 byte 단위 일치** (이름만 같으면 통과 아님)
     - 포터블 zip: 엔트리 수와 `MediaSimilarityFinder.exe` 존재 여부
   - 이 규칙은 버전 변경 시점마다 수행한다. `docs/STRUCTURE.md`, `docs/llms.txt`, `BUILD_STATUS.md`에 상태를 기록한다.
5. 엔진/DB 버전 규칙 (빌드 번호 0.9.2.x와 분리, "M.m.p" 형식):
   - 검색엔진 판정 버전 `MediaSearchEngine::kEngineVersion`: patch=임계·가중·게이트 튜닝, minor=새 단계·규칙 추가, major=판정 아키텍처 교체. 상향 시 다음 스캔에서 저장 쌍 자동 재검증 (전체 재스캔 불필요)
   - DB 스키마 버전 `Database::kDatabaseVersion`: patch=부가적 추가(테이블·컬럼·인덱스, 구코드 읽기 가능), minor=마이그레이션 필요 변경, major=파괴적 변경(구행 무효, wipe+재스캔). open 시 마이그레이션 후 스탬프
   - 빌드 번호와 무관하게 필요할 때만 상향. 상향한 빌드는 build-history에 명시
6. 공식 기준선(0.9.2.32)과 작업 검증선(0.9.4.25, 개발선 0.9.4) 구분 유지. 판정 의미 불변, additive CUDA 커널·호출 병합 허용, CPU fallback 유지

7. **0.9.4 개발선 작업 규칙**

- **OpenCode 및 ChatGPT 에이전트는 새로운 세션을 시작할 때 아래 문서를 반드시 읽고 기억한다.**
  `docs/node-status-gate-matrix.ko.md` / `.en.md`는 전체 Node/Gate를 복원하는 1페이지 인덱스이며,
  `docs/development-progress.ko.md` / `.en.md`는 현재 실행 큐와 실제 진행 상태를 복원하는 **필수 실행 기준 문서**다.
  **소스를 변경하기 전에 아래 순서를 반드시 읽고, 현재 Gate가 허용하지 않는 Node를 먼저 시작하지 않는다.**
  1. `docs/node-status-gate-matrix.ko.md` (통합 상태판)
  2. `docs/development-progress.ko.md` (현재 작업 우선순위 · 현재 위치 · blocker · 완료 주요 이정표)
  3. `docs/development-roadmap.ko.md` (방향 · 선행조건)
  4. 활성 Node의 `docs/implementation-briefs/<Node>-*.ko.md` (실행 계약)
  5. 필요한 `docs/architecture/*.ko.md`
  6. 관련 `docs/build-history/<version>.ko.md` + `docs/worklog/0.9.4.ko.md`
  7. source / test
  `development-progress`의 Active Build Queue가 현재 작업의 우선순위를 결정하며,
  완료된 상세 기록은 Work Log / Build History에 보존하고 Progress에는 압축된 주요 이정표만 남긴다.
  **Node 또는 Build의 진척·판정·Gate·우선순위가 바뀌면 `development-progress.ko.md` / `.en.md`를 즉시 갱신한다.** 구현/검증의 시작·완료, 판정 변경, 다음 Build 확정은 발생한 세션에서 반영하며, 완료된 항목은 Active Build Queue에서 제거하고 완료 주요 이정표로 이동한다.
  상태판의 판정과 수치가 원본과 다르면 **원본이 맞다.** 상태판은 갱신이 늦어질 수 있는 인덱스로 취급한다.
- 상위 개발 방향은 `docs/development-roadmap.ko.md` / `.en.md`가 기준이다.
- 현재 실제 상태는 `docs/development-progress.ko.md` / `.en.md`가 기준이다.
- 통합 상태판(`node-status-gate-matrix`)은 위 둘의 복사본이 아니다. Node 상태/선행조건/정체 원인/판정/다음 Gate를 한 장에 모은 진입점이며, 판정·수치의 원본은 여전히 Roadmap/Progress/Brief/Build History/Work Log이다.
- Roadmap 노드 A/B/C는 버전 번호가 아니다.
- 검증된 코드 상태에 따라 버전을 `0.9.4.0 → 0.9.4.1 → 0.9.4.2 → ...`로 진행한다.
- 문제 발생 시 A1/B1/C1 같은 하위 작업으로 기록하고 진단 → 수정 → 회귀검증 후 같은 node gate로 복귀한다.
- 구조 방향을 바꿔야 하면 Roadmap과 Progress를 함께 갱신한다.
- 사용자/상위 아키텍처 명칭은 CPU/GPU로 통일한다.
- GPU는 ON/OFF만 사용자에게 노출한다. GPU 사용률 수동 설정은 제거한다.
- Resource Mode는 Maximum/High/Balanced/Gaming/Manual을 유지한다.
- GPU 작업 배분은 고정 50:50이 아니라 capability + calibration + 실시간 부하 + throughput + queue + transfer cost에 의해 동적으로 결정한다.
- 성능 profile은 INI에 저장하고 live runtime state가 항상 우선한다.
- 상위 GPU abstraction은 vendor-neutral이며 NVIDIA CUDA/NVDEC, Vulkan, AMD HIP/ROCm, Intel Level Zero는 독립 backend 후보로 취급한다.
- CPU fallback은 항상 유지한다.
- Build entry point는 build-windows-cpu / build-windows-gpu 명칭을 사용한다.
- 버전별 예정표를 다른 문서에 중복 작성하지 않고 Development Roadmap에서 통합 관리한다.
- 활성 development node의 세부 구현은 `docs/implementation-briefs/`의 KO/EN 문서를 따른다. Roadmap에는 방향과 경계만 두고 상세 구현 단계를 중복 작성하지 않는다.
- Node B와 D는 신규 설계, Node C는 기존 profile/benchmark 개념의 부분 재사용 + 확장으로 취급한다.
- Benchmark/Telemetry는 각 development node의 완료조건과 함께 구현한다.

8. **벤치마크 / Telemetry**

- Benchmark / Telemetry는 모든 development node의 핵심 계층이다.
- 상세 설계는 `docs/architecture/benchmark-telemetry-roadmap.ko.md` + `.en.md`를 따른다.
- 측정되지 않은 값은 0으로 기록하지 않는다.
- measured / not_measured / not_available / partial / failed / fallback 상태를 구분한다.
- scheduler decision, calibration, backend, decoder, queue, transfer, fallback을 기록한다.
- decodedFrames와 sampledFrames를 분리한다.
- human-readable summary와 machine-readable JSON을 구분한다.
- instrumentation이 검색 정합성이나 결과를 변경해서는 안 된다.
- 실제 버전의 변경과 검증은 `docs/build-history/<version>.ko.md` + `.en.md`에 기록한다.


9. **성능 튜닝 / 프로파일링 실험의 장기 기록 (성공 여부와 무관)**

- **모든 성능 튜닝·프로파일링·benchmark·microbenchmark·병목 분석·CPU/GPU 비용 분석·memory/I/O/decode·thread contention·cache 성능·알고리즘 최적화 후보 실험은, 성공했든 실패했든 기술 기록으로 보존한다.** 기각된 가설, 측정값, 실행 조건, 기각 사유, 향후 재검토 조건까지 남긴다.
- **새로운 문서 종류를 만들지 않는다.** 전용 `docs/experiments/` 같은 디렉터리를 만들지 않는다. 상세 수치는 `docs/build-history/<version>.*`에, 실험 간 관계와 판단 흐름은 `docs/worklog/<line>.*`의 **Performance / Tuning Experiment Index**에 기록한다. 연결 순서는 `Implementation Brief → Build History → Work Log Index → Development Progress`다.
- **문서 역할** — Implementation Brief=무엇을 시험할지 사전 정의, Build History=실제 변경·측정값·수치·기각 근거, Work Log=왜 그 후보를 선택/기각했고 다음은 무엇인가, Development Progress=현재 상태와 살아 있는 후보 요약.
- **Build History 필수 기록 항목** — Experiment ID, Version, Date, Purpose, Hypothesis, Baseline, Target, 후보 선정 이유, Implementation, Dataset, Dataset fingerprint, Hardware, OS, Build configuration, Run count, Raw measured values, Mean, Median, Min, Max, Range, 파생 비율, Accuracy result, Verdict parity, CPU/GPU parity, Compatibility impact, Performance result, Status, Reason, Known limitation, **Future revisit condition**, Related experiments.
- **측정하지 않은 값은 `N/A` 또는 `Not measured` 로 명시한다. 추정해서 채우지 않는다.**
- **사용하는 상태값**: BASELINE / PASS / NOT ACCEPTED / REJECTED / DEFERRED / LOW PRIORITY / INCONCLUSIVE / SUPERSEDED.
- **`NOT ACCEPTED` 와 `REJECTED` 는 "현재 조건에서 성공하지 못했다"는 뜻이지 "영구 폐기"가 아니다.** 코드 구조, 라이브러리, Windows/WIC, compiler/runtime, CPU/GPU, dataset, build configuration 이 바뀌면 다시 유효해질 수 있으므로 **삭제하지 않고 재검토 조건과 함께 유지한다.**
- **과거 측정값은 임의로 덮어쓰지 않는다.** 명백한 오류(계산·단위·instrumentation bug·잘못된 fingerprint·문서 오기)만 수정하며, 수정 시 `원래 기록 → 오류 원인 → 수정된 값 → 수정 이유` 를 함께 남긴다. D1 의 factory telemetry bug 가 실제 사례다.
- **Profiling telemetry 자체도 검증 대상이다.** 단계별 합이 원래 총합을 재구성하는지 검사하고, 그럴듯한 수치라는 이유로 자동 신뢰하지 않는다.
- **측정 오차 범위의 차이는 개선으로 선언하지 않는다.** 실행 횟수(5회 이상 권장)와 median/min/max/range 를 기록한다.


- **GPT Fix:** Node C4.1 calibration lifecycle correction. See docs/build-history/0.9.4.13.ko.md / .en.md.

10. **파일 삭제 — 직접 삭제 절대 금지, 항상 휴지통으로 (예외 없음)**
   - **어떤 파일·폴더도 직접 삭제하지 않는다.** `Remove-Item`, `rm`, `del`, `rd`, `git clean`,
     `git rm`, 파일시스템 API 하드 삭제 등 우회 경로도 금지한다.
   - 삭제해야 하는 상황이면 **반드시 휴지통(Recycle Bin)으로 보낸다.** 사용자가 직접 비운다.
   - 준삭제로 끝내지 않는다. 휴지통 이동 후 **경로·건수를 보고**하고, 무엇을 버렸는지 사용자에게 알린다.
   - **적용 범위**: 소스·문서·dataset·테스트 픽스처·빌드 산출물·백업 zip·임시파일을 **전부 포함**한다.
     "재생성 가능", "파일이므로 하드 삭제해도 된다" 는 이유로 예외를 만들지 않는다.
     단, 빌드 디렉터리 전체 재생성(`prepare_dataset.ps1 -Root`)처럼 **대상 경로 자체가 재생성되는 작업**은
     그 전에 대상·파일 수를 보고하고 **사용자 승인을 받아** 진행한다.
   - **도구**: `scripts/safe_remove.ps1` 를 사용한다. 하드 삭제 기능이 없고 휴지통으로만 보낸다.
   - **실수 사례 (2026-09-28)**: dataset fingerprint 갱신 목적으로 `prepare_dataset.ps1` 를 실행하면서
     `-FingerprintOnly` 스위치를 쓰지 않아 dataset을 통째로 재생성하면서
     `test_sample_img_vid/images/format` 의 수백 개 픽스처가 하드 삭제되었다.
     원본 풀(`G:\Downloads\ss_twit`)에서 재선택하여 복구했으나, **지문만 갱신할 때는 반드시 `-FingerprintOnly`.**


11. **외부 프로세스 안전 (C++ 구현 규칙 — 예외 없음)**
   - 외부 프로세스 실행은 `src/proc_capture.h::captureSilent` 로만 한다.
     raw `_popen` / `system` / `CreateProcess` / `ShellExecute` 직접 호출을 금지한다.
   - `captureSilent(cmd, out, timeoutMs)` — timeout은 호출자가 등급별로 명시한다. 전역 기본값은 두지 않는다.
     telemetry 10000ms / metadata 30000ms / decode 120000ms. 등급과 근거는 호출부 주석에 남긴다.
   - Timeout은 실패다. child 종료 요청 → 강제 종료 → handle 정리 → `false` 반환.
     부모는 timeout에 bounded 정리 시간을 더한 범위를 넘겨 대기하지 않는다.
   - POSIX 경로는 `popen` 유지 (release 대상이 아니므로). Windows가 아닌 플랫폼의 bounded 대기는 재검토 항목으로 남긴다.
   - GPU telemetry 우선순위: `in-process API (NVML 후보, deferred) > persistent helper (별도 설계 후) > 단발성 프로세스 (현재)`.
   - FFmpeg는 native library path를 우선한다. CLI fallback은 no-FFmpeg 빌드용으로 유지하되,
     frame/sample 단위 반복 호출과 timeout을 release gate에서 검증한다.
   - Benchmark hot path/monitor/frame loop의 반복 spawn은 Category C (unsafe)로 취급하고 release에 남기지 않는다.
     run-level isolation(측정 run마다 제품 프로세스 1개)은 허용한다.
   - Validation harness는 실패 시 즉시 재시도를 반복해서 process explosion을 만들지 않는다 (circuit breaker).
   - Release Gate 검증 항목: raw 반복 생성 0 / console popup 0 / 무한 대기 0 / orphan 0 / FFmpeg release path 확인 / GPU telemetry 반복 spawn 금지.
   - **실측 사례 (2026-10-01)**: `monitor.cpp` 3초 throttle `_popen` → 약 3000개 창 반복 (`6cc19ba` 로 교체),
     `captureSilent` INFINITE 대기 → bounded lifecycle + 호출별 timeout (`proc_capture_test` · `monitor_test` 반복 샘플로 검증),
     `WINDOWS_GUI` 미수정 바이너리는 팝업 불가피함이 증명됨 (40파일 실행 중 보이는 창 11건).

12. **임시 작업 디렉터리 — `D:\Temp\OpenCodeWork` 고정 (영구 규칙)**
   - 조사·격리·프로브·대량 복사 등 모든 임시 작업은 **`D:\Temp\OpenCodeWork` 아래에서만** 수행한다.
     OS 영역인 `C:` (`$env:TEMP`, `C:\Users\...\AppData\Local\Temp` 포함)에는 임시 파일을 만들지 않는다.
   - 근거 (2026-10-05 실측): C++ 조사 프로브와 격리 복사물이 C: TEMP에 누적되어 OS 디스크 압박의 한 원인이 되었다.
     OpenCode 세션 DB(`~/.local/share/opencode/opencode.db`)가 대량 도구 출력을 통째로 저장해 수십 GB로 불어난 것도 같은 날 확인됨.
     OS 디스크와 작업 스크래치는 물리적으로 분리한다. `D:` 여유 공간을 작업 전에 확인한다.
   - bash `workdir` 인자 등 작업 경로 지정이 필요하면 `D:\Temp\OpenCodeWork\<목적>` 형태를 쓴다.
   - 임시물 정리 시에도 10항(휴지통 이동)을 그대로 적용한다.

## 문서 네이밍 및 구조

- 정식 명명/위치 규칙은 `docs/document-naming.ko.md` + `.en.md`를 따른다.
- Work Log는 `docs/worklog/<development-line>.ko.md` + `.en.md`로 개발선 단위 누적 관리하며, 버전 범위를 파일명에 넣지 않는다.
- Build History는 `docs/build-history/<version>.ko.md` + `.en.md`를 유지하고 파일명을 바꾸지 않는다.
- Implementation Brief는 `docs/implementation-briefs/<Node>-<topic>.ko.md` + `.en.md` 규칙을 따른다.
- 문서를 이동/이름 변경할 때는 내부 링크, `STRUCTURE.md`, `llms.txt`를 같은 변경에서 갱신한다.
- 문서-only 정리는 제품 버전을 올리지 않고 별도의 `docs:` 커밋으로 분리한다.
