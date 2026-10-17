# MediaSimilarityFinder

## 현재 개발 버전: 0.9.4.120 (엔진 1.5.1, DB 1.0.6, 공식 기준선 0.9.2.32, 개발선 0.9.4, 프로세스 분리 P1–P4 완료, 매칭 검증 모드 선택 + B 1000 슬라이스, EXIF 회전 이미지 WIC FlipRotator 폭주 제거, 인덱스 우선 표시, 라이브 "인덱스 완료" 카운터 수정, 엔진버전 재검증 진행률 표시, dataset fingerprint 캐시, 백엔드 셸 썸네일 경로 제거, 통합 CPU 표기, 증분 dataset fingerprint, 그룹 정렬 없음 + 스캔 중 정렬 잠금, 그룹 정렬 없음 최상단/기본값 + append-only 목록, 쌍검증 DBV-1/DBV-2(캐시키 읽기 제거 + 캐시 확대), 불러오기(인덱스 로드 전용) + 팝업 분리, 완료 경로 중복 제거 + 스크롤 점프 수정, 요약 패널 쌍검증 표기 + 안정형 재스캔 측정 기록, 자원 정책 UI 개편(Minimum 개명 + Custom 삭제 + 설정 이전), 설정 GPU 개명 + 불러오기 썸네일 수정, 테스트 버튼 표시 설정, 종류 드롭다운 + 대표 제외 마크 + 상세정보 소실 수정, 상세정보 원격 진단 + 파일명 견고화, 열기 경로 상세 동기화, 상세 폼 동기 활성화, 체크 우선 파일 액션 + 파일바 구분선, 휴지통 일괄 처리 + 캐시 일괄 삭제 + 단계 계측, 미리보기 소실 근본 수정 + 상세정보 핑퐁 수정 + 인덱스 즉시 반영 + 파일 I/O 백그라운드화, 100.0% 의미 표시(동일 내용/지각 동일 배지), A/AB 타깃 열거(변경 파일 한정 후보 탐색), 상세 로그 요약 헤더(검색 방법+검색 폴더), 보기 모드 전환 겹침 재발 수정(웜 캐시 아이콘 재적용), 보기 전환 후 휠 속도 복원(List 단위 잠금 수정), G4 IPC backpressure + G6 Job Object/GUI 종료 검증(프로세스 분리 release gate), S4 상세 로그 저장/종료 acceptance(저장 단위 분리 + 실버튼 E2E), 독립 감사 B1~B3·B7 반영(동일 내용 배지 전체 내용 확인 + 분석 모드 round-trip + no-FFmpeg SKIP), 핫픽스(불러오기/검색 시작 백엔드 크래시 — loadMatches 참조 무효화 UAF), 핫픽스 2(62097 킬 루프 — 이벤트 스레드 차단 + 잔존 킬 타이머), 삭제 파일 유령 그룹 수정 + GUI 자동 정리(잔존 인덱스 행), GUI 대량 결과 적용 정지 제거(지연·청크 채우기 + 메타 요청 폭풍 수정), 스캔 목록 churn 제거(append 우선 재사용 + 대형 append 지연), 처리량 표기 의미 명확화(분석 단계 라벨 + 전체/신규 요율), GUI-less 구성 복구 + 배지 비용 판정 기록 + 제품 acceptance 런북, S4 verification in progress, S5 product benchmark DEFERRED, F-1 CONDITIONAL)

Windows 11 x64 미디어 중복/유사 검색 엔진. CPU + GPU 공동 실행 구조로 개발 중이며 현재 concrete GPU backend는 NVIDIA CUDA입니다.

### 현재 기능
- 프로그램 소유 Index 저장소를 쓰는 증분 SQLite 인덱스.
- 이미지/비디오 지문 및 CandidateIndex 가속.
- 고속 CPU pHash(캐시된 코사인 테이블, 분리형 DCT, 1회 DCT로 normal+mirror 동시 산출)와 NVIDIA CUDA 백엔드 및 CPU 폴백. CUDA 커널이 동일한 1e-7 근사 0 스냅을 적용해 CPU와 GPU가 비트 단위로 일치.
- 전경 작업 보호 기능이 있는 상주 실시간 폴더 모니터.
- 좌우 반전 이미지/비디오 유사도 검출.
- 영속 비디오 지문 캐시(v9: 기본 프레임 + 프레임별 crop 해시, 64항목 메모리 LRU).
- 중앙 crop(4:3, 1:1, 9:16) 변환 대응 2차 매칭(미러 변형 포함)과 SSIM 그레이존 검증.
- 병렬 검증, 다이아딕 샘플링 격자, 비교 시 격자 솎기를 적용한 비디오 temporal 2차 crop 비교.
- 일반 D<=8 구간의 4분할 16비트 multi-index + 반경 2 열거를 쓰는 대용량 exact CandidateIndex 가속.
- 스트리밍 매치 전달(전체 결과 유지 선택, 리포트 측 매치 수 상한 설정 가능).
- 검색 벤치마크 로그: 단계별 시간, 파일별 디코드/해시 비용, 재생분당·GB당 비디오 시간, 250ms CPU/메모리/GPU 듀티 샘플링을 완료·중단 시 단일 JSON으로 저장하고 요약 팝업 표시.
- 포터블 Windows 배포: 실행 파일 옆에 Qt 플랫폼 플러그인, FFmpeg 도구, 대응 VC++ 런타임 동봉.
- 탐색기에서 보기: 이미 열린 폴더 창을 우선 재사용하고 없으면 새로 열기. 그룹 기준 파일은 최고 유사도 우선, 동률은 해상도→용량 순.

