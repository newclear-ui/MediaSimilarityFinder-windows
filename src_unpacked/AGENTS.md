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

- 상위 개발 방향은 `docs/development-roadmap.ko.md` / `.en.md`가 기준이다.
- 현재 실제 상태는 `docs/development-progress.ko.md` / `.en.md`가 기준이다.
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


## 문서 네이밍 및 구조

- 정식 명명/위치 규칙은 `docs/document-naming.ko.md` + `.en.md`를 따른다.
- Work Log는 `docs/worklog/<development-line>.ko.md` + `.en.md`로 개발선 단위 누적 관리하며, 버전 범위를 파일명에 넣지 않는다.
- Build History는 `docs/build-history/<version>.ko.md` + `.en.md`를 유지하고 파일명을 바꾸지 않는다.
- Implementation Brief는 `docs/implementation-briefs/<Node>-<topic>.ko.md` + `.en.md` 규칙을 따른다.
- 문서를 이동/이름 변경할 때는 내부 링크, `STRUCTURE.md`, `llms.txt`를 같은 변경에서 갱신한다.
- 문서-only 정리는 제품 버전을 올리지 않고 별도의 `docs:` 커밋으로 분리한다.

11. **벤치마크/실시간 모니터 자동화의 프로세스 안전 규칙**

- benchmark hot path, 실시간 monitor/telemetry, 자동 benchmark harness에서는 **짧은 외부 프로세스를 반복 spawn하는 구조를 금지한다.** 특히 sample 주기마다 `_popen`, `system`, 반복 `CreateProcess`, `ShellExecute` 등을 호출하여 child/console을 계속 만들고 닫는 패턴을 사용하지 않는다.
- 외부 프로그램이 꼭 필요하면 **in-process API를 우선**하고, 불가피하면 session/run 수명 동안 하나의 persistent helper를 재사용하는 방식을 우선 검토한다. 반복 GPU telemetry에서 `nvidia-smi`를 sample마다 새로 실행하는 구조는 금지한다. NVML 등 in-process API는 별도 설계 후보로 우선한다.
- Windows의 child process는 **console 없는 실행(`CREATE_NO_WINDOW` 또는 검증된 동등 방식)**과 stdout/stderr capture를 기본으로 한다. 이미 존재하는 `captureSilent` 같은 승인된 wrapper를 재사용하고 호출부마다 숨김 실행 방식을 다시 구현하지 않는다.
- 외부 process wrapper에는 **무한 대기(예: `WaitForSingleObject(INFINITE)`)를 정상 계약으로 두지 않는다.** timeout, 소유권, 정상 종료, 비정상 종료 시 child 정리와 부모 프로세스 보호를 설계한다. 장기 helper는 명확한 start/stop lifecycle을 가져야 한다.
- benchmark harness는 공통 실행 정책을 사용한다. `Start-Job`, 반복 `Start-Process`, 임의의 `cmd/powershell` 중첩 실행 등으로 child/console을 폭증시키지 않는다.
- 자동 chain에서 **unexpected console window, child process runaway, orphan process, 반복 비정상 종료**가 관찰되면 다음 run을 자동으로 계속하지 않는다. 원인 기록 후 chain을 중단하는 circuit-breaker를 우선한다.
- **측정 오염 여부와 PC 안정성은 별도 판정한다.** 오염된 run이 S6에서 제외되었다고 해서 반복 spawn 문제를 정상으로 간주하지 않는다.
- 새 Windows release gate에서는 source와 validation script의 raw process creation 경로를 점검한다. 제품 hot path의 반복 spawn은 승인된 예외가 아니면 통과시키지 않는다.
- 이번 규칙은 기존 benchmark/telemetry 정확성 원칙을 보완하는 안전성 규칙이며, 새로운 process API 구현 자체를 요구하는 것이 아니다. 필요성이 입증되었을 때만 별도 implementation brief로 제품 코드를 변경한다.

