# Runtime Audit: v0.9.2.63 API / Call / Thread / Lifetime Consistency

> Audit basis: `25095f1` (v0.9.2.64 dev tree). Every finding except QuickLook
> applies identically to `origin/main` (`cb68b3c`). Read-only code audit;
> no builds, no modifications. GUI = main thread, worker = scan thread.

## 1. Critical Runtime Issues

| File | Location | Issue | Trigger | Impact |
|---|---|---|---|---|
| `gui/mainwindow.cpp` `cancelScan()` / `togglePauseScan()` | 987-991, 974-986 | Pause/stop buttons never act mid-scan. `invokeMethod(..., QueuedConnection)` slots run only after `run()` releases the worker event loop, which it occupies for the whole scan. Buttons change cosmetics only | Every mid-scan click | Users believe it stopped while scanning continues → force-quit → unsaved loss. `scanFinished` CANCELLED branch unreachable from GUI. v0.9.2.57 StopCheck, checkpoints, and CANCELLED UI neutralized together. `scan_cancel_test` drives control directly, masking the gap |

Fix: call `worker_->pause()/resume()/cancel()` directly (atomic stores only, thread-safe; the destructor's direct `worker_->cancel()` is precedent).

## 2. Potential Runtime Issues

| File | Location | Potential issue | Evidence | Risk |
|---|---|---|---|---|
| `gui/mainwindow.cpp` `ffprobeSize()` + `src/video_decoder.cpp:39,156` | ~1468 | `ffmpeg`/`ffprobe` resolved via PATH only. Bundled portable exes next to the exe are never found | Bare-name `_popen`. No app-dir-first lookup | Medium (portable) |
| `src/media_search_engine.cpp` `processOne` | fp==0 retry rule | Corrupt videos fully re-opened + probed on every rescan (no engine skip-list; `.62` covers thumbnails only) | Unconditional retry in `changed` | Medium (perf) |
| `gui/mainwindow.cpp` `scanStatusText()` | 2130-2137 | ETR grows while paused (no pause guard) | No branch | Low |
| QuickLook detection (`25095f1` only) | `_wgetenv` | NULL env would AV in `fromWCharArray(NULL)` | No guard (vars always exist in practice) | Low |
| `src/media_search_engine.cpp` | 276 | Cancelled scans still record `updateLastScan` | Unconditional call | Low |

## 3. API Consistency

| Class | Header | CPP | Call Site | Status |
|---|---|---|---|---|
| `ScanPipeline` | 3 `analyze` overloads + `StopCheck` + `ScanCancelled` (line 12) | Internal `LocalCancel` used, `ScanCancelled` unused | Engine calls 3-arg, tests check partial stats | ⚠️ Header leftover (see 6) |
| `MediaSearchEngine` | All APIs defined | Match | `upsertFingerprint`→tests, `removePath`/`compareFingerprint`→monitor, rest worker/GUI. `onMatchRef` set only by tests (documented optional, fine) | OK |
| `Database` | `putThumb/getThumb/pruneThumbs` defined | Match | GUI `fileThumb`/exit prune. Autocommit single statements | OK |
| `ScanWorker` | 8 signals defined | All emitted in `run()` | All 8 connected in `startScan` + finished/failed→thread quit | OK |
| `MainWindow` | `matches_`, `lastDone_`, `lastTotal_` declared | One clearing write each, never read (live ones are `lastDoneN_` family) | — | ⚠️ 3 dead members |

## 4. Thread / Lifetime

```text
MainWindow (GUI thread, main() stack → destructor guaranteed)
 ├─ thread_ = new QThread(this) → cleaned first (child)
 ├─ worker_ = plain new + moveToThread → quit/wait then delete
 │    ├─ engine_/control_/allMatches_ = value members (same lifetime)
 │    ├─ onMatch/progress lambdas run worker-thread-only, no widget access
 │    └─ takePending mutex-guarded, gpuDone_ atomic
 ├─ thumbDb_ (GUI-only sqlite handle) → closed before worker teardown
 └─ monitor_ (own threads/connections) → stopped last
```

- No QtSql (one raw sqlite3 handle per thread) → thread-affinity failure modes structurally absent. Shared WAL + busy_timeout(5000).
- Destruction order safe. Residual: `wait()` blocks inside one giant video decode.
- Thumbnail/resolution decodes run synchronously on the GUI thread → no background-delete races. FFmpeg handles open/close in scope.
- No late-callback reactivation after finish (restart is user action only).

## 5. v0.9.2.63 Feature Checks

- **Disk thumbnail cache**: all 10 scenarios traced (hit/miss/modify/size-change/replace/delete/orphan/prune/restart/concurrent) — as designed. ms mtime consistent. Concurrent requests single-threaded.
- **ms timestamps**: scanner/DB/GUI agree.
- **ffprobe fallback**: one-time, cached. Except the PATH issue in 2 above.
- **Favorite normalization / filename elide / file size fallback**: correct (full path in tooltip, refresh on zoom).
- **Second DB connection**: covered by `startScan` guard, destruction order, WAL.

## 6. Stale / Misleading Code

- **`ScanCancelled` (`scan_pipeline.h:12`)**: unused. Real abort is cpp-internal `LocalCancel` (caught, partial stats). Header comment describes a removed design → delete + tidy comment.
- **`saveMatches` comment** ("deleted files disappear"): contradicts union semantics (loaded rows re-saved).
- **Dead members**: `matches_`, `lastDone_`, `lastTotal_`.
- **Pairs `n(n-1)/2`**: complete-graph assumption over union-find components. Transitive pairs may overcount → document the assumption.

## 7. Recommended Fix Order (dependency-aware)

```text
1. Direct pause()/resume()/cancel() calls (few GUI lines + comment)
   └─ Without this, StopCheck, checkpoints, and CANCELLED UI stay meaningless
2. Remove header ScanCancelled + fix saveMatches comment + drop 3 dead members
3. App-dir-first ffmpeg/ffprobe lookup (portable requirement)
4. Engine corrupt-file skip (fail_count column + migration + retry/reset + test)
5. ETR pause freeze + updateLastScan-on-cancel + Pairs assumption docs
6. QuickLook _wgetenv null guard (on resume, 25095f1-branch basis)
7. GUI lifecycle test (meaningful only after 1-4)
```
