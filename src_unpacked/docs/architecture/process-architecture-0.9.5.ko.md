# 0.9.5 프로세스 세분화 및 Core Supervisor 설계

## 상태

- 대상: 0.9.5.x
- 상태: 주요 예정 아키텍처
- 선행 조건: 0.9.4.x GUI↔Backend 격리와 안정성/정확성 acceptance
- 목적: Image/Video/Monitor를 독립 프로세스로 승격하고 Core를 작업·자원·결과·인덱스의 중앙 권한으로 만든다.
- Index Writer는 기본적으로 별도 프로세스로 만들지 않는다.

## 1. 해결할 문제

0.9.4의 2프로세스 구조는 GUI를 보호하지만 Backend 내부에서는 Image, Video, Monitor, Index가 같은 프로세스에 있다.

따라서:
- Image native failure가 Video까지 중단시킬 수 있다.
- Video decoder failure가 Image까지 중단시킬 수 있다.
- Monitor failure가 검색 프로세스에 영향을 줄 수 있다.
- Image/Video throughput과 failure telemetry를 독립적으로 측정하기 어렵다.
- 한 작업만 복구하려 해도 Backend 전체 restart가 필요하다.

0.9.5는 기능마다 process를 만드는 것이 아니라 장애 격리와 최적화 가치가 큰 경계만 추가한다.

## 2. 목표 프로세스

    GUI process
         │
         ▼
    Core process
    Coordinator / Scheduler / Index Authority / Result Aggregator
         │                 │
         ▼                 ▼
    ImageWorker         VideoWorker

    MonitorService ─────────→ Core

주요 역할:
- GUI: presentation + top-level supervision
- Core: scheduling, resource, task, aggregation, index authority
- ImageWorker: WIC/image decode/hash/verify/CUDA
- VideoWorker: FFmpeg/decode/sampling/verify/CUDA
- MonitorService: filesystem events/resync/pending queue

LogService와 IndexService는 초기 범위에 넣지 않는다.

## 3. Core 역할

Core는 단순 broker가 아니라 상태를 가진 coordinator다.

소유:
- session
- task
- queues
- worker state
- resource state
- Image/Video workload state
- result aggregation
- match aggregation
- checkpoint state
- database/index authority
- worker restart state
- process-aware telemetry

결정:
- task-to-worker routing
- worker admission/backpressure
- CPU/GPU allocation
- failure 후 retry
- result-batch commit timing

## 4. ImageWorker

책임:
- WIC/image decoder
- image decode
- orientation
- crop/resize
- fingerprint
- image verification
- image-side CUDA
- image telemetry

Core에는 compact result만 전달:
- taskId
- path/file identity
- fingerprint
- mirror fingerprint
- status
- timing
- error

decoded image buffer는 Core로 보내지 않는다.

## 5. VideoWorker

책임:
- FFmpeg/native decoding
- frame sampling
- video fingerprint
- frame verification
- temporal verification
- video-side CUDA
- video telemetry

raw frame은 Core로 전달하지 않는다.

## 6. MonitorService

독립 process가 되며 다음을 담당한다.
- current watcher mechanism / ReadDirectoryChangesW
- created/modified/deleted events
- stable-file detection
- watcher resync
- Core unavailable 상태에서 bounded event queue
- Core reconnect 후 replay/resync

MonitorService는 SQLite authoritative write, final match aggregation, global scheduling을 담당하지 않는다.

## 7. Index Authority

기본 구조:

    ImageWorker ─┐
                 ├─ result → Core → SQLite
    VideoWorker ─┘
    Monitor ───────────→ Core

SQLite write ordering과 checkpoint semantics를 Core 한 곳에서 유지한다.

IndexService는 실제 운영 측정에서 DB 병목이나 Core resource pressure, 또는 독립 process의 measurable benefit이 증명될 때만 재검토한다.

## 8. Task identity

process boundary를 넘는 task identity를 정식화한다.

최소:
- sessionId
- taskId
- sequence
- workerInstanceId
- taskType
- state
- attempt

예시 state:
- QUEUED
- ASSIGNED
- RUNNING
- RESULT_RECEIVED
- COMMITTED
- FAILED
- CANCELLED
- RETRY_WAIT

이 정보로 crash, duplicate result, delayed/stale result, retry, cancellation을 deterministic하게 처리한다.

## 9. Worker failure

ImageWorker crash:

    ImageWorker crash
          ↓
    Core detects worker loss
          ↓
    in-flight image tasks = interrupted
          ↓
    restart ImageWorker
          ↓
    retry safe tasks
          ↓
    VideoWorker continues

VideoWorker는 같은 원칙을 적용한다.

이미 commit된 task는 다시 commit하지 않는다. Core는 task identity와 commit state로 duplicate/stale result를 거부한다.

## 10. Core failure

Core도 별도 process이므로 GUI와 함께 죽지 않아야 한다.

    Core crash
       ↓
    GUI alive
       ↓
    GUI supervisor detects failure
       ↓
    Core restart
       ↓
    SQLite/index reopen
       ↓
    candidate index rebuild if needed
       ↓
    Monitor reconnect/replay
       ↓
    worker lifecycle recovery

