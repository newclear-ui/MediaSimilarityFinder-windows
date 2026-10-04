# Implementation Brief — I-Color Thumb no-FFmpeg Classification (Pre-register)

Status: **PRE-REGISTERED** (audit 완료, fixture/production 수정 이전)

```text
Base         v0.9.4.45 / 97db24f
Experiment   I-color_thumb no-FFmpeg classification audit
Product change   NO — 이번 단계는 audit + contract 정의까지만
```

## 1. 목적

`color_thumb` 표시 경로가 FFmpeg 가용성 때문에 **미디어 종류 분류**를 바꾸는지
확인하고, 두 축을 분리하는 계약을 정의한다.

```text
Classification  !=  Decoder availability
```

## 2. Audit 결과 — 현재 분류 경로 (전부 file:line 근거)

분류 진실원은 `Scanner::isVideoPath` **하나**이며 확장자만 본다.

- `src/scanner.cpp:12-16` — `.mp4 .mkv .avi .mov .webm .m4v .wmv` (video)
- `src/scanner.cpp:17-22` — 위 외 image allow-list (`.jpg .jpeg .png .bmp .webp .gif .tif .tiff`)
- `src/media_search_engine.cpp:38` — `kindOf()` 는 `isVideoPath()` 만 호출 (content 판별 없음)
- `gui/mainwindow.cpp:366-369` — GUI `isVideoExt()` 는 동일 7종 (core 미사용, 중복)

분류에 **개입하지 않는** 것 (audit 결과 = 의도된 구조):

- 컨테이너 probe 없음. `avformat_open_input` 은 분류가 아니라 디코드(`src/video_decoder.cpp:22`)
- 디코더/FFmpeg/WIC capability 검사 없음. `MSF_HAS_FFMPEG` 는 컴파일 타임 매크로일 뿐
  런타임 가용성 플래그가 아니다 (`CMakeLists.txt:640`, `src/video_decoder.h:57`)
- 디코드 실패는 `fingerprint=0` / 결과 제외로만 이어지고 `kind` 는 불변
  (`src/media_search_engine.cpp:511`, `:594`, `src/media_pipeline.cpp:53`)

**판정: 세 상태의 classification 은 동일하다.** 의도적이며, 우연한 결과가 아니다.

## 3. 세 상태의 분리 (이번에 명시)

| 상태 | 판정 경로 | classification | 표시 결과 |
|---|---|---|---|
| FFmpeg 사용 가능 | `frameAtColor` native (`video_decoder.cpp:58-90`) | Video 유지 | 색상 프레임 |
| FFmpeg 미사용 | `frameAtColor` 는 `(void)o; return false;` (`video_decoder.cpp:91-93`) | **Video 유지** | 색상 프레임 없음 → 회색 엔진 썸네일/placeholder |
| 런타임 디코드 실패 | `open()` 또는 `frameAtColor` 실패 | **Video 유지** | 동일 폴백 |

`frameAtColor` 의 미링크 `false` 는 **문서화된 의도**다 (`src/video_decoder.h:30-32`
"Display path (previews) ... Returns false without linked FFmpeg"). 단 `CMakeLists.txt:640` 의 configure 메시지
"falls back to ffmpeg/ffprobe command line" 은 `frameAtColor`(색상)과
`framesAt96Plus32ExactSparse`(`video_decoder.cpp:280`)에는 CLI 폴백이 **없음**을 반영하지
않으므로 **과장된 안내**다. 메시지 정정 대상.

## 4. color_thumb 가 classification 을 바꾸지 않는다는 근거

- 생산: `src/media_pipeline.cpp:149-157` (`decodeColorAspect` 성공 시에만 `hasColorThumb`)
- 저장: `src/media_search_engine.cpp:189-214` LRU (최대 `kColorThumbMax = 2048`)
- 소비: `gui/mainwindow.cpp:2476-2487` — `isVid` 는 **확장자** 기준이고 색상 썸네일
  성공/실패와 무관하다
- 실패 시: `hasColorThumb=false` → `getColorThumb` 가 `false` → `px.clear()` → 다음 폴백 lane.
  **kind 대입 코드가 이 경로 전체에 없다.**

## 5. 이번 audit 가 실제로 찾은 correctness risk

### R1 (높음) `color_thumb_test` 가 no-FFmpeg 빌드에서 항상 red

- `CMakeLists.txt:426-428` — `color_thumb_test` 는 `MSF_ENABLE_FFMPEG` 와 무관하게
  **무조건 등록**된다
- `tests/color_thumb_test.cpp:42-47` — `std::system("ffmpeg ...")` 실패 시 `return 4`,
  `open()` 실패 시 `return 5`, `frameAtColor` 실패 시 `return 6`. **skip 처리 없음**
- `src/video_decoder.cpp:91-93` — no-FFmpeg 빌드에서 `frameAtColor` 는 무조건 `false`
- `MSF_ENABLE_FFMPEG=OFF` 는 supported configuration 이다
  (`tests/video_decode_planner_probe.cpp:294` 주석). 그 구성에서 이 테스트는 항상 실패한다.
- 이는 worklog `2026-10-02` 082 실행의 `color_thumb_test exit 5` **미분류 항목을 확정 분류**한다
  (링크 없음 + CLI 없음 → `open()` 실패 → exit 5).
- 대조 불일치: 같은 성격의 `tests/benchmark_integration_test.cpp:761-767` 은 `[SKIP]` 처리한다.
- 부수: `tests/color_thumb_test.cpp:43` 의 raw `std::system` 은 AGENTS.md 11항 위반
  (`src/proc_capture.h::captureSilent` 단일 wrapper 규칙).

### R2 (중간) classification 진실원 2중화

