# MediaSimilarityFinder

## 현재 개발 버전: 0.9.3.19 (엔진 1.5.0, DB 1.0.3, 공식 기준선 0.9.2.32)

Windows 11 x64 미디어 중복/유사 검색 엔진. CPU/CUDA 병행 개발 중.

### 현재 기능
- 프로그램 소유 Index 저장소를 쓰는 증분 SQLite 인덱스.
- 이미지/비디오 지문 및 CandidateIndex 가속.
- 고속 CPU pHash(캐시된 코사인 테이블, 분리형 DCT, 1회 DCT로 normal+mirror 동시 산출)와 NVIDIA CUDA 백엔드 및 CPU 폴백. CUDA 커널이 동일한 1e-7 근사 0 스냅을 적용해 CPU와 GPU가 비트 단위로 일치.
- 전경 작업 보호 기능이 있는 상주 실시간 폴더 모니터.
- 좌우 반전 이미지/비디오 유사도 검출.
- 영속 비디오 지문 캐시(v7: 기본 프레임 + 프레임별 crop 해시, 64항목 메모리 LRU).
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

- Resource Mode: Maximum / High / Balanced / Gaming / Manual 유지.
- Maximum/High/Balanced/Gaming은 CPU 자원 정책을 결정한다.
- Manual은 CPU 자원 한도를 사용자가 직접 지정한다.
- GPU는 ON/OFF만 사용한다.
- GPU ON 상태에서는 GPU 사용률을 사용자가 직접 지정하지 않고 Adaptive GPU Scheduler가 자동으로 작업량을 결정한다.
- CPU/GPU 작업 배분은 50:50으로 고정하지 않고 실제 처리능력, 현재 시스템 부하, queue 상태, 데이터 이동 비용을 반영해 동적으로 조정한다.
- 다른 프로그램이 CPU/GPU를 사용하면 MediaSimilarityFinder도 실시간 상태를 반영하여 작업량을 줄이거나 늘린다.
- 최초 실행/교정 시 측정한 하드웨어 성능 프로파일은 INI에 저장하여 다음 검색의 초기값으로 사용하되, 현재 실행의 실시간 부하가 우선한다.
- 저사양 GPU 또는 GPU 가속이 불리한 환경에서는 CPU 중심 또는 CPU-only 실행으로 자동 수렴할 수 있어야 한다.
- GPU video decode/NVDEC 역시 선택적 backend로 취급하고, 실패 시 Software FFmpeg 경로로 안전하게 폴백한다.

세부 설계와 구현 순서는 [CPU/GPU Adaptive Resource Scheduling](docs/architecture/resource-scheduling.ko.md) 및 [GPU Backend and Build Naming Roadmap](docs/architecture/gpu-backend-roadmap.ko.md)에 기록한다.

> 참고: 0.9.3.19 현재 코드에는 기존 CPU/GPU 고정 퍼센트 정책과 GPU 퍼센트 UI가 남아 있다. 위 Adaptive GPU AUTO와 INI 기반 성능 프로파일은 승인된 다음 단계의 목표 아키텍처이며, 이후 0.9.3.x에서 단계적으로 구현한다.

### 개발 넘버링
- 0.9.1.x: CPU 베이스라인
- 0.9.2.x: GPU 및 고급 검색 개발
- 0.9.3.x: 벤치마크·패키징·정확도 후속 (현재 마이너 라인)
- 1.0.0: CPU + GPU 완성 목표

빌드 기록은 docs/build-history/에 한글/영문으로 관리(CTest 59개). 아키텍처 문서는 docs/architecture/ 참조.
