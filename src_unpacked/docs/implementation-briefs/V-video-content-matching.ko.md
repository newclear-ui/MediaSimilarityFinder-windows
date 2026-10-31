# V-video-content-matching — 동영상 콘텐츠 중복 판정 구현 예정서

Status: **PROPOSED / PRE-REGISTERED / DEFERRED**  
Workstream: **V — Video Content Matching** (A→B→C→D→I→E→F→G→H 본선 밖 별도 제품 기능)  
Product baseline: **0.9.4.155** · Build version: **0.9.4.156 (V tab 1차: 설정+전파+간격/예산+캐시 분리; V2/V3/V4/V6 + V-gates pending)**  
Production code change: **NONE** — 구현 계약과 검증 Gate의 사전등록만 수행한다.

## 1. 목적

영상 길이와 구조가 다른 파일을 같은 간격으로 샘플링하고 모든 파일 쌍을 비교하는 방식을 개선한다. 목표는 (1) 10초 숏폼과 2시간 영화를 기본 전체 중복 후보에서 분리, (2) 길이별 소수 대표 프레임과 시간 구조 정보 사용, (3) 광고/인트로/크레디트 편집 차이를 흡수하되 본편의 고유 장면·시간 연속성을 중심으로 판정, (4) 애매한 후보만 추가 추출, (5) 보관 프레임과 실제 디코드 수 분리 계측, (6) 전체 영상 중복과 부분 클립 검색 분리, (7) 설정 창의 동영상 탭에서 자동 권장값과 숏폼/롱폼별 사용자 지정 관찰 간격·프레임 예산 제공이다.

CPU/GPU 디코더나 하드웨어 가속 선택은 이 예정서의 범위가 아니다.

## 2. 기준 코드와 제약

- src/video_sampling.cpp: duration만 입력. 간격은 <=10s:1s, <=60s:2s, <=240s:4s, <=960s:8s, <=3840s:16s, 그 이상 32s; 종료 지점은 필요 시 추가.
- src/video_decoder.cpp::VideoDecoder::framesAt(): 첫 target으로 backward seek 1회 후 마지막 target까지 순차 디코드.
- src/video_fingerprint.cpp: 저분산 필터, pHash, 좌우 반전, crop, scene-change, cache write 처리.
- VideoBuildStats: decodedFrames, sampledFrames, keptFrames 분리. 0.9.4.155에서 decodedFrames는 실제 decoder receive frame 수로 교정됐다.
- video_similarity(): 기존 DTW/scene-anchor 시간 비교를 후보로 재사용하되 새 판정 결과가 이미 검증된 것으로 간주하지 않는다.

130분(7,800초)의 현재 32초 계획은 약 245 timestamp를 요청하지만, sequential decode가 끝까지 실행되면 25fps 기준 산술상 약 195,000 frame을 출력할 수 있다. (245는 요청 위치 수, 약 195,000은 실측이 아닌 예측, 10~14는 새 설계의 kept-frame 목표다.)

### Node E 경계

Node E production sparse seek는 sequential baseline과 frame exactness가 깨져 ExactnessPolicy::RefuseAll로 종결됐다. V는 이를 재개하거나 우회하지 않는다. 별도 prototype 측정만 허용하며 production 채택은 별도 Gate와 명시적 승인 전까지 금지다.

## 3. 검색 경로·결과 분리

- Whole-video: 길이 cohort와 본편 구조가 호환되는 영상만 기본 후보로 만든다.
- Partial-clip: 짧은 영상이 장편의 임의 시간 구간에서 추출됐는지 찾는 별도 opt-in 경로/색인이다.
- Byte-identical file은 전체 파일 해시로 먼저 확인할 수 있다.
- 10초와 2시간 파일은 기본 whole-video comparison에 넣지 않는다.

결과: Byte-identical / Same full content / Edited full-content variant / Partial clip / Similar scenes only / Uncertain.

## 4. Duration class와 kept-frame 예산

