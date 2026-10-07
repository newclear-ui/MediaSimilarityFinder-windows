# 0.9.4 Backend Process Isolation Implementation Brief

## Status

- Target: 0.9.4.x
- Document role: implementation brief that makes the 0.9.4 process architecture concrete enough for implementation
- Status: implementation baseline
- Higher-level authority: `docs/architecture/process-architecture-0.9.4.en.md`
- Related future design: `docs/architecture/process-architecture-0.9.5.en.md`
- Implementation agent: OpenCode
- This document does not freeze every implementation detail. It freezes the **process boundaries, ownership, IPC meaning, failure recovery, non-goals, and acceptance criteria**.

---

## 1. Purpose and scope

The largest structural change in 0.9.4.x is to split the current single-process `MediaSimilarityFinder.exe` execution model into **GUI process + Backend process**.

This is not a search-algorithm rewrite.

The goals are:

1. Prevent WIC/COM, FFmpeg, CUDA, heap, CRT abort/terminate, and other native/runtime failures in Backend from terminating the GUI process.
2. Let the GUI supervise Backend lifetime and perform bounded Backend-only restart when necessary.
3. Preserve existing Search / Index / Comparison semantics and current durable storage behavior.
4. Establish the boundary needed for the 0.9.5.x ImageWorker / VideoWorker / MonitorService decomposition without prematurely multiplying processes in 0.9.4.

This is an **execution-boundary and fault-containment change**. It must not be presented as proof that the current crash's first cause has already been fixed.

---

## 2. Current code baseline

The current GUI side contains direct ownership of functionality that must move behind the Backend process boundary.

Current core structure:

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

Important baseline facts:

- `ScanWorker` being declared in the GUI header is the current structure and is a migration target for 0.9.4.
- `MainWindow` currently owning `std::unique_ptr<msf::MediaMonitor>` is also a migration target; Monitor becomes Backend-owned.
- Some GUI tests directly compile `gui/mainwindow.cpp` into test targets. Do not discard this coverage just because the process boundary changes. Adapt it carefully.
- `gui/main.cpp` also contains headless/CLI paths. The mandatory process-isolation acceptance for 0.9.4 is the **GUI execution path**. Do not expand this task into a separate CLI semantics rewrite. A directly runnable Backend executable is nevertheless useful for Backend integration testing.

---

## 3. Target processes and ownership

### 3.1 Target structure

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

### 3.2 GUI-owned state

GUI process owns:

- Qt application/window/widget objects
- search presentation state and UI models
- Detailed Logs presentation state
- Backend connection state
- Backend Supervisor
- IPC client/transport
- user-command generation
- presentation of Backend results

GUI does not directly own:

- `MediaSearchEngine`
- `ScanWorker`
- `MediaMonitor`
- WIC/FFmpeg/CUDA processing objects
- CandidateIndex
- SQLite connection
- Backend-side match aggregation state

### 3.3 Backend-owned state

Backend process continues to own:

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

0.9.4 is not a new internal algorithm architecture. It is a **process-boundary migration of the current Backend execution scope**.

---

## 4. Process launch and entry point

### 4.1 Backend executable

The exact executable filename may be selected during implementation to match existing CMake/packaging conventions. The following contract is mandatory:

- GUI and Backend must be different OS processes.
- Backend must be launchable as a GUI-owned child process.
- Backend must not be started by constructing a shell command string.
- GUI must be able to locate the Backend executable in installed and portable layouts.
- Backend must not create GUI widgets or pass Qt object pointers to the GUI process.

### 4.2 Launch context

Backend startup must be able to receive at least:

- protocol mode/version
- data/index path or required execution context
- explicitly requested test/diagnostic mode when necessary

Configuration authority remains the existing configuration model. GUI should not invent a second configuration store; Backend receives only what is needed for the current execution.

### 4.3 CLI/headless

CLI/headless semantics are not redesigned in this task.