새 Core instance는 stale worker connection을 무효화하고 새 worker instance identity를 사용한다.

## 11. GPU resource management

ImageWorker와 VideoWorker가 각각 CUDA를 초기화하면 CUDA context와 VRAM/resource overhead가 증가할 수 있다.

GPU는 Core가 관리하는 전역 자원으로 취급한다.

    Core
      └─ GPU resource scheduler
           ├─ ImageWorker lease
           └─ VideoWorker lease

정책 후보:
- Image priority
- Video priority
- constrained concurrency
- 한쪽 GPU active / 다른 쪽 CPU fallback
- global GPU off / CPU-only

고정 50:50을 기본으로 하지 않는다.

실제 정책은 throughput, GPU duty, VRAM, context overhead, CPU load, transfer cost, queue wait, user responsiveness를 측정한 뒤 결정한다.

## 12. CPU/I/O 관리

Core는 전체 CPU/I/O 압력을 관찰한다.

예:
- CPU saturated → worker admission 감소
- NAS I/O saturated → read/decode concurrency 감소
- Image queue large / Video queue small → Image admission 증가
- Video temporal verification expensive → Video concurrency 보수화

기존 scheduler 원칙은 유지하고 process-aware telemetry를 확장한다.

## 13. IPC 원칙

대형 media data는 IPC로 보내지 않는다.

허용:
- command
- task metadata
- compact fingerprint
- status
- small telemetry
- result batches

금지:
- full-resolution decoded image
- raw video frame
- large pixel buffer
- GPU buffer
- DB connection object
- Qt object pointer

## 14. 독립 telemetry

Image:
- queue wait
- decode
- crop/resize
- fingerprint
- verify
- CUDA H2D/kernel/D2H
- files/s
- megapixels/s
- failures/retries
- worker uptime

Video:
- queue wait
- FFmpeg decode
- decoded/sampled frames
- sampling
- fingerprint
- temporal verify
- CUDA timings
- frames/s
- videos/s
- failures/retries

목적은 utilization 숫자를 최대화하는 것이 아니라 병목과 효율을 독립적으로 측정하는 것이다.

## 15. Monitor event flow

    MonitorService
         ↓ file event
    Core
         ↓ classify/admission
       ├─ ImageWorker
       └─ VideoWorker
         ↓ result
       Index
         ↓ event/update
        GUI

Monitor는 최종 GUI 결과를 직접 만들지 않는다.

## 16. 시작/종료

시작:

    GUI
     ↓
    Core ready
     ↓
    ImageWorker / VideoWorker ready
     ↓
    MonitorService ready
     ↓
    operational

종료:

    GUI requests shutdown
     ↓
    Monitor stops new events
     ↓
    Core stops admission
     ↓
    workers drain/cancel by session policy
     ↓
    Core commits durable state
     ↓
    Core/workers close
     ↓
    GUI exits

긴급 종료에서는 graceful drain보다 containment를 우선할 수 있다.

## 17. 0.9.4 → 0.9.5 migration

0.9.5는 search semantics rewrite가 아니다.

유지:
- existing DB format unless separately approved
- fingerprint/search semantics
- CPU fallback
- exactness rules
- resource policy principles
- telemetry terminology

변경:
- process boundaries
- supervision
- task identity
- worker lifecycle
- process-aware resource management

## 18. 0.9.5 acceptance

1. GUI와 Core가 독립 process다.
2. ImageWorker와 VideoWorker가 독립 process다.
3. ImageWorker crash가 VideoWorker를 종료시키지 않는다.
4. VideoWorker crash가 ImageWorker를 종료시키지 않는다.
5. Core가 worker failure를 감지하고 failed worker만 재시작한다.
6. Core crash가 GUI를 종료시키지 않는다.
7. commit된 index가 process failure를 견딘다.
8. stale/duplicate result가 index correctness를 손상시키지 않는다.
9. MonitorService crash가 GUI/Core를 종료시키지 않는다.
10. Core restart 후 Monitor event replay/resync가 가능하다.
11. GPU policy가 실제 CUDA/VRAM/throughput 측정에 근거한다.
12. Image/Video telemetry로 독립 throughput/bottleneck 분석이 가능하다.
13. raw media frame이 IPC를 통과하지 않는다.
14. 개인 PC에서 memory/process footprint와 throughput이 허용 범위인지 실측한다.

## 19. 비목표

- IndexService
- LogService
- remote/distributed workers
- network service architecture
- microservice framework
- raw media IPC
- fixed 50:50 GPU partition
- search semantics rewrite

## 20. 최종 원칙

0.9.4:
    GUI process
         ↕
    Backend process

0.9.5:
    GUI
      ↕
    Core / Index Authority
      ↙       ↓       ↘
    Image   Video   Monitor
    Worker  Worker  Service

원칙:
1. 프로세스는 장애 경계를 만든다.
2. Core는 작업과 자원을 통제한다.
3. Index는 Core의 단일 권한으로 유지한다.

추가 process boundary는 실제 운영 증거가 있을 때만 도입한다.