| Class | 길이 | 정책 | 목표 |
|---|---:|---|---:|
| G1 | 15초 이하 | 전체 시간축, 장면 변화 중심 | 4~6장 |
| G2 | 15초 초과~60초 | 전체 구간, 5초 초기 Probe + 이벤트 보완 | 6~10장 |
| G3 | 1~3분 | 전체 시간축 6~8 관찰 구간 | 8~10장 |
| G4 | 3~10분 | 전체 시간축 6~8 관찰 구간 | 10~14장 |
| G5 | 10~25분 | 시작·중간·끝 분산 | 9~13장 |
| G6 | 25~60분 | 반복 인트로/요약/크레디트 감쇠, 본편 중심 | 9~12장 |
| G7 | 60~150분 | 시작 + 소수 중간 + 역방향 엔딩 | 10~14장 |
| G8 | 150분 초과 | 구조 복잡도에 따라 확장 | 12~16장 |

130분 영화는 오프닝 0~30초 약 6 Probe(1~2장 유지), 30초~3분 본편 경계(1~2장), 본편 초반 1장, 중간 2~3장, 본편 후반/엔딩 직전 1~3장을 목표로 한다. T-15초/30초/1/2/4/8/12/20분은 종료부 구조 Probe 예시이며 암전/크레디트는 interval metadata로 기록한다. 중심 목표 약 12장이다. 수치는 검증된 최적값이나 quota가 아니다.

## 5. 구현 예정 단계

### V0 — 데이터/정답/기준선

현행 production 결과를 baseline으로 보존한다. 길이·codec·resolution·fps·구조 속성으로 표본을 분류하고 byte-identical, re-encode, intro/ad/credit 수정, 서로 다른 회차, 좌우 반전, 빠른 컷, post-credit 케이스의 정답 쌍을 정의한다. 개인 영상은 로컬 전용이며 Git에 커밋하지 않는다. 공용 VCDB/VCSL/FiGVCL 계열은 사용 조건을 확인한 경우만 참고한다.

**Gate:** 장편과 반복 intro/credits가 포함된 실데이터 및 ground truth 확보 전 진행 금지.

### V1 — 메타데이터 후보 분리

Byte hash와 content similarity 경로를 분리한다. Whole-video는 동일/인접 cohort에서 후보를 만들고 extreme duration pair는 기본 경로에서 제외한다. Total-duration ratio는 후보 순위용이지 판정 임계값이 아니다. 본편 경계가 유효하면 core-duration ratio를 보조로 쓴다.

**Gate:** 10초 대 2시간이 기본 후보에 들어가지 않고 경계 cohort 양성 쌍이 검색됨.

### V2 — 구조 Probe (measurement-only)

순차 decode 중 밝기/dark ratio, 저해상도 색상·히스토그램 변화, 에지 밀도, scene cut/fade 후보를 관찰한다. OCR은 첫 단계 필수가 아니다. introInterval, mainContentInterval, creditsInterval, postCreditsInterval과 confidence를 scratch 기록으로 만든다.

**Gate:** 암전과 어두운 본편, 페이드, 빠른 컷, 크레디트, 쿠키 장면의 오경계율을 측정.

### V3 — 대표 프레임 선택

Probe와 Kept를 분리한다. 숏폼은 전체 시간축, 장편은 시작·종료부 집중 + 본편 중간 1~3개 anchor. normal + horizontal mirror만 사용하고 vertical flip은 구현하지 않는다. 동일 프레임 파생 특징을 독립 증거로 중복 집계하지 않는다.

**Gate:** class별 kept-frame 평균/p95, probe count 기록. 130분 약 10~14장 목표를 평가하되 accuracy를 희생해 quota를 맞추지 않는다.

### V4 — 시간 연속성 검증

timestamp 대응 단조 증가를 강제한다. affine offset/scale부터 확인하고 삽입/삭제가 있으면 piecewise monotonic alignment/DTW를 적용한다. 공통 logo/intro/recap/credits는 라이브러리 출현빈도에 따라 낮은 가중치. 장편은 opening+ending만 일치하는 후보를 거부하고 떨어진 본편 구간의 독립 근거를 요구한다. 애매하면 V3에서 해당 본편 구간만 추가 추출.

**Gate:** re-encode/intro/ad/credit 수정본 검출과 서로 다른 회차의 분리. Candidate recall과 final precision/recall을 분리 계측.

