# 빌드 이력 / 한국어

MediaSimilarityFinder의 버전별 개발 이력과 중요한 설계 결정을 기록한다.

- [English](README.en.md)
- 일반 변경은 해당 버전 로그에 간단히 기록한다.
- 중요 설계 변경은 해당 버전 로그에 **변경 필요성 → 기존 구조 → 변경 구조 → 해결된 상황 → 검증 → 향후 영향**을 기록한다.
- 큰 설계 변경으로 교체된 이전 구현은 `docs/architecture/legacy/`에 비빌드용 snapshot으로 보존한다. 활성 소스에는 죽은 코드를 주석 처리해 남기지 않는다.

## 버전 목록

| 버전 | 요약 |
|---|---|
| 0.9.1.0 | CPU baseline |
| 0.9.2.0~0.9.2.3 | NVIDIA CUDA/GPU 기반 강화 |
| 0.9.2.4~0.9.2.7 | Portable Index / SQLite / CandidateIndex 기반 강화 |
| 0.9.2.8 | 실시간 Monitor 기반 |
| 0.9.2.9 | Windows ReadDirectoryChangesW 이벤트 감시 |
| 0.9.2.10 | 비교 Index 실시간 동기화 |
| 0.9.2.11 | 조건변수 지연 스케줄러 / exponential backoff |
| 0.9.2.12 | Resident CandidateIndex / root ownership / event resync / Windows path 안정화 |
| 0.9.2.13 | 사용자 작업 보호 Load Gate |
| 0.9.2.14 | MonitorStatus / telemetry |
| 0.9.2.15 | 수동 Pause/Resume |
| 0.9.2.16 | Monitor Settings GUI |
| 0.9.2.17 | Mirror-aware Similarity |
| 0.9.2.18 | Mirror CandidateIndex 최적화 |
| 0.9.2.19~0.9.2.20 | Image Crop-Aware 후보/비교 |
| 0.9.2.21~0.9.2.22 | Video Temporal Crop-Aware 후보/비교 통합 |
| 0.9.2.23~0.9.2.24 | Video decode/Monitor stability 최적화 |
| 0.9.2.25 | SQLite/Index 성능 및 migration 강화 |
| 0.9.2.26 | CandidateIndex 9-part Multi-Index Hash 전환 |
| 0.9.2.27 | Video fingerprint persistent cache 버전 관리/SQLite 재사용 강화 |
| 0.9.2.28 | CandidateIndex candidatePairs 대규모 후보 생성 최적화 |
| 0.9.2.29 | ScanPipeline streaming candidate consumption and complete-index short-circuit |
| 0.9.2.30 | Scan 결과 callback 전달 및 이중 match 저장 제거 |
| 0.9.2.31 | 대용량 Match 결과 streaming / report 보관 상한 |
| 0.9.2.32 | Compact index 기반 대용량 Match streaming callback |
| 0.9.2.33-test | Windows CPU 테스트 중간판 (기준선 아님, 0.9.2.34에 통합) |
| 0.9.2.34 | Windows 공통 호환성 반영판 (VS18 2026 기준 통일, CUDA 불변) |
| 0.9.2.35 | Windows CPU 빌드/테스트 하드닝 (36/36 PASS, GUI 런타임 검증) |
| 0.9.2.36 | 탐색기형 UI 재작성 (그리드+리스트 전환, 한영, mark, 실시간 스트리밍) |
| 0.9.2.37 | UI 피드백 반영 (설정 내 언어, 그룹 보기 6종, 키보드 mark, 스플리터 저장) |
| 0.9.2.38 | 3단 통일 UI + 비디오 탭/무시 목록/처리량 (37종 PASS) |
| 0.9.2.39 | 실행 화면 피드백 반영 (툴바 정리, 폴더 탭 삭제) |
| 0.9.2.40 | CUDA 실기 검증 (RTX 3080 Ti, 38종 PASS, 커널 불변) |
| 0.9.2.41 | 비ASCII(한글) 파일명 크래시 수정 + unicode_path_test (39종 PASS) |
| 0.9.2.42 | 검색 체감 개선 (워킹 표시, 버튼 통합, 상태 저장) |
| 0.9.2.43 | 중지 후 상태 복원 + 일시정지 스코프 |
| 0.9.2.44 | 대용량 스캔 live 검증 + 스트리밍 전환 |
| 0.9.2.45 | 스캔 활동 로그 (0% CPU 원인 규명용) |
| 0.9.2.46 | 스캔 파이프라이닝 (워킹‖분석) |
| 0.9.2.47 | 중단해도 저장되는 스캔 (체크포인트) |
| 0.9.2.48 | 콘솔 창 제거 + 스플리터 경고 수정 |
| 0.9.2.49 | 선택 검색 + 실시간 매칭 + 멀티코어 |
| 0.9.2.50 | 매치 영속화(빠른 불러오기) + 미리보기 WIC 폴백 |
| 0.9.2.51 | 동영상 장면전환 검색 + GPU 상태 가시화 + 캐시 v4 + duration 게이트 |
| 0.9.2.52 | 스캔 중 매치 점진 저장(이어하기 보장) |
| 0.9.2.53 | 미리보기 토글 크래시 수정 + 탐색기식 타일 보기 |
| 0.9.2.54 | 폴더 가로스크롤·XL 기본보기·UI상태 복원·프로그레스 스로틀·High 모드·멀티코어 개선 |
| 0.9.2.55 | 창 크기·배열 미복원 원인 수정(QSettings 무효) |
| 0.9.2.56 | 스캔 중 UI 프리징 수정(썸네일 디코드 예산) |
| 0.9.2.57 | 최종 분석단계 정지 불능 수정 + 최근폴더 즐겨찾기 |
| 0.9.2.58 | 빈 그룹창 수정(placeholder 아이콘 폭풍) |
| 0.9.2.59 | 빈 그룹창 재발 수정(리스트 재구축 라이브락) |
| 0.9.2.60 | 아이콘/리스트 보기 복구 + fav 중복 제거 + 컬러 미리보기 |
| 0.9.2.61 | 미리보기 크기 균일화 + 셸 레인 확대 |
| 0.9.2.62 | Similarity 참고 반영 (Pairs·남은시간·썸네일 skip-list) |
| 0.9.2.63 | 디스크 썸네일 캐시 + fav 중복·파일명·해상도·0B 수정 |
| 0.9.2.64 | 정지 직접 전달 + 비디오 진단 카운터 |
| 0.9.2.65 | 비디오 L1 멀티앵커 + variant dedup 수정 |
| 0.9.2.66 | e2e 비디오 회귀 + EXIF 회전 + 자잘 정리 |
| 0.9.2.67 | L3 SSIM 검증 셀 + thumb48 캐시 v5 |
| 0.9.2.68 | QuickLook 연동 재개 |
| 0.9.2.69 | 툴바 정리 + 중간결과 실시간 + 요약/카운트 수정 |
| 0.9.2.70 | 이미지 오탐 2차 검증 게이트 |
| 0.9.2.71 | 엔진 버전 스탬프 + 저장 매치 재검증 |
| 0.9.2.72 | 엔진/DB 버전 체계 분리 (semver) |
| 0.9.2.73 | 감사 수용분 (SSIM 게이트·QuickLook·문서) |
| 0.9.2.74 | 모니터 툴바 정리 (일시정지 삭제·토글 하이라이트·메뉴 우측) |
| 0.9.2.75 | 툴바·상세·요약 UI 라운드 |
| 0.9.2.76 | 콘솔 플래시 제거 + 해상도 무스폰화 |
| 0.9.2.77 | 버튼 양식 통일 + 상세/그리드 침범 수정 |
| 0.9.2.78 | 고정 사전 카운트·선택 필터 정합화 |
| 0.9.2.79 | 기존 탐색기 창 재사용 + GPU 상태 3종 표기 |
| 0.9.2.80 | 기준 파일 동률 해소(유사도 유지 + 해상도→용량) |
| 0.9.2.81 | 단일 트리 재검증 + GUI 테스트 플러그인 경로 |
| 0.9.2.82 | 포터블 실행 불가 수정(플러그인 배치+CRT 동봉) |
| 0.9.2.83 | 시작 실패 자가진단(플랫폼 플러그인 사전 점검) |
| 0.9.2.84 | 빌드 산출물 즉시 실행(Qt 플러그인 빌드 내장 배포) |
| 0.9.2.85 | 자가진단 경로 버그 수정(GetModuleFileNameW) |
| 0.9.2.86 | 고속 pHash·탐색기 폴백·ffprobe DLL |

각 버전의 상세 로그는 해당 버전의 `.ko.md` / `.en.md`를 참조한다.

## 설계 문서

- `../architecture/realtime-monitor.ko.md` / `.en.md`
- `../architecture/candidate-index.ko.md` / `.en.md`
- `../architecture/reference-similarity.ko.md` / `.en.md`
- `../architecture/runtime-audit-0.9.2.63.ko.md` / `.en.md`
- `../architecture/storage-design.md`

## Legacy

이전 구현/설계 snapshot은 `../architecture/legacy/`에 보존한다.
