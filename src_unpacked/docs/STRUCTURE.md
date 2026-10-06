# MediaSimilarityFinder 소스 구조

> 실제 소스는 src_unpacked/에 있다. 저장소 루트에는 매 빌드 산출물
> (소스 zip, 포터블 zip, 바이너리 zip)이 쌓일 수 있다. 모든 작업은 src_unpacked/ 기준으로 수행한다.

## 최상위

- vcpkg.json — 의존성 선언의 유일 기준(project-local vcpkg_installed).
- CMakeLists.txt — 빌드/테스트 정의, 버전(project(... VERSION ...))은 여기서 시작.
- CMakePresets.json — x64 MSVC 프리셋.
- scripts/ — build_windows_cpu.ps1(CPU 빌드), build_windows_gpu.ps1(GPU 빌드, 0.9.4.x 목표),
  package_portable.ps1(포터블 패키징 → 저장소 루트 backup/MediaSimilarityFinder-v<버전>-Portable-Windows-x64.zip,
  exe/DLL/ffmpeg 수집 후 smoke 통과 확인, 개수 3개 회전),
  backup_src.ps1(GitHub 동일 소스+문서 zip 백업, 최대 3개 회전).
- gui/ — Qt 위젯 앱. main.cpp(kVersion 포함), mainwindow.h/.cpp(한글 포함),
  video_decoder.h/.cpp.