### V5 — GUI 동영상 설정 탭

V0~V4의 offline 정확도·비용 Gate를 통과한 정책을 사용자가 조정할 수 있도록 설정 대화상자에 **동영상 탭**을 추가한다. UI는 숏폼/중간 길이/롱폼 세 그룹으로 보여주되 내부 G1~G8 cohort와 구조 판정은 보존한다.

- 기본 모드: **자동(권장)**. 길이와 구조에 따라 기존 cohort 정책을 자동 적용한다. 처음부터 사용자가 영상 유형을 강제로 분류하게 하지 않는다.
- 사용자 지정 모드: 프로파일별 Probe 간격과 대표 프레임 목표/예산, 불확실 후보 추가 보강량을 편집한다. 대표 프레임 목표는 cohort별 제안 범위 안에서 정수로 선택하며, quota가 아닌 soft target로 취급한다.
- 숏폼(60초 이하): 전체 시간축의 프레임 확인 간격 **5초 권장**. 1~60초 범위에서 사용자 지정이 가능하도록 하며, 예를 들어 5초를 3초로 변경하면 숏폼 전체 시간축의 관찰 위치가 더 촘촘해진다. 대표 프레임 권장 중심값은 G1(15초 이하) **5장**(범위 4~6장), G2(15~60초) **8장**(범위 6~10장)이다. 실제 보관 수는 중복·저정보량 프레임을 제거하므로 목표보다 적을 수 있다.
- 중간 길이(60초 초과~25분): 자동 구조 기반 정책을 기본으로 한다. 대표 프레임 권장 중심값은 G3(1~3분) **9장**(8~10장), G4(3~10분) **12장**(10~14장), G5(10~25분) **11장**(9~13장)이다. 전체 구간을 무조건 동일 간격으로 샘플링하는 사용자 옵션은 기본 권장으로 두지 않는다.
- 롱폼(25분 초과): 구조 경계 상세 간격 **5초 권장**, 본편 정밀 보강 간격 **5초 권장**. 각각 자동으로 좁혀진 오프닝/본편 시작/종료 경계 창과, 후보가 불확실한 본편 국소 창 안에서만 적용한다. 영화 전체를 5초/3초마다 훑는 모드가 아니다. 5초→3초 변경은 선택된 창 안의 관찰을 촘촘하게 한다.
- 롱폼의 본편 anchor는 자동 1~3개 분산 구간을 유지한다. 대표 프레임 권장 중심값은 G6(25~60분) **10장**(9~12장), G7(60~150분) **12장**(10~14장), G8(150분 초과) **14장**(12~16장)이다. 따라서 130분 영화의 기본 목표는 12장이다. 애매한 후보는 식별력 있는 본편 구간에 1~2장씩 추가 보강한다.
- 부분 클립 검색은 별도 기능이며 기본 OFF를 유지한다.

사용자 지정 간격의 초기 UI 입력 범위는 1~60초 제안으로 둔다. 최종 상한과 파일당 Probe/Kept 예산은 실데이터 측정으로 확정하고 무제한 추출을 허용하지 않는다. 프레임 확인 간격은 Probe timestamp 간격이지 대표 프레임 수나 실제 decodedFrames 수가 아니다.

설정은 기존 사용자 설정 저장 계층에 저장한다. 검색 시작 시 설정 snapshot을 고정하고 실행 중 변경은 다음 검색부터 적용한다. fingerprint 생성 정책을 바꾸는 값은 cache reuse 적합성 검사에 반영해 서로 다른 정책의 지문이 혼합되지 않도록 한다. 실제 cache contract 확인 전 임의 DB/schema/cache migration을 하지 않는다.

**Gate:** 동영상 탭 표시, 자동(권장) 첫 실행값, 저장·재시작 round-trip, 5초→3초 값 적용 범위, 잘못된 입력 거부/상한, 실행 중 변경의 다음 검색 적용, cache stale/reuse 안전성, 숏폼·에피소드·영화별 candidate recall/precision 및 Probe/Kept/Decoded/시간 계측을 확인한다. UI는 interval 조정을 실제 디코드 프레임 감소로 표현하지 않는다.