- Preserve `--help`, `--version`, smoke, and existing headless benchmark/scan semantics.
- Do not force CLI paths through IPC merely to make the architecture look uniform.
- The new Backend executable should still be independently runnable so Backend integration tests can exercise the real process boundary.

---

## 5. GUI ↔ Backend responsibility rules

### GUI → Backend

The GUI says **what should happen**.

Examples:

- start scan
- pause
- resume
- cancel
- configure
- shutdown

### Backend → GUI

Backend reports **what happened / what state it is in**.

Examples:

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

### Forbidden shortcuts

GUI must not bypass IPC by:

- opening SQLite directly during Backend ownership
- calling MediaSearchEngine methods directly
- sharing ScanWorker pointers
- wiring QObject signals/slots across processes as if they were in-process objects
- passing Qt object pointers

---

## 6. IPC contract

### 6.1 Transport direction

```text
GUI → Backend
    command

Backend → GUI
    event / state / result
```

The initial implementation should prefer **QProcess-managed child supervision plus a local structured/line-oriented channel**.

Named Pipes or a dedicated IPC framework are not mandatory for 0.9.4. Replace the initial transport only if real throughput, framing, reconnect, deadlock, or operational evidence requires it.

### 6.2 Message envelope

The minimum semantic contract is:

```json
{
  "protocol": 1,
  "type": "state",
  "requestId": "...",
  "sequence": 123,
  "payload": {}
}
```

Meaning:

- `protocol`: IPC protocol revision
- `type`: message kind
- `requestId`: correlation identifier where a command/result relationship matters
- `sequence`: monotonically increasing Backend→GUI event ordering within one connection
- `payload`: message-specific data

This does not change DB schema version or engine version.

### 6.3 Minimum GUI → Backend messages

#### HELLO

Confirms protocol compatibility and connection state.

#### START_SCAN

Starts a search.

Existing search configuration semantics must be reused.

#### PAUSE

Pause the current search session.

#### RESUME

Resume a paused search session.

#### CANCEL

Request cancellation of the current search session.

#### CONFIGURE

Apply currently supported UI configuration.

Do not change existing resource-strategy semantics.

#### SHUTDOWN

Request clean Backend shutdown.

The Backend may acknowledge shutdown before terminating.

### 6.4 Minimum Backend → GUI messages

#### HELLO_ACK

Protocol negotiation result.

#### READY

Backend is ready to accept normal search commands.

#### STATE

Examples:

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

Exact enum/string names may follow existing state conventions, but the semantics remain.

#### PROGRESS

Current scan progress.

#### LISTING_PROGRESS

Directory/file enumeration progress.

#### FINGERPRINT_PROGRESS

Fingerprint-stage progress.

#### MATCHES_BATCH

Batch of compact match/group results for presentation.

Large result streams are split into batches.

#### TELEMETRY

Existing Detailed Logs/diagnostic telemetry semantics, as far as practical.

#### FINISHED

Normal search-session completion.

#### FAILED

Backend or search failure.

#### HEALTH

Watchdog heartbeat/health event.

---

## 7. IPC payload rules

### Allowed

- path or file identity
- file metadata
- fingerprints
- match/group metadata
- scalar counters
- progress
- timings
- error codes/messages
- small telemetry
- configuration
- command state

### Forbidden

- full-resolution image
- raw video frame
- large pixel buffer
- GPU buffer
- WIC/FFmpeg/CUDA native handle
- SQLite handle
- QObject pointer
- QWidget pointer
- internal C++ object pointer

Original media is always read directly by Backend or its internal processing path.

---

## 8. Match-result batch rules

The existing GUI result presentation must remain viable.

1. Backend completes match/group aggregation internally.
2. GUI receives only the compact metadata needed for presentation.
3. GUI does not reconstruct Backend results by decoding full media again.
4. If thumbnails/previews are needed, preserve the existing GUI rendering/cache responsibilities without sharing Backend native objects across the process boundary.
5. Multiple batches must be distinguishable by session context and event ordering so stale batches can be rejected.

