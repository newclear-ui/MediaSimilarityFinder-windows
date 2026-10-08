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
- v0.9.4.41 — **E-3A HEVC Seek Landing Characterization**. **판정 `PASS` / `HEVC_EXACT_SPARSE_SEEK = NOT VERIFIED` (exit 3) / `HEVC = SequentialPreferred` 최종 확정. production 코드 변경 없음.** **절차 준수**: brief 단독 커밋(`f7b7f3b`, 코드 없음) → probe 수정 → 측정. 여섯 개 전략(A `av_seek_frame` BACKWARD / B `avformat_seek_file` 3 window / C `AVSEEK_FLAG_ANY` / D `avformat_flush`)이 **전부 HEVC 1080p 에서 NOT EXACT** 이었고, 진짜 keyframe seek 인 A·D 는 E-2B 와 동일한 viol=2. **`AVSEEK_FLAG_ANY` 는 해결이 아니라 악화**(HEVC 2→14, H.264 270s 0→33) — 지시 §7 의 진단 전용 예선 정당. **`avformat_flush` 는 8개 파일 전부에서 A 와 동일** — 원인 아님. 유일한 안전 primitive 는 `av_seek_frame(..., BACKWARD)` 뿐. HEVC 1360x808 은 EXACT 라 "항상 불가"도 "항상 가능"도 아니다. B 의 좁은 window 와 C 는 **H.264 exactness 도 깨뜨려** 거부. **이것은 E 실패가 아니라 안전한 fallback 선택이다.** 고친 실제 결함 2건: ① **verdict 로직 버그** — 전 파일 합산으로 `VERIFIED` 잘못 보고, H.264 smoke 가 HEVC 실패를 가림 ② use-after-free 크래시(0xC0000005). planner 코드는 변경하지 않음. CPU CTest 83/83, GPU 84/84, selfcheck 39 checks(기존 29 유지 + 10 추가). 상세: `docs/build-history/0.9.4.41.ko.md`
 - v0.9.4.42 — **E-3B Adaptive Sampling Planner Calibration + End-to-End Validation**. **판정 `NOT ACCEPTED` / `SPARSE_SEEK_PRODUCTION_ADOPTION = NO` / `EXACTNESS = DISPROVEN` / production 기본값 Sequential 유지. production에서 sparse 경로가 제거되었다(도달 불가).** **절차 준수**: brief 단독 커밋(`b3ee35e`) → 구현 → 측정. **핵심 발견: E-2A/E-2B 의 "exact" 판정은 자기참조(self-referential)였다.** 두 실험 모두 seek 기반 구현을 **다른 seek 기반 구현과** 비교했고, 둘 다 `av_seek_frame` + `avcodec_flush_buffers` 를 호출하므로 **같은 decoder reference state 손실을 공유**하여 틀린 이유로 일치했다. production(from-zero sweep)을 포함한 **첫 측정이 E-3B** 였고, 여기서 4K H.264 1개가 **실제 불일치**를 보였다(`reference count overflow` / `no frame!` / `concealing` 로그). seek 경로가 from-zero sweep와 다른 frame을 재구성한다. 같은 실행에서 sparse 는 **end-to-end +16.77% 더 느림**(4K decode 지배) → **성능 논거도 소멸**. **정정 3건**: ① executor가 **truncated 결과를 성공으로 반환**(`!out32.empty()`) → sample-count contract 추가로 폐기 ② container-index GOP 을 `Known` 으로 보고(실측 index ≈109 vs decoded I-frame ≈120) → `Estimated` 로 하향 ③ 발췌된 `0.5 × framesPerSample` threshold 는 brief 금지사항이자 calibration 데이터와 모순 → 제거하고 `sampleCount × GOP/2 < totalFrames` 비교만 사용. **결과**: `ExactnessPolicy::RefuseAll` 기본값 도입 — production-parity 증명이 있는 codec 없으므로 전 파일 Sequential. `AllowVerified` 는 재검토용 1줄 변경. A/B/C **13/13 bit-identical**, adaptive **-0.02%**(중립). CPU CTest 84/84, selfcheck 31 checks. 상세: `docs/build-history/0.9.4.42.ko.md`