### V6 — Partial-clip (별도 제안)

기본 whole-video 경로와 별도의 timeline index/query route. 10~14 대표 프레임만으로 장편 임의 위치의 숏폼을 모두 찾을 수 있다고 가정하지 않는다. source/query interval을 반환해야 하며 별도 사용자 옵션 전에는 기본 검색에 포함하지 않는다.

### V7 — Production 채택

V0~V4를 offline/shadow로 검증한다. 정답 데이터와 현행 결과를 대조해 정확도·비용 Gate를 통과한 부분만 production 제안한다. 판정 semantics가 바뀔 때만 engine version 검토, persistent payload가 필요할 때만 cache/DB migration 검토. GUI acceptance는 core algorithm 이후 별도 수행한다.

## 6. 필수 평가·계측

평가쌍: byte-identical, re-encode/resolution/fps 변경, horizontal mirror, leading ad/intro, credit 길이 변경, mid-roll, head/tail trim, 서로 다른 episode의 공통 intro, 공통 로고/음악만 같은 영상, fast montage, 장편에서 추출한 부분 숏폼(V6).

계측: duration/structural profile/boundary confidence, probeCount, requested samples, keptFrames, actual decodedFrames, seek/landing(실험만), decode·convert·fingerprint·compare·alignment time, candidate count, candidate recall, precision/recall, false positive/negative, core coverage, cache hit/bytes, kept-frame p95와 index size. 미측정은 0이 아니라 Not measured/Not available.

## 7. Production Gate와 중단 조건

- 극단적 길이 차이 쌍의 기본 whole-video 비교 분리
- 공통 intro/credits만으로 다른 episode를 중복 판정하지 않음
- 광고/인트로/크레디트 변화가 있어도 본편 독립 구간 일치 검출
- horizontal mirror 지원, vertical flip 미구현
- cohort별 candidate recall과 final precision/recall 통과
- 대표 프레임 및 비교 비용 감소와 허용 가능한 accuracy 유지
- decode 개선 주장은 decodedFrames와 end-to-end time의 실측 감소가 있을 때만 허용
- 취소/실패 시 partial fingerprint 오저장 없음
- CPU/GPU 및 fallback의 verdict 의미 일치

공통 intro/credits 오탐, 암전/어두운 본편 오분류, frame budget 축소로 candidate recall 저하가 발생하면 해당 cohort의 production 채택을 중단하고 기존 경로를 유지한다.

## 8. 금지·비목표

NVDEC/D3D11VA/Vulkan 구현, Node E sparse seek production 재도입, vertical flip, 모든 길이 조합 비교, partial clip을 기본 중복 그룹에 혼합, 근거 없는 engine/DB/cache version bump, 실측 전 최적 프레임 수/임계값 확정.

## 9. 참고 문서

- 권위 설계: architecture/video-similarity-search.{ko,en}.md
- Node E decoder planner: implementation-briefs/E-video-decode-planner.{ko,en}.md
- sparse exactness 거부 근거: implementation-briefs/E-adaptive-sampling-strategy.{ko,en}.md, build-history/0.9.4.42.*
- GPU hash/SSIM: architecture/video-gpu-hash.{ko,en}.md, architecture/video-ssim-gpu.{ko,en}.md
- 2026 IEEE Access PVCD review DOI 10.1109/ACCESS.2026.3707321; 2025 IEEE TPAMI FiGVCL DOI 10.1109/TPAMI.2025.3595625.

## 10. 현재 상태

이 문서는 예정서/사전등록이며 Active Build Queue에 넣지 않는다. GUI 동영상 탭의 V5 UI 1차(설정+전파+간격/예산+캐시 분리)는 0.9.4.156에 구현되었다. V0~V4 정확도·비용 Gate와 V2/V3/V4/V6 본 구현은 여전히 pending이다. XMP acceptance 및 S5 real-dataset benchmark를 대체하지 않는다. 빌드 버전·일정·production 채택은 미정이다. 지시 폴더의 별도 착수 지시에 따라 V5 UI 1차 범위에 한해 구현되었다. 범위 외 source/test/DB/engine 변경은 여전히 별도 지시가 필요하다.