- `src/media_search_engine.cpp:672` — `kindOf(x.path)` (확장자)
- `src/media_search_engine.cpp:674` — `files_.push_back(..., (MediaKind)x.kind, ...)` (DB 저장값)
- `src/media_search_engine.cpp:675` — `loadVideoAnchors` 는 확장자 기준으로만 호출
- `src/media_search_engine.cpp:115` — `rebuildCandidateIndexes()` 는 DB `x.kind` 기준으로
  image/video 후보 인덱스를 분리
- 어긋나면 anchor 로딩과 후보 인덱스가 서로 다른 분류를 쓰게 된다. 대조/검증 코드가 없다.

### R3 (중간) 확장자 목록 4중 중복

`src/scanner.cpp:15`, `src/scanner.cpp:21`, `src/monitor.cpp:119/243/320`,
`gui/mainwindow.cpp:366-369`. 현재 4벌은 동일 집합이라 동작 차이가 없으나 drift 시 조용히
갈린다. `src/monitor.cpp:323` 은 else 분기로 `kind=image?1:2` 를 만들므로 여기서 어긋나면
비media 파일이 video 분기로 들어간다.

### R4 (중간) 셸 썸네일이 엔진 색상 썸네일을 무조건 덮어씀 + 통계 오염

- `gui/mainwindow.cpp:2482` — 셸 lane 이 `pm.isNull()` 가드 **없이** `pm` 을 덮어씀
- 반면 이후 lane 들은 가드한다 (`:2487`, `:2526`)
- 결과: 엔진 색상 썸네일이 버려지고도 `thumbStatEngine_` 가 증가하며, 예산 1회를 소비한 뒤
  discard 된다. 주석(`:2477`)과 실제 실행 순서도 불일치.

### R5 (낮음) configure 메시지 과장 — §3 에서 기술.

### R6 (낮음) `MediaKind` ↔ 정수 매핑 매직넘버 (`src/benchmark_core.cpp:88-94`,
`src/monitor.cpp:323`, 공개 API `int kind`). `static_assert` 0건.

## 6. 기존 테스트가 실제 no-FFmpeg semantics 를 고정하는가

**고정하지 못한다.** 근거:

- `scanner_test` 는 대소문자/Unknown/개수를 pin 하지만 "FFmpeg 상태와 분류는 무관"이라는
  관계 자체를 검증하지 않는다
- `image_pipeline_test` 는 `hasColorThumb` 성공만 pin. **실패 시 동작 미pin**
- `color_thumb_test` 는 성공 경로만 다루며 위 R1 때문에 no-FFmpeg 구성에서 실패한다
- `tests/` 어디에도 FFmpeg unavailable 을 **시뮬레이션**하는 수단(env override, stub binary)이 없다.
  `#ifdef MSF_HAS_FFMPEG` 는 빌드 구성 가드이지 런타임 시뮬레이션이 아니다.

→ **테스트가 없다고 버그를 단정하지 않는다.** R1 은 CMake 등록과 exit code 로 직접 증명되므로
버그로, 나머지는 "미검증"으로 분류한다.

## 7. Expected behavior (계약 후보)

```text
Classification   : 확장자 기반 단일 규칙. FFmpeg/디코더/썸네일 결과와 무관.
Color thumb fail : 분류 불변, 폴백 lane 진행, kind 미대입.
Video no-FFmpeg  : kind=Video 유지, 색상 프리뷰 없음(회색/placeholder), 오분류 금지.
Image -> video 경로 진입 : 금지.
```

## 8. Fixture plan (다음 단계, 이번에 구현하지 않음)

1. **분류 불변 fixture** — 동일 확장자 목록으로 `MediaKind` 결과가 FFmpeg 링크 유무와 무관함을
   고정. 가능한 구조: `MSF_ENABLE_FFMPEG=OFF` 별도 빌드에서 실행되는 classification 전용 target.
2. **색상 썸네일 실패 fixture** — 디코드 불가 이미지 픽스처로 `hasColorThumb=false` 이고
   `kind=Image` 가 유지됨을 pin. 표준 dataset 은 수정하지 않는다.
3. **R1 skip/pass** — `color_thumb_test` 를 링크 여부에 따라 skip 하거나 색상 lane 만 조건부로
   실행하도록 CTest 등록을 분기한다. `tests/benchmark_integration_test.cpp:761-767` 선례를 따른다.
4. `std::system` → `captureSilent` 교체 (AGENTS.md 11항).

## 9. Acceptance criteria

- [ ] no-FFmpeg 빌드에서 CTest 가 초록 또는 명시적 skip 로 끝난다
- [ ] 세 상태(available / unavailable / runtime decode failure)의 `MediaKind` 가 동일함을
      테스트로 고정
- [ ] 색상 썸네일 실패가 `kind` 를 바꾸지 않음을 테스트로 고정
- [ ] `CMakeLists.txt:640` configure 메시지가 실제 폴백 커버리지를 반영
- [ ] R2/R3/R4 는 각각 별도 결정으로 남기고, 이 brief 의 production correction 범위에
      함부로 끌어들이지 않는다

## 10. Forbidden changes (이번 단계 및 fixture 단계)

- Scanner / MediaKind semantics 변경
- FFmpeg fallback 경로 변경
- thumbnail pipeline 변경
- VideoDecoder / ImageDecoder 변경
- CandidateIndex / threshold 변경
- GUI 표시 변경
- 표준 dataset 수정

## 11. 이번 단계의 경계

```text
color_thumb production correction = NOT PERFORMED
color_thumb fixture implementation = NOT PERFORMED
CPU/GPU regression for color_thumb = NOT PERFORMED (제품 코드 변경 없음)
S4 final GUI visual/save acceptance  = DEFERRED
S5 product benchmark                = DEFERRED
S6                                = DEFERRED
NVDEC production adoption          = DEFERRED
```