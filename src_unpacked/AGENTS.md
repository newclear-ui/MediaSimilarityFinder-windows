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
3. 공식 기준선(0.9.2.32)과 작업 검증선(0.9.3.19, 마이너 라인 0.9.3.x) 구분 유지. 판정 의미 불변, additive CUDA 커널·호출 병합 허용, CPU fallback 유지

4. **다음 개발선 0.9.4.x**
   - 권장 첫 빌드 번호: 0.9.4.0
   - 사용자/상위 아키텍처 명칭은 CPU/GPU로 통일
   - GPU는 ON/OFF만 사용자에게 노출하며 GPU 사용률 수동 설정은 제거
   - Resource Mode는 Maximum/High/Balanced/Gaming/Manual 유지. CPU 정책은 기존 의미를 유지하고 GPU는 AUTO Adaptive Scheduler로 관리
   - CPU/GPU 작업 배분은 고정 50:50 금지. 하드웨어 capability + calibration + 실시간 부하 + throughput + queue + transfer cost를 기반으로 동적 조정
   - 성능 프로파일은 INI에 기록하여 다음 실행의 초기값으로 사용하되 live runtime state가 항상 우선
   - 상위 GPU abstraction은 vendor-neutral. NVIDIA CUDA, NVDEC, Vulkan, AMD HIP/ROCm, Intel Level Zero를 독립 backend 후보로 연결
   - Intel/AMD iGPU 및 dGPU는 동일 GPU abstraction으로 취급
   - 상위 계층에서 CUDA API를 직접 확산시키지 않으며 CPU fallback은 항상 유지
   - 빌드 이름은 build-windows-cpu / build-windows-gpu로 전환. build-windows-cuda는 0.9.4.x GPU build가 검증될 때까지 보존
   - CMake 상위 옵션은 MSF_ENABLE_GPU, backend 선택은 MSF_GPU_BACKEND 계열을 목표로 한다. 실제 구현 파일의 CUDA/Vulkan/HIP/Level Zero 이름은 기술명으로 유지
   - 0.9.4.0의 구조 변경 후 build-history를 ko/en 쌍으로 기록

 
5. **벤치마크 / Telemetry**
   - 0.9.4.x에서는 benchmark를 scheduler와 동급의 핵심 설계 계층으로 취급한다.
   - Adaptive Scheduler, calibration, backend 선택, fallback, queue, transfer, decoder 단계의 실제 근거를 benchmark가 기록해야 한다.
   - 측정되지 않은 값은 0으로 기록하지 않는다. measured / not_measured / not_available / partial / failed / fallback 상태를 구분한다.
   - benchmark JSON에는 독립적인 schemaVersion을 둔다.
   - decodedFrames와 sampledFrames는 반드시 분리한다.
   - CPU-only, GPU OFF, GPU ON/AUTO, low-end simulation, external CPU/GPU load, decoder success/fallback, cancellation/partial scan을 회귀 benchmark 시나리오로 유지한다.
   - human-readable 요약과 machine-readable 상세 JSON을 분리한다.
   - benchmark instrumentation은 검색의 정합성과 판정 결과를 변경하면 안 된다.
   - 상세 설계는 docs/architecture/benchmark-telemetry-roadmap.ko.md + .en.md를 기준으로 한다.
