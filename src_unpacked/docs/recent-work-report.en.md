# Recent Three Significant Work Items — Result Report (2026-10-08)

Baseline commit: `8f3ba47` (synchronized with `origin/main`)
Version: `0.9.4.71` / CPU CTest 116/116 / GPU CTest 117/117

This document is a summary report that lets the three most recently completed
significant work items be checked **in one place**. The detailed evidence for
each already lives in Build History / Work Log; this document only collects
"what was done and where things stand" without duplication.

---

## Item 1 — `0.9.4.69` P3: real Backend process spawn + Supervisor + IPC

Commit `9985ac2` / details `docs/build-history/0.9.4.69.{ko,en}.md`

### What was done

Up to P2 the `BackendClient` abstraction had only the in-process
`LoopbackBackendClient`. P3 **split the GUI and the search engine into separate
OS processes** and connected them with line-oriented JSON IPC.

- GUI process: `MainWindow` + `BackendClient` + `BackendSupervisor`.
- Backend process: `MediaSimilarityFinderBackend.exe` (`msf_core` + `Qt6::Core` only).
- `ScanWorker` moved to `src/scan_worker.*`; `BackendSession` is the shared
  session owning the thread/worker/monitor. Loopback is a thin forwarder over it.
- IPC contract: stdin commands / stdout JSONL events (flush per message) / stderr
  diagnostics. A nonce is issued per spawn and stale events are dropped on nonce
  mismatch. MATCHES 500/batch, RESULTS 2000/page chunking. `THUMBNAIL` (JPEG base64).

### Supervisor safety

- QProcess spawn (no shell assembly), Win32 Job Object `KILL_ON_JOB_CLOSE`.
- Bounded restart 3 times / backoff 2s, 5s, 10s; terminate-to-kill escalation is
  QTimer-based (non-blocking).
- health 1s / heartbeat timeout 10s / READY timeout 15s.
- Only `shutdown()` waits up to 3s (documented G3 exception).

### Result / verification

- `backend_ipc_test` 7 checks (UTF-8 path round-trip, malformed/oversize/protocol reject).
- `backend_e2e_test` (real Windows process): PID split, kill-to-restart, FAILED, DB reopen measured.
- `MSF_TEST_BACKEND_FAIL_FAST` / `MSF_TEST_BACKEND_SILENT` crash-injection seams.
- CPU CTest 116/116, GPU CTest 117/117.

---

## Item 2 — `0.9.4.70` P4: hardening + FILE_META

Commit `ed4c0c1` / details `docs/build-history/0.9.4.70.{ko,en}.md`

### What was done

At the end of P3 two decoder remnants were still in the GUI (the detail pane's
direct resolution/duration probe). Following the directive's ban on direct
FFmpeg calls, they were moved to Backend requests.

- New `src/file_meta.*` (`FileMeta`, std `ffprobeSize`).
- `BackendSession::requestFileMeta`: engine record then video info then
  dimensionsFast then ffprobe, all Backend-side.
- IPC `GET_FILE_META`/`FILE_META` plus supervisor forwarding plus GUI
  `requestFileMeta`/`onFileMetaReady` (pending map, stale-drop, repaint on
  arrival). The same Type B pattern as thumbnails.
- QtGui remaining in the GUI is presentation-only (QIcon display, EXIF text tag —
  no pixel decode).

### Result / verification

- **The GUI process performs no media pixel decode.** WIC/FFmpeg/CUDA/native
  decode is all in the Backend.
- The §15 15-item checklist was reconciled with evidence and the numbers
  (heartbeat/backoff/cap) locked.
- **dumpbin measured: Backend dependencies = `Qt6Core.dll` + `turbojpeg.dll`
  (+ffmpeg/sqlite). No `Qt6Widgets`/`Qt6Gui`.**
- CPU CTest 116/116, GPU CTest 117/117.

---

## Item 3 — `0.9.4.71` backend defect fixes + ThumbnailStore

Commit `8f3ba47` / details `docs/build-history/0.9.4.71.{ko,en}.md`

### What was done

Fixed the seven defects confirmed by the second independent review after the
P3/P4 process split.

