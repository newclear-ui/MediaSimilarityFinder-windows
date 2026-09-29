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
| 0.9.2.87 | CUDA 커널 1e-7 스냅(CPU·GPU 단색 일치) |
| 0.9.2.88 | 탐색기 보기 근본 수정(CLSCTX_ALL) |
| 0.9.2.89 | 우클릭 항목 선택(탐색기 보기 오동작 수정) |
| 0.9.2.90 | 외부 리포트 잔여 개선(샘플링·검증·색인·2차 단계) |
| 0.9.3.1 | 검색 벤치마크 로그(JSON, 0.9.2.91에서 번호 승격) |
| 0.9.3.2 | 벤치마크 토글(1.0 제거 예정) |
| 0.9.3.3 | 그룹 미리보기 실시간 채움 |
| 0.9.3.4 | 벤치마크 항상 기록·검색 로그 버튼·스크롤 개선 |
| 0.9.3.5 | 엔진 썸네일 재사용 + 스크롤 점프 수정 |
| 0.9.3.6 | 썸네일 DB 경합 제거 + 정체구간 fill |
| 0.9.3.7 | 엔진 조회 버짓 제외 + 비디오 단위 수정 |
| 0.9.3.8 | 검색 완료 후 thumbnail catch-up scheduler 수정 |
| 0.9.3.9 | 검색 신선도·영상 fingerprint·Benchmark·Monitor 안정성 개선 |
| 0.9.3.10 | 그룹 목록 스크롤 anchor 복원 |
| 0.9.3.11 | Video/Image/Thumbnail cache quick identity 검증 |
| 0.9.3.12 | 탐색기 재사용·캐시 freshness·그룹 identity 후속 수정 |
| 0.9.3.13 | 영상 pHash CUDA batch 가속 1단계 |
| 0.9.3.14 | 영상 MSSIM CUDA row-batch 가속 |
| 0.9.3.15 | 퇴화 해시 오판 수정·리포트 대기 UI·영상 GPU 활성 연결 |
| 0.9.3.16 | crop-only 판정 분리·정지 응답성·컬러 미리보기 |
| 0.9.3.17 | 영상 단일 스윕 디코드·재생시간 폴백 |
| 0.9.3.18 | Benchmark 디스크 I/O·재생시간 직접 폴백·팝업 가독성 |
| 0.9.3.19 | PDH 디스크 카운터 수정·통합 프롬프트 검토 반영 |
| 0.9.4.0 | Node A 기반: GPU 용어/추상화, GPU ON/OFF UI, 빌드 명명, 벤치마크 계측 |
| 0.9.4.1 | MainWindow 스캔 workflow 회귀 테스트 (offscreen 슬롯 경로 드라이버, 모달 closer, UI CPU/GPU parity) |
| 0.9.4.2 | Node B1 Minimal Adaptive Allocation (CpuGpuScheduler, 비례 배분, 재평가 주기, scheduler telemetry) |
| 0.9.4.3 | Node B2 Runtime Throughput Feedback (ThroughputWindow, 관측 비율 배분, effective 용량) |
| 0.9.4.4 | Node B3 Live Load Awareness (시스템 부하 headroom, 외부 부하 throttle, D1 예약 queue 필드) |
| 0.9.4.5 | Node B4 Stability Control (SMA 스무딩, kill-band hysteresis, minimum hold) |
| 0.9.4.6 | Node B5 Transfer / Workload Cost (total-cost 규칙, transfer 항, 관측 rate 내 workload) |
| 0.9.4.7 | Node B6 Resource Mode Integration (모드별 floor/hold/kill band, Manual == Balanced) |
| 0.9.4.8 | Execution binding + Node B7 게이트 (fresh 발표 읽기, 엔진 scheduler 단언, 커버리지 감사) |
| 0.9.4.9 | Node C1 Profile Foundation (PerformanceProfile, INI 저장소, 판정, initial-estimate 전달) |
| 0.9.4.10 | Node C2 Initial Calibration (bounded probe, writer, telemetry, 우선순위) |
| 0.9.4.11 | Node C3 Opportunistic Recalibration (deviation trigger, 후보 검사, confidence 스텝) |
| 0.9.4.12 | Node C4 Calibration Gate (17조합 증거, usability 헬퍼, 게이트 종료) |

