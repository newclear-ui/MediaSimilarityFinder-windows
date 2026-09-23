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

각 버전의 상세 로그는 해당 버전의 `.ko.md` / `.en.md`를 참조한다.

## 설계 문서

- `../architecture/realtime-monitor.ko.md` / `.en.md`
- `../architecture/candidate-index.ko.md` / `.en.md`
- `../architecture/storage-design.md`

## Legacy

이전 구현/설계 snapshot은 `../architecture/legacy/`에 보존한다.