Formal taskId/workerInstanceId result protocol is explicitly deferred to 0.9.5.

---

## 9. Backend lifecycle

### 9.1 Normal startup

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

Before READY, normal search admission should be minimized. A command received before READY should be explicitly rejected or held by a narrowly defined connection-level mechanism; do not silently pretend Backend is operational.

### 9.2 Normal shutdown

```text
GUI requests SHUTDOWN
  ↓
Backend enters SHUTTING_DOWN
  ↓
Backend stops accepting new scans
  ↓
Backend persists durable state under existing semantics
  ↓
Backend acknowledges shutdown
  ↓
process exits
  ↓
GUI clears supervisor state
```

User cancellation and application shutdown are not the same semantic event.

---

## 10. Backend crash / unexpected exit

Unexpected exit follows:

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

Important:

- Never terminate the GUI because Backend terminated.
- Do not delete or recreate the existing DB/index merely because Backend restarted.
- GUI must not guess or reconstruct the crashed ScanWorker's in-memory state.
- Do not promise automatic resume from the exact last file.

---

## 11. Recovery semantics

### 11.1 Preserved

- index state already committed to SQLite
- already persisted matches
- durable state recoverable through existing WAL/checkpoint behavior
- existing user settings
- existing index/database files

### 11.2 Not guaranteed in 0.9.4

- automatic resume from exactly the last file
- in-memory queue restoration
- restoration of the current worker object state
- restoration of partially computed GPU buffers
- a new DB transaction replay system
- a new task journal whose purpose is exact process resume

### 11.3 Search after restart

After the restarted Backend reaches READY, it **does not automatically resume the interrupted scan**.

The GUI should clearly represent that the previous session did not complete and return to a state where the user can start a new scan.

This prevents false resume, duplicate work, and assumptions about stale in-memory state.

---

## 12. Backend Supervisor

Supervisor is a distinct GUI responsibility.

### Responsibilities

- create Backend process
- monitor startup timeout
- monitor process exit
- monitor heartbeat
- distinguish clean shutdown from unexpected exit
- perform bounded restart
- protect UI state while restarting
- transition to FAILED after repeated failures
- verify READY after restart

### Not responsible for

- directly performing DB recovery
- editing SQLite files
- constructing WIC/FFmpeg/CUDA objects
- controlling search algorithms
- guessing crash causes from stack/exit state
- infinite restart

---

## 13. Heartbeat / watchdog

Process liveness and health liveness are distinct.

### Process liveness

Use QProcess process-state, error, and finished signals.

### Health liveness

Use Backend HEALTH/heartbeat messages to show that the Backend control path is still alive.

Recommended structure:

```text
Backend control/supervisor context
        ↓
periodic HEALTH
        ↓
GUI watchdog timer
```

Heartbeat generation must not depend on the scan hot path itself.

A temporary lack of normal progress events is not automatically a dead Backend; long directory enumeration, native I/O, SQLite commits, and decoder work may legitimately reduce progress-event frequency.

### Initial implementation candidates

These are **acceptance-oriented initial candidates**, not immutable architecture constants:

- heartbeat period: about 1 second
- health timeout: about 10 seconds
- startup READY timeout: about 15 seconds
- restart attempts: bounded
- restart backoff: increasing rather than immediate unbounded relaunch

Final values are selected after verifying that they do not create false positives in real scans.

---

## 14. Restart policy

Normal path:

```text
READY / RUNNING
      ↓ unexpected exit
RECOVERING
      ↓
RESTARTING
      ↓ success
READY
```

Repeated failure:

```text
RESTARTING
   ↓
repeated failure
   ↓
FAILED
```

Policy requirements:

- Restart must never terminate the GUI.
- Restart must never be infinite.
- Fast crash loops must not explode process/CPU/log activity.
- A user-requested application shutdown must cancel restart.
- Restart success is proven by HELLO/READY, not merely by creation of a process handle.
- A restart that starts but cannot reach READY counts as failure.

