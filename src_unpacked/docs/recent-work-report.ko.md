# 최근 중요 3건 작업 결과 보고 (2026-10-04)

기준 커밋: `619f74a` (`origin/main`과 동기화 완료)
버전: `0.9.4.45` / HEAD `619f74a` / CPU CTest 102/102 / GPU CTest 103/103

이 문서는 최근 완료된 중요 작업 3건을 **한 문서에서 확인**하기 위한 요약 보고다.
각 작업의 상세 근거는 Build History / Work Log에 이미 있고, 이 문서는
"무엇을 했고 지금 상태가 어떤가"만 중복 없이 모은다.

---

## 작업 1 — `--version` 콘솔 출력 오류 수정

커밋 `619f74a` / 파일 `src_unpacked/gui/main.cpp`

### 무엇을 했나

`MediaSimilarityFinder.exe --version`이 **PowerShell에서 아무것도 출력하지
않았다.** CMD에서는 정상이었다.

원인은 `gui/main.cpp::attachParentConsole()` 안의 판정 함수
`streamIsRedirected()`였다. 실행 파일은 GUI subsystem(`WIN32_EXECUTABLE TRUE`)
이라 `GetConsoleWindow()`가 null이면 `AttachConsole(ATTACH_PARENT_PROCESS)`를
시도하고, 이때 리다이렉션이 있으면 `CONOUT$` 재오픈을 건너뛴다.

문제는 그 판정이 **"스트림을 쓸 수 없다"는 상태를 "리다이렉션되어 있다"로
착각했다**는 점이었다.

| 조건 | 이전 판정 | 의미였던 것 |
|---|---|---|
| `fd < 0` | `true` (잘못됨) | 디스크립터 없음 = **사용 불가** |
| `_get_osfhandle()`가 `-1` 또는 `0` | `true` (잘못됨) | 유효 OS 핸들 없음 = **사용 불가** |
| `GetFileType()`가 `FILE_TYPE_UNKNOWN` | 마지막 `FILE_TYPE_DISK`/`FILE_TYPE_PIPE` 비교까지 흘러가므로 `false` | 무효 핸들 = **사용 불가** |

즉 `true`로 잘못 처리된 것은 **두 가지**였다. `FILE_TYPE_UNKNOWN`은 이미 `false`로
귀결되었으나 명시적 판정이 아니라 마지막 비교에 흘러간 결과였기에, 이번 수정에서
그 경로도 드러내도록 했다.

그래서 PowerShell 실행 시 실제 출력 대상이 아닌 `stdout`/`stderr`를 리다이렉션으로
판정해 `AttachConsole`이 건너뛰어지고, `std::cout`에 쓰기 대상이 없어졌었다.

### 수정은 무엇이었나

세 경우 모두를 명시적인 `false`(사용 불가)로 바꿨다. 그 결과 **유효한
`FILE_TYPE_DISK`/`FILE_TYPE_PIPE`만 리다이렉션으로 남는다.**

이게 중요한 이유는, "실제 리다이렉션은 반드시 보존한다"는 기존 설계 의도를
그대로 유지하면서 **판정 오류만 제거**했다는 점이다. 무조건 `AttachConsole`을
부르는 방식과 다르다. 파일/파이프로의 리다이렉션은 그대로 보존되고, 출력 자체가
불가능한 상태만 `AttachConsole` + `CONOUT$` 경로로 넘어간다.

### 결과

- `--version`, `--help`, `cmd /c`, 잘못된 옵션(stderr + EXIT=2) 전부 정상.
- OS 수준 리다이렉션(`cmd /c "exe --version > file"`) 45바이트 정상 기록.
- `Start-Process -Wait -RedirectStandardOutput` 45바이트, EXIT=0.
- 검색/인덱스/비교 로직과 GUI 동작은 영향 없음.

### 검증 중 나온 주의사항 하나

PowerShell에서 `--version > file`이 0바이트가 되는 현상이 함께 확인됐다.
**이것은 이번 수정이 만든 회귀가 아니다.** 수정 전 exe에서도 똑같이 재현되며,
`$LASTEXITCODE`가 비어 있다. 원인은 PowerShell이 GUI subsystem exe를 기다리지
않는다는 점이다.

앞으로 콘솔 출력 검증은 **OS 수준 리다이렉션**을 기준으로 해야 한다.
(`cmd /c` 또는 `Start-Process -Wait`)

---

## 작업 2 — CUDA `C4819` 인코딩 경고 제거

커밋 `619f74a` / 파일 `src_unpacked/CMakeLists.txt`

### 무엇을 했나

