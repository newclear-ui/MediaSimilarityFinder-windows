# 0.9.5 Process Decomposition and Core Supervisor Design

## Status

- Target: 0.9.5.x
- Status: planned major architecture
- Prerequisite: 0.9.4.x GUI↔Backend isolation plus stability/correctness acceptance
- Purpose: promote Image/Video/Monitor into separate processes and make Core the central scheduler, resource manager, result aggregator, and index authority.
- Index Writer is not planned as a separate process by default.

## 1. Problems addressed

The 0.9.4 two-process design protects the GUI, but Image, Video, Monitor, and Index still share the Backend process.

Therefore:
- Image native failure can interrupt Video.
- Video decoder failure can interrupt Image.
- Monitor failure can affect searches.
- Image/Video throughput and failure telemetry remain coupled.
- Recovering one workload requires restarting the whole Backend.

0.9.5 adds only the boundaries with clear fault-isolation and optimization value.

## 2. Target structure

    GUI process
         │
         ▼
    Core process
    Coordinator / Scheduler / Index Authority / Result Aggregator
         │                 │
         ▼                 ▼
    ImageWorker         VideoWorker

    MonitorService ─────────→ Core

Roles:
- GUI: presentation and top-level supervision
- Core: scheduling, resource, task, result aggregation, index authority
- ImageWorker: WIC/image decode/hash/verify/CUDA
- VideoWorker: FFmpeg/decode/sampling/verify/CUDA
- MonitorService: filesystem events/resync/pending queue

LogService and IndexService are out of initial scope.

## 3. Core responsibility

Core is a stateful coordinator, not a dumb broker.

Core owns:
- sessions
- tasks
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

Core decides:
- task-to-worker routing
- admission/backpressure
- CPU/GPU allocation
- retry after worker failure
- result-batch commit timing

## 4. ImageWorker

Owns:
- WIC/image decoder
- image decode
- orientation
- crop/resize
- fingerprint
- image verification
- image-side CUDA
- image telemetry

Core receives compact result data only:
- taskId
- path/file identity
- fingerprint
- mirror fingerprint
- status
- timing
- error

Decoded image buffers never cross IPC.

## 5. VideoWorker

Owns:
- FFmpeg/native decoding
- frame sampling
- video fingerprint
- frame verification
- temporal verification
- video-side CUDA
- video telemetry

Raw frames never cross the Core IPC boundary.

## 6. MonitorService

Becomes an independent process.

Responsibilities:
- current watcher mechanism / ReadDirectoryChangesW
- created/modified/deleted events
- stable-file detection
- watcher resync
- bounded pending event queue while Core is unavailable
- replay/resync after reconnect

It does not own authoritative SQLite writes, final match aggregation, or global scheduling.

## 7. Index Authority

Baseline:

    ImageWorker ─┐
                 ├─ result → Core → SQLite
    VideoWorker ─┘
    Monitor ───────────→ Core

Keeping SQLite ordering and checkpoint semantics in one place is preferred.

Reconsider a separate IndexService only when measurements demonstrate a clear DB bottleneck, Core resource pressure, or measurable isolation benefit.

## 8. Task identity

Formal task identity crosses process boundaries.

Minimum recommended:
- sessionId
- taskId
- sequence
- workerInstanceId
- taskType
- state
- attempt

Example states:
- QUEUED
- ASSIGNED
- RUNNING
- RESULT_RECEIVED
- COMMITTED
- FAILED
- CANCELLED
- RETRY_WAIT

This must make crash, duplicate result, delayed result, stale result, retry, and cancellation deterministic.

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

VideoWorker uses the same rule.

Already committed tasks must not be committed again. Task identity and commit state reject stale/duplicate results.

## 10. Core failure

Core is also a separate OS process, so a Core crash must not terminate GUI.

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

New Core instances invalidate stale worker connections and establish new worker instance identity.

## 11. GPU resource management

If ImageWorker and VideoWorker each initialize CUDA, multiple CUDA contexts and additional VRAM/resource overhead may occur.

GPU is therefore a Core-managed global resource.

    Core
      └─ GPU resource scheduler
           ├─ ImageWorker lease
           └─ VideoWorker lease

Possible policies:
- Image priority
- Video priority
- constrained concurrency
- one worker GPU-active while the other uses CPU fallback
- global GPU off / CPU-only

Fixed 50:50 is not the default.

Policy is selected from measurements of throughput, GPU duty, VRAM, context overhead, CPU load, transfer cost, queue wait, and user responsiveness.

## 12. CPU/I/O management

Core observes system-wide pressure.

Examples:
- CPU saturated → reduce worker admission
- NAS I/O saturated → reduce read/decode concurrency
- Image queue large / Video queue small → increase Image admission
- expensive Video temporal verification → reduce Video concurrency

The existing scheduler principles are extended, not discarded.

## 13. IPC rules

Large media data never crosses IPC.

Allowed:
- commands
- task metadata
- compact fingerprints
- status
- small telemetry
- result batches

Forbidden:
- full-resolution decoded image
- raw video frame
- large pixel buffer
- GPU buffer
- DB connection object
- Qt object pointer

## 14. Independent telemetry

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

The purpose is independent bottleneck measurement, not maximizing utilization percentage.

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

Monitor does not directly create final GUI results.

## 16. Startup/shutdown

Startup:

    GUI
     ↓
    Core ready
     ↓
    ImageWorker / VideoWorker ready
     ↓
    MonitorService ready
     ↓
    operational

Shutdown:

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

Emergency termination may favor containment over graceful drain.

## 17. Migration from 0.9.4

0.9.5 is not a search-semantics rewrite.

Preserve:
- existing DB format unless separately approved
- fingerprint/search semantics
- CPU fallback
- exactness rules
- resource policy principles
- telemetry terminology

Change:
- process boundaries
- supervision
- task identity
- worker lifecycle
- process-aware resource management

## 18. Acceptance

1. GUI and Core are independent processes.
2. ImageWorker and VideoWorker are independent processes.
3. ImageWorker crash does not terminate VideoWorker.
4. VideoWorker crash does not terminate ImageWorker.
5. Core detects worker failure and restarts only the failed worker.
6. Core crash does not terminate GUI.
7. Committed index state survives process failures.
8. Stale/duplicate results cannot corrupt index correctness.
9. MonitorService crash does not terminate GUI/Core.
10. Monitor events can replay/resync after Core restart.
11. GPU policy is based on real CUDA/VRAM/throughput measurements.
12. Image/Video telemetry independently identifies throughput and bottlenecks.
13. Raw media frames never cross IPC.
14. Personal-PC memory/process footprint and throughput are empirically acceptable.

## 19. Non-goals

- IndexService
- LogService
- remote/distributed workers
- network service architecture
- microservice framework
- raw media IPC
- fixed 50:50 GPU partition
- search-semantics rewrite

## 20. Final principles

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

Principles:
1. Processes create fault boundaries.
2. Core controls work and resources.
3. Index remains a single authority owned by Core.

Additional boundaries require operational evidence, not architectural aesthetics.
