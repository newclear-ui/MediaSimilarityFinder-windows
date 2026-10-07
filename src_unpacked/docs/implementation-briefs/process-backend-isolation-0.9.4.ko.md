# 0.9.4 Backend 프로세스 격리 구현 상세 설계

## 상태

- 대상: 0.9.4.x
- 문서 역할: 0.9.4 프로세스 아키텍처를 실제 구현 가능한 수준으로 구체화한 Implementation Brief
- 상태: 구현 기준 설계
- 상위 기준: `docs/architecture/process-architecture-0.9.4.ko.md`
- 관련 미래 설계: `docs/architecture/process-architecture-0.9.5.ko.md`
- 구현 주체: OpenCode
- 본 문서는 구현 방법 전체를 고정하는 것이 아니라 **프로세스 경계, 소유권, IPC 의미, 장애 복구, 금지선, acceptance 기준**을 고정한다.

---

## 1. 목적과 범위

0.9.4.x의 가장 큰 구조 변경은 현재 하나의 `MediaSimilarityFinder.exe` 안에서 함께 실행되는 GUI와 검색/처리 계층을 **GUI process + Backend process**의 두 OS 프로세스로 분리하는 것이다.

이번 변경의 목적은 검색 알고리즘을 다시 작성하는 것이 아니다.

핵심 목적은 다음과 같다.

1. WIC/COM, FFmpeg, CUDA, heap, CRT abort/terminate 및 기타 native/runtime failure가 Backend에서 발생하더라도 GUI process까지 함께 종료되지 않게 한다.
2. GUI가 Backend의 생명주기를 감시하고 필요한 경우 Backend만 bounded restart할 수 있게 한다.
3. 기존 Search / Index / Comparison semantics와 현재 데이터 저장 구조를 유지한다.
4. 0.9.5.x의 ImageWorker / VideoWorker / MonitorService 분리를 위한 경계를 미리 만들되, 0.9.4에서는 과도한 process decomposition을 하지 않는다.

이번 변경은 **fault containment과 execution boundary 변경**이다. 현재 crash의 최초 원인을 해결했다고 간주해서는 안 된다.

---

## 2. 현재 코드 기준점

현재 GUI 쪽에는 Backend로 이동해야 할 기능이 직접 연결되어 있다.

현재 핵심 구조:

```text
gui/main.cpp
    └─ MainWindow

gui/mainwindow.h/.cpp
    ├─ MainWindow
    ├─ ScanWorker
    │   ├─ msf::MediaSearchEngine
    │   ├─ ScanControl
    │   └─ match / GPU state
    └─ MediaMonitor

MediaSearchEngine
    ├─ directory scan
    ├─ image/video processing
    ├─ candidate index
    ├─ matching / aggregation
    └─ Database / SQLite
```

중요 기준:

- `ScanWorker`가 GUI header에 직접 선언되어 있는 것은 현재 구조이며 0.9.4의 분리 작업에서 정리 대상이다.
- `MainWindow`가 `std::unique_ptr<msf::MediaMonitor>`를 소유하는 것도 현재 구조이며 Backend 소유로 이동한다.
- GUI 테스트 중 일부는 `gui/mainwindow.cpp`를 직접 test target에 포함한다. 이를 무조건 폐기하지 말고, GUI와 Backend 경계를 도입하면서도 가능한 한 기존 GUI 테스트 coverage를 유지한다.
- `gui/main.cpp`에는 GUI 경로 외에 headless/CLI 경로가 있다. 0.9.4의 필수 process-isolation acceptance는 **GUI 실행 경로**다. CLI/headless 경로는 검색 semantics를 바꾸기 위한 별도 작업으로 확장하지 않는다. 다만 Backend executable을 직접 실행하는 backend integration test 경로는 새로 추가할 수 있다.

---

## 3. 목표 프로세스와 소유권

### 3.1 목표 구조

```text
GUI Process
├─ QApplication / MainWindow
├─ presentation / GUI state
├─ BackendClient
└─ BackendSupervisor / Watchdog
          │
          │ local IPC
          ▼
Backend Process
├─ Backend control / IPC endpoint
├─ Search session
├─ ScanWorker
├─ MediaSearchEngine
├─ Image processing
├─ Video processing
├─ Scheduler / resource policy
├─ CandidateIndex
├─ Database / SQLite
├─ Match aggregation
├─ telemetry / diagnostics
└─ MediaMonitor
```