Exact retry count and backoff numbers are selected during implementation and acceptance.

---

## 15. GUI behavior while Backend is unavailable

The GUI must remain alive.

Presentation/control rules:

- Preserve existing results where practical.
- Block or clearly disable new search commands.
- Backend-dependent actions must not fail silently.
- Show a clear Backend restarting / unavailable state.
- Restore normal search controls after READY returns.

Fault containment is not a reason to fake a healthy UI.

---

## 16. SQLite / Index ownership

In 0.9.4, the Backend is the **single authoritative SQLite writer**.

```text
GUI
  X direct SQLite write

Backend
  └─ Database / SQLite
       └─ CandidateIndex / match persistence
```

Rules:

- GUI never writes the DB.
- Restarted Backend reopens the existing DB.
- Do not increment DB schema version.
- Do not increment engine version.
- Do not create a DB migration merely because process separation was introduced.
- Reuse existing WAL/checkpoint behavior.

A separate IndexService is a later 0.9.5+ design decision only.

---

## 17. Monitor ownership in 0.9.4

Monitor remains **inside Backend** in 0.9.4.

```text
Backend
├─ MediaSearchEngine
├─ ScanWorker
├─ MediaMonitor
└─ SQLite
```

Therefore a Monitor native/runtime failure is still within the Backend fault boundary in 0.9.4.

The important change is that such failure does not directly propagate to the GUI process.

Only 0.9.5 promotes it to:

```text
MonitorService
       ↓
      Core
```

Do not create MonitorService, ImageWorker, or VideoWorker executables preemptively in the 0.9.4 implementation.

---

## 18. GUI / Backend source migration strategy

### GUI side

Target:

```text
MainWindow
   ↓
BackendClient / Supervisor
   ↓ IPC
Backend
```

Remove direct GUI ownership of:

- `ScanWorker`
- `MediaSearchEngine`
- `MediaMonitor`

Keep existing UI slot/signal behavior where possible, but replace direct engine calls with Backend command/event adapters.

### Backend side

The new Backend entry point initializes:

1. application/control runtime
2. IPC endpoint
3. MediaSearchEngine / ScanWorker
4. current Monitor
5. Database/index
6. telemetry connection
7. command handler

The Backend target must not link or instantiate GUI widget code.

### Tests

If existing GUI tests are tightly coupled to `mainwindow.cpp`:

- do not simply delete them
- add a minimal injectable/fake BackendClient boundary where useful
- verify real process separation with Backend integration/E2E tests
- keep existing engine unit tests unchanged wherever possible

---

## 19. Existing semantics that must remain unchanged

This architecture change does not alter:

- fingerprint algorithms
- mirror fingerprint
- crop fingerprints
- similarity thresholds
- candidate-index semantics
- image sampling
- video sampling
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

What changes is **where code executes and where failure is contained**.

---

## 20. Error classification

0.9.4 distinguishes:

### Clean shutdown

GUI explicitly requested SHUTDOWN and Backend terminated normally.

### Controlled failure

Backend reports FAILED and may still remain alive or finish through a controlled shutdown path.

### Unexpected process exit

Backend exits while READY/RUNNING without a normal shutdown sequence.

### Startup failure

Backend starts but cannot reach READY within the startup window.

### Watchdog timeout

Backend process is alive but health protocol exceeds its timeout.

### Restart exhausted

Bounded restart policy is exhausted.

Final GUI state becomes `FAILED / Backend unavailable`.

---

## 21. Logging / diagnostics

GUI and Backend logs have different responsibilities.

### GUI log

- Supervisor state
- IPC connection state
- restart reason
- restart count
- user-command failure
- Backend unavailable state

### Backend log

- search-engine diagnostics
- decoder/runtime failure
- database errors
- telemetry
- scan session state

Backend stdout/stderr should remain separately observable by the GUI/supervision layer.

Do not push large binary payloads into logs.

