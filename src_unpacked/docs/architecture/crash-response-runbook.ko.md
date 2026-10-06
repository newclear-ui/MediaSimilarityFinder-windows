# 크래시 대응 런북 (Crash Response Runbook)

목적: 강제종료 발생 시 무엇을 수집하고 어떤 순서로 좁히는지 정한 절차서.
제품 버전과 무관하게 이 절차를 먼저 수행한다.

## 1. 크래시 발생 직후 수집 (순서대로)

### 1-1. `%TEMP%\msf_scan.log` 꼬리 확인

- `start folder=...` 뒤에 `finish`가 없으면 비정상 종료다
  (정상 완료·사용자 Stop은 반드시 `finish`행을 남긴다).
- `alive ...` heartbeat(약 10초 간격)가 언제 끊겼는지 확인.
  마지막 heartbeat 시각 ≈ 크래시 시각이다.
- `lastPct`/`lastPath`는 분석·실패 파일에서만 갱신된다.
  0%+빈 경로는 "아직 분석 없음"이지 정지의 증거가 아니다
  (열거·스킵 구간은 신호가 없다).

### 1-2. Windows 이벤트 뷰어

`eventvwr.msc` → Windows 로그 → Application → 오류, 해당 시각 전후:

- 이벤트 ID **1000**: faulting 모듈명·오프셋·예외 코드·프로세스 경로.
  - `0xc0000409` = fail-fast (abort 계열: terminate, abort, Qt fatal 등).
  - `0xc0000005` = 액세스 위반 (힙 손상·wild pointer 의심).
- 이벤트 ID **1001**: WER 버킷 ID (동일 크래시 식별자).
- **동일 오프셋 반복** = 결정적 abort 경로. 오프셋 분산 = 메모리 손상 의심.

### 1-3. `%TEMP%\msf_qt.log` (0.9.4.62+)

Qt warning/critical/fatal의 최종 기록. fatal 직전 줄이 마지막 단서다.

### 1-4. WER 보고서

`C:\ProgramData\Microsoft\Windows\WER\ReportQueue` (및 `ReportArchive`)의
`.wer` 파일. 로드 모듈 목록·OS 빌드·버킷이 들어 있다. 기본 설정에는
`.dmp`가 없으므로 아래 덤프 수집을 권장한다.

## 2. 덤프 수집 설정 (다음 발생 대비)

관리자 PowerShell에서 1회 실행한다.

```powershell
New-Item -Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\MediaSimilarityFinder.exe" -Force
New-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\MediaSimilarityFinder.exe" -Name "DumpFolder" -Value "D:\Temp\OpenCodeWork\dumps" -PropertyType ExpandString -Force
New-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\MediaSimilarityFinder.exe" -Name "DumpCount" -Value 3 -PropertyType DWord -Force
New-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\MediaSimilarityFinder.exe" -Name "DumpType" -Value 1 -PropertyType DWord -Force
```

레지스트리 기록 사항:

| 값 | 형식 | 의미 |
|---|---|---|
| `DumpFolder` | REG_EXPAND_SZ | 덤프 저장 폴더. 미리 만들어 둘 것 |
| `DumpCount` | REG_DWORD | 보관 개수 (초과분 자동 삭제). 3 권장 |
| `DumpType` | REG_DWORD | 1 = mini (스레드·스택 특정에 충분). 2 = full (수 GB 가능, 대규모 스캔 프로세스에서 주의) |

다음 강제종료 시 해당 폴더의 `.dmp` + 발생 시각을 확보하면 fault 스레드·
스택을 특정한다.

## 3. 알려진 크래시 이력

### 3-1. 2026-10-07 00:32:18 / 00:53:33 — 0xC0000409 fail-fast (원인 미특정)

- 개발 GPU 빌드(`build-windows-gpu\Release`), 대규모 스캔 중 2회 연속.
- `ucrtbase.dll` 동일 오프셋(`0xa527e`) 2회 → 결정적 abort 경로.
  힙 손상(0xC0000005) 아님.
- 1차는 스캔 후반(79%) 파일 처리 중, 2차는 19분간 진행 표시 0%
  (열거·스킵 구간으로 설명됨, 정지 아님) 후 사망.
- 유력 후보: worker 스레드 미처리 예외 → terminate → abort
  (`ScanWorker::run`이 `std::exception`만 catch했음).
  덤프 확보 후 스택 특정 필요.