### 3.2 GUI가 소유하는 것

GUI process가 직접 소유:

- Qt application/window/widget
- 검색 화면 상태와 표시 모델
- Detailed Logs 표시 상태
- Backend 연결 상태
- Backend Supervisor
- IPC client/transport
- 사용자 입력에 대한 명령 생성
- Backend가 보낸 결과의 presentation

GUI는 다음을 직접 소유하지 않는다.

- `MediaSearchEngine`
- `ScanWorker`
- `MediaMonitor`
- WIC/FFmpeg/CUDA 처리 객체
- CandidateIndex
- SQLite connection
- Backend 내부의 match aggregation state

### 3.3 Backend가 소유하는 것

Backend process가 계속 소유:

- Search session
- ScanWorker
- MediaSearchEngine
- directory walk
- image/video processing
- current scheduler/resource policy
- CandidateIndex
- Database / SQLite
- match aggregation
- current Monitor
- backend-side telemetry

0.9.4는 내부 algorithm/class를 새로 설계하는 단계가 아니라 **현재 backend 실행 범위를 다른 OS process로 이동시키는 단계**다.

---

## 4. 프로세스 실행과 진입점

### 4.1 Backend 실행 파일

구체적인 executable 파일명은 구현 단계에서 기존 CMake/패키징 규칙에 맞춰 결정할 수 있다. 다만 다음 계약을 지켜야 한다.

- GUI와 Backend는 실제 서로 다른 OS process여야 한다.
- Backend는 GUI에 의해 child process로 실행 가능해야 한다.
- shell 명령 문자열을 통해 실행하지 않는다.
- GUI는 Backend executable의 위치를 설치/portable 구조에서 결정할 수 있어야 한다.
- Backend가 직접 GUI 위젯을 생성하거나 GUI process에 Qt object pointer를 전달하지 않는다.

### 4.2 실행 인자

Backend 시작 시 최소한 다음 종류의 정보를 전달할 수 있어야 한다.

- protocol mode/version
- 데이터/index 경로 또는 필요한 실행 context
- 테스트/diagnostic mode가 필요한 경우의 명시적 flag

경로와 설정의 원본은 GUI가 임의로 복제하지 않는다. 실제 authoritative configuration은 기존 설정 모델을 기준으로 하며, Backend에는 현재 실행에 필요한 값만 전달한다.

### 4.3 CLI/headless

CLI/headless semantics는 본 작업의 범위에서 재설계하지 않는다.

- `--help`, `--version`, smoke 및 기존 headless benchmark/scan semantics는 유지한다.
- GUI process isolation을 위해 CLI semantics를 억지로 IPC 경로로 변경하지 않는다.
- 다만 Backend executable 자체가 독립 실행 가능한 구조가 되어야 backend-side integration test를 수행할 수 있다.

---

## 5. GUI ↔ Backend 책임 분리 규칙

다음은 이번 구현에서 반드시 유지할 경계다.

### GUI → Backend

GUI는 "무엇을 할지" 명령한다.

예:

- start scan
- pause
- resume
- cancel
- configure
- shutdown

### Backend → GUI

Backend는 "현재 무슨 일이 일어났는지"와 결과를 보고한다.

예:

- ready
- state
- progress
- listing progress
- fingerprint progress
- match batch
- telemetry
- finished
- failed
- health

### 금지

GUI가 Backend 내부 상태를 직접 읽기 위해:

- SQLite를 동시에 open
- MediaSearchEngine method 직접 호출
- ScanWorker pointer 공유
- QObject signal/slot을 process 경계를 넘어 직접 연결
- Qt object pointer 전달

을 하지 않는다.

---

## 6. IPC 계약

### 6.1 전송 방향

```text
GUI → Backend
    command

Backend → GUI
    event / state / result
```

초기 구현은 **QProcess 기반 child supervision + local structured/line-oriented channel**을 우선한다.

Named Pipe 또는 별도의 IPC framework는 현재 구현의 필수 조건이 아니다. 실제 throughput, framing, reconnect, deadlock 또는 운영상의 한계가 검증될 때만 교체 후보로 올린다.

### 6.2 메시지 envelope

