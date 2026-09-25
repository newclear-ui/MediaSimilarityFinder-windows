# Build History / English

This directory records MediaSimilarityFinder release history and important architecture decisions.

- [Korean](README.ko.md)
- Ordinary changes are summarized briefly in their version log.
- Important architecture changes record **why the change was needed → previous structure → new structure → resolved state → validation → future impact**.
- Replaced implementations are preserved as non-build snapshots under `docs/architecture/legacy/`. Dead code is not left commented out in the active source tree.

## Version list

| Version | Summary |
|---|---|
| 0.9.1.0 | CPU baseline |
| 0.9.2.0~0.9.2.3 | NVIDIA CUDA/GPU foundation |
| 0.9.2.4~0.9.2.7 | Portable Index / SQLite / CandidateIndex foundation |
| 0.9.2.8 | Real-time Monitor foundation |
| 0.9.2.9 | Windows ReadDirectoryChangesW event monitoring |
| 0.9.2.10 | Live comparison-index synchronization |
| 0.9.2.11 | Condition-variable scheduler / exponential backoff |
| 0.9.2.12 | Resident CandidateIndex / root ownership / event resync / Windows path hardening |
| 0.9.2.13 | Foreground-workload protection load gate |
| 0.9.2.14 | MonitorStatus / telemetry |
| 0.9.2.15 | Manual Pause/Resume |
| 0.9.2.16 | Monitor Settings GUI |
| 0.9.2.17 | Mirror-aware Similarity |
| 0.9.2.18 | Mirror CandidateIndex optimization |
| 0.9.2.19~0.9.2.20 | Image Crop-Aware candidate/comparison pipeline |
| 0.9.2.21~0.9.2.22 | Video Temporal Crop-Aware candidate/comparison integration |
| 0.9.2.23~0.9.2.24 | Video decode / Monitor stability optimization |
| 0.9.2.25 | SQLite/Index performance and migration hardening |
| 0.9.2.26 | CandidateIndex 9-part Multi-Index Hash replacement |
| 0.9.2.27 | Video fingerprint persistent cache versioning/SQLite reuse hardening |
| 0.9.2.28 | CandidateIndex candidatePairs large-scale candidate generation optimization |
| 0.9.2.29 | ScanPipeline streaming candidate consumption and complete-index short-circuit |
| 0.9.2.30 | Scan match callback delivery and duplicate result-storage removal |
| 0.9.2.31 | Large-result Match streaming / report retention bounds |
| 0.9.2.32 | Compact index-based large-result Match streaming callback |
| 0.9.2.33-test | Windows CPU test interim (not a baseline, folded into 0.9.2.34) |
| 0.9.2.34 | Windows common compatibility alignment (VS18 2026 baseline, CUDA unchanged) |
| 0.9.2.35 | Windows CPU build/test hardening (36/36 PASS, GUI runtime verified) |
| 0.9.2.36 | Explorer-style UI rewrite (grid+list switch, KO/EN, marks, live streaming) |
| 0.9.2.37 | UI feedback round (in-settings language, 6 group views, keyboard mark, splitter save) |
| 0.9.2.38 | Unified 3-pane UI + video tab/ignore list/throughput (37 tests PASS) |
| 0.9.2.39 | Screenshot feedback round (toolbar cleanup, folder tab removal) |
| 0.9.2.40 | CUDA first-hardware validation (RTX 3080 Ti, 38 tests PASS, kernels untouched) |
| 0.9.2.41 | Non-ASCII (Korean) filename crash fix + unicode_path_test (39 tests PASS) |
| 0.9.2.42 | Search feel fixes (walk progress, merged buttons, state save) |
| 0.9.2.43 | Post-stop state restore + pause scope |
| 0.9.2.44 | Large-scan live verification + streaming switch |
| 0.9.2.45 | Scan activity log (0% CPU triage) |
| 0.9.2.46 | Scan pipelining (walk ‖ analyze) |
| 0.9.2.47 | Checkpointed scans (interruption keeps progress) |
| 0.9.2.48 | No console window + splitter warning fix |
| 0.9.2.49 | Selective scan + live matching + multicore |
| 0.9.2.50 | Match persistence (quick load) + preview WIC fallback |
| 0.9.2.51 | Video scene-changes search + GPU status visibility + cache v4 + duration gate |
| 0.9.2.52 | Incremental match checkpointing during scan (resume guarantee) |
| 0.9.2.53 | Preview-toggle crash fix + Explorer-style Tiles view |
| 0.9.2.54 | Folder h-scroll, XL default, UI-state restore, progress throttle, High mode, multicore |
| 0.9.2.55 | Window-layout non-restore root-cause fix (void QSettings) |
| 0.9.2.56 | Scan-time UI freeze fix (thumbnail decode budget) |
| 0.9.2.57 | Unstoppable final-analyze fix + recent-folders favorites |
| 0.9.2.58 | Blank group pane fix (placeholder icon storm) |
| 0.9.2.59 | Blank pane recurrence fix (list-rebuild livelock) |
| 0.9.2.60 | Icon/list view restore + favorites dedup + color previews |
| 0.9.2.61 | Uniform preview sizes + wider shell lane |
| 0.9.2.62 | Similarity-inspired additions (Pairs, ETR, thumbnail skip list) |
| 0.9.2.63 | Disk thumbnail cache + favorites/filename/resolution/0B fixes |
| 0.9.2.64 | Direct stop delivery + video diagnostic counters |
| 0.9.2.65 | Video L1 multi-anchor + variant dedup fix |
| 0.9.2.66 | E2E video regression + EXIF rotation + cleanups |
| 0.9.2.67 | L3 SSIM verification cells + thumb48 cache v5 |
| 0.9.2.68 | QuickLook integration resume |
| 0.9.2.69 | Toolbar cleanup + live intermediate results + summary/count fixes |
| 0.9.2.70 | Image false-positive second-stage gate |
| 0.9.2.71 | Engine version stamp + stored-match revalidation |
| 0.9.2.72 | Engine/DB version split (semver) |
| 0.9.2.73 | Accepted audit items (SSIM gate, QuickLook, docs) |
| 0.9.2.74 | Monitor toolbar cleanup (pause removed, toggle highlight, menu right) |
| 0.9.2.75 | Toolbar, detail, and summary UI round |
| 0.9.2.76 | Console-flash removal + spawn-free resolutions |
| 0.9.2.77 | Button style unification + detail/grid intrusion fixes |
| 0.9.2.78 | Fixed pre-count and selection-filter alignment |
| 0.9.2.79 | Reuse existing Explorer window + three GPU states |
| 0.9.2.80 | Reference tie-break (similarity first, then resolution/size) |
| 0.9.2.81 | Single-tree revalidation + GUI test plugin path |
| 0.9.2.82 | Portable launch fix (plugin layout + bundled CRT) |
| 0.9.2.83 | Startup self-check (platform plugin pre-flight) |
| 0.9.2.84 | Build output runs out of the box (in-build Qt plugin deploy) |
| 0.9.2.85 | Self-check path bug fix (GetModuleFileNameW) |
| 0.9.2.86 | Fast pHash, reveal fallback, ffprobe DLLs |
| 0.9.2.87 | CUDA kernel 1e-7 snap (CPU/GPU flat-image agreement) |
| 0.9.2.88 | Reveal root fix (CLSCTX_ALL) |
| 0.9.2.89 | Right-click selection (reveal misfire fix) |
| 0.9.2.90 | External report follow-ups (sampling, verify, index, 2nd stage) |
| 0.9.3.1 | Search benchmark log (JSON, renumbered from 0.9.2.91) |

See the version-specific `.ko.md` / `.en.md` files for details.

## Architecture documents

- `../architecture/realtime-monitor.ko.md` / `.en.md`
- `../architecture/candidate-index.ko.md` / `.en.md`
- `../architecture/reference-similarity.ko.md` / `.en.md`
- `../architecture/runtime-audit-0.9.2.63.ko.md` / `.en.md`
- `../architecture/storage-design.md`

## Legacy

Previous implementation/design snapshots are preserved under `../architecture/legacy/`.
