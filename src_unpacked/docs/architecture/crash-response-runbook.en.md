# Crash Response Runbook

Purpose: the fixed procedure for what to collect and in which order when a
forced termination happens. Run this procedure first, regardless of version.

## 1. Collect immediately after a crash (in order)

### 1-1. Tail of `%TEMP%\msf_scan.log`

- A `start folder=...` with no later `finish` means abnormal termination
  (clean completions and user Stops always leave a `finish` line).
- Check when the `alive ...` heartbeat (about every 10 seconds) stopped.
  The last heartbeat roughly equals the crash time.
- `lastPct`/`lastPath` advance only on analyzed or failed files. 0% plus an
  empty path for many minutes means "no analysis yet", not proof of a hang
  (enumeration and unchanged-skip phases emit no such signal).

### 1-2. Windows Event Viewer

`eventvwr.msc` -> Windows Logs -> Application -> Errors around that time:

- Event ID **1000**: faulting module plus offset, exception code, process path.
  - `0xc0000409` = fail-fast (abort family: terminate, abort, Qt fatal, ...).
  - `0xc0000005` = access violation (heap corruption or wild pointer).
- Event ID **1001**: WER bucket ID (identifies repeat crashes).
- **Same offset repeating** = a deterministic abort path. Scattered offsets
  point at memory corruption instead.

### 1-3. `%TEMP%\msf_qt.log` (0.9.4.62+)

Final record of Qt warnings, criticals, and fatals. The lines right before a
fatal are the last clue.

### 1-4. WER reports

`.wer` files under `C:\ProgramData\Microsoft\Windows\WER\ReportQueue` (and
`ReportArchive`): loaded module list, OS build, bucket. No `.dmp` by default,
so set up dump collection below.

## 2. Dump collection setup (for the next occurrence)

Run once in an elevated PowerShell:

```powershell
New-Item -Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\MediaSimilarityFinder.exe" -Force
New-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\MediaSimilarityFinder.exe" -Name "DumpFolder" -Value "D:\Temp\OpenCodeWork\dumps" -PropertyType ExpandString -Force
New-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\MediaSimilarityFinder.exe" -Name "DumpCount" -Value 3 -PropertyType DWord -Force
New-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\MediaSimilarityFinder.exe" -Name "DumpType" -Value 1 -PropertyType DWord -Force
```

Registry record:

| Value | Type | Meaning |
|---|---|---|
| `DumpFolder` | REG_EXPAND_SZ | Dump folder. Create it in advance |
| `DumpCount` | REG_DWORD | Kept dumps (oldest auto-deleted). 3 recommended |
| `DumpType` | REG_DWORD | 1 = mini (enough for thread plus stack). 2 = full (can reach GBs on a large-scan process, use with care) |

With the next forced termination, grab the `.dmp` plus its timestamp to pin
down the faulting thread and stack.

## 3. Known crash history

### 3-1. 2026-10-07 00:32:18 / 00:53:33 — 0xC0000409 fail-fast (cause open)

- Dev GPU build (`build-windows-gpu\Release`), twice in a row in a large scan.
- Same `ucrtbase.dll` offset (`0xa527e`) twice: a deterministic abort path,
  not heap corruption (which would show 0xC0000005).
- First died late in a scan (79% file progress); second died after 19 minutes
  with no progress display (explained as enumeration plus skip phases, not a
  hang). Needs a dump to pin the stack.
- Leading candidate: unhandled worker-thread exception into terminate then
  abort (`ScanWorker::run` caught `std::exception` only).

### 3-2. 2026-10-07 04:30:43 — 0xC0000409 fail-fast, third occurrence (fault path pinned by dump)

- Product GPU build 0.9.4.64 (`build-windows-gpu\Release`, timestamp
  `0x6AC5408D`); died 21 minutes into a `G:\Downloads\ss_twit` scan. The
  heartbeat was alive 8 seconds before death (walked=156481/listed=228000
  at 0% — enumeration phase).
- Event 1000: `0xc0000409` at offset `0xa527e` — same as the previous two.
  Same offset three times running means a deterministic abort path.
