# MediaSimilarityFinder 소스 구조

> 실제 소스는 src_unpacked/에 있다. 저장소 루트에는 매 빌드 산출물
> (소스 zip, 포터블 zip, 바이너리 zip)이 쌓일 수 있다. 모든 작업은 src_unpacked/ 기준으로 수행한다.

## 최상위

- vcpkg.json — 의존성 선언의 유일 기준(project-local vcpkg_installed).
- CMakeLists.txt — 빌드/테스트 정의, 버전(project(... VERSION ...))은 여기서 시작.
- CMakePresets.json — x64 MSVC 프리셋.
- scripts/ — build_windows_cpu.ps1(CPU 빌드), build_windows_gpu.ps1(GPU 빌드, 0.9.4.x 목표),
  package_portable.ps1(포터블 패키징, 버전 문자열을 여기서도 상향).
- gui/ — Qt 위젯 앱. main.cpp(kVersion 포함), mainwindow.h/.cpp(한글 포함),
  video_decoder.h/.cpp.
- src/ — 엔진(순수 C++ 공통 계층). GPU는 공통 abstraction 아래 선택적 backend로 연결하며 현재 NVIDIA CUDA가 기준 구현.
- tests/ — CTest 테스트(msf_*_test 컨벤션).
- docs/ — development-roadmap.ko/.en.md(전체 개발 방향), development-progress.ko/.en.md(현재 진척도), build-history/<버전>.ko/.en.md(실제 버전 증거), architecture/*.ko/.en.md, legacy/(교체 구현 스냅샷),
  STRUCTURE.md(이 파일), llms.txt(LLM용 텍스트 인덱스).
- docs/development-roadmap.ko/.en.md — A→B→C 개발 순서도와 recovery branch 규칙.
- docs/development-progress.ko/.en.md — 현재 node/version/substep/blocker/validation 상태.
- docs/architecture/resource-scheduling.ko/.en.md — CPU 정책 + Adaptive GPU AUTO + INI calibration + 실시간 부하 기반 자원관리 목표 아키텍처.
- docs/architecture/benchmark-telemetry-roadmap.ko/.en.md — benchmark/telemetry 상세 설계.

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
- 현재 CMakeLists.txt에는 GPU 64개 / CPU 63개 CTest가 등록되어 있다.
- MediaSimilarityFinder.exe --smoke(offscreen), --version — GUI 스모크/버전 확인.
- 버전 상향 파일(검색용): CMakeLists.txt, vcpkg.json, gui/main.cpp, scripts/package_portable.ps1, src/index_manager.cpp.

## 문서 갱신 규칙(AGENTS.md)

- 빌드마다 docs/build-history/<버전>.ko.md + .en.md 쌍, README.ko/.en.md 버전표.
- 변경은 "변경 필요성 → 기존 구조 → 변경 구조 → 해결된 상황 → 검증 → 향후 영향" 형식.
- GPU backend별 알고리즘은 독립적으로 유지하며 CPU fallback을 보존한다. 상위 engine에 vendor-specific GPU API를 직접 확산하지 않는다.