GPU 빌드 로그에 MSVC `warning C4819`가 CUDA 헤더(`driver_types.h`,
`cuda_runtime_api.h`)에서 반복해서 나왔다.

**중요: 이건 CUDA 문법 오류도 링크 오류도 아니었다.** 코드 페이지 949가 CUDA
헤더 안의 비ASCII 문자를 표현하지 못해서 나는 인코딩 경고이며, 빌드 자체는
성공했고 GPU CTest도 103/103으로 통과했다.

원인은 `/utf-8` 적용 범위였다. 기존에는 이것 하나뿐이었다.

```cmake
add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>)
```

즉 MSVC C++ 컴파일에만 `/utf-8`가 들어갔고, CUDA는 host compiler가 별도이므로
그 플래그를 전달받지 못했다.

### 수정은 무엇이었나

`nvcc`는 `/utf-8`를 직접 받지 못하므로 MSVC host compiler에 `-Xcompiler`로
경유시킨다.

```cmake
add_compile_options($<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CXX_COMPILER_ID:MSVC>>:-Xcompiler=/utf-8>)
```

**여기서 첫 시도는 실패했고, 그 실패가 기록-worthy였다.** 처음에는
`COMPILE_LANG_AND_ID:CUDA,MSVC`를 썼는데 매칭이 되지 않았다. CUDA의 compiler id는
`NVIDIA`이고 `MSVC`는 frontend variant이기 때문이다. 즉 언어는 CUDA인데 id는
MSVC라고 동시에 말할 수 없는 상황이었다. `COMPILE_LANGUAGE:CUDA`와
`CXX_COMPILER_ID:MSVC`를 조합하니 nvcc 명령줄에
`-Xcompiler="/EHsc -Ob2 /utf-8"`가 실제로 들어가는 것을 확인했다.

### 결과

- `cuda_backend.cu` 강제 재컴파일: `C4819=0` / `warning=0` / `error=0`.
- `msf_cuda.lib` 정상 생성.
- GPU 전체 빌드 + CTest **103/103 PASS**, 경고 0건.
- CUDA architecture(`compute_75/86/89`)와 runtime 동작 불변.
- 소스/ABI 변경 없음. 빌드 옵션 추가뿐.

---

## 작업 3 — XMP Orientation Fallback 구현 + 독립 검수 정정

커밋 `97db24f`(구현) + `392a4c2`(검수 정정 및 `color_thumb` 사전 등록)

관련 문서: `docs/implementation-briefs/I-xmp-orientation-fallback.{ko,en}.md`

### 무엇을 했나 — 1단계: 구현

이미지가 회전해 보이는데 EXIF Orientation이 없는 파일에서 방향을 잃는 문제를
해결했다.

- EXIF `VT_UI2` 값 `1..8`이면 EXIF를 적용한다(XMP는 조회조차 안 함).
- EXIF가 없거나 실패하거나 타입이 잘못되었거나 범위 밖이면 XMP로 넘어간다.
- XMP는 WIC 경로 `/xmp/tiff:Orientation`을 쓴다. **실측 결과 `VT_LPWSTR`
  문자열**로 나타났고, 정수 VARIANT도 함께 정규화한다.
- EXIF와 XMP가 충돌하면 **EXIF 우선**.
- 양쪽의 출력 경로(`decodeBoth` fingerprint, color display lane)가 **같은
  resolver를 공유**하므로 변환 의미가 갈라지지 않는다.
- invalid XMP는 identity로 처리한다. telemetry 필드는 추가하지 않았다.

### 무엇을 했나 — 2단계: 독립 검수와 정정

구현이 끝났다고 "통과"로 칠 필요가 없다는 IndependENT REVIEW가 지적해서
fixture를 실제로 보강했다. 처음 14 checks였던 fixture를 **38 checks**로 늘렸고,
검수 시 **NOT VERIFIED**였던 항목을 실제로 채웠다.

| 검수 지적 항목 | 지적 당시 | 이번 결과 |
|---|---|---|
| mapping `1/3/6/8` | PASS | PASS 유지 |
| mapping `2/4/5/7` | **NOT VERIFIED** | **PASS** (H2/H4/H5/H7) |
| 90/270 방향 | 기하 `32x16<->16x32` 뿐이라 미검증 | **PASS** (사분면 평균으로 방향 구분) |
| 180 변환 | 기하 불변이라 미검증 | **PASS** (좌우 사분면 평균 교환) |
| EXIF 범위 밖 fallback | 미검증 | **PASS** (I9: EXIF=9 → XMP=6 적용) |
| EXIF 타입 오류 fallback | 미검증 | **PASS** (IT: type=ASCII → XMP=8 적용) |
| full scan regression | 미시연 | **DEFERRED** |
| 표준 dataset XMP coverage | 보고만 있음 | **보고만 있음, 유지** |