최소 의미 계약은 다음과 같이 유지한다.

```json
{
  "protocol": 1,
  "type": "state",
  "requestId": "...",
  "sequence": 123,
  "payload": {}
}
```

필수 의미:

- `protocol`: IPC protocol revision
- `type`: 메시지 종류
- `requestId`: command와 response/event의 correlation이 필요한 경우 사용
- `sequence`: 현재 연결에서 Backend→GUI event ordering을 확인하기 위한 단조 증가 값
- `payload`: 해당 메시지의 데이터

이것은 DB schema version이나 engine version을 변경하는 것이 아니다.

### 6.3 GUI → Backend 최소 메시지

#### HELLO

Backend protocol 지원 여부와 연결을 확인한다.

#### START_SCAN

검색 시작 요청.

필요한 실제 검색 옵션은 기존 configuration semantics를 그대로 전달한다.

#### PAUSE

현재 실행을 pause한다.

#### RESUME

pause된 작업을 재개한다.

#### CANCEL

현재 search session에 cancel을 요청한다.

#### CONFIGURE

현재 UI에서 선택 가능한 설정을 Backend가 적용한다.

기존 resource strategy semantics를 변경하지 않는다.

#### SHUTDOWN

Backend 정상 종료 요청.

정상 종료에서는 Backend가 shutdown acknowledgment를 보낸 후 종료할 수 있다.

### 6.4 Backend → GUI 최소 메시지

#### HELLO_ACK

protocol 협상 결과.

#### READY

Backend가 검색 명령을 받을 수 있는 상태.

#### STATE

예:

```text
STARTING
READY
SCANNING
PAUSING
PAUSED
CANCELLING
RECOVERING
FAILED
SHUTTING_DOWN
```

실제 enum/string 이름은 구현 시 기존 state 명명과 충돌하지 않도록 조정할 수 있으나 의미는 유지한다.

#### PROGRESS

현재 scan 진행 상태.

#### LISTING_PROGRESS

directory/file enumeration 진행.

#### FINGERPRINT_PROGRESS

fingerprint 단계 진행.

#### MATCHES_BATCH

GUI 표시를 위한 match/group 결과 batch.

대량 결과는 작은 단위의 batch로 분할한다.

#### TELEMETRY

기존 Detailed Logs/diagnostic telemetry semantics를 가능한 한 그대로 전달한다.

#### FINISHED

정상적인 search session 종료.

#### FAILED

Backend 처리 실패 또는 검색 실패.

#### HEALTH

watchdog용 heartbeat/health event.

---

## 7. IPC payload 원칙

### 허용

- path 또는 file identity
- file metadata
- fingerprint
- match/group metadata
- scalar counters
- progress
- timing
- error code/message
- small telemetry
- configuration
- command state

### 금지

- full-resolution image
- raw video frame
- large pixel buffer
- GPU buffer
- WIC/FFmpeg/CUDA native handle
- SQLite handle
- QObject pointer
- QWidget pointer
- 내부 C++ object pointer

원본 media는 항상 Backend/그 내부 processing path가 직접 읽는다.

---

## 8. Match result batch 규칙

GUI가 기존 결과 표시를 유지할 수 있어야 하므로 match 전달은 다음 원칙을 따른다.

1. Backend가 match/group을 내부적으로 완성한다.
2. GUI에는 기존 GUI가 표현하는 데 필요한 compact metadata를 batch로 전달한다.
3. GUI는 image/video file을 다시 전체 decode해서 Backend result를 재구성하지 않는다.
4. thumbnail/preview가 필요하면 기존 GUI rendering path의 책임과 캐시 정책을 유지하되, Backend의 검색 pipeline과 GUI의 화면용 thumbnail은 동일한 native object를 공유하지 않는다.
5. batch가 여러 번 도착해도 sequence/order와 session context를 통해 stale batch를 버릴 수 있어야 한다.

0.9.4에서 정식 taskId/workerInstanceId 기반 결과 protocol을 만드는 것은 범위 밖이다. 그것은 0.9.5의 formal task protocol에서 다룬다.

---

## 9. Backend lifecycle

### 9.1 정상 시작

```text
GUI starts
  ↓
Supervisor creates Backend
  ↓
Backend process starts
  ↓
HELLO / protocol negotiation
  ↓
Backend opens required state
  ↓
READY
  ↓
GUI enables normal search controls
```