- src/ — 엔진(순수 C++ 공통 계층). GPU는 공통 abstraction 아래 선택적 backend로 연결하며 현재 NVIDIA CUDA가 기준 구현.
- tests/ — CTest 테스트(msf_*_test 컨벤션).
- docs/ — development-roadmap.ko/.en.md(전체 개발 방향), development-progress.ko/.en.md(현재 진척도), implementation-briefs/(활성 Node 세부 구현 계약), build-history/<버전>.ko/.en.md(실제 버전 증거), architecture/*.ko/.en.md, legacy/(교체 구현 스냅샷), worklog/<개발선>.ko/.en.md(개발선 누적 작업 내역), document-naming.ko/.en.md(문서 네이밍 기준), STRUCTURE.md(이 파일), llms.txt(LLM용 텍스트 인덱스).
- docs/development-roadmap.ko/.en.md — A→B→C 개발 순서도와 recovery branch 규칙.
- docs/development-progress.ko/.en.md — 현재 node/version/substep/blocker/validation 상태.
- docs/document-naming.ko/.en.md — 문서 명명, 정식 위치, 역할 분리, rename 절차의 기준.
- docs/worklog/0.9.4.ko/.en.md — 0.9.4 개발선의 누적 작업 내역(측정 근거, 기각된 가설, 보류 판단, 학습한 방식). **Performance / Tuning Experiment Index** 를 포함하며, 실험 계보와 살아 있는/기각된 후보를 상태와 재검토 조건으로 연결한다. 기각된 후보는 삭제하지 않는다.
- docs/recent-work-report.ko/.en.md — 최근 중요 작업 3건의 결과 요약 보고(무엇을 했고 현재 상태가 어떤지). 상세 증거는 build-history/worklog에 있고, 이 문서는 한 곳에서 확인하기 위한 요약본이다.
- docs/node-status-gate-matrix.ko/.en.md — **Node별 현재 상태/선행조건/정체 원인/판정/다음 Gate를 한 장에 모은 통합 상태판(인덱스)**. 세션 시작 시 가장 먼저 읽는 진입점이며, 사실의 원본이 아니라 roadmap/progress/brief/architecture/build-history/worklog로 연결하는 색인이다. 진행률 %를 쓰지 않고 CLOSED/CONDITIONAL/DEFERRED/NOT ACCEPTED 같은 상태값과 Gate로 기록한다. 본선 A~H와 병렬 S 트랙(S0~S8)을 분리 표기한다.
- docs/architecture/resource-scheduling.ko/.en.md — CPU 정책 + Adaptive GPU AUTO + INI calibration + 실시간 부하 기반 자원관리 목표 아키텍처.
- docs/implementation-briefs/A-foundation-terminology-instrumentation.ko/.en.md — 종료된 Node A 의 참조 계약(GPU 추상화/ON-OFF/build 명칭/MeasureState/deprecatedFrames 분리). 원본 근거는 `docs/build-history/0.9.4.0.*`.
- docs/implementation-briefs/S-validation-benchmark-track.ko/.en.md — S0~S8 검증/벤치마크 트랙의 통합 계약. 단계별 상세(S4/S5/S6)는 기존 brief 를 유지하고 이 문서는 대체하지 않는다.
- docs/architecture/benchmark-telemetry-roadmap.ko/.en.md — benchmark/telemetry 상세 설계.
- docs/architecture/storage-design.ko.md / docs/architecture/storage-design.md — 인덱스 저장 위치(스캔 대상 밖 `Index/`), 트랜잭션/체크포인트 정책, 썸네일 캐시, GUI 상세 로그와 Console benchmark 저장소 분리. **KO/EN 각본을 서로 독립된 파일로 보존한다. 영문은 suffix 없는 `storage-design.md` 를 유지하며 개명하지 않는다.**

## src/ 핵심 계층

| 파일 | 역할 |
| --- | --- |
| fingerprint.* | 지각 해시(pHash) 기반 64비트 지문 |
| crop_fingerprint.* | 4:3/1:1/9:16 크롭 지문(이미지·동영상 공통) |
| image_decoder.* | 이미지 디코드(+ GPU 해시 배치 통합) |
| gpu_backend.* | vendor-neutral GPU 공통 계층(현재 NVIDIA CUDA backend 연결, 향후 Vulkan/HIP/Level Zero 확장점) |
| media_pipeline.* | 이미지 분석 파이프라인(CPU/GPU 선택) |
| video_sampling.* | 동영상 샘플 플랜(sampling_interval) |
| video_decoder.h | FFmpeg(MSF_HAS_FFMPEG) 디코더 인터페이스 |
| video_fingerprint.* | 동영상 지문: timestamps+hashes+mirror, SQLite 캐시(v9, base+crop 프레임), 단일 스윕 96 디코드+32 유도, 저분산 프레임 필터 + 장면전환, video_similarity(DTW, sceneBonus, 다이아딕 격자 정렬) |
| docs/architecture/video-gpu-hash.ko.md / .en.md | 영상 32x32 pHash CUDA batch 경로와 CPU fallback 설계 |
| docs/architecture/video-ssim-gpu.ko.md / .en.md | 영상 48x48 MSSIM CUDA row-batch 경로와 CPU fallback 설계 |
| docs/architecture/resource-scheduling.ko.md / .en.md | CPU/GPU 자원 정책, 실시간 Adaptive Scheduler, INI 성능 프로파일, 선택적 hardware video decode 목표 설계 |
| similarity.* | 해시 유사도 / 이미지 매칭 |
| candidate_index.* | 유사 후보 색인(4-part 16-bit Multi-Index Hash) |
| scan_pipeline.* | 중복 검색 엔진: duration 게이트 + temporal 동영상 검증(스캔 경로) |
| media_search_engine.* | 스캔/증분/라이브 매칭 오케스트레이션, 체크포인트, GPU 카운터 |
| database.* | SQLite 파일 상태/지문, 영속 Match, GUI 썸네일 캐시(thumbs) 저장 |
| index_manager.* | 앱 데이터 인덱스(경로/버전 관리) |
| monitor.* | 실시간 감시(폴더 변경 → 매칭), CPU/GPU 시스템 부하 확인 |
| resource_policy.* | CPU Resource Mode 정책과 Adaptive GPU Scheduler 연결 계층
| benchmark.* | CPU/GPU/decoder stage, scheduler decision, calibration, resource sampling, fallback 상태의 benchmark/telemetry 기록 |
| scanner.*, incremental_* | 파일 스캔/증분 스캔 |
| sampling.* | 동영상 샘플 타이밍 헬퍼 |

## GUI (gui/)

- mainwindow.cpp — 모든 위젯/트리스트/상태바.
- 미리보기 fileThumb() — 메모리 캐시 → persistent SQLite thumbnail cache → Windows
  IThumbnailCache(WTS_INCACHEONLY) fast lane → video는 VideoDecoder, image는 QImageReader/WIC 계열.
  디코드 실패는 thumbFail_ skip-list로 기억한다.
- 해상도 표시 — video는 VideoDecoder metadata, image는 QImageReader header 우선, 실패 시 ffprobe header probe 폴백.
- 상태바: 진행률 + GPU 라벨(gpuLbl_).

## 빌드/테스트

- scripts/build_windows_gpu.ps1 -VcpkgRoot C:\src\vcpkg — GPU 빌드 진입점(0.9.4.x 목표; 현재 backend는 NVIDIA CUDA).
- scripts/build_windows_cpu.ps1 -VcpkgRoot C:\src\vcpkg — CPU 빌드/CTest.
- scripts/prepare_dataset.ps1 -Root ..\test_sample_img_vid [-Scale small|full] — D8 표준 dataset 결정론적 생성기(binary 미커밋). 산출물 설명/기대 fingerprint: docs/test_sample_img_vid.md.
- scripts/backup_src.ps1 [-Keep 3] — GitHub과 동일한 소스+문서 zip 백업. `git archive`로만 생성하므로 워킹트리 편집이 섞일 수 없고, HEAD != origin/main 이거나 미커밋 tracked 변경이 있으면 refuse. 산출물은 저장소 루트 `backup/MediaSimilarityFinder-v<버전>-src.zip`. 기본 3개 보존이며 초과 시 가장 오래된 것을 휴지통으로 보낸다(AGENTS.md 4번).
- msf_dataset_report <root> — dataset fingerprint 출력 + <root>.fingerprint.json 기록(root 옆, root 안쪽 금지).
- msf_dataset_baseline <root> <app-dir> [runs] — 동일 dataset 반복 스캔, walker queue / GPU 내부 타이밍 / stage 분해 리포트. CTest 아님(의사결정 입력). D9c: verify 내부 비용 분해(decode/key/crop/flip/frame_ssim/other, exclusive 합계 = verifyMs) 도 함께 출력.
- 현재 CMakeLists.txt에는 111개 CTest 등록이 정의되어 있으며, 현재 구성된 CPU/GPU Release 트리의 전체 CTest는 각각 110/110, 111/111이다.
- MediaSimilarityFinder.exe --smoke(offscreen), --version — GUI 스모크/버전 확인.
- 버전 상향 파일(검색용): CMakeLists.txt, vcpkg.json, gui/main.cpp, scripts/package_portable.ps1, src/index_manager.cpp.
- Portable UI settings: `initAppSettings()` 가 organization `MediaSimilarityFinder-ui` + application `MediaSimilarityFinder` 로 INI 를 exe 옆에 기록한다. 0.9.4.24 이전의 `newclear-ui` 디렉터리는 첫 실행 시 1회 자동 migration 되며, 새 위치에 이미 파일이 있으면 덮어쓰지 않는다. QuickLook 레지스트리 조회는 `NativeFormat` + 명시 path 라서 이 identity 와 무관하다.

## 문서 갱신 규칙(AGENTS.md)

- 빌드마다 docs/build-history/<버전>.ko.md + .en.md 쌍, README.ko/.en.md 버전표.
- 변경은 "변경 필요성 → 기존 구조 → 변경 구조 → 해결된 상황 → 검증 → 향후 영향" 형식.
- GPU backend별 알고리즘은 독립적으로 유지하며 CPU fallback을 보존한다. 상위 engine에 vendor-specific GPU API를 직접 확산하지 않는다.


## 소스 편집 규칙 (UTF-8)

- **UTF-8 C++/문서 소스는 PowerShell 텍스트 왕복(`Get-Content` → `Set-Content`)
  으로 수정하지 않는다.** Windows PowerShell 5.1 의 `Get-Content` 는 BOM 이
  없는 파일을 시스템 ANSI 코드페이지로 읽으므로, UTF-8 한국어가 깨지고
  다시 UTF-8 로 쓰면서 원본과 다른 바이트가 된다. 2026-09-29 실제로
  `gui/mainwindow.cpp` 의 한국어 222 줄이 이 방식으로 손상되었다가
  `git restore` 후 바이트 보존 방식으로 복구된 사례가 있다.
- 안전한 방법: 저장소 편집 도구를 사용하거나, 바이트를 그대로 다루는
  재인코딩 없는 경로를 쓴다(예: ISO-8859-1 의 1:1 바이트 매핑으로 읽고 쓰기).
  읽고 쓴 뒤 반드시 원본 line ending 과 BOM 유무를 확인한다.
- 수정 후 `git diff --numstat` 로 변경 줄 수가 의도한 크기인지 확인하고,
  한글이 들어간 파일은 UTF-8 상태로 남았는지 확인한다.


## Active implementation briefs

- docs/implementation-briefs/B-adaptive-scheduler.ko.md / .en.md — Node B staged Scheduler implementation scope and gate.
- docs/implementation-briefs/C-calibration-profile.ko.md / .en.md — Node C profile/calibration reuse + extension scope.
- docs/implementation-briefs/D-pipeline-queue.ko.md / .en.md — Node D pipeline/queue optimization scope.
- docs/implementation-briefs/S4-gui-diagnostic-logging.ko.md / .en.md — current S4 GUI detailed-logging design.
- docs/implementation-briefs/S4-gui-benchmark-integration.ko.md / .en.md — historical/superseded GUI benchmark execution design, retained for provenance.
- docs/build-history/S4-phase3-4-verification.ko.md / .en.md — S4 Phase 3-1..3-4 regression record, real-engine GUI E2E results, and the two unresolved issues (Resource Policy not delivered to the benchmark, datasetFingerprint empty) that keep S4 from being CLOSED.
- docs/implementation-briefs/S5-console-benchmark-execution.ko.md / .en.md - S5 Console benchmark execution scope: --benchmark entry, Console renderer (fixed header, middle-ellipsis display only), Ctrl+C cancellation, and the five confirmed decisions (S2-observable progress, --log/--log-dir, --mode list, MSF_BUILD_GIT, auto suite id).
- docs/build-history/S5-verification.ko.md / .en.md - S5 verification record: S5-1/2/3 scope, the `Scanner::scan_stream()` FileState.kind defect and its one-line fix, the traced conclusion that production indexing/search is unaffected, real --media / mode / suite / log-dir / log / non-TTY E2E results, CPU 96/96 and GPU 97/97 CTest, the two NOT RUN items (real TTY ANSI repaint, real Ctrl+C trigger), and the unfixed S3 timestamp labelling mismatch.
- docs/implementation-briefs/S6-data-mining-automation.ko.md / .en.md - S6 design contract (no code yet). Verified journal field inventory, the measured 0-occurrence gaps for git/resourcePolicy/distance that block roadmap cross-build comparison, measured/derived/invalid split, journal reuse of replayJournal, deferred regression threshold, the four-way timestamp split workaround, S6-1..S6-7 internal decomposition, and the entry/exit conditions.
- docs/architecture/legacy/S5_REJECTED_CPU_FB_INDICATOR.ko.md / .en.md - REJECTED design record for the Console CPU FB column: never implemented in the product, with the absence evidence and revisit conditions.
- docs/implementation-briefs/S6-measurement-gate.ko.md / .en.md - S6 measurement gate (design only, no product code). Measured environment (Win11 Pro 26300 / Ryzen 7 5800X3D / RTX 3080 Ti present), measured dataset (3347 files / 102,475,315 bytes / zero video files), and the finding that the current 31-run store is a 10-file S5 e2e fixture. Reuses the already-measured 0.9.4.32 run-to-run variation (3.7-7.5% full run, 19.4-45.9% short probe) instead of inventing a new baseline. Fixes the A/B procedure (crossed A B B A order, ModeElapsed as primary metric, RunWallDuration excluded from primary because of its one-second resolution), the explicit warm-cache policy that does not claim OS cache control, and the exact entry/exit conditions for the S6 verdict layer.

Roadmap remains the high-level direction document; implementation briefs contain node-level engineering detail.
