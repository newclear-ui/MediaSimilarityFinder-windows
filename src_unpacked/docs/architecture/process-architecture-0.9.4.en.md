# 0.9.4 Frontend/Backend Process Isolation Design

## Status

- Target: 0.9.4.x
- Status: approved implementation design
- Scope: minimum two-process separation, GUI and Backend
- Image/Video/Monitor decomposition is deferred to 0.9.5.x
- Index Writer/SQLite remains inside Backend

## 1. Why this changes

QThread, std::async, and worker threads still execute inside the same MediaSimilarityFinder.exe process. A WIC/COM, FFmpeg, CUDA, heap, CRT abort/terminate, or other native/runtime failure can therefore terminate the GUI too.

The 0.9.4 goal is to contain those failures at the Backend process boundary without changing search semantics.

## 2. Current structure

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

Thread/object separation is not an OS process boundary.

## 3. Target structure

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

Decisions:
- GUI and Backend are separate OS processes.
- GUI does not own direct backend objects.
- Existing search/index semantics remain unchanged.
- Image and Video remain together in Backend in 0.9.4.x.
- Monitor remains in Backend in 0.9.4.x.
- No separate IndexService.
- No schema/engine/database version bump for this architecture change.

## 4. GUI responsibilities

The GUI owns user input, result presentation, Detailed Logs, resource strategy, and Backend status.

It also acts as a minimal supervisor:
- start Backend
- perform handshake
- detect child exit
- monitor health heartbeat
- restart Backend under a bounded policy
- protect search controls while Backend is unavailable
- stop restart loops after repeated crashes

The GUI does not reconstruct WIC, FFmpeg, CUDA, or SQLite objects.

## 5. Backend responsibilities

Backend continues to own the existing search behavior:
- search session
- ScanWorker
- MediaSearchEngine
- directory walking
- image/video processing
- candidate index
- match aggregation
- SQLite/index writes
- current scheduler
- current Monitor
- telemetry/diagnostics

This is an execution-boundary change, not a search-engine rewrite.

## 6. Minimal IPC

No generic broker/service bus is introduced.

GUI to Backend:
- hello / protocol
- start_scan
- pause
- resume
- cancel
- configure
- shutdown

Backend to GUI:
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

Large result streams use batches. Raw image/video frames and large pixel buffers never cross IPC.

The first implementation should prefer QProcess-managed child supervision plus a local structured/line-oriented channel. Named Pipes or a dedicated IPC framework require evidence before introduction.

## 7. Lifecycle

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
    Backend unavailable
       ↓
    bounded restart
       ↓
    reopen existing DB/index
       ↓
    ready

## 8. Recovery semantics

Backend restart is the process-level recovery unit.

Preserve:
- committed SQLite/index state
- committed matches
- durable WAL/checkpoint state already available
- settings

Do not invent:
- arbitrary in-memory task restoration
- object-level worker restoration
- a new DB recovery protocol
- guaranteed resume from exactly the last file

An incomplete item may be reconsidered by existing scanner/index semantics on the next safe processing cycle.

## 9. Heartbeat and watchdog

Backend sends health heartbeat in addition to process liveness.

Possible fields:
- session/state
- scan phase
- walked/listed
- last progress timestamp
- optional current path/status

The watchdog uses a sufficient grace period for long enumeration, native I/O, and DB phases.

## 10. Restart policy

Conceptual lifecycle:

    RUNNING
       ↓ crash
    RESTARTING
       ↓ success
    RUNNING

    RESTARTING
       ↓ repeated failure
    FAILED

Exact retry/backoff values are implementation and acceptance decisions.

## 11. Correctness preservation

Process isolation must not change:
- fingerprint algorithms
- similarity thresholds
- candidate semantics
- image/video sampling
- DB schema
- engine version
- CPU fallback
- GPU scheduler semantics

## 12. Acceptance

1. GUI → Backend start → handshake → search is behaviorally equivalent.
2. Clean and abnormal Backend exits are distinguishable.
3. A forced Backend crash leaves the GUI alive.
4. GUI restarts only Backend.
5. Restarted Backend reopens existing SQLite/index.
6. State committed before crash remains intact.
7. Search controls are safely restricted while Backend restarts.
8. Repeated crashes do not create an infinite restart loop.
9. CPU/GPU regression coverage remains valid.
10. Crash injection proves GUI and Backend are separate OS processes.

## 13. Deferred to 0.9.5

- ImageWorker.exe
- VideoWorker.exe
- MonitorService.exe
- worker-level restart
- formal task/session/sequence protocol
- global GPU lease
- multi-process CUDA measurement
- worker queue migration
- formal result aggregation protocol
- IndexService
- LogService