각 버전의 상세 로그는 해당 버전의 `.ko.md` / `.en.md`를 참조한다.

## 설계 문서

- `../architecture/realtime-monitor.ko.md` / `.en.md`
- `../architecture/candidate-index.ko.md` / `.en.md`
- `../architecture/reference-similarity.ko.md` / `.en.md`
- `../architecture/runtime-audit-0.9.2.63.ko.md` / `.en.md`
- `../architecture/storage-design.md`

## Legacy

이전 구현/설계 snapshot은 `../architecture/legacy/`에 보존한다.

| 0.9.4.13 | Node C4.1 Calibration Lifecycle Fix — GPU OFF→ON metric-gap, 30-day stale policy, CPU/GPU identity completion — **GPT Fix** |
| 0.9.4.14 | C4.1 빌드 복구 (kDefaultMaxAgeDays 선언 위치, 테스트 msf:: 접두) |

- v0.9.4.15 — Node D1a Image-Path Observability (구현 완료, 검증 PASS는 0.9.4.16에서 확정)
- v0.9.4.16 — D1a 게이트: schema v3 복구, 버전 문자열 완성, 전체 스위트 검증
- v0.9.4.17 — D1b 게이트: walker queue·video range 관측, schema v4
- v0.9.4.18 — D2 barrier 검토: video completion-order join, maxRangeFileMs, schema v5
- v0.9.4.19 — D3-Minimal: bounded walker queue, backpressure, 취소/일시정지 안전, schema v6
- v0.9.4.20 — D4a: CUDA 백엔드 내부 타이밍(h2d/kernel/d2h device 분리 + host 대기), schema v7
- v0.9.4.21 — D8a: 재현 가능한 dataset + benchmark JSON dataset fingerprint, schema v8
- v0.9.4.22 — D8b: dataset 규모 확장(2700파일) + walker queue / stage 분해 증거, D4b·Full D3 근거 기반 보류
- v0.9.4.23 — D9a: analyze 내부 stage 분해 + verify/SSIM 카운터, schema v9. "캐시 용량 32" 가설은 측정으로 기각
- v0.9.4.24 — QSettings organization 을 `MediaSimilarityFinder-ui` 로 변경 + 기존 설정 안전한 1회 자동 마이그레이션
- v0.9.4.25 — D9b 후보 B: **기각.** centerCropResize 8→6 + aspect 버퍼 재사용. parity 25건 double 동일, 카운터 D9a 와 모두 동일하나 비용 감소 없음 (frame_ssim 이 지배)
- v0.9.4.26 — D9c: expensive verify 내부 비용 계측 (instrumentation). **PASS.** 8.564 ms 중 decode 94.90% / frame_ssim 0.55%. parity 유지, groups 156,152
- v0.9.4.27 — D1: open / factory 원인 계측. **PASS.** Factory2 25,898회 전부 성공(fallback 0), OS `CreateFileW` 0.0576 ms, open 의 97.8% 가 WIC 고유
- v0.9.4.28 — D2: WIC decoder 진입 경로 3종 비교 (measurement-only). **PASS.** 7개 형식 전부 `CreateDecoderFromStream` 이 decoder 단계 최단(A 대비 16~30% ↓). 단 제품 채택 아님 — **Path C = `DEFERRED`**. 구현 중 실제 버그 3건(handle lifetime / 잘못된 stream 연결 / COM lifetime) 발견 및 기록. dataset 에 TIFF 14 + ICO 12 추가 (3,347 files, `e8f8fa6a…e2640a`)
- v0.9.4.29 — D3: decode / decodePreserveAspect 중복 비용 계측 (measurement-only). **PASS.** verify miss 1건당 각 12,962회 호출, 두 번째 decode 가 verifyDecodeMs 의 **49.60 %** / verifyMs 39.19 % / analyzeMs 38.47 %. 단 `f`(고정 32×32)와 `a`(aspect 보존)는 서로 다른 지오메트리이고 둘 다 scoring 이 소비하므로, "두 번째 decode 제거" 는 정확성 회귀다. 후속 후보는 **단일 decode 로 두 벌 생성**. split identity `decodeSplitOverMs = 0.000000`
- v0.9.4.30 — CPU 사용량 10~90% 자동 정규화. 사용자 표시값과 `ResourcePolicy.cpuPercent`를 항상 일치시키며, 프리셋·GPU 정책·스케줄러·워커 계산식은 변경하지 않았다. CPU 80/80, GPU 81/81 PASS.
- v0.9.4.31 — D3 후속 공유 decode 후보 측정 (measurement-only). **측정 PASS.** R∈{128,192,256,384,512}에서 probe 후보 비용이 기준선 대비 23.8~45.9 % 낮게 관찰됐으나(제품 성능 아님), 바이트 재현 불가·R≥384에서 verdict flip 2건(TIFF 근사중복 쌍, 5회 재현) — **후보 `DEFERRED`**, 생산 미채택. (→ **0.9.4.32에서 정정**: 위 flip 2건과 "R 증가 → delta 증가(2.63→9.19)"는 probe의 baseline/candidate 버퍼 혼합에 의한 measurement artifact 로 무효. 원본 기록은 삭제하지 않는다.)
- v0.9.4.32 — D3 후속 후보 안정성·리샘플 차이 원인 조사. **조사 PASS / 후보 `DEFERRED` 유지 / production adoption NO.** pure baseline/candidate pairing 재측정 결과 조사한 8개 R(R128·192·256·288·320·352·384·512) 전부 verdict flip **0건**, max abs delta **1.972205~2.557407**(R 에 대해 단조 증가 아님). f/a pixel divergence 원인은 2단계 Fant 체인 + 중간 8-bit GrayImage 양자화 + 서로 다른 resampling chain 이며, scoring 단계의 `centerCropResize`(정수 nearest-neighbor)는 그 차이를 crop 으로 전달할 뿐 최초 원인이 아니다. DEFERRED 사유는 flip 이 아니라 byte parity 미유지·geometry 차이·pixel divergence·full-scan groups 미측정·EXIF/PGM fixture 미검증. verdict 비교는 해상도당 1691 sampled pairs 에서만 수행했고 전스캔 groups 비교는 수행하지 않았다. 정정 기록은 `docs/build-history/0.9.4.32.ko.md` §2.
- v0.9.4.33 — I-2 후보: **공유 WIC source/frame + 독립 2개 scaler** (measurement-only). **정확성 PASS / production 채택 NO.** 중간 `GrayImage` 를 만들지 않아 각 결과의 리샘플 체인이 baseline 과 동일해졌다. 전체 dataset **f geometry 849/849, f byte 849/849, a geometry 849/849, a byte 849/849** — geometry·pixel 불일치 **0건**(I-1 과의 결정적 차이). baseline/후보가 동일한 5개 파일에서 둘 다 실패(`WINCODEC_ERR_FRAMEMISSING`), **후보만 실패하는 파일 0건**. probe 기준 비용 감소 **40.8~42.6 %**(CPU 5회 + GPU 5회, 전 회전 parity 동일). `CopyPixels` 두 번째 호출이 평균 3.88→1.09 ms 로 감소 → WIC 가 실제 decode 를 공유함이 확인됨. **EXIF 종단간 검증은 `not_measured`**, full-scan groups 비교는 수행하지 않음. 상세: `docs/build-history/0.9.4.33.ko.md`
- v0.9.4.34 — I-2 검증 보완. **후보 = `READY FOR PRODUCTION IMPLEMENTATION REVIEW` / 반영 미수행.** ① 위 849 파일은 표준 dataset 이 아니라 **probe corpus** 였음을 정정하고 **표준 dataset 3,347 개 전체**로 재측정 — `both_success=3341`, `baseline_only_fail=0`, `candidate_only_fail=0`, `both_fail=6`, f/a geometry·pixel **3,341/3,341**. ② EXIF 진단 결함 2건 정정(`char*`→`LPCWSTR` 캐스트에 의한 허위 음성, 증가하지 않는 counter). 그 결과 **EXIF fixture 는 8/8 유효**했고 값도 1~8 로 일치. ③ **부수 발견: 제품 query path `/app1/ifd/exif/{ushort=274}` 는 WIC `BADPROPERTYKEY` 로 거부된다** — I-2 와 무관한 제품 결함이며 미수정. ④ **full-scan groups 전수 5,579,470쌍 verdict diff 0**, 점수 최대차 0.000000000. ⑤ 회전 강제 시 공유 source vs 완전 독립 pipeline **7/7 byte 동일**. ⑥ telemetry `metaMs`/`orientMs` 분리. 상세: `docs/build-history/0.9.4.34.ko.md`
- v0.9.4.35 — EXIF Orientation query path 결함 수정 (production). **Gate A/B/C/D 모두 PASS.** 제품이 쓰던 `/app1/ifd/exif/{ushort=274}` 는 WIC `BADPROPERTYKEY` 로 거부되어 회전이 한 번도 적용되지 않았다. 공유 헬퍼 1개로 3개 call site(고정 지문/aspect 지문/표시 color) 통합 후 `/app1/ifd/`(JPEG) → `/ifd/`(TIFF) 순서로 시도하고 값이 나오면 즉시 반환. 컨테이너 분기·XMP fallback·새 framework 없음. fixture 1~8 정상, EXIF parity f/a 8/8, **groups 전수 5,579,470쌍 결과 무변화**. dataset 의 EXIF 8건은 전부 orientation 1 → 회전 이미지 없음 확인(그 7건 TIFF 가 `/ifd/` 경로 동작 증명). 수정 비용 파일당 0.02870 ms, JPEG 는 미지불. **EXIF 회귀 테스트 selfcheck 11→16, CTest 80/80→81/81·81/81→82/82.** **I-2 = `READY FOR PRODUCTION IMPLEMENTATION`, 통합은 다음 단계.** 상세: `docs/build-history/0.9.4.35.ko.md`
- v0.9.4.36 — I-2 Shared WIC Source **production 통합**. **`PRODUCTION ADOPTION = YES`.** 중복 두 몸통을 `decodeWicBranches` 단일 source of truth 로 통합하고, 두 결과를 함께 요구하는 유일한 production 경로인 `verifyBuffersFor`(`image_verify.cpp:82-83`)를 신규 `decodeBoth()` 로 전환. factory·decoder·frame·metadata·orientation source·`GetSize` 가 파일당 2회 → 1회, **중간 `GrayImage` 없음**(기존과 동일한 1단계 Fant 체인). `decode()`/`decodePreserveAspect()` 는 시그니처·semantics 유지 → `media_pipeline`·`monitor` 단독 호출자는 무변경. 실패 시 기존 PGM fallback 을 그대로 호출해 실패 경로 byte 동일. telemetry 는 버킷을 하나씩 배분해 D3 불변식(`calls=1`, `aspectCalls=1`, 두 `totalMs` 합 = 호출 시간) 유지, benchmark JSON schema 무변경. **신규 `--production` 모드가 실제 production entry point 를 전수 비교**: `both_success=3341`, `base_only_fail=0`, `cand_only_fail=0`, `both_fail=6`, fixed/aspect geometry·pixel **3,341/3,341 diff_px=0**, telemetry 위반 0. **`--production-groups` 로 exhaustive sweep 의 candidate 를 production 경로로 교체**: pairs 5,579,470, groups 457,126 동일, **verdict diff 0, max score diff 0.000000000, group parity identical**. EXIF fixture 8/8, 회전 7/7 shared-vs-independent byte 동일. **CPU CTest 81/81, GPU 82/82, selfcheck 16 checks.** **production 측정 5회 전부 개선 — baseline 0.481800 ms → decodeBoth 0.304900 ms, 36.72 % 감소**(probe 수치가 아닌 실제 product 경로). 상세: `docs/build-history/0.9.4.36.ko.md`
- v0.9.4.37 — **I-3 Production Full-Scan End-to-End Validation** (measurement-only, production 코드 변경 없음). **`I-3 END-TO-END VALIDATION = PASS` / `I NODE = COMPLETE` / `I-1 = DEFERRED` / `I-2 = PRODUCTION` / `NEXT = E`.** 드라이버는 이미 존재하던 `msf_dataset_baseline` → `MediaSearchEngine::scan()`(GUI 와 동일 `scan_pipeline` + `image_verify` 경로). 필요한 telemetry 가 이미 전부 있어 **추가 telemetry 도 코드 변경도 없었음**. baseline 은 `git worktree` + 기존 `vcpkg_installed` 재사용으로 별도 빌드(작업 트리 미훼손, 2.2 GB 의존성 재빌드 없음). dataset 무변경(fingerprint `e8f8fa6a..e2640a`). **조건 A(cold process = decode-active, BCBCBCBCBC 교차)**: CPU 10 paired → full scan **-10.17 %**(9/10), GPU 5 paired → **-11.13 %**(5/5, 두 버전 범위 완전 비겹침), decode 단계 **-22.3~22.4 %**. **조건 B(warm/repeat scan)**: GPU 12행 -26.73 %(24/24 더 빠름)이나 동일 프로세스 2회째부터 양쪽 모두 30~60 % 느려지는 미해명 현상이 있어 headline 근거로 미사용. **정합성**: decode share 50.7 % × 22.3 % = 11.3 % ≈ 관측 11.13 %. **절감 해체: 28~30 %는 계측 전용 D1 reference probe 중복 제거**(product 아님), 진짜 이득은 WIC decoder 생성 25,924→12,962회, `copy`(실제 픽셀 decode)는 -95 ms 로 거의 불변. correctness 39회 실행 전부 `groups=156211`·`misses=12962`·`hits=14524` 동일, exactness 3,341/3,341 `diff_px=0`, EXIF 8/8, 전수 5,579,470쌍 무변화, CPU 81/81·GPU 82/82. 상세: `docs/build-history/0.9.4.37.ko.md`
- v0.9.4.38 — **E-1 Adaptive Video Decode Planner 사전 조사 / 측정 / 설계**. **Gate A~F 전부 PASS. production 코드 변경 없음.** 실코드 조사 결과: FFmpeg 9.0.1, 모든 FFmpeg 심볼이 `video_decoder.cpp` 182줄 한 곳에, hwaccel(dxva2/d3d11va/d3d12va)은 가용하나 hw 관련 심볼은 0개, **sampling 은 duration 만** 보는 6버킷 사다리, **seek 는 1회 후 순차 스윕**, **GOP/keyframe 인식 전무**, `VideoInfo::fps` 는 읽히지 않음, **telemetry 에 requested sample frames·seek count/latency·GOP cost·decode throughput 필드가 존재하지 않고** `videos.decodedFrames` 는 실측값이 아님. measurement-only probe 를 추가하고 **실제 `framesAt96Plus32` 출력과 8/8 byte 동일**함을 검증. **핵심 측정: 샘플 140개에 12,573프레임 디코드 = 89.81배 낭비**(300s 25fps 1건 192.3배), decode 가 sweep 의 **97.57 %**. **H1** 비율은 독립 변수가 아니라 `fps × interval(duration)`(오차 6.48 %). **H2** 프레임당 비용은 codec×resolution(동일 조합에서 fps 5배·duration 60배에도 19.2 % 편차, 640x360→1080p 는 픽셀 9.0배→비용 9.2배 선형). **planner input 18 후보 → 5개**(duration·fps·resolution·codec·GOP), 나머지 13개는 파생값·측정 불가·퇴화·추측 금지 근거와 함께 제외. **가장 중요한 재구성: 이 낭비는 sampling 전략 문제이지 hardware 문제가 아니며 E-2 는 hardware 없이도 가치가 있다.** 미해명 — GOP 측정 원본(ffv1 에서 `AV_PKT_FLAG_KEY` 부정확, 데이터점 2개), HEVC/AV1/VP9 는 번들 FFmpeg 에 software 인코더 부재로 생성 불가(제약으로 기록), sparse-seek 절감률 42~97 % 는 **산술이지 실측 아님**. CPU CTest 82/82, GPU 83/83, probe selfcheck 24 checks. 상세: `docs/build-history/0.9.4.38.ko.md`
- v0.9.4.39 — **E-2A Adaptive Sampling Strategy 실측** (measurement-only, production 코드 변경 없음). **판정 `CONDITIONAL` — 실측은 성공했으나 exactness 파괴가 확인되어 production 통합하지 않는다.** 실제 콘텐츠 8편(HEVC·4K·portrait·270.5초 장편 포함, `-c copy` trim) + 제어된 GOP 합성 5편(gop5/15/60/250 + intra-only) + **E-1 기록 정정 — HEVC/AV1 은 "생성 불가"가 아니라 실제 콘텐츠가 존재했으며, AV1 은 이 빌드에서 아예 **디코드 불가**(`Function not implemented`)라는 별개 제약을 발견**. **성능: sparse seek 가 대체로 대승**(decoded 17,164→6,203 = 2.77x, elapsed **-41.7 %**, 계측 오버헤드 0.2~0.4 % 분리 확인). 최선은 270.5초 실제 h264 **30.1s → 0.31s(99.0 %**, 낭비율 231.9x→1.40x). **그러나 역조건도 확인: GOP250 -136.8 %, 실제 GOP225 -132.2 % 로 sparse 가 baseline 보다 느리고 낭비율도 악화**(46.9x→109.6x). E-1 산술 모델 `GOP/2 < wasteRatio` 가 **13건 중 11건 일치**. **exactness: pixel parity 4/14 FAIL**(maxAbs 31~188/255 = 다른 프레임). 원인은 제품 predicate `ft+0.05>=target` 가 target **이전** 프레임을 허용하기 때문이며(30fps 에서 0.05s = 1.5 프레임), 순차 스윕은 한 프레임 앞선을 고르지만 sparse seek 는 seek 착지점 이전에 도달할 수 없다 — **전략의 구조적 차이지 decoder 결함이 아니다**. **GOP 는 이중 측정**(packet key flag + decoded `pict_type` I-frame)으로 Known 12 / Estimated 2 / Unavailable 0, 4K 파일에서 실제 불일치 11건 발견. CPU CTest 83/83, GPU 84/84, probe selfcheck 20 checks. 상세: `docs/build-history/0.9.4.39.ko.md`
- v0.9.4.40 — **E-2B Exact Sparse Seek + Adaptive Sampling Planner**. **판정 `CONDITIONAL` / `PRODUCTION ADOPTION = NO` / production default 는 Sequential 유지. production 코드 변경 없음.** 지시 §1 순서(exactness 먼저)를 지켰다. **핵심 해결: seek 목표를 `target` → `target - 0.05` 로 변경 — 착지 keyframe 이 `K' <= target-0.05` 를 만족하므로 제품이 고르는 프레임(`pts >= target-0.05`)이 항상 착지점 이후에 존재해 동일 predicate 가 동일 프레임을 고른다. `pixel parity 4/14 → 13/14`, tsLater 발생 파일 10 → 1. production predicate·tolerance·target 은 불변(selfcheck 가 0.05 검증). 정확성의 대가도 실측: sparse decoded 6,203→8,612(+38.8 %), 감소율 41.7 %→37.0 %. **잔여 실패 1건(HEVC 1080p, tsLater 2)** 은 지시 §8 의 seek 후 decoder state 검증이 필요조건(`firstDecodedPts <= seekRequestPts`)을 규명했고 **pixel parity 실패 파일과 동일한 파일**에서 위반 검출 — **탐지 성공, 해결 안 함**. **Adaptive Sampling Planner 구현**(분류만/실행만 분리, hard fallback 을 비용 비교보다 먼저 평가): SequentialPreferred 8 / SparseSeekCandidate 5 / SparseSeekUnavailable 1, **불리하거나 exactness 깨지는 7건을 7건 모두 회피**하고 이득 있는 8건 중 5건 포착·3건 보수적으로 놓침. AV1→Unavailable, 4K(GopEstimated)→Sequential, HEVC(landing 위반)→Sequential. 4K GOP mismatch 11 은 VFR+edit list 정황 있으나 미확정이라 새 parser 없이 `GopEstimated` 보수 처리. CPU CTest 83/83, GPU 84/84, probe selfcheck 29 checks(모든 mandatory fallback 자동화). 상세: `docs/build-history/0.9.4.40.ko.md`