The purpose of this change is not to hide the crash cause, but to make the **process boundary and failure path observable**.

---

## 22. Safe execution rules

This is a local child-process architecture.

- Do not launch Backend by building shell command strings.
- Pass executable path and arguments through structured APIs.
- IPC framing must be explicit.
- Malformed messages must be rejected rather than allowed to crash Backend.
- Unknown message types must be ignored safely or return an explicit error.
- Impose a reasonable message-size limit.
- Do not extend the protocol to carry raw media.
- Do not expose a network endpoint.
- Do not introduce remote workers.

---

## 23. Acceptance gate

### A. Structure

- [ ] GUI and Backend are separate OS processes.
- [ ] GUI does not directly own `MediaSearchEngine`, `ScanWorker`, or `MediaMonitor`.
- [ ] SQLite authoritative access is Backend-only.
- [ ] Backend does not depend on GUI widgets.

### B. Normal execution

- [ ] GUI starts
- [ ] Backend spawned
- [ ] HELLO / HELLO_ACK
- [ ] READY
- [ ] existing search executes
- [ ] progress / matches / telemetry displayed
- [ ] FINISHED
- [ ] normal shutdown

### C. Control

- [ ] pause
- [ ] resume
- [ ] cancel
- [ ] configure
- [ ] shutdown

### D. Fault isolation

- [ ] force Backend termination
- [ ] GUI remains alive
- [ ] GUI detects Backend exit
- [ ] Backend unavailable state is shown
- [ ] only Backend restarts
- [ ] new Backend reaches READY
- [ ] existing SQLite/index reopens
- [ ] committed state remains intact

### E. Repeated failure

- [ ] inject repeated Backend crashes
- [ ] no infinite restart loop
- [ ] bounded retry transitions to FAILED
- [ ] GUI remains alive

### F. Health

- [ ] heartbeat is received
- [ ] long scan/native I/O does not cause false watchdog failure
- [ ] process death and heartbeat timeout are distinguishable

### G. Correctness

- [ ] CPU CTest regression check
- [ ] GPU CTest regression check
- [ ] Search/Index/Comparison semantics unchanged
- [ ] DB/schema/engine/benchmark versions unchanged

### H. OS-process proof

Verify at OS level:

```text
GUI PID != Backend PID
Backend forced termination
GUI PID remains alive
new Backend PID appears after restart
```

Different Qt thread IDs are not evidence for this gate.

---

## 24. Scope expansion prohibited during 0.9.4 implementation

Do not add the following just because process separation makes them tempting:

- ImageWorker.exe
- VideoWorker.exe
- MonitorService.exe
- IndexService
- LogService
- remote worker / network service
- 0.9.5 formal taskId/workerInstanceId protocol
- global GPU lease
- multi-process CUDA optimization
- search-semantics rewrite
- DB migration
- GUI benchmark redesign
- a claim that the crash root cause is now solved

Do not delete rejected or failed experiment records because they became irrelevant to the new structure.

---

## 25. Deferred to 0.9.5

Do not implement these in 0.9.4; retain them as 0.9.5 architecture:

```text
ImageWorker
VideoWorker
MonitorService
formal Core coordinator/process model
worker-level restart
sessionId/taskId/sequence/attempt protocol
global GPU lease
multi-process CUDA context measurement
worker queue migration
formal result aggregation protocol
Monitor reconnect/replay queue
candidate-index service split
```

0.9.4 should create only the minimum boundary that does not block this future architecture.

---

## 26. Decision priority during implementation

If OpenCode encounters a conflict, use this priority:

1. This document's ownership and fault-boundary rules
2. Higher-level `process-architecture-0.9.4` decisions
3. Existing Search/Index/Comparison semantics
4. Existing tests and durable-storage semantics
5. Existing code shape

If the existing code shape conflicts with the approved process boundary, do not sacrifice the process boundary merely to minimize source movement.

Conversely, do not silently weaken the IPC or recovery contract merely to make implementation shorter.