Backend가 READY 이전에 검색 명령을 받으면 명시적으로 reject하거나 queue하지 않는다. 0.9.4에서는 **READY 전 command admission을 최소화**하는 것이 안전하다.

### 9.2 정상 종료

```text
GUI requests SHUTDOWN
  ↓
Backend enters SHUTTING_DOWN
  ↓
Backend stops accepting new scan
  ↓
Backend persists durable state under existing semantics
  ↓
Backend acknowledges shutdown
  ↓
process exits
  ↓
GUI clears supervisor state
```

사용자 취소와 애플리케이션 종료를 같은 의미로 취급하지 않는다.

---

## 10. Backend crash / unexpected exit

예상하지 않은 종료는 다음 순서를 갖는다.

```text
Backend crash / unexpected exit
          ↓
QProcess finished / error signal
          ↓
GUI remains alive
          ↓
state = Backend unavailable / recovering
          ↓
disable or guard scan-dependent controls
          ↓
bounded restart policy
          ↓
new Backend process
          ↓
HELLO / READY
          ↓
existing SQLite/index reopen
          ↓
Backend ready
```

중요:

- GUI 자체를 종료하지 않는다.
- 기존 database/index를 지우거나 새 DB를 만들지 않는다.
- crash 당시 in-memory ScanWorker state를 GUI가 추측하여 복원하지 않는다.
- crash 직전의 마지막 file을 무조건 다시 이어서 처리한다고 약속하지 않는다.

---

## 11. Recovery semantics

### 11.1 복구되는 것

- 이미 SQLite에 commit된 index state
- 이미 저장된 match
- 기존 WAL/checkpoint에 의해 durable하게 복구 가능한 상태
- 기존 사용자 설정
- 기존 index/database 파일

### 11.2 0.9.4에서 복구를 보장하지 않는 것

- 정확한 마지막 파일부터 automatic resume
- in-memory queue 복원
- 현재 worker object 상태 복원
- 부분 계산 중이던 GPU buffer 복원
- 새로운 DB transaction replay system
- 새로운 task journal을 만들어 정확한 process resume을 보장하는 기능

### 11.3 재시작 후 scan

Backend가 다시 READY가 된 뒤에는 **자동으로 이전 scan을 재개하지 않는다.**

검색 session이 완전히 완료되지 않은 상태였음을 GUI가 명확하게 표시하고, 사용자가 다시 scan을 시작할 수 있는 상태로 돌아가는 것을 0.9.4 기본 semantics로 한다.

이 결정은 false resume, duplicate processing, stale in-memory state 추정을 방지하기 위한 것이다.

---

## 12. Backend Supervisor

Supervisor는 GUI 내부의 별도 책임 영역이다.

### 책임

- Backend process 생성
- startup timeout 감시
- process exit 감시
- heartbeat 감시
- 정상 shutdown과 unexpected exit 구분
- bounded restart
- restart 중 UI 상태 보호
- 반복 crash 시 FAILED 전환
- restart 이후 READY 확인

### Supervisor가 하지 않는 것

- Backend 내부 DB recovery 직접 수행
- SQLite 파일을 직접 수정
- WIC/FFmpeg/CUDA object 생성
- search algorithm 제어
- crash stack을 해석하여 자동으로 원인을 추정
- 무한 restart

---

## 13. Heartbeat / Watchdog

Process liveness와 health liveness는 분리해서 본다.

### process liveness

QProcess 자체의 process state/finished/error 신호로 판단한다.

### health liveness

Backend가 정상적으로 control path를 유지하고 있음을 HEARTBEAT/HEALTH 메시지로 판단한다.

권장 구조:

```text
Backend control/supervisor context
        ↓
periodic HEALTH
        ↓
GUI watchdog timer
```

heartbeat 생성은 scan hot path 자체에 종속시키지 않는다.

긴 directory enumeration, native I/O, SQLite commit, decoder 작업 때문에 일반 progress event가 잠시 없더라도 이를 곧바로 Backend dead로 판단하지 않는다.

### 초기 구현 기본값

다음 값은 **초기 acceptance를 위한 구현 후보**이며 아키텍처 불변값은 아니다.

