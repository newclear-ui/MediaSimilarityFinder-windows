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


## 0.9.4.13 fix trace
- **GPT Fix:** Node C4.1 calibration lifecycle correction. See docs/build-history/0.9.4.13.ko.md / .en.md.


## 문서 네이밍 및 구조

- 정식 명명/위치 규칙은 `docs/document-naming.ko.md` + `.en.md`를 따른다.
- Work Log는 `docs/worklog/<development-line>.ko.md` + `.en.md`로 개발선 단위 누적 관리하며, 버전 범위를 파일명에 넣지 않는다.
- Build History는 `docs/build-history/<version>.ko.md` + `.en.md`를 유지하고 파일명을 바꾸지 않는다.
- Implementation Brief는 `docs/implementation-briefs/<Node>-<topic>.ko.md` + `.en.md` 규칙을 따른다.
- 문서를 이동/이름 변경할 때는 내부 링크, `STRUCTURE.md`, `llms.txt`를 같은 변경에서 갱신한다.
- 문서-only 정리는 제품 버전을 올리지 않고 별도의 `docs:` 커밋으로 분리한다.