---

## 27. Definition of design completeness

After reading this document, there must be no ambiguity about:

- who owns ScanWorker
- who owns MediaMonitor
- who writes SQLite
- how GUI communicates with Backend
- what GUI does after Backend death
- what restart means and when it stops
- what recovery is guaranteed and what is not
- whether raw media crosses IPC
- whether Image/Video/Monitor are separate processes in 0.9.4
- what is deferred to 0.9.5
- whether Search semantics change

The implementation should not begin until the source and this brief can be reconciled on all of these points.

---

## 28. Document role

`process-architecture-0.9.4.en.md` defines **higher-level architecture and scope**.

This brief turns those decisions into an **implementation contract**.

Source code may choose implementation details only while satisfying both documents.

During 0.9.4.x implementation this brief may be amended when a significant design or acceptance finding is discovered, but historical execution records must not be rewritten retroactively; the reason and evidence belong in worklog/build-history.

---

## 29. Supplemental decisions (G1–G13, reflecting the additional directive)

The following does not replace §1–§28 above. It only adds interpretations and hard rules confirmed before implementation starts.

### 29.1 G1 — Backend authoritative thumbnail + GUI memory cache

- Canonical path: `GUI → GET_THUMBNAIL → Backend (getColorThumb/WIC/FFmpeg) → THUMBNAIL/THUMB_BATCH → GUI memory cache → paint`.
- The GUI memory cache reuses already-delivered finished thumbnails. It is not a decode authority. Never build a GUI WIC/FFmpeg/engine-`getColorThumb` fallback (an A-path failure must not detour into GUI decoding).
- IPC allowance: bounded derived payloads up to 256px / 200KB (`GET_THUMBNAIL`, `THUMBNAIL`/`THUMB_BATCH`). Raw frames, WIC/FFmpeg/GPU objects stay banned. Numbers may move after acceptance measurements.
- Backend unavailable: cached thumbs keep painting, new requests impossible, placeholders shown. The GUI never decodes around the failure.
- The current `MainWindow::thumbDb_` is not a memory cache: it opens the managed index DB directly and does thumbnail persistence/prune/writes. Do not leave it in the GUI. Per the single-DB-authority principle the Backend owns authoritative SQLite/thumbnail persistence; the GUI keeps a memory presentation cache only. See the §32 P0 inventory for every `thumbDb_` use.

### 29.2 G2 — Call-site taxonomy (Type A/B/C)

- Type A (fire-and-forget): pause/resume/cancel/configure/shutdown. One way; the GUI never waits for a response.
- Type B (request/response): thumbnails, selected-item detail, diagnostic values. Carry `requestId` plus generation/version; drop a response whose generation does not match the current request as stale.
- Type C (tick getters): do not move existing polling to remote polling. The Backend pushes STATE/HEALTH/progress/counter/telemetry snapshots and GUI ticks read the local snapshot only.
- See the §32 P0 inventory for the table.

### 29.3 G3 — GUI-thread non-blocking hard rule

- No synchronous wait API on the production GUI path (`waitFor*`, blocking read/receive, `QProcess::execute`, `startScanAndWait()`/`waitForReady()`/`requestAndWait()` style calls are banned).
- Allowed: command, signal/event, callback, state transition, QTimer-based timeout. Blocking waits needed only by test harnesses stay out of the production path.

### 29.4 G4 — Asymmetric backpressure

- A slow GUI must never make the scan engine depend on IPC speed. Bounded outgoing Backend→GUI queue (initial candidates: about 1000 messages / 64MB; adjust after acceptance workload measurements).
- Coalescible (merge to latest): PROGRESS, LISTING_PROGRESS, FINGERPRINT_PROGRESS, TELEMETRY, HEALTH.
- Lossless (must not drop): MATCHES_BATCH, FINISHED, FAILED. Shrink excessive MATCHES by batch coalescing first; never drop matches on queue overflow. No disk spool up front.