---

## Node E 현재 상태 (v0.9.4.42 이후 — 실험 기록과 구분)

위 v0.9.4.42 항목은 **그 시점의 실험 기록**이며 그대로 보존한다. 아래는 **현재 Node 상태**다.

- **Node E: 종결.** E-3B 를 근거로 종료되었다.
- **production sequential decode 가 production baseline**이다.
- **sparse production adoption 은 하지 않는다.**
- **`ExactnessPolicy::RefuseAll` 을 유지한다.** sparse production path 는 도달 불가.
- **E-4** 는 sparse production path 가 존재하지 않으므로 **별도의 sparse integration 단계로
  수행하지 않고 Node E 종결에 흡수한다.** E-4 를 sparse 재도입으로 해석하지 않는다.
- **E-3C** 는 별도 roadmap Stage 가 아니며 F 로 작업 항목 이관하지 않는다. Node E 종결
  범위에 포함된다. F architecture 참고 가능성은 참고사항으로만 유지한다.
- **추가 sparse adoption 은 새로운 production-parity 증거가 확보될 때까지 진행하지 않는다.**
- E-1-GOP / E-2B-4K 는 **실험 결과 `INCONCLUSIVE` 그대로 보존**하며, production adoption
  판단 대상이 종료되었으므로 `DEFERRED` 처리된다. 이 둘은 PASS 로 승격되지 않는다.

