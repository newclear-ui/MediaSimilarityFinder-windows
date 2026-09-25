# CandidateIndex 아키텍처 및 설계 변경 기록

## 목적

CandidateIndex는 최종 유사도 판정을 수행하는 계층이 아니라, 64-bit perceptual hash의 Hamming distance를 이용해 **최종 SimilarityEngine이 검사해야 할 후보를 빠르게 줄이는 1차 후보 필터**다. 따라서 이 계층의 최우선 요구사항은 후보 누락이 없어야 하며, 그 다음이 대규모 인덱스에서의 검색/메모리 효율이다.

## 구조 발전

### 초기 구조 — 선형 검색

초기 CandidateIndex는 저장된 모든 hash를 순회하면서 Hamming distance를 계산했다. 구현은 단순하고 정확하지만 파일 수가 증가하면 후보 검색 비용이 선형으로 증가한다.

### BK-tree 도입

후속 단계에서 exact Hamming-distance BK-tree로 변경했다. BK-tree는 metric distance를 이용해 검색 범위를 가지치기할 수 있어 기존 선형 검색보다 후보 탐색량을 줄일 수 있었다.

단, 각 노드를 별도의 heap 객체로 만들고 `unique_ptr`와 노드별 `unordered_map`으로 자식 관계를 관리하는 구조였다. 작은 인덱스에서는 충분하지만 대규모 인덱스에서는 pointer/heap 분산, allocator 비용, cache locality 저하와 tree traversal 비용이 병목이 될 수 있다.

### 0.9.2.18 — transform-aware BK-tree

mirror-aware 검색 도입 후 별도의 mirror-only BK-tree를 두는 대신 normal/mirror fingerprint를 하나의 CandidateIndex에서 처리하도록 정리했다. 이 단계에서는 BK-tree 자체의 정확한 후보 의미는 유지했다.

### 현재 구현 — 4개 16-bit Multi-Index Hash partition

대규모 인덱스 확장을 위해 BK-tree의 저장/탐색 구조를 4개 16-bit Multi-Index Hash partition으로 교체했다.

64-bit hash를 4개의 16-bit partition으로 나누고 partition별 bucket을 만든다. `D <= 8`에서는 각 partition의 exact 값과 반경 1·2 값을 조회한다. 모든 partition이 3 bit 이상 다르면 총 거리가 최소 12가 되므로, 거리 8 이하의 pair는 반드시 하나 이상의 조회 bucket에 들어온다.

후보 bucket에서 수집한 entry는 중복 제거 후 전체 64-bit Hamming distance를 다시 계산한다. 즉 bucket은 **후보 축소용**, 최종 Hamming 계산은 **정확성 검증용**이다.

`D > 8`에서는 위의 pigeonhole 보장이 적용되지 않으므로 보수적으로 전체 hash를 정확히 검사하는 fallback을 사용한다.

## 설계 결정

1. **정확성 유지** — CandidateIndex는 근사 검색으로 바꾸지 않는다. 후보 필터 이후 full Hamming distance를 다시 검증한다.
2. **기본 검색 범위 최적화** — 현재 프로젝트의 일반적인 `D <= 8` 범위를 정확하게 가속한다.
3. **대규모 메모리 구조 단순화** — node별 heap allocation과 pointer-heavy tree 구조를 제거하고 entry 배열 + bucket index 구조를 사용한다.
4. **상위 계층 비변경** — SearchEngine/SimilarityEngine의 최종 similarity threshold 및 mirror/crop/video temporal semantics는 CandidateIndex 교체로 변경하지 않는다.
5. **보수적 fallback** — 최적화 범위를 벗어나는 `D > 8`은 속도보다 정확성을 우선한다.

## 0.9.2.26 검증

- Core Release build: PASS
- CTest: 35/35 PASS
- 6k benchmark: indexed query 약 64.45 ms
- 25k benchmark: indexed query 약 1.09 s
- 50k benchmark: indexed query 약 4.62 s
- 100k benchmark: indexed query 약 19.61 s
- 100k max RSS: 약 17.9 MB
- partition boundary를 가로지르는 8-bit 차이 regression PASS
- `D > 8` exact fallback regression PASS

주의: 위 benchmark의 indexed/linear 비교는 후보 검색 workload 정의가 완전히 동일한 1회 query 비교가 아니라 기존 benchmark의 전체 query workload 비교를 포함한다. 따라서 speedup 수치는 절대적인 알고리즘 배수로 해석하지 않고 버전 간 추세 판단용으로 사용한다.

## 비디오 fold 해시의 위치

`foldVideoHashes()`로 접은 영상 대표 해시는 저장·색인용 **식별자**이며 Hamming 유사도 키가 아니다.
유사도 판단은 per-frame 해시·anchor·temporal 검증이 담당하고, fold 간 Hamming 거리를 단독 근거로 쓰지 않는다.

## 향후 최적화 대상

- bucket 조회와 reserve 경로의 중복 작업 축소
- 후보 dedup 비용 최적화
- bucket-heavy/worst-case 데이터에서의 성능 검증
- 대규모 candidatePairs 생성 비용 검증
- concurrent query/add 정책이 필요해질 경우 thread-safety 명시


## 0.9.2.28 candidatePairs 최적화

0.9.2.26의 9-part Multi-Index Hash는 일반 `query()`의 후보 축소를 담당하고, 0.9.2.28에서는 전체 pair 생성 경로인 `candidatePairs()`를 별도로 최적화했다. 각 entry에서 partition bucket을 직접 순회하고 generation-stamp 배열로 후보 중복을 제거해, 매 query마다 후보 vector를 생성하고 정렬/unique하는 비용을 제거했다. `D=0`은 동일 hash의 특성상 partition 0만 사용한다. `D>8`은 exact fallback을 유지한다. 최종 Hamming 검증과 기존 결과 정렬 규칙은 유지된다.


## 0.9.2.29 streaming candidate consumption

0.9.2.29에서는 `CandidateIndex::forEachCandidatePair()`를 추가해 ScanPipeline이 후보쌍 전체를 vector로 materialize하지 않고 callback으로 즉시 소비한다. 여러 fingerprint variant가 동일 media index를 가리킬 수 있으므로 entry group을 내부적으로 유지하고 현재 source group에서 partner group을 generation-stamp로 deduplicate한다. full index가 해당 kind의 모든 가능한 pair를 이미 커버하면 crop index를 생략해 worst-case 중복 생성도 피한다. 기존 `candidatePairs()` API는 호환성을 위해 유지하며 callback 결과와 동일한 pair semantics를 사용한다.