fixture BMP를 **사분면 이미지**(레벨 `0/85/170/255`)로 바꿨다. 기존 fixture는
좌우 두 덩어리뿐이라 상하 방향 구분이 불가능했다. BMP는 행이 bottom-up이라
단언은 디코딩된 이미지 좌표로 작성했다. `5`와 `7`은 `6`/`8`과 기하가 같으므로
방향을 주장하지 않고, **동일 transform을 거친 EXIF 경로와의 byte identity**로
고정했다(`XMP=5` == `EXIF=5`, `XMP=7` == `EXIF=7`).

### 현재 판정 — 여기를 구분해야 한다

```text
XMP code implementation        PASS
XMP fixture (1..8 + pixel)    PASS
XMP real-dataset coverage     NOT_AVAILABLE  (표준 dataset에 XMP 파일 0, 미검증)
full Search/Scan regression   DEFERRED       (S4 functional acceptance / S5 benchmark 의존)
XMP production acceptance     CONDITIONAL
```

fixture가 구현 정합성(1..8 + pixel 방향 + EXIF invalid fallback)을 증명하지만,
full scan regression과 real-dataset coverage가 없으므로 production acceptance는
CONDITIONAL이다. `docs/build-history/0.9.4.45.*`는 소급 수정하지 않고 이 문서와
Work Log에 정정을 기록한다.

### 부수 산출물 — `color_thumb` audit 사전 등록

같은 커밋에서 `docs/implementation-briefs/I-color-thumb-no-ffmpeg-classification.*`
를 새로 만들었다. **audit만 하고 production 수정은 하지 않았다.**

핵심 결론: classification은 단일 확장자 규칙이며 **FFmpeg 상태 세 가지 모두에서
동일**하다. decoder capability 부재가 media type을 바꾸어서는 안 된다는 것이
계약 후보다.

확정 위험 요소:
- **R1(high)**: no-FFmpeg 빌드에서 `color_thumb_test`가 항상 실패한다
  (무조건 CMake 등록 + skip 처리 없음 + `frameAtColor` 무조건 `false`).
  worklog run 082의 미분류 `exit 5`를 분류한다.
- **R2(medium)**: `kindOf()`(확장자)와 DB `x.kind`가 이중 진실원이고 교차 검증 없음.
- **R3(medium)**: 확장자 목록이 4곳에 중복(`scanner`, `monitor` 3곳, GUI).
- **R4(medium)**: shell thumbnail가 `isNull()` 가드 없이 엔진 color thumbnail를
  덮어써 `thumbStatEngine_`를 오염시킨다.
- **R5/R6(low)**: configure 메시지 과장 / magic-number `MediaKind` 매핑.

---

## 현재 전체 상태 요약

| 항목 | 상태 |
|---|---|
| 버전 | `0.9.4.45` (bump 없음) |
| 커밋 | `619f74a`, `origin/main`과 동기화 |
| CPU CTest | 102/102 PASS |
| GPU CTest | 103/103 PASS |
| CUDA 경고 | 0건 |
| XMP production acceptance | CONDITIONAL |
| `color_thumb` production 수정 | 미수행 (사전 등록만) |
| S4 최종 GUI visual/save acceptance | DEFERRED |
| S5 product benchmark | DEFERRED |
| S6 | DEFERRED |
| NVDEC production adoption | DEFERRED |

## 남은 후보 / 다음 단계

- 콘솔 출력 전용 회귀 테스트 추가 (현재는 수동 검증만 존재).
- nvcc 자체 경고를 release gate에 등록할지 결정.
- `color_thumb` R1 fixture 및 skip/pass 처리 구현 → 그다음 R2~R6은 별도 결정.
- XMP full scan regression — S4 functional acceptance 완료 후.
- 지시 2항의 백업 zip(`backup_src.ps1` / `package_portable.ps1`)은 이 변경에서
  수행하지 않았다. 두 스크립트 모두 미커밋 tracked 변경 0건을 요구하는데,
  `.gitattributes` 2개가 CRLF 정규화 때문에 `M`으로 표시되어 있어 선결정이 필요하다.

## 관련 문서

- `docs/build-history/0.9.4.45.{ko,en}.md`
- `docs/worklog/0.9.4.{ko,en}.md`
- `docs/implementation-briefs/I-xmp-orientation-fallback.{ko,en}.md`
- `docs/implementation-briefs/I-color-thumb-no-ffmpeg-classification.{ko,en}.md`
- `docs/development-progress.{ko,en}.md`