- heartbeat period: 약 1초
- 정상 health timeout: 약 10초
- startup READY timeout: 약 15초
- restart attempt: 짧은 bounded sequence
- restart backoff: 즉시 무한 재실행이 아니라 증가하는 backoff

실제 값은 acceptance와 사용 환경에서 false positive가 없는지 검증하여 확정한다.

---

## 14. Restart policy

정상 상태:

```text
READY / RUNNING
      ↓ unexpected exit
RECOVERING
      ↓
RESTARTING
      ↓ success
READY
```

반복 장애:

```text
RESTARTING
   ↓
repeated failure
   ↓
FAILED
```

정책 조건:

- restart는 GUI를 죽이지 않는다.
- restart는 무한 반복하지 않는다.
- 빠른 crash loop로 process/CPU/log가 폭증하지 않는다.
- 사용자가 명시적으로 application shutdown을 한 경우에는 restart하지 않는다.
- restart 성공 여부는 실제 HELLO/READY 수신으로 판정한다.
- 단순히 process handle이 생성된 것만으로 성공 처리하지 않는다.

정확한 retry 횟수와 backoff 숫자는 구현/acceptance에서 검증 후 정한다.

---

## 15. Backend unavailable 상태에서 GUI 동작

Backend가 없을 때 GUI는 살아 있어야 한다.

표시/제어 원칙:

- 기존 결과 화면은 가능한 한 유지한다.
- 새 search 시작은 차단하거나 명확하게 disabled 상태로 만든다.
- Backend-dependent action은 오류를 조용히 무시하지 않는다.
- 사용자에게 Backend restarting / unavailable 상태를 명확하게 표시한다.
- restart 후 READY가 되면 검색 관련 control을 정상 상태로 되돌린다.

GUI crash를 막는 것이 목적이므로, Backend 장애를 GUI에서 "가짜 정상"으로 숨기지 않는다.

---

## 16. SQLite / Index ownership

0.9.4에서 SQLite의 **authoritative writer는 Backend 하나뿐**이다.

```text
GUI
  X direct SQLite write

Backend
  └─ Database / SQLite
       └─ CandidateIndex / match persistence
```

원칙:

- GUI는 DB를 직접 변경하지 않는다.
- Backend restart 후 기존 DB를 reopen한다.
- 기존 schema version을 올리지 않는다.
- 기존 engine version을 올리지 않는다.
- process split을 이유로 DB migration을 만들지 않는다.
- 기존 WAL/checkpoint behavior를 그대로 활용한다.

IndexService를 새로 만드는 작업은 0.9.5 이후의 별도 판단 사항이다.

---

## 17. Monitor ownership in 0.9.4

0.9.4에서는 Monitor는 **Backend 내부에 그대로 유지**한다.

```text
Backend
├─ MediaSearchEngine
├─ ScanWorker
├─ MediaMonitor
└─ SQLite
```

따라서 Monitor가 Backend의 일부이므로 Monitor 자체의 native/runtime failure도 0.9.4에서는 Backend fault boundary 안에 있다.

그러나 GUI에는 그 failure가 직접 전파되지 않는다.

0.9.5에서만 다음으로 분리한다.

```text
MonitorService
       ↓
      Core
```

0.9.4 구현에서 MonitorService, ImageWorker, VideoWorker를 미리 별도 executable로 쪼개지 않는다.

---

## 18. GUI / Backend source migration 전략

### GUI 쪽

목표:

```text
MainWindow
   ↓
BackendClient / Supervisor
   ↓ IPC
Backend
```

기존:

- `ScanWorker`
- `MediaSearchEngine`
- `MediaMonitor`

직접 소유를 제거한다.

GUI는 기존 slot/signal 중심 UI 로직을 가능한 한 유지하되, engine 직접 호출을 Backend command/event adapter로 바꾼다.

### Backend 쪽

새 Backend entry point가 다음을 초기화한다.

1. application/control runtime
2. IPC endpoint
3. MediaSearchEngine / ScanWorker
4. current Monitor
5. Database/index
6. telemetry connection
7. command handler

Backend가 GUI widget code를 link/instantiate하지 않도록 target dependency를 정리한다.

### 테스트

현재 GUI unit/UI tests가 `mainwindow.cpp`와 강하게 결합된 경우:

- 전체 테스트를 무작정 제거하지 않는다.
- GUI는 fake/local BackendClient를 주입할 수 있도록 최소 abstraction을 둔다.
- 실제 process boundary는 별도 backend integration/E2E test에서 검증한다.
- 기존 engine unit tests는 가능한 한 변경하지 않는다.

---

## 19. Process boundary에서 유지해야 할 기존 semantics

이번 구조 변경으로 다음을 변경하지 않는다.

- fingerprint algorithm
- mirror fingerprint
- crop fingerprint
- similarity thresholds
- candidate index semantics
- image sampling semantics
- video sampling semantics
- video comparison semantics
- CPU fallback
- GPU ON/OFF semantics
- adaptive scheduler semantics
- Detailed Logs semantics
- SQLite schema
- engine version
- database version
- benchmark schema
- exactness policy
- XMP orientation semantics
- color_thumb semantics

변경되는 것은 **실행 위치와 장애 경계**다.

---

## 20. Error classification

0.9.4에서는 다음을 구분한다.

### Clean shutdown

GUI가 명시적으로 SHUTDOWN을 요청했고 Backend가 정상 종료.

### Controlled failure

Backend가 FAILED 메시지를 보내고 process는 살아 있거나 정상 종료 절차로 끝남.

### Unexpected process exit

Backend가 READY/RUNNING 상태에서 종료했으나 정상 shutdown sequence가 아니었음.

### Startup failure

Backend가 시작됐으나 READY를 일정 시간 안에 만들지 못함.

### Watchdog timeout

process는 살아 있으나 health protocol이 timeout을 초과함.

### Restart exhausted

bounded restart 정책의 조건을 모두 소진함.

최종적으로 GUI state는 `FAILED / Backend unavailable`로 전환한다.

---

## 21. Logging / diagnostics 원칙

GUI와 Backend 로그는 역할을 분리한다.

### GUI log

- Supervisor state
- IPC connection state
- restart reason
- restart count
- user command failure
- Backend unavailable state

### Backend log

- search engine diagnostics
- decoder/runtime failure
- database error
- telemetry
- scan session state

Backend stderr/stdout은 GUI가 별도 channel로 수집할 수 있게 한다.

대량 binary payload를 로그로 흘리지 않는다.

이번 문서 변경의 목적은 crash 원인을 새로 포장하는 것이 아니라 **crash가 발생한 process boundary를 명확하게 관측**할 수 있게 하는 것이다.

---

## 22. 보안/안전한 실행 방식

로컬 child process이므로 다음 규칙을 유지한다.

- shell command 문자열 조립으로 Backend를 실행하지 않는다.
- executable path와 argument는 구조화된 방식으로 전달한다.
- IPC framing은 명시적이어야 한다.
- malformed message는 Backend를 crash시키지 않고 reject한다.
- unknown message type은 무시 또는 명시적 error response로 처리한다.
- message length에는 합리적인 상한을 둔다.
- raw media data를 IPC로 받는 확장을 허용하지 않는다.

이번 단계에서 인증/네트워크 공개 endpoint/remote worker는 만들지 않는다.

---

## 23. Acceptance gate

### A. 구조

- [ ] GUI와 Backend가 실제 서로 다른 OS process다.
- [ ] GUI가 `MediaSearchEngine`, `ScanWorker`, `MediaMonitor`를 직접 소유하지 않는다.
- [ ] SQLite authoritative access가 Backend에만 있다.
- [ ] Backend가 GUI widget을 직접 참조하지 않는다.

### B. 정상 실행

- [ ] GUI 시작
- [ ] Backend spawn
- [ ] HELLO / HELLO_ACK
- [ ] READY
- [ ] 기존 search 실행
- [ ] progress / matches / telemetry 표시
- [ ] FINISHED
- [ ] 정상 종료

### C. 제어

- [ ] pause
- [ ] resume
- [ ] cancel
- [ ] configure
- [ ] shutdown

### D. 장애 격리

- [ ] Backend 강제 종료
- [ ] GUI가 살아 있음
- [ ] GUI가 Backend 종료를 감지
- [ ] Backend unavailable 상태 표시
- [ ] Backend만 restart
- [ ] 새 Backend가 READY
- [ ] 기존 SQLite/index reopen
- [ ] commit된 state 유지

### E. 반복 장애