| # | Defect | Fix |
|---|---|---|
| 1 | After the split the GUI resource mode was not delivered over IPC, so the worker always ran `make_policy(Custom)` | New `ExecutionPolicy` to `START_SCAN.exec` to `ScanWorker::setResourceMode` to `make_policy(mode)`. Live refresh via `UPDATE_RESOURCE_POLICY`/`POLICY_APPLIED` (partial) |
| 2 | "Index Complete" showed only `analyzedCount_` | Engine `unchangedCount_` + `BackendStatus.unchanged`, GUI `liveAnalyzed = analyzed + unchanged` |
| 3 | Summary CPU/RAM labels overwritten alternately by GUI process / system-wide | `updateSysLabels` GPU-only; CPU/RAM from the single working-process status snapshot writer (`sampleOwnProcess`) |
| 4 | Slowest-file list padded by cache-hit (~0ms) / failed decode (0ms) | `addImage` excludes 0-cost, `addVideo` excludes `cacheHit` |
| 5 | `ScanWorker::allMatches_` stayed resident after final persist | `clear()` + `shrink_to_fit()` (normal and failure paths, honoring the no-throw handler rule) |
| 6 | Backend thumbnails read only the engine `thumbMap_` -> most previews empty after the split | New `src/thumbnail_store.*` (engine art -> shell `IThumbnailCache` -> WIC -> FFmpeg -> gray, SQLite persistence + LRU256) + `backend_thumb` moved to msf_core + JPEG end-to-end |
| 7 | `onFileMetaReady` called `refreshFileViews()` (full recreation) -> selection destroyed | `QMap<id,path>` dedup + `refreshFileMetaRow` in-place, zero widget recreation |

### Regression found during verification — Qt JPEG plugin not deployed

After ThumbnailStore, `ui_scroll_regression_test`/`view_mode_probe` failed. The
cause was that loopback/supervisor decoded JPEG with `QImage::fromData`, but the
Qt JPEG plugin (`qjpeg.dll`) depends on **`jpeg62.dll`, which is not in the
deployed set**, so jpeg was absent from `QImageReader::supportedImageFormats()`
and decode returned null.

- Fix: new `msf::decodeJpegArgb32` (libjpeg-turbo, using the already shipped
  `turbojpeg.dll`) replaced it in loopback and supervisor, fully removing the Qt
  JPEG plugin dependency.
- Re-checked: both GUI tests pass.

### Result / verification

- CPU CTest **116/116**, GPU CTest **117/117**.
- Both GUI/Backend exes `--version 0.9.4.71`.
- Changed source U+FFFD 0 / CJK 0.
- Source zip 774 files byte-identical to HEAD (0 mismatches); portable zip 86
  entries (GUI + Backend exe + turbojpeg), smoke PASS.

---

## Current overall status

| Item | Status |
|---|---|
| Version | `0.9.4.71` |
| Commit | `8f3ba47`, synchronized with `origin/main` |
| CPU CTest | 116/116 PASS |
| GPU CTest | 117/117 PASS |
| Process split | P1-P4 complete |
| Backend Qt dependency | Qt6Core only (dumpbin measured) |
| XMP production acceptance | CONDITIONAL |
| `color_thumb` production correction | not performed (pre-registered only) |
| S4 final GUI visual/save acceptance | DEFERRED (manual acceptance required) |
| S5 product benchmark | DEFERRED |
| S6 | DEFERRED |
| NVDEC production adoption | NO (F-1 CONDITIONAL) |

## Remaining candidates / next steps

- Real Windows manual GUI acceptance: preview display, selection, kill UI guard,
  recovery after restart, Tiles/ListMode, large-dataset traversal.
- Review the unimplemented GPU MAX GPU share boost (documented gap).
- The exact split of the backend 400MB needs a VMMap/heap snapshot.
- `color_thumb` R1 fixture and skip/pass handling, then R2-R6 separately.
- XMP full scan regression after S4 functional acceptance.
- Backup zips (`backup_src.ps1` / `package_portable.ps1`) were produced for
  0.9.4.71 (3 per kind kept, `.68` portable recycled).

## Related documents

- `docs/build-history/0.9.4.69.{ko,en}.md`
- `docs/build-history/0.9.4.70.{ko,en}.md`
- `docs/build-history/0.9.4.71.{ko,en}.md`
- `docs/worklog/0.9.4.{ko,en}.md`
- `docs/architecture/process-architecture-0.9.4.{ko,en}.md`
- `docs/implementation-briefs/process-backend-isolation-0.9.4.{ko,en}.md`
- `docs/development-progress.{ko,en}.md`
