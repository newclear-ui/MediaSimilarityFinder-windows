# 0.9.4 프론트엔드/백엔드 프로세스 격리 설계

## 상태

- 대상: 0.9.4.x
- 상태: 구현 예정 설계 승인
- 범위: GUI 프로세스와 Backend 프로세스의 최소 2프로세스 분리
- Image/Video/Monitor의 추가 프로세스 분리는 0.9.5.x로 이관
- Index Writer/SQLite는 Backend 내부에 유지

## 1. 변경 필요성

현재 QThread, std::async, worker thread는 모두 동일한 MediaSimilarityFinder.exe 안에서 실행된다. 따라서 WIC/COM, FFmpeg, CUDA, heap, CRT abort/terminate 등 프로세스 수준 native/runtime failure가 발생하면 GUI까지 같이 종료될 수 있다.

0.9.4.x의 목표는 failure의 원인을 숨기는 것이 아니라 Backend 장애가 GUI 프로세스까지 전파되지 않게 하는 최소 실행 경계를 만드는 것이다.

## 2. 기존 구조

    MediaSimilarityFinder.exe
    ├─ MainWindow / GUI
    ├─ ScanWorker
    │   └─ MediaSearchEngine
    │       ├─ scheduler / resource policy
    │       ├─ directory walk
    │       ├─ image pipeline
    │       ├─ video pipeline
    │       ├─ candidate index
    │       ├─ matching / aggregation
    │       └─ Database / SQLite
    └─ MediaMonitor

Thread/object 분리는 OS 프로세스 분리가 아니다.

## 3. 0.9.4 목표 구조

    GUI process
    ├─ MainWindow / presentation
    ├─ GUI state
    └─ Backend Supervisor / Watchdog
             │ local IPC
             ▼
    Backend process
    ├─ Core / search orchestration
    ├─ ScanWorker
    ├─ MediaSearchEngine
    ├─ Image processing
    ├─ Video processing
    ├─ Scheduler / resource policy
    ├─ CandidateIndex
    ├─ Database / SQLite
    ├─ Match aggregation
    └─ current Monitor

핵심 결정:
- GUI와 Backend는 서로 다른 OS 프로세스다.
- GUI는 Backend 내부 객체를 직접 소유하거나 참조하지 않는다.
- 기존 search/index semantics는 유지한다.
- 0.9.4.x에서는 Image와 Video를 다시 분리하지 않는다.
- 0.9.4.x에서는 Monitor도 Backend 안에 둔다.
- Index Writer를 별도 프로세스로 만들지 않는다.
- schema/engine/database version bump를 하지 않는다.

## 4. GUI 책임

GUI는 사용자 입력, 결과 표시, Detailed Logs, resource strategy, Backend 상태 표시를 담당한다.

또한 최소 supervisor 역할을 가진다.
- Backend 시작
- handshake 확인
- child process 종료 감시
- heartbeat/health 감시
- 제한된 자동 재시작
- Backend unavailable 상태에서 검색 UI 보호
- 반복 crash 시 restart loop 중단 및 failure 표시

GUI는 WIC, FFmpeg, CUDA, SQLite 객체를 직접 복구하지 않는다.

## 5. Backend 책임

Backend는 기존 검색 기능을 최대한 그대로 소유한다.
- Search session
- ScanWorker
- MediaSearchEngine
- directory walk
- image/video processing
- candidate index
- match aggregation
- SQLite/index write
- current resource scheduler
- current Monitor
- telemetry/diagnostics

즉 0.9.4는 검색 엔진 재작성보다 현재 backend 실행 범위를 별도 process로 이동하는 작업이다.

## 6. 최소 IPC

처음부터 broker/service bus를 도입하지 않는다.

GUI -> Backend:
- hello / protocol
- start_scan
- pause
- resume
- cancel
- configure
- shutdown

Backend -> GUI:
- hello_ack
- ready
- state
- progress
- listing_progress
- fingerprint_progress
- matches_batch
- telemetry
- finished
- failed
- health

대량 match는 batch로 보낸다. raw image/video frame과 대형 pixel buffer는 IPC로 보내지 않는다.

첫 구현의 우선 후보는 QProcess 기반 child-process supervision과 local structured/line-oriented channel이다. Named Pipe 또는 별도 IPC framework는 실제 필요성이 입증될 때만 도입한다.

## 7. 수명주기

    GUI start
       ↓
    spawn Backend
       ↓
    hello / protocol negotiation
       ↓
    Backend ready
       ↓
    commands / events

Backend failure:

    Backend crash/exit
       ↓
    GUI detects exit
       ↓
    GUI remains alive
       ↓
    state = Backend unavailable
       ↓
    bounded restart policy
       ↓
    Backend restart
       ↓
    reopen existing DB/index
       ↓
    ready

## 8. 복구 의미

0.9.4에서 Backend 재시작은 process-level recovery unit이다.

보존:
- 이미 commit된 SQLite/index
- 이미 저장된 match
- 기존 WAL/checkpoint로 복구 가능한 durable state
- 설정

새로 만들지 않음:
- 임의 in-memory task restoration
- object-level worker restoration
- 새로운 DB recovery protocol
- 정확히 마지막 파일부터 무조건 재개한다는 보장

완전히 처리되지 않은 항목은 기존 scanner/index semantics에 따라 다음 안전한 처리에서 다시 판단될 수 있다.

## 9. Heartbeat와 watchdog

Backend는 process-alive와 별도로 health heartbeat를 보낸다.

가능한 값:
- session/state
- scan phase
- walked/listed
- last progress timestamp
- optional current path/status

watchdog timeout은 충분한 grace period를 사용한다. 긴 directory enumeration, native I/O, DB 작업에서 정상적으로 event 빈도가 낮아질 수 있기 때문이다.

## 10. Restart policy

    RUNNING
       ↓ crash
    RESTARTING
       ↓ success
    RUNNING

    RESTARTING
       ↓ repeated failure
    FAILED

정확한 retry count/backoff는 구현/acceptance 단계에서 결정한다.

목표:
- 단일 crash가 GUI를 종료시키지 않는다.
- 반복 crash도 GUI를 종료시키지 않는다.
- 빠른 restart loop로 process/CPU/log가 폭증하지 않는다.

## 11. 정확성 보존

Process isolation은 search semantics를 변경하지 않는다.

변경 금지:
- fingerprint algorithm
- similarity threshold
- candidate semantics
- image/video sampling semantics
- DB schema
- engine version
- CPU fallback
- GPU scheduler semantics

## 12. 0.9.4 acceptance

1. GUI → Backend start → handshake → search가 기존 제품과 동등하게 동작한다.
2. Backend 정상 종료와 비정상 종료가 구분된다.
3. Backend 강제 종료 테스트에서도 GUI가 계속 살아 있다.
4. GUI가 Backend 종료를 감지하여 Backend만 재시작한다.
5. 재시작 Backend가 기존 SQLite/index를 다시 연다.
6. crash 직전까지 commit된 state가 유지된다.
7. Backend 재시작 중 검색 UI가 안전하게 제한된다.
8. 반복 crash가 무한 restart loop를 만들지 않는다.
9. CPU/GPU regression coverage가 유지된다.
10. GUI와 Backend가 실제로 다른 OS process임을 crash-injection으로 증명한다.

## 13. 0.9.5로 이관

- ImageWorker.exe
- VideoWorker.exe
- MonitorService.exe
- worker 단위 restart
- formal task/session/sequence protocol
- global GPU lease
- multi-process CUDA measurement
- worker queue migration
- formal result aggregation protocol
- IndexService
- LogService