- [ ] Backend 연속 crash를 주입
- [ ] 무한 restart loop 없음
- [ ] bounded retry 후 FAILED
- [ ] GUI는 계속 살아 있음

### F. Health

- [ ] heartbeat 정상 전달
- [ ] false timeout이 긴 scan/native I/O에서 발생하지 않음
- [ ] process death와 heartbeat timeout을 구분

### G. Correctness

- [ ] CPU CTest regression 확인
- [ ] GPU CTest regression 확인
- [ ] 기존 Search/Index/Comparison semantics unchanged
- [ ] DB/schema/engine/benchmark version unchanged

### H. 실제 OS process 증명

다음은 반드시 실제 운영체제 수준에서 확인한다.

```text
GUI PID != Backend PID
Backend forced termination
GUI PID remains alive
Backend new PID appears after restart
```

단순히 Qt thread ID가 다른 것은 이 acceptance의 증거가 아니다.

---

## 24. 구현 중 금지하는 범위 확장

이번 0.9.4 작업 중 다음을 임의로 추가하지 않는다.

- ImageWorker.exe
- VideoWorker.exe
- MonitorService.exe
- IndexService
- LogService
- remote worker / network service
- taskId/workerInstanceId 기반 0.9.5 formal task protocol
- global GPU lease
- multi-process CUDA optimization
- search semantics rewrite
- DB migration
- GUI benchmark 재설계
- crash root cause를 해결했다고 단정하는 문서 변경

또한 process split을 이유로 기존 rejected/failed experiment 기록을 삭제하지 않는다.

---

## 25. 0.9.5로 넘기는 것

다음은 구현하지 않고 0.9.5 설계로 유지한다.

```text
ImageWorker
VideoWorker
MonitorService
Core process naming / formal coordinator
worker-level restart
sessionId/taskId/sequence/attempt protocol
global GPU lease
multi-process CUDA context measurement
worker queue migration
formal result aggregation protocol
Monitor reconnect/replay queue
candidate index service split
```

0.9.4는 그 구조를 방해하지 않는 최소 경계만 만든다.

---

## 26. 구현 판단의 우선순위

OpenCode가 구현 중 설계 충돌을 발견하면 다음 우선순위를 따른다.

1. 이 문서의 process ownership / fault boundary 규칙
2. `process-architecture-0.9.4`의 상위 구조 결정
3. 기존 Search/Index/Comparison semantics
4. 기존 테스트와 durable storage semantics
5. 기존 코드의 구현 세부

기존 코드가 새로운 process ownership과 충돌하는 경우 **현재 코드 모양을 보존하기 위해 architecture boundary를 포기하지 않는다.**

반대로, 구현을 단순화하기 위해 IPC protocol 또는 recovery semantics를 임의로 축소해서도 안 된다.

---

## 27. 설계 완료 기준

이 문서만 읽고도 다음 질문에 모호함이 없어야 한다.

- 누가 ScanWorker를 소유하는가?
- 누가 MediaMonitor를 소유하는가?
- 누가 SQLite를 쓰는가?
- GUI는 Backend와 어떻게 통신하는가?
- Backend가 죽으면 GUI는 무엇을 하는가?
- Backend를 몇 번이나 자동 재시작할 수 있는가?
- crash 후 무엇을 복구하고 무엇을 복구하지 않는가?
- raw frame을 IPC로 보내는가?
- 0.9.4에서 Image/Video/Monitor를 별도 process로 만드는가?
- 0.9.5로 무엇을 미루는가?
- 이 변경이 search semantics를 바꾸는가?

이 질문에 대한 답이 모두 문서와 일치해야 0.9.4 구현을 시작할 수 있다.

---

## 28. 문서 역할

`process-architecture-0.9.4.ko.md`는 **상위 아키텍처와 범위**를 정의한다.

본 문서는 그 결정을 **구현 계약 수준으로 구체화**한다.

실제 source code는 이 두 문서의 계약을 만족하는 범위에서 구현 세부를 선택한다.

이 문서는 0.9.4.x 구현 중 발견되는 중요한 설계 변경 또는 acceptance 결과에 따라 갱신할 수 있으나, 과거 실행 기록을 소급 변경하지 않고 변경 사유와 검증 결과를 별도 worklog/build-history에 남긴다.
