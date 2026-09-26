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
4. 엔진/DB 버전 규칙 (빌드 번호 0.9.2.x와 분리, "M.m.p" 형식):
   - 검색엔진 판정 버전 `MediaSearchEngine::kEngineVersion`: patch=임계·가중·게이트 튜닝, minor=새 단계·규칙 추가, major=판정 아키텍처 교체. 상향 시 다음 스캔에서 저장 쌍 자동 재검증 (전체 재스캔 불필요)
   - DB 스키마 버전 `Database::kDatabaseVersion`: patch=부가적 추가(테이블·컬럼·인덱스, 구코드 읽기 가능), minor=마이그레이션 필요 변경, major=파괴적 변경(구행 무효, wipe+재스캔). open 시 마이그레이션 후 스탬프
   - 빌드 번호와 무관하게 필요할 때만 상향. 상향한 빌드는 build-history에 명시
3. 공식 기준선(0.9.2.32)과 작업 검증선(0.9.4.4, 개발선 0.9.4) 구분 유지. 판정 의미 불변, additive CUDA 커널·호출 병합 허용, CPU fallback 유지

4. **0.9.4 개발선 작업 규칙**

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

5. **벤치마크 / Telemetry**

- Benchmark / Telemetry는 모든 development node의 핵심 계층이다.
- 상세 설계는 `docs/architecture/benchmark-telemetry-roadmap.ko.md` + `.en.md`를 따른다.
- 측정되지 않은 값은 0으로 기록하지 않는다.
- measured / not_measured / not_available / partial / failed / fallback 상태를 구분한다.
- scheduler decision, calibration, backend, decoder, queue, transfer, fallback을 기록한다.
- decodedFrames와 sampledFrames를 분리한다.
- human-readable summary와 machine-readable JSON을 구분한다.
- instrumentation이 검색 정합성이나 결과를 변경해서는 안 된다.
- 실제 버전의 변경과 검증은 `docs/build-history/<version>.ko.md` + `.en.md`에 기록한다.
