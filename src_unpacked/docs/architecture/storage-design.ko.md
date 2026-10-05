# Storage / Portable Index Design

## 현재 설계

MediaSimilarityFinder 는 스캔 대상 폴더 안에 검색 인덱스를 쓰지 않는다.
관리되는 모든 인덱스는 애플리케이션 자신의 `Index` 디렉터리 아래에 저장된다.

```text
C:\MediaSimilarityFinder\
├─ MediaSimilarityFinder.exe
└─ Index\
   ├─ <stable-folder-id>\
   │  ├─ metadata.json
   │  ├─ index.sqlite
   │  │  ├─ files
   │  │  ├─ matches
   │  │  └─ thumbs
   │  └─ video_cache.sqlite
   └─ ...
```

각 스캔 루트는 정규화된 canonical 경로로 변환되고 안정적인 folder ID 로 매핑된다.
기존 인덱스를 재사용하기 전에 metadata 레코드를 검증한다.

## 중요 규칙

스캔 대상 자체에는 MediaSimilarityFinder 인덱스 파일이 남지 않는다.
애플리케이션 디렉터리를 스캔할 때는 자신의 `Index` 서브트리를 제외한다.

## 트랜잭션과 체크포인트 정책

인덱스 갱신은 트랜잭션이지만, 현재 스캔은 의도적으로 체크포인트를 사용한다.
완료된 작업을 주기적으로 커밋하므로 취소가 진행 상황을 전부 잃게 하지 않는다.
취소 시 이미 커밋된 체크포인트는 그대로 남으며, 실제 실패에서만 현재 미커밋
트랜잭션이 롤백된다.

삭제된 파일의 정리는 디렉터리 walk 가 정상적으로 완료된 뒤에만 수행하므로,
취소되었거나 불완전한 walk 가 아직 보지 못한 인덱스 행을 잘못 지우지 않는다.

이미 인덱싱된 파일이 변경되면, 재분석 전에 기존 DB 행을 제거하고 fingerprint
분석 전에 skeleton 행을 쓴다. 분석 진행 중 stale fingerprint 가 파일에 남아 있는
상태를 방지한다.

## 썸네일 캐시

`index.sqlite` 는 `thumbs` 테이블에 GUI 썸네일 캐시를 영구 저장한다.
항목은 경로로 키가 정해지고 파일 mtime + size 로 검증된다. 성공한 preview decode
결과는 192px JPEG(품질 70)로 저장되어, 이후 스캔에서 per-tick GUI decode 예산을
소비하지 않고 재사용된다. GUI 는 같은 인덱스 DB에 두 번째 SQLite 연결을 열며,
WAL 모드가 이를 스캔 연결과 공존하도록 한다.

고아(thumbnail) 행은 스캔 종료 시 정리된다.

## 발전 방향

이 문서는 현재 저장 설계를 기술한다. 버전별 변경은 `docs/build-history/` 아래에
별도로 기록한다.

영문 각본은 `docs/architecture/storage-design.md` 이며, 두 파일은 서로 독립된
KO/EN 문서로 각각 보존된다(개명하지 않는다). 두 각본의 내용은 동일하다.

## Detailed Log 와 Benchmark 저장소 분리

GUI 상세 로그는 실제 사용자 작업에 속한다. 별도의 benchmark run 이 아니다.

진단 수집만을 위해 GUI 가 Benchmark Run/Stop/Pause UI 를 새로 만들면 안 된다.

최종 GUI 로그 경로는 S4 구현 결정 사항이다. 의미 규칙은 고정이다: GUI 로그는 실제
사용자 작업에서 나온 진단 증거다.

### Console benchmark

Application data root
└─ Benchmark/
   └─ Console/
      └─ suite-<suite-id>/
         ├─ suite.json
         ├─ runs.jsonl
         └─ summary.json

- `runs.jsonl` 은 파일별/mode별 증거의 복구 원본이다.
- `summary.json` 은 파생값이며 권위 있는 값이 아니다.
- Console benchmark 결과는 장기 비교를 위해 보관한다.
- Benchmark runtime/index/cache 는 일반 Search Index 와 분리한다.
- GUI 는 Console benchmark 이력을 자동으로 읽지 않는다.
- Console 은 GUI 상세 로그를 benchmark 이력으로 자동 주입하지 않는다.

### 의미 경계

GUI
  Search/Update
    └─ [상세 로그]
         └─ TelemetryRecorder / UserDiagnostic

CLI
  --benchmark
    └─ BenchmarkSession / BenchmarkRunner
         └─ TelemetryRecorder / Benchmark

Benchmark 와 Telemetry 는 동의어가 아니다.
