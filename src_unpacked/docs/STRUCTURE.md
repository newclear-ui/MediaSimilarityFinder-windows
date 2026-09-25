# MediaSimilarityFinder 소스 구조

> 실제 소스는 `src_unpacked/`에 있다. 저장소 루트에는 매 빌드 산출물
> (소스 zip, 포터블 zip, 바이너리 zip)이 쌓일 수 있다. 모든 작업은 `src_unpacked/` 기준으로 수행한다.

## 최상위

- `vcpkg.json` — 의존성 선언의 유일 기준(project-local `vcpkg_installed`).
- `CMakeLists.txt` — 빌드/테스트 정의, 버전(`project(... VERSION ...)`)은 여기서 시작.
- `CMakePresets.json` — x64 MSVC 프리셋.
- `scripts/` — `build_windows_cuda.ps1`(CUDA 빌드), `build_windows_cpu.ps1`(CPU 빌드),
  `package_portable.ps1`(포터블 패키징, 버전 문자열을 여기서도 상향).
- `gui/` — Qt 위젯 앱. `main.cpp`(kVersion 포함), `mainwindow.h/.cpp`(한글 포함),
  `video_decoder.h/.cpp`.
- `src/` — 엔진(순수 C++ 공통 계층). CUDA는 컴파일 시간 활성화된 옵션 계층.
- `tests/` — CTest 테스트(`msf_*_test` 컨벤션).
- `docs/` — `build-history/<버전>.ko/.en.md`, `architecture/*.ko/.en.md`, `legacy/`(교체 구현 스냅샷),
  `STRUCTURE.md`(이 파일), `llms.txt`(LLM용 텍스트 인덱스).

## src/ 핵심 계층

| 파일 | 역할 |
| --- | --- |
| `fingerprint.*` | 지각 해시(pHash) 기반 64비트 지문 |
| `crop_fingerprint.*` | 4:3/1:1/9:16 크롭 지문(이미지·동영상 공통) |
| `image_decoder.*` | 이미지 디코드(+ GPU 해시 배치 통합) |
| `gpu_backend.*` | CUDA 백엔드(공통 계층에서 명시적 비활성화 가능, `GpuBackend::available()`로 검출) |
| `media_pipeline.*` | 이미지 분석 파이프라인(CPU/GPU 선택) |
| `video_sampling.*` | 동영상 샘플 플랜(`sampling_interval`) |
| `video_decoder.h` | FFmpeg(MSF_HAS_FFMPEG) 디코더 인터페이스 |
| `video_fingerprint.*` | 동영상 지문: timestamps+hashes+mirror, SQLite 캐시(v7, base+crop 프레임), 저분산 프레임 필터 + 장면전환, `video_similarity`(DTW, sceneBonus, 다이아딕 격자 정렬) |
| `similarity.*` | 해시 유사도 / 이미지 매칭 |
| `candidate_index.*` | 유사 후보 색인(9-part Multi-Index Hash) |
| `scan_pipeline.*` | 중복 검색 엔진: duration 게이트 + temporal 동영상 검증(스캔 경로) |
| `media_search_engine.*` | 스캔/증분/라이브 매칭 오케스트레이션, 체크포인트, GPU 카운터 |
| `database.*` | SQLite 파일 상태/지문, 영속 Match, GUI 썸네일 캐시(`thumbs`) 저장 |
| `index_manager.*` | 앱 데이터 인덱스(경로/버전 관리) |
| `monitor.*` | 실시간 감시(폴더 변경 → 매칭) |
| `resource_policy.*` | 자원 정책(CPU/GPU 퍼센트, 캡핑) |
| `scanner.*`, `incremental_*` | 파일 스캔/증분 스캔 |
| `sampling.*` | 동영상 샘플 타이밍 헬퍼 |

## GUI (gui/)

- `mainwindow.cpp` — 모든 위젯/트리스트/상태바.
- 미리보기 `fileThumb()` — 메모리 캐시 → persistent SQLite thumbnail cache → Windows
  `IThumbnailCache(WTS_INCACHEONLY)` fast lane → video는 VideoDecoder, image는 QImageReader/WIC 계열.
  디코드 실패는 `thumbFail_` skip-list로 기억한다.
- 해상도 표시 — video는 VideoDecoder metadata, image는 QImageReader header 우선, 실패 시 ffprobe header probe 폴백.
- 상태바: 진행률 + GPU 라벨(`gpuLbl_`).

## 빌드/테스트

- `scripts/build_windows_cuda.ps1 -VcpkgRoot C:\src\vcpkg` — CUDA 빌드.
- 현재 `CMakeLists.txt`에는 **58개 CTest**가 등록되어 있다.
- `MediaSimilarityFinder.exe --smoke` (offscreen), `--version` — GUI 스모크/버전 확인.
- 버전 상향 파일(검색용): `CMakeLists.txt`, `vcpkg.json`, `gui/main.cpp`, `scripts/package_portable.ps1`, `src/index_manager.cpp`.

## 문서 갱신 규칙(AGENTS.md)

- 빌드마다 `docs/build-history/<버전>.ko.md` + `.en.md` 쌍, `README.ko/.en.md` 버전표.
- 변경은 "변경 필요성 → 기존 구조 → 변경 구조 → 해결된 상황 → 검증 → 향후 영향" 형식.
- CUDA 알고리즘 불변: GPU 계층을 바꾸지 않고 공통 계층만 수정.