관련: `docs/development-roadmap.*` (Node E 완료/종결 조건), `docs/development-progress.*`,
`docs/build-history/0.9.4.42.*`, `docs/implementation-briefs/E-planner-calibration.*`,
`docs/implementation-briefs/F-hardware-video-decode-backend.*`
 - v0.9.4.43 — **F-1 Random-Access Safety Contract + NVDEC Exactness Preflight**. **판정 `CONDITIONAL` / `PRODUCTION ADOPTION = NO` / F-2 production integration 금지. Gate A PASS · B FAIL · C PASS · D PASS(측정) · E PASS.** **핵심 반전: "IDR로 시작하면 안전"은 거짓이었다.** dataset 14개 중 **13개가 file-start IDR**(key=1 at index 0)인데, 그중 **1360x0808가 20/20 mismatch**였고 **확인된 IDR에서 재시작해도(ss 2.0) 20/20 mismatch**였다. 1080x1920도 1/20 mismatch. 즉 **mid-GOP은 실패 모드 중 하나일 뿐**이고 NVDEC exactness는 4K에서도, IDR-start에서도 성립하지 않는다. → 계약에 **`exactnessVerified`를 `Safe`의 필수조건**으로 도입. **structure는 필요조건일 뿐 충분조건이 아니다.** `Unsafe`/`Unknown`은 CPU로 collapse하며 optimistic 분기가 없다. **1360x0808 특성화(INCIDENT-F1-1)**: 98.6 % bytes differ, maxAbs 185, nv12 정렬 비교에서도 잔존(→ format conversion 아님), **chroma가 luma보다 심함**, 높이 808이 **16 미정렬**(chroma 높이 404) — root cause는 **미확정(INCONCLUSIVE)**, parser 금지 조건상 SPS/PPS 직접 파싱 불가. **성능 preflight(확정 exact 조건 파일)**: CPU software 0.112 s/100f · **4.438 s**/870f vs NVDEC+transfer 0.337 s/100f · **9.397 s**/870f → **frame당 NVDEC이 2.1배 느림**(한계비 CPU 5.62 vs NVDEC 11.77 ms/frame). 고정비 story가 아니다. production은 ~12 frame만 샘플링하므로 실제 transfer는 더 작을 수 있으나, sparse decode 불가 → 전량 decode 구조에서 **성능 이득 근거 없음**. **추가 fixture 필요**: 4K IDR-start H.264 없음, 1360x808이 구조 문제인지 크기 문제인지 구분 불가 — 이번에는 생성하지 않음. 변경은 `tests/f_random_access_safety_test.cpp`(probe 전용, production 경로 미연결) + version bump뿐. CPU CTest 85/85, GPU CTest 86/86, F-1 selfcheck 20 checks. 상세: `docs/build-history/0.9.4.43.ko.md`
  - v0.9.4.44 — **S4 후속 GUI 정리 통합 회귀 기준선**. S4 semantic reset(`0a7955a`) + GUI benchmark plumbing 제거·전략 드롭다운·툴바 정리(`2db7aca`, `c0c5ad6`~`cf126c4`) + 상태 문서 정정(`028c5ef`, `cd62ea5`) 통합. 이번 버전의 source 변경은 version bump 1줄뿐. CPU CTest 100/100, GPU CTest 101/101, `--version` 0.9.4.44 확인, CPU/GPU `--smoke` exit=0. S4 verification in progress(CLOSED 아님), S5 product benchmark DEFERRED. 상세: `docs/build-history/0.9.4.44.ko.md`
  - v0.9.4.45 — **XMP Orientation Fallback production 경로**. 독립 계약(`I-xmp-orientation-fallback.*`), EXIF 계약 불변. EXIF VT_UI2 1..8 우선, 아니면 XMP `tiff:Orientation` 시도(실측 VT_LPWSTR). Fixture A–G 통과(14 checks). CPU CTest 102/102, GPU CTest 103/103. 표준 dataset XMP coverage 0(분리 기록). 상세: `docs/build-history/0.9.4.45.ko.md`
  - v0.9.4.45 (부수 수정) — **`--version` 이력 판정 오류 + CUDA C4819 제거**. `attachParentConsole()`의 `streamIsRedirected()`가 사용 불가 스트림(`fd<0`, 무효 핸들, `FILE_TYPE_UNKNOWN`)을 리다이렉션으로 오인해 PowerShell에서 `--version` 출력이 사라진 경로를 제거(`gui/main.cpp`). CUDA host compiler에 `/utf-8` 전달(`-Xcompiler=/utf-8`)해 C4819 0건. 버전 bump 없음. CPU CTest 102/102, GPU CTest 103/103. 상세: `docs/build-history/0.9.4.45.ko.md`
  - v0.9.4.46 — **DEFECT-A + DEFECT-B 수정 (분석 실패 상태 모델 도입)**. `fingerprint==0` 이 "분석 미완료(재시도 필요)" 와 "분석 실패(재시도 불필요)" 를 동시에 표현해, 디코드 실패 파일이 성공 인덱싱으로 보고되고 이후 스캔마다 `modified` 로 영구 반복되던 두 결함을 **하나의 상태 전이**로 수정. `files.analysis_failed` 컬럼 추가(DB 1.0.3 → 1.0.4), `added`/`modified` 를 분석 결과 시점으로 이동, 신규 `failed` 카운터, telemetry `pending` 정정. 회귀 테스트 `analysis_failure_state_test` 28 checks(CPU·GPU). `candidates`/`groups` 실측 불변으로 정상 Search/Index/Comparison semantics 보존 확인. engine 판정 버전 1.5.0 유지. CPU CTest 103/103, GPU CTest 104/104. 상세: `docs/build-history/0.9.4.46.ko.md`
  - v0.9.4.47 — **비ASCII 경로 스캔 실패 수정**. GUI "검색 업데이트" 클릭 시 `검색 오류` 다이얼로그(`No mapping for the Unicode character...`). 원인은 `dataset_fingerprint.cpp:118` 의 narrow `fs::path(string)` 생성으로, UTF-8 경로를 ACP 로 재해석해 비ASCII 이름에서 던졌다. 스캔 시작 단계에서 매번 호출되어 "1초 만에 파일 처리 없이" 실패했다. `path_from_utf8()` 로 교체하고 동일 패턴을 전수 조사해 다른 사례 없음을 확인. CLI 무출력 크래시(0xC0000409)와 GUI 다이얼로그는 같은 예외의 두 얼굴이었음. 기존 `unicode_path_test` 에 dataset fingerprint check 추가. engine/DB/schema/cache 버전 전부 불변. CPU CTest 103/103, GPU CTest 104/104. 상세: `docs/build-history/0.9.4.47.ko.md`
  - v0.9.4.48 — **GUI Stop 무응답 수정**. 정지 버튼은 flag를 정상 설정했으나 스캔 시작 단계의 dataset fingerprint가 모든 파일을 끝까지 읽으면서 cancellation을 보지 않아 대용량 dataset에서 Stop 후 10분 이상 I/O가 지속됨. fingerprint에 cancel 전달, 취소 시 state=cancelled로 즉시 반환. 신규 cancel_fingerprint_test 11 checks(CPU·GPU). engine/DB/schema/cache 전부 불변. CPU CTest 104/104, GPU CTest 105/105. 상세: docs/build-history/0.9.4.48.ko.md
  - v0.9.4.49 — **Detailed Logs 사용자 요약 추가**. 벤치마크 내부 계측은 많으나 일반 사용자용 요약이 없고 특히 중복 파일 총개수가 어디에도 없었음. SearchReport 10개 필드(이미지/비디오별 scanned/analyzed/pairs/groups/dupFiles), telemetry matches·images·videos 확장, GUI 대화창 최상단 요약 블록(Duration 12m 34.5s 형식, 시간 배분 Total/Image/Video/Matching). groups(클러스터 수)와 files(멤버 수)를 분리 표시. 번역 키 9개 추가·tr-keys gate 등록. 기존 matches 필드는 불변. 신규 summary_breakdown_test 21 checks(CPU·GPU). CPU CTest 105/105, GPU CTest 106/106. GUI 화면 렌더링은 NOT_VALIDATED. 상세: docs/build-history/0.9.4.49.ko.md
  - v0.9.4.50 — **S4 telemetry Phase-A 보강**. GUI 로그 감사에서 확인된 공백 6종: 종료 시각(finishedAt), 취소 위치(cancelledDuring: fingerprint/walk/analyze, cancel 후 전진 금지로 고정), fingerprint 소요·읽은량(durationMs/bytesRead), 합산 바이트(totalScannedBytes), series 절대 시각(wallTime, 빠른 스캔용 finalize 1회 보장 포함), 시스템 총 메모리(memSystemMB). 전부 additive, 기존 필드·의미 불변. 회귀는 기존 테스트 확장(cancel_fingerprint +2, summary_breakdown +8). CPU CTest 105/105, GPU CTest 106/106. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.50.ko.md
  - v0.9.4.51 — **fingerprint 진행 보고 + telemetry OFF 스킵**. 수 분간 전체 파일을 읽으면서 UI에 아무것도 보고하지 않던 공백 해소. ScanControl::fingerprintProgress 신설, GUI 별도 signal + 150ms 스로틀 + indeterminate 바 표시, 번역 키 fpProgress 등록. Detailed Logs 해제 시 fingerprint 미실행(not_available 유지)으로 I/O 절약. 회귀는 cancel_fingerprint_test 11→21 checks 확장. CPU CTest 105/105, GPU CTest 106/106. GUI 화면은 NOT_VALIDATED. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.51.ko.md
  - v0.9.4.52 — **이미지 처리 구간 내제 문제 2건 수정**. XMP VT_LPSTR 분기의 mbstowcs 로케일 의존을 명시적 CP_ACP 변환으로 교체. parallelFor catch 의도(워커 종료 방지)를 주석으로 고정. fingerprint 0 불가능함을 검증해 상태 모델 sound 확인. CPU CTest 105/105, GPU CTest 106/106. XMP 38 checks 직접 커버. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.52.ko.md
  - v0.9.4.53 — **좌측 요약 패널 3행 분리**. '검색 완료' 행이 r.scanned 표시면서 인덱싱처럼 읽히던 모호성 해소. 총 파일 / 읽기 완료(scanned) / 인덱스 완료(analyzed) 순서. 번역 키 readDone·indexDone 신설·gate 등록. 기존 scanned 키는 CANCELLED 경로 유지. CPU CTest 105/105, GPU CTest 106/106. GUI 화면 NOT_VALIDATED. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.53.ko.md
  - v0.9.4.54 — **읽기 카운터 실시간화 + 리포트 팝업 hardening**. fingerprint 중 패널 0 고정 해소(onFingerprintProgress가 sumValDone_ 갱신). 엔진 analyzedCount atomic 접근자 + live 인덱스 표시. updateStatusCounts 형식 통일. scanFinished try/catch로 팝업 close·idle 복귀 보장. CPU CTest 105/105, GPU CTest 106/106. GUI 화면·대규모 Stop NOT_VALIDATED. engine/DB/telemetry 전부 불변. 상세: docs/build-history/0.9.4.54.ko.md
  - v0.9.4.55 — **패널 카운터 monotonic + 취소 후 보존**. 읽기 완료가 fingerprint→walk 전환에서 0으로 리셋되던 문제(lastReadN_ max로 단조 유지). 정지 후 최종 0 표시 문제(무보고 시 live 값 보존). CPU CTest 105/105, GPU CTest 106/106. GUI 화면 NOT_VALIDATED. engine/DB/telemetry 전부 불변. 상세: docs/build-history/0.9.4.55.ko.md
  - v0.9.4.56 — **scanFinished hang 진단 추적**. Stop 후 리포트 팝업 미종료 보고에 대해 엔진 즉시 반환은 실측 확인만, 남은 미관측 구간인 scanFinished 진입/종료에 scanLog 2줄 추가(동작 변경 없음). 인덱스 없음은 fingerprint 취소 정상(우회로: Detailed Logs 해제 시 fingerprint 스킵). CPU CTest 105/105, GPU CTest 106/106. 양쪽 exe 0.9.4.56 선확인. engine/DB/telemetry 전부 불변. 상세: docs/build-history/0.9.4.56.ko.md
  - v0.9.4.57 — **fingerprint scope 인식**. 이미지 전용 스캔도 비디오까지 해시해 walk에 도달 못하던 문제. scope 전달로 in-scope 미디어만 해시. 무제한은 바이트 단위 동일. 신규 G 섹션 회귀. CPU CTest 105/105, GPU CTest 106/106. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.57.ko.md
  - v0.9.4.58 — **Stop 시 드레인 + 지문 실행 시점 재배치**. 취소 시 admitted 이미지 배치를 버리던 구조 수정. 최대 1배치 드레인 후 종료. 비디오는 unbounded라 drop 유지(문서화). telemetry 전용 전체 해시가 walk 앞을 막던 구조도 해소 — 지문은 match/group 이후 finishScan 직전으로 이동, 취소·실패·telemetry OFF면 생략. 신규 scan_cancel_drain_test 5 checks(CPU·GPU, 게이트 증명 확인). CPU CTest 106/106, GPU CTest 107/107. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.58.ko.md
  - v0.9.4.59 — **Video Cancelled/Failed 분리**. Stop이 failed·rollback으로 승격되던 구조 수정. `VideoRangeResult` 3상태 도입, stop 이후 완료분 persist, generation 스탬프 조건부 기록. 신규 scan_cancel_video_test 26 checks(CPU·GPU). drain 테스트 exact 강화. CPU CTest 107/107, GPU CTest 108/108. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.59.ko.md
  - v0.9.4.60 — **Generation scope 분리 + 명시적 terminal state**. 이미지 전용 스캔의 video generation 갱신 차단. `videoScopeSeen` 분리, 완료·취소 공통 invariant. `ScanTerminal` 명시 전달로 DB 실패 뒤 Stop도 Failed 보고. stamp 실패는 old 유지. 신규 scan_generation_scope_test 35 checks(CPU·GPU). CPU CTest 108/108, GPU CTest 109/109. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.60.ko.md
  - v0.9.4.61 — **유사그룹 Scroll 회귀 수정**. thumbStarved 단독 full rebuild 제거. in-place catch-up + scroll gate(slider/wheel/키 500ms). 0.9.3.10 anchor 유지. 신규 ui_scroll_regression_test 23 checks(CPU·GPU). CPU CTest 109/109, GPU CTest 110/110. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.61.ko.md
  - v0.9.4.62 — **크래시 대응 3종**. ScanWorker catch-all로 fail-fast를 기록된 실패로 격하. Qt 메시지 파일 싱크 + heartbeat 열거 단계 표시. 신규 crash_diagnostics_test 5 checks(CPU·GPU). CPU CTest 110/110, GPU CTest 111/111. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.62.ko.md
  - v0.9.4.63 — **GUI usability 3종**. 상세 로그 표시 설정(기본 ON). splitter 가운데 우선 + 저장 state 최소폭 보정. filename 선택 가능. Test Mode·traversal은 설계 검토 후 DEFERRED. 신규 ui_usability_test 13 checks(CPU·GPU). CPU CTest 111/111, GPU CTest 112/112. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.63.ko.md
  - v0.9.4.64 — **표시 옵션 설정 대화상자 통합**. 별도 표시 설정 대화상자 삭제, "상세 로그 표시"를 설정 일반 탭으로 이동. `ui/showDetailLog` 유지, round-trip 불변. 테스트 closer race 수정(스캔 구간 한정). 신규 테스트 없음. CPU CTest 111/111, GPU CTest 112/112. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.64.ko.md
  - v0.9.4.65 — **스캔 실패 핸들러 무throw 확정**. 세 번째 0xC0000409 덤프 분석: fault 스택이 catch 핸들러 persist 경로. 두 핸들러의 persist·emit을 독립 try/catch로 감싸 핸들러 밖 탈출 경로 제거. 신규 MSF_TEST_THROW_PERSIST seam. crash_diagnostics 7 checks(CPU·GPU). CPU CTest 111/111, GPU CTest 112/112. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.65.ko.md
  - v0.9.4.66 — **보기 모드 전환 후 이미지 겹침 수정**. 전환 후 thumb 도착 시 uniform+Batched가 layout pass를 실행하지 않아 작은 셀에 큰 그림이 그림(254x71에 256px). catch-up 변경 시 + 전환 끝에 doItemsLayout 1회씩. testThumbCatchUp hook. view_mode_probe 18시나리오 201 checks(CPU·GPU). CPU CTest 111/111, GPU CTest 112/112. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.66.ko.md
  - v0.9.4.67 — **P1 BackendCore 경계 + Backend 진입점**. msf_core가 이미 Qt 무의존 BackendCore임을 확인해 경계 선언. 신규 MediaSimilarityFinderBackend(Qt6::Core only, --help/--version, --backend는 P3 전 거부). portable에 동봉. GUI 동작 불변. 신규 backend 3종. CPU CTest 114/114, GPU CTest 115/115. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.67.ko.md
  - v0.9.4.68 — **P2 BackendClient + Loopback**. GUI의 worker/thread/monitor 직접 소유 제거, BackendClient 1개로 통합. matches pull→push, tick은 snapshot 캐시, thumb는 requestThumb. ScanWorker 무수정, GUI 테스트 전수 무수정 통과. 신규 테스트 없음. CPU CTest 114/114, GPU CTest 115/115. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.68.ko.md
  - v0.9.4.69 — **P3 실 spawn + Supervisor + IPC**. ScanWorker→src, BackendSession 공용, JSONL IPC, THUMBNAIL(JPEG), fileThumb 비동기화, thumbDb_/decode Backend 이동. E2E: PID 분리·kill→restart·FAILED·DB reopen 실측. backend_ipc 7 checks + backend_e2e. CPU CTest 116/116, GPU CTest 117/117. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.69.ko.md
  - v0.9.4.70 — **P4 hardening + final acceptance**. 잔여 decode(fileResolution/duration probe)를 FILE_META 요청으로 이전. GUI pixel decode 0. dumpbin Qt6Core-only 실측. §15 15항 전수 대조 + 숫자 확정. CPU CTest 116/116, GPU CTest 117/117. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.70.ko.md
  - v0.9.4.71 — **백엔드 결함 수정 + ThumbnailStore**. 2차 재검토 확정 7건 수정: ExecutionPolicy IPC 전달(Maximum 복구), Index Complete(analyzed+unchanged), 요약 CPU/RAM 단일 의미, 느린 파일 cacheHit/0ms 제외, allMatches_ 해제, ThumbnailStore(엔진 art·디스크 DB·decode 체인 → JPEG end-to-end), fileMeta in-place(선택 유지). 검증 중 Qt JPEG 플러그인 미배포 회귀 발견 → 공용 libjpeg-turbo 디코드로 교체. CPU CTest 116/116, GPU CTest 117/117. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.71.ko.md
  - v0.9.4.72 — **스캔 정체 수정(walk/analyze 병렬화) + 표시/ETR 결함**. 사용자 보고(<1% CPU, ETR 7000분+, 0% 정지)를 headless --scan으로 재현 → 두 단일 스레드 병목 확인. walk: Scanner::scan_stream의 파일별 64KB quick() 읽기를 bounded pool(≤8)로 병렬화(순서 보존, G: ~10.6→~55MB/s). analyze: ScanPipeline::analyze 후보 쌍 루프를 group 경계 샤딩으로 병렬화(순서 보존·판정 패리티, D9a 불변식 유지). 표시: supervisor kStatus가 Health CPU/RSS를 0으로 덮던 결함 수정 + ETR 초반 폭주 가드. CPU CTest 116/116, GPU CTest 117/117. engine/DB/schema/cache 전부 불변. 상세: docs/build-history/0.9.4.72.ko.md
  - v0.9.4.73 — **백엔드 크래시 방어 + PDB 활성화 + 스캔 자동 재개**. 사용자 보고(백엔드 비정상 종료 "backend process exited unexpectedly", 재시작 후 검색 미재개). 덤프 분석: 0xC0000409 FAST_FAIL_FATAL_APP_EXIT = std::terminate/abort, ucrtbase+0xA527E(런북 3-2와 동일 오프셋), walk 미완료 후 사망. Release에 PDB가 없어 심볼 불가였음. 수정: walker 스레드 try/catch+walkError(미처리 예외→기록된 실패), flushBatch std::async 실패 시 순차 fallback, CMake Release PDB(/Zi CXX 전용 + /DEBUG), 백엔드 재시작 후 스캔 1회 자동 재개(beginScan). CPU CTest 116/116, GPU CTest 117/117. engine/DB/schema/cache 전부 불변. 크래시 root cause는 미확정. 상세: docs/build-history/0.9.4.73.ko.md
  - v0.9.4.74 — **감사 통합 + telemetry 종료 크래시 수정 + 읽기 진행률 + 자동 재개 검증**. 서로 다른 감사를 C++ 코드·로그·덤프와 대조. `MSF_TEST_THROW_WALKER`로 0xC0000409 재현, 심볼 스택이 `TelemetryRecorder::~TelemetryRecorder`의 joinable sampler thread를 특정; RAII stop/join + ScanWorker catch의 abortTelemetry로 수정. `Scanner::count` 사전 열거 진행률 추가, `readProgress`를 consumer walked에서 분리해 producer queue admission을 표시, scan heartbeat stale listing reset. Supervisor restart 예약을 failure UI보다 먼저, MainWindow는 saved `BackendScanConfig`로 1회 자동 재개. CPU CTest 116/116, GPU 117/117. 기존 quickHash I/O 정책 및 IPC backpressure는 추가 측정 전 유지. 상세: docs/build-history/0.9.4.74.ko.md