- Direct parsing of the dump
  (`D:\Temp\OpenCodeWork\dumps\MediaSimilarityFinder.exe.11104.dmp`,
  12.8MB mini):
  - Exception `0xC0000409` with parameter `0x7` is the `int 29h` inside
    ucrtbase `abort()` (`abort+0x4E`, confirmed by disassembly). Not a Qt
    fatal (no `msf_qt.log`).
  - The fault thread is the scan worker. The top of its stack matches the
    `catch (std::exception&)` handler region of `ScanWorker::run` (a
    `persistMatchesSnapshot` call plus an `e.what()` virtual call plus
    `emit failed` — identified by PDB-less IAT/disassembly reverse lookup
    against a shadow PDB build).
- Verdict: the 0.9.4.62 catch-all catches the first exception, but if
  persist or emit inside the handler throws again, it leaves the Qt slot
  for `terminate()` -> `abort()`. The dump points exactly at that chain.
  The dump cannot reveal the FIRST exception's origin (it died after being
  caught).
- Response: 0.9.4.65 wraps persist and emit in both handlers in independent
  try/catch blocks, removing every escape path. Details:
  `docs/build-history/0.9.4.65.en.md`.

### 3-3. Post-fill crash after a gradient match storm (test env, needs its own directive)

- Thousands of cross-matches streamed at once, then 0xC0000005 after a fill.
  Engine-only 240-file scan is clean. Recorded separately as a GUI-side
  match-storm volume issue, unrelated to the scroll regression.

## 4. Defense patch history (0.9.4.62, 0.9.4.65)

- Added `catch (...)` to `ScanWorker::run`. Checkpoints partial matches and
  reports failed, downgrading silent death to a recorded failure.
- Cause: the abort family from 3-1 above (non-standard exceptions went
  straight to terminate).
- SEH access violations still crash: MSVC builds without /EHa do not unwind
  those through catch(...), so real memory corruption keeps failing fast
  instead of being masked.
- Includes the `MSF_TEST_THROW_NONSTD` test seam. Never set by production code.
- Detail: `docs/build-history/0.9.4.62.en.md`.

### 0.9.4.65 — non-throwing handlers (response to 3-2 above)

- `persistMatchesSnapshot()` and `emit failed()` in both catch handlers are
  each wrapped in independent `try/catch(...)` blocks. If persist fails, the
  report is still attempted; no path lets an exception leave a handler.
- Cause: the 3-2 dump's fault stack points at the handler's persist path.
  `run()` is a Qt slot, so a handler escape goes straight to `terminate()`
  -> `abort()`.
- New `MSF_TEST_THROW_PERSIST` test seam. Combined with
  `MSF_TEST_THROW_NONSTD` it deterministically replays the dump's chain
  (persist throwing inside the handler). `crash_diagnostics_test` 7 checks
  (CPU and GPU).
- The no-masking-of-SEH-fail-fast principle is kept.
- Detail: `docs/build-history/0.9.4.65.en.md`.

## 5. Observability patch history (0.9.4.62)

- `installQtMessageLog()`: Qt message file sink. Per-message open, append,
  and close under a mutex: crash-safe and thread-safe. Installed on all three
  main() paths (GUI, headless, benchmark). Path: `%TEMP%\msf_qt.log`.
- Heartbeat gains `walked=<lastTotalN>` and `listed=<lastListN>`, so a rescan
  showing 0% for many minutes reads as slow enumeration rather than a hang.
  Existing field order kept (parser safe).
- Detail: `docs/build-history/0.9.4.62.en.md`.

## 6. Related documents

- `docs/build-history/0.9.4.62.{ko,en}.md` — patch detail and verification numbers.
- `docs/build-history/0.9.4.65.{ko,en}.md` — non-throwing handler patch and dump analysis.
- `docs/worklog/0.9.4.{ko,en}.md` — 0.9.4.62 / 0.9.4.65 entries (cause, measurements).
- `docs/architecture/image-burst-shot-similarity.{ko,en}.md` — separate topic
  (similarity verdicts). Do not confuse with crashes.