### 29.5 G5 — Spawn only after terminate → kill → verify

- Never spawn a new Backend while the old one is alive (SQLite double-writer prevention hard rule).
- Order: terminate request → asynchronous grace period → check finished → kill if needed → check finished → spawn. The 3s/5s figures are QTimer-based asynchronous escalation timeouts, not blocking waits (no conflict with G3).

### 29.6 G6 — Job Object + channel-loss self-exit

- The GUI puts the Backend in a Windows Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` (the OS cleans up the Backend when the GUI dies abnormally).
- The Backend attempts orderly exit when it detects control-channel loss (stdin/transport EOF, parent/connection loss). The criterion is real connection loss, not "no command for 30s" (a long scan legitimately has no commands).

### 29.7 G7 — UTF-8 + flush per message

- Wire encoding is UTF-8. Malformed/invalid UTF-8 or framing errors are rejected, never processed as normal messages (no repeat of 0.9.4.47).
- Senders flush per message (or an equivalent unbuffered transport).

### 29.8 G8 — Three test layers, confirmed

- Pure GUI: `MainWindow → FakeBackendClient` (splitter/selection/dialog/display-only).
- GUI + real BackendCore: `MainWindow → LoopbackBackendClient → real BackendCore`. Loopback is not a fake; it is the transitional/test path that skips only the OS process split. Keep existing search-oriented GUI tests on it wherever possible.
- Real-process acceptance: `MainWindow → real BackendClient → Backend.exe` (PID/crash/restart/Job Object/SQLite reopen).
- Never delete existing GUI tests to fake verification coverage.

### 29.9 G9 — Fixed P0→P4 order

- P0: call-site/ownership inventory (read/analyze, minimal behavior change).
- P1: `BackendCore` library extraction + `backend_main` entry (same process, all existing CTest green).
- P2: `BackendClient` abstraction + Loopback (real BackendCore in the same process).
- P3: real Backend spawn + Supervisor + IPC + crash injection.
- P4: hardening (thumbnail/backpressure/nonce/Job Object/escalation/health/UTF-8/stale event/final acceptance).
- Keep every phase-endpoint buildable and testable; never hide a phase's problem by moving on.

### 29.10 G10 — Version split

- Do not pack P1/P2/P3/P4 into one giant patch. Each patch must stay independently buildable, retestable, and rollbackable, following `phase → implementation → build → tests → evidence → next phase`.
- Exact patch numbers are fixed after checking branch/HEAD, never pre-recorded retroactively (sequential candidates after the current HEAD `0.9.4.66`).

### 29.11 G11 — backendInstanceNonce

- A minimal 0.9.4-only process-instance identity, not the 0.9.5 formal task protocol. The supervisor mints a random nonce per spawn, passes it as a Backend start argument, and the Backend echoes it in every event.
- The GUI drops events whose nonce does not match the current instance (a late event from an old Backend arriving while the new one runs).

### 29.12 G12 — READY timeout + STARTING health

- Keep the initial ~15s startup READY timeout candidate, but not as an unconditional kill 15s after spawn. During startup the Backend periodically sends `STATE=STARTING`/HEALTH so "slow but alive" and "stuck" stay distinguishable. Numbers move after large-DB reopen measurements.

### 29.13 G13 — Backend Qt dependency

- The Backend links only up to `Qt6::Core`; never wire `Qt6::Widgets`/`Qt6::Gui` as direct dependencies. If some Backend feature seems to need them, re-investigate the dependency itself first. Verify both target links and packaged binary dependencies at acceptance.

## 30. Command ordering / terminal once-only (§14)

- Without the 0.9.5 formal task protocol, command/event order inside one Backend session must still be deterministic (`START_SCAN`→`PAUSE`→`RESUME`→`CANCEL` arriving in quick succession are processed in the defined order).
- The terminal state of one session (`FINISHED`/`FAILED`/`CANCELLED`) is never terminalized twice or out of order (terminal state once-only).
- Minimum preserved: command sequence, session generation/state, terminal once-only, event sequence.

## 31. Pre-completion checklist (§15)

1. The GUI does not open/write the existing index DB's thumbnail table/cache directly?
2. The GUI thumbnail path does not collide with the Backend decode path?
3. No WIC/FFmpeg thumbnail fallback appeared in the GUI?
4. No synchronous wait API exists on BackendClient?
5. No waitFor*/blocking read happens on the GUI thread?
6. The scan engine cannot block on the IPC queue?
7. MATCHES_BATCH/FINISHED/FAILED cannot be lost to overflow?
8. A new Backend is never spawned while the old one is alive?
9. Restart escalation never blocks the GUI thread?
10. No orphaned Backend remains when the GUI exits?
11. UTF-8 paths and message framing handled strictly?
12. Stale events from the old Backend dropped by nonce?
13. The Backend does not depend on Qt6::Widgets/Qt6::Gui?
14. Loopback tests and real-process acceptance clearly separated?
15. Each of P1→P2→P3→P4 independently verifiable?

## 32. P0 inventory — call sites moving to the Backend (at HEAD 0.9.4.66)

`engine_.*` calls inside the `ScanWorker::run()` body (L421–620) move with the worker to the Backend, so they need no IPC (stay local). Below are the `MainWindow` GUI-side junctions that must become IPC adapters.

| # | Location | Call | Type | Note |
|---|----------|------|------|------|
| 1 | L1541 | `worker_->pause()` / `resume()` | A | one-way command |
| 2 | L1555 | `worker_->cancel()` | A | one-way command |
| 3 | L1506–1508 | `setIgnored/setDetailedLog/moveToThread` + thread creation | A+connect | replaced by CONFIGURE + START_SCAN; QThread goes away |
| 4 | L792, L1479 | thread quit/wait/delete | removed | replaced by process lifecycle |
| 5 | L2761, L2832 | `scanEngine().getColorThumb/getVideoThumb` | B | GET_THUMBNAIL (§29.1). No GUI decode fallback |
| 6 | L3011 | `scanEngine().files()` | B | selected-item detail request |
| 7 | L1845–1846 | `telemetryJsonForTest()` | B | test hook; keep loopback-only under review |
| 8 | L3481 | `worker_->gpuActive()` | C | replaced by STATE/HEALTH push snapshot |
| 9 | L3685 | `scanEngine().analyzedCount()` | C | same |
| 10 | L3698–3699 | `gpuAvailable/gpuDone` | C | same |
| 11 | L1883, L1956 | `monitor_->setPolicy()` | A | monitor moves to Backend; fold into CONFIGURE |
| 12 | L3842, L3860 | `monitor_->stop/start` | A+connect | Backend-owned; GUI receives state events |
| 13 | L3880–3881 | `monitor_->running/status` | C | replaced by HEALTH snapshot |
| 14 | L2066 | `takePending()` + `pending_` queue | replaced | replaced by MATCHES_BATCH (same-process queue removed) |
| 15 | L1468–1474 | `thumbDb_.open/initialize` (index DB opened directly) | move to Backend | violates G1.3. Must not stay in the GUI |
| 16 | L791, L1789 | `thumbDb_.close/pruneThumbs` | move to Backend | persistence ops |
| 17 | L2724–2726 | `thumbDb_.getThumb` | B | GET_THUMBNAIL on miss (GUI keeps memory cache only) |
| 18 | L2847, L2857, L2885–2886 | `thumbDb_.putThumb` + flush | move to Backend | writes are Backend-only |
| 19 | L3825–3827 | `watchRoots/compareRoots/applicationDirectory` collection | A | START_SCAN/CONFIGURE payload (reuse existing model, §10) |

`thumbBudget_/thumbStarved_` move to the Backend side under review (decode spend lives there); only GUI memory-cache hit statistics stay GUI-side. Worker-internal state (`control_`, `pending_`, `allMatches_`, …) moves to the Backend whole.