### CPU + GPU 공동 실행 원칙

MediaSimilarityFinder는 CPU와 GPU를 함께 활용하는 프로그램이며, GPU가 있는 시스템이라고 해서 GPU만 사용하는 구조가 아니다. CPU는 항상 안정적인 기준/폴백 경로로 유지하고, GPU는 가능한 작업을 가속한다.

승인된 목표 자원관리 구조는 다음과 같다.

- Resource Mode: Maximum / High / Balanced / Minimum (0.9.4.96부터 Gaming→Minimum 개명, Custom 삭제).
- Maximum/High/Balanced/Minimum은 CPU 자원 정책을 결정한다 (Minimum 25/25, 사용자 직접 지정은 GUI에서 제거, 정책 계층은 유지).
- GPU는 ON/OFF만 사용한다 (설정 일반 탭의 GPU 가속 스위치).
- GPU ON 상태에서는 GPU 사용률을 사용자가 직접 지정하지 않고 Adaptive GPU Scheduler가 자동으로 작업량을 결정한다.
- CPU/GPU 작업 배분은 50:50으로 고정하지 않고 실제 처리능력, 현재 시스템 부하, queue 상태, 데이터 이동 비용을 반영해 동적으로 조정한다.
- 다른 프로그램이 CPU/GPU를 사용하면 MediaSimilarityFinder도 실시간 상태를 반영하여 작업량을 줄이거나 늘린다.
- 최초 실행/교정 시 측정한 하드웨어 성능 프로파일은 INI에 저장하여 다음 검색의 초기값으로 사용하되, 현재 실행의 실시간 부하가 우선한다.
- 저사양 GPU 또는 GPU 가속이 불리한 환경에서는 CPU 중심 또는 CPU-only 실행으로 자동 수렴할 수 있어야 한다.
- GPU video decode/NVDEC 역시 선택적 backend로 취급하고, 실패 시 Software FFmpeg 경로로 안전하게 폴백한다.

세부 설계와 구현 순서는 [CPU/GPU Adaptive Resource Scheduling](docs/architecture/resource-scheduling.ko.md) 및 [GPU Backend and Build Naming Roadmap](docs/architecture/gpu-backend-roadmap.ko.md)에 기록한다.

> 참고: 0.9.4.0은 Node A 기반(GPU 용어/추상화, GPU ON/OFF UI, 빌드 명명, 벤치마크 계측)을 구현했다. 내부 GPU cap은 Node B Adaptive Scheduler가 대체할 때까지 deprecated 상태로 유지하며, INI 성능 프로파일은 Node C에서 온다.

### 개발 넘버링
- 0.9.1.x: CPU 베이스라인
- 0.9.2.x: GPU 및 고급 검색 개발
- 0.9.3.x: 벤치마크·패키징·정확도 후속
- 0.9.4.x: Node A → B → C → D → I 분석/계측까지의 현재 개발선
- 1.0.0: CPU + GPU 완성 목표

빌드 기록은 docs/build-history/에 한글/영문으로 관리하며 현재 0.9.4.44 기준 CTest는 GPU 101개 / CPU 100개입니다. 아키텍처 문서는 docs/architecture/ 참조.


### Benchmark / Telemetry 방향

0.9.4.x에서는 Adaptive Scheduler와 함께 benchmark도 전면 개편한다. 총 시간뿐 아니라 CPU/GPU/decoder throughput, scheduler 판단, calibration, backend 선택, fallback, queue/transfer, video decode 병목을 기록하며, 측정되지 않은 값은 0으로 기록하지 않는다.

자세한 내용은 [Benchmark and Runtime Telemetry Roadmap](docs/architecture/benchmark-telemetry-roadmap.ko.md)을 참조한다.
