# Database Structure (현재 DB 구조)

> 이 문서는 MediaSimilarityFinder의 **현재 영속 저장 구조(스키마/버전/공유 모델)** 를 기록한다.
> 버전별 변경 이력은 `docs/build-history/`, 실측 수치는 `docs/architecture/database-verification-benchmark.ko.md`에 둔다.
> 저장 위치·트랜잭션 정책의 상위 설명은 `docs/architecture/storage-design.ko.md`를 따른다.

## 1. 위치와 파일 구성

스캔 루트마다 앱 소유 인덱스 디렉터리가 하나 생긴다 (`src/index_manager.cpp`).

```text
<appDir>/Index/<folderId>/
├─ metadata.json          # rootPath, created, lastScan, schemaVersion, applicationVersion
├─ index.sqlite           # 주 인덱스 DB (이미지+비디오 공유)
├─ index.sqlite-wal / -shm (WAL 모드)
└─ video_cache.sqlite     # 비디오 지문 캐시 DB (비디오 전용, 분리)
```

- `folderId` = canonical 루트 경로(소문자, `\`→`/`, 말미 구분자 제거)에 대한 FNV-1a64 2회(seed 2개) → 32 hex.
- 기존 인덱스 재사용 전 `metadata.json`의 `rootPath`를 검증한다(해시 충돌 방지).
- 스캔 대상 폴더에는 인덱스 파일을 만들지 않는다.

## 2. index.sqlite — 주 인덱스 (이미지+비디오 공유)

`src/database.cpp`의 `initialize()`가 테이블을 생성한다.

### 2.1 `files` — 파일 1행(이미지/비디오 공통)

| 컬럼 | 타입 | 의미 |
|---|---|---|
| path | TEXT PK | canonical UTF-8 경로 |
| size | INTEGER | 파일 크기(bytes) |
| modified | INTEGER | mtime (Windows epoch ms) |
| quick_hash | TEXT | 앞 64 KiB FNV-1a 해시(변경 감지) |
| fingerprint | INTEGER | 완료된 지문(0 = skeleton, 미분석) |
| kind | INTEGER | 1=Image, 2=Video |
| duration | REAL | 비디오 길이(초) |
| mirror_fingerprint | INTEGER | 좌우반전 지문 |
| crop_4x3 / crop_1x1 / crop_9x16 | INTEGER | 크롭 지문 |
| mirror_crop_4x3 / mirror_crop_1x1 / mirror_crop_9x16 | INTEGER | 반전 크롭 지문 |
| analysis_failed | INTEGER | 분석 실패 확정 플래그 |

- 인덱스: `idx_files_modified(modified)`.
- `fingerprint=0`은 skeleton(수락됐으나 미분석)이며 매칭에 보이지 않는다. `analysis_failed`가 `false`(중단/미도달, 재시도)와 `true`(동일 내용 분석 실패, 재시도 안 함)를 구분한다.
- 변경 파일은 재분석 전에 기존 행을 제거하고 skeleton을 먼저 쓴다(0.9.4.76 크래시 프론티어).

### 2.2 `matches` — 보존된 중복 쌍(이미지/비디오 공통)

| 컬럼 | 타입 | 의미 |
|---|---|---|
| left_path, right_path | TEXT | 쌍(정렬: left ≤ right) |
| percent | REAL | 유사도 % |

- PK: `(left_path, right_path)`. 인덱스: `idx_matches_left(left_path)`, `idx_matches_right(right_path)`.
- 이미지·비디오 쌍이 **같은 테이블**에 저장된다(kind 컬럼 없음).

### 2.3 `thumbs` — GUI 썸네일 캐시(표시 전용)

| 컬럼 | 타입 | 의미 |
|---|---|---|
| path | TEXT PK | 경로 |
| modified, size | INTEGER | 검증용 스탬프 |
| quick_hash | TEXT | 검증용 |
| jpeg | BLOB | 인코드된 JPEG |

- size+mtime으로 검증. `pruneThumbs()`가 `files`에 없는 행을 제거한다.
- ThumbnailStore(백엔드)가 같은 `index.sqlite`를 두 번째 연결로 연다(WAL 공존).

### 2.4 `file_trace` — 스캔 포렌식(0.9.4.78)

| 컬럼 | 타입 | 의미 |
|---|---|---|
| path | TEXT PK | 경로 |
| admitted_ms, started_ms, finished_ms | INTEGER | UTC epoch ms |
| outcome | TEXT | analyzed / failed / 그 외 |

- 크래시 조사용. `finished_ms=0`은 워커가 중단된 미완료 유닛.

### 2.5 `content_hash` — 증분 dataset fingerprint 캐시(0.9.4.89, DB 1.0.6)

| 컬럼 | 타입 | 의미 |
|---|---|---|
| rel | TEXT PK | canonical 상대 경로 |
| size, modified | INTEGER | (size,modified) 일치 시 재사용 |
| sha256 | TEXT | 파일 전체 SHA-256 |

- 파일셋이 바뀌어도 변경/신규 파일만 재해시하게 한다.

### 2.6 `meta` — key/value

| key | 의미 |
|---|---|
| db_version | DB 스키마 버전(현재 1.0.6) |
| engine_version | 판정 생성 버전(현재 1.5.1) |
| sampling_generation | 비디오 샘플링 세대(현재 "3") |
| analyze_frontier | B-슬라이스 재개 프론티어 |
| dataset_fp_cache | 전체 지문 캐시(key|fp|files|bytes) |

## 3. video_cache.sqlite — 비디오 지문 캐시(분리)

`src/video_fingerprint.cpp`가 별도 DB로 연다.

### 3.1 `video_fingerprint_cache`

| 컬럼 | 타입 | 의미 |
|---|---|---|
| path | TEXT PK | 경로 |
| size, modified | INTEGER | 스탬프 |
| quick_hash | TEXT | 스탬프 |
| duration | REAL | 길이 |
| step_count | INTEGER | 샘플 수 |
| cache_version | INTEGER | 캐시 포맷(v9) |
| payload | BLOB | 프레임 지문(base+crop) |

- 인덱스: `idx_vfc_stamp(size,modified)`.

## 4. 이미지/비디오 공유 모델

| 저장소 | 범위 | 파일 |
|---|---|---|
| files / matches / thumbs / content_hash / file_trace / meta | **이미지+비디오 공유** | `index.sqlite` |
| video_fingerprint_cache | **비디오 전용** | `video_cache.sqlite` |
| 매칭 후보 인덱스(런타임) | 메모리에서 분리 | (파일 아님) |

즉 파일·매치·썸네일은 하나의 DB를 공유하고(`files.kind`로 구분), 비디오는 자체 지문 캐시 DB를 추가로 둔다.

## 5. PRAGMA / 동시성

- `journal_mode=WAL`, `synchronous=NORMAL`, `busy_timeout=5000`, `temp_store=MEMORY`.
- WAL 덕분에 엔진(스캔) 연결과 GUI/ThumbnailStore 연결이 공존한다.

## 6. 버전 상수

| 상수 | 값 | 위치 |
|---|---|---|
| kDatabaseVersion | 1.0.6 | `src/database.h` |
| kEngineVersion | 1.5.1 | `src/media_search_engine.h` |
| kBenchmarkSchemaVersion | 9 | benchmark |
| kCacheFormatVersion | 9 | `src/video_fingerprint.h` |
| kSamplingGeneration | "3" | `src/media_search_engine.h` |

- 마이그레이션은 **가산적**이다(컬럼/테이블 추가, 구코드 무시). 파괴적 변경은 없다.
- `db_version`은 open 성공 시 현재 값으로 스탬프된다.

## 7. 쌍매칭 검증 시간 개선 예정 내역 (예비 판정)

> 아래는 **예비 판정**이며, `docs/architecture/database-verification-benchmark.ko.md`의 실측으로 확정/조정한다.
> 근거 수치는 0.9.4.91 실측 로그(177,372 파일, 이미지 검증 `verifyKeyMs` 170s · `verifyDecodeMs` 1095s · `verifyFrameSsimMs` 0.48s, 워커 합산값)에서 도출했다.

| ID | 항목 | 메커니즘 | 예비 판정 | 위험 |
|---|---|---|---|---|
| V-1 | 검증 캐시키의 64 KiB quickHash 읽기 제거 | `FileState.quickHash`(DB 저장) 또는 (size,modified)만 사용 → 파일 읽기 0회 | **채택 후보(저위험)** — verifyKeyMs의 대부분(≈13%) 절감 | 낮음(키 정합성만) |
| V-2 | 검증 버퍼 캐시 확대 | `kVerifyCacheMax` 32 → 512~2048 (엔트리 ≈12 KB) | **채택 후보(저위험)** — 히트율 상승으로 decode miss 감소 | 낮음, 효과는 데이터 의존 |
| V-3 | 디코드 비용 축소 | 축소 디코드 / aspect에서 고정 버퍼 파생 / 빠른 보간 | **CONDITIONAL** — decode의 최대 31% 가능 | 판정 변경 위험 → 패리티 검증 필수 |
| V-4 | 인덱스 단계 디코드 재사용 | 64×64(+aspect) 버퍼를 인덱스에서 보관(메모리/디스크) | **검토 대상** — decode 대부분 제거 가능 | 메모리/디스크 비용 큼 |
| V-5 | 후보 쌍 수 감소 | L1 Hamming 게이트 강화/2차 저비용 필터 | **CONDITIONAL** | 정확도 검증 필요 |
| V-6 | 디코드 I/O 겹침 | 비동기 프리페치로 고지연 드라이브 대기 감소 | **검토 대상** | 구현 복잡도 |
| V-7 | 이미지 검증에 GPU SSIM | `ssimBatch`(48×48 고정) 32×32 일반화 + 배선 | **LOW PRIORITY** — SSIM은 verify의 0.04% | 커널 일반화 필요 |

- 권장 순서: **V-1 → V-2 → (V-4 또는 V-3)**.
- 모든 변경은 **판정 불변**(engine 1.5.1)이 계약이다. CPU/GPU 수치 일치와 저장 쌍 재검증 영향을 확인해야 한다.
- 상세 수치·실행 조건·기각 사유·재검토 조건은 build-history와 worklog의 Performance/Tuning Experiment Index에 남긴다(AGENTS 9항).

## 8. 실측 기록 링크

- 실측 계획/결과/판정: **`docs/architecture/database-verification-benchmark.ko.md`** (EN: `.en.md`).
- 실험 계보/판단 흐름: `docs/worklog/0.9.4.ko.md`의 **Performance / Tuning Experiment Index**.

## 9. 관련 문서

- `docs/architecture/storage-design.ko.md` — 인덱스 위치·트랜잭션·썸네일 정책(상위).
- `docs/architecture/video-gpu-hash.ko.md`, `docs/architecture/video-ssim-gpu.ko.md` — 비디오 해시/SSIM GPU 경로.
- `docs/build-history/0.9.4.76.*`(크래시 프론티어), `0.9.4.81.*`(B-슬라이스 프론티어), `0.9.4.86.*`/`0.9.4.89.*`(dataset fingerprint 캐시).
