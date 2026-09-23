# External Reference: GAR Similarity v2.5.1 Analysis

> Source: `C:\project\Similarity.zip` (binaries only, no source). GAR Software, 2007-2021, discontinued.
> Audio duplicate search is the core. Images are a secondary feature since v1.7.1 (`content` + `experimental`).
> Closed source: no code reuse. Ideas and behavior patterns only (clean-room reference).

## Evidence

- `Similarity.exe` 15MB, `Decoder.exe`, `Scripter.exe`, `Speech.exe`
- `Scripts/*.js` (24 files: `example-async.js`, `test.js`, `scan-samename.js`, `automark.js`, `export-results.js`)
- `Locale/English.lng` (v2.0.1, full UI strings), `Locale/Korean.lng` (older v1.4.0)
- `readme.txt`, `changes.txt` (full history v0.2-v2.5.1)
- Static unicode-string extraction from `Similarity.exe` (pipe name, OpenCL, cache/decoder strings)

## Search engine

```
queue(file, algs) -> compare(file, callback) -> calculate(i1, i2, algs, cb) -> results.add()
process(folder, onfile, onfolder)  // recursive stream, Scripts API
```

- Multi-stage scores: audio `tags -> content -> precise -> speech`, images `content + experimental`. Per-algorithm thresholds with OR logic; mixed formulas possible (`example.js` tags x content x precise mix).
- `duration` prefilter avoids N^2 (`scan-samename.js`, `scan-with-restrictions.js`).
- `v2.2.0 work queue` multiprocessor queue, work-thread limit option, `precise` offloaded to OpenCL (CUDA/AMD).
- `v2.3.1 Global optimization indexing` for huge collections.
- Mirrored/flipped image detection (v1.7.1, v1.9.0) - same idea as our mirror handling.

## File stream

- `Decoder.exe` out-of-process + named pipe (`\\.\pipe\similarity...`). Crash isolation, 30s idle auto-shutdown, failed-file skip list (v1.1, v1.8.4, v1.9.0).
- Unified cache: view cache == compare cache (v1.8.2); analysis-result cache shortens rescans.
- `journal` real-time autosave (v1.9.0) - results survive crashes. `ignores.list` stored separately.
- Corrupt-file tolerance (v1.9.1: 8-bit/damaged WAV), per-decoder enable/priority/extension settings.

## In-scan UI

- Start / Pause / Stop, `Estimated Time Remaining`, taskbar progress (v1.5.3), status bar `duplicate(s) / Cache / New`.
- Tabs: `Folders / Results:Audio / Results:Images / Analysis`. Grouped/flat modes, `Pairs` count column (v1.8.1), bold base file.
- `Automark` (thresholds + folder priorities + formats + tags), `Rearrange`, `Invert marked`, Analysis<->Duplicates synchronized delete/swap/rename.
- Power-state restore after scan (v1.8.2), minimize to tray, per-folder Mark/Unmark.

## Mapping to our project

| Similarity | MediaSimilarityFinder current state | Direction |
|---|---|---|
| journal autosave | 5s checkpoint + `loadMatches` quick load (equivalent) | Keep, use as validation |
| duration prefilter | video duration gate exists (`scan_pipeline.cpp`) | Keep video gate; not needed for images |
| decoder isolation + skip list | in-process decoders, failures retried | Introduce failure skip list in 0.9.2.62 |
| Pairs column | files-per-group only | Add Pairs column in 0.9.2.62 |
| remaining-time display | elapsed only | Add ETR in 0.9.2.62 |
| Automark priorities | mark/invert only | Later (folder-priority dialog) |
| V8 scripting | none | Deferred (too broad) |
| OpenCL precise | CUDA image hashing (`gpu_backend`) | Structurally covered, no change |

## License note

- Similarity itself is a GAR proprietary license. Do not bundle or copy `Decoder.exe` / `Similarity.exe` / `Scripts` verbatim.
- The OSS components in `readme.txt` (libFLAC/libvorbis/TagLib/V8, etc.) apply only if newly introduced; our baseline stays `vcpkg.json`.
- This document records observed behavior as ideas only; no decompilation or code replication.