### 3-2. 2026-10-07 04:30:43 — 0xC0000409 fail-fast, 3번째 (덤프로 fault 경로 특정)

- 제품 GPU 빌드 0.9.4.64 (`build-windows-gpu\Release`, 타임스탬프 `0x6AC5408D`),
  `G:\Downloads\ss_twit` 스캔 시작 21분 후 사망. heartbeat는 사망 8초 전까지
  alive (walked=156481/listed=228000, 0% — 열거 구간).
- 이벤트 1000: `0xc0000409`, 오프셋 `0xa527e` — 앞선 2회와 동일.
  동일 오프셋 3회 = 결정적 abort 경로.
- 덤프(`D:\Temp\OpenCodeWork\dumps\MediaSimilarityFinder.exe.11104.dmp`,
  mini 12.8MB) 직접 파싱 결과:
  - 예외 `0xC0000409` + 파라미터 `0x7` = ucrtbase `abort()` 내부의
    `int 29h` (`abort+0x4E`, disasm 확인). Qt fatal 아님 (`msf_qt.log` 없음).
  - fault 스레드 = 스캔 worker. 스택 최상단이 `ScanWorker::run`의
    `catch (std::exception&)` 핸들러 영역과 일치
    (`persistMatchesSnapshot` 호출 + `e.what()` 가상 호출 + `emit failed` —
    PDB 없이 IAT/disasm 역조회 + 섀도우 PDB 대조로 특정).
- 판정: 0.9.4.62 catch-all은 첫 예외를 잡지만, 핸들러 안의 persist·emit이
  다시 던지면 Qt 슬롯 밖으로 나가 `terminate()` → `abort()`로 직행한다.
  덤프가 바로 그 사슬을 가리킨다. 첫 예외의 원인은 덤프로 알 수 없음
  (이미 잡힌 뒤 사망).
- 대응: 0.9.4.65에서 두 핸들러의 persist·emit을 독립 try/catch로 감싸
  핸들러 밖 탈출 경로 제거. 상세: `docs/build-history/0.9.4.65.ko.md`.

### 3-3. gradient match-storm 후 크래시 (테스트 환경, 별도 지시 필요)

- 수천 cross-match가 한 번에 stream되자 fill 이후 0xC0000005.
  엔진 단독 240-file 스캔은 정상. GUI 측 match-storm 규모 문제로 분리 기록.
  scroll 회귀와는 다른 버그.

## 4. 방어 패치 내역 (0.9.4.62)

- `ScanWorker::run`에 `catch (...)` 추가. 부분 매치 checkpoint 후
  `failed` 보고로 전환하여 무기록 종료를 기록된 실패로 격하.
- 원인: 위 3-1의 abort 계열 (비표준 예외는 terminate로 직행했음).
- MSVC 기본 `/EHsc`에서는 SEH 액세스 위반이 `catch(...)`를 통과하지
  않으므로 실제 메모리 손상은 계속 fail-fast (마스킹 없음).
- 테스트용 `MSF_TEST_THROW_NONSTD` seam 포함. 제품 코드는 설정하지 않음.
- 상세: `docs/build-history/0.9.4.62.ko.md`.

## 5. 관측 패치 내역 (0.9.4.62)

- `installQtMessageLog()`: Qt 메시지 파일 싱크. 메시지별 open/append/
  close + mutex라 크래시 안전·스레드 안전. fatal은 기록 후 Qt가 기존대로
  abort. main() 3경로(GUI/headless/benchmark)에 설치.
  경로: `%TEMP%\msf_qt.log`.
- heartbeat 확장: `walked=<lastTotalN> listed=<lastListN>` 추가.
  재스캔 초반 0%가 저속 열거인지 hang인지 구분 가능.
  기존 필드 순서 유지 (파서 호환).
- 상세: `docs/build-history/0.9.4.62.ko.md`.

## 6. 관련 문서

- `docs/build-history/0.9.4.62.{ko,en}.md` — 패치 상세와 검증 수치.
- `docs/worklog/0.9.4.{ko,en}.md` — 0.9.4.62 항목 (원인·실측).
- `docs/architecture/image-burst-shot-similarity.{ko,en}.md` — 별개 주제
  (유사 판정). 크래시와 무관하므로 혼동하지 말 것.
