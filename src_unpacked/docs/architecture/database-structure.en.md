# Database Structure (current)

> This document records MediaSimilarityFinder's **current persistent storage structure (schema / versions / sharing model)**.
> Per-version change history lives in `docs/build-history/`; measured numbers live in `docs/architecture/database-verification-benchmark.en.md`.
> The higher-level description of storage location and transaction policy is in `docs/architecture/storage-design.ko.md`.

## 1. Location and files

Each scan root gets one application-owned index directory (`src/index_manager.cpp`).

```text
<appDir>/Index/<folderId>/
├─ metadata.json          # rootPath, created, lastScan, schemaVersion, applicationVersion
├─ index.sqlite           # main index DB (images + videos shared)
├─ index.sqlite-wal / -shm (WAL mode)
└─ video_cache.sqlite     # video fingerprint cache DB (video-only, separate)
```

- `folderId` = 32 hex from two FNV-1a64 passes (two seeds) over the canonical root path (lowercased, `\`→`/`, trailing separator removed).
- Before reusing an existing index the `metadata.json` `rootPath` is verified (collision protection).
- No index file is ever written inside a scanned folder.

## 2. index.sqlite — main index (images + videos shared)

`src/database.cpp` `initialize()` creates the tables.

### 2.1 `files` — one row per file (images and videos together)

| Column | Type | Meaning |
|---|---|---|
| path | TEXT PK | canonical UTF-8 path |
| size | INTEGER | file size (bytes) |
| modified | INTEGER | mtime (Windows epoch ms) |
| quick_hash | TEXT | FNV-1a of the first 64 KiB (change detection) |
| fingerprint | INTEGER | completed fingerprint (0 = skeleton, unanalyzed) |
| kind | INTEGER | 1=Image, 2=Video |
| duration | REAL | video length (s) |
| mirror_fingerprint | INTEGER | mirrored fingerprint |
| crop_4x3 / crop_1x1 / crop_9x16 | INTEGER | crop fingerprints |
| mirror_crop_4x3 / mirror_crop_1x1 / mirror_crop_9x16 | INTEGER | mirrored crop fingerprints |
| analysis_failed | INTEGER | analysis-failure flag |

- Index: `idx_files_modified(modified)`.
- `fingerprint=0` is a skeleton (admitted but unanalyzed) and is invisible to matching. `analysis_failed` separates `false` (interrupted/not reached, retry) from `true` (failed on identical content, do not retry).
- A changed file has its old row removed and a skeleton written before re-analysis (0.9.4.76 crash frontier).

### 2.2 `matches` — retained duplicate pairs (images and videos together)

| Column | Type | Meaning |
|---|---|---|
| left_path, right_path | TEXT | pair (ordered: left ≤ right) |
| percent | REAL | similarity % |

- PK: `(left_path, right_path)`. Indexes: `idx_matches_left(left_path)`, `idx_matches_right(right_path)`.
- Image and video pairs are stored in the **same table** (no kind column).

### 2.3 `thumbs` — GUI thumbnail cache (display only)

| Column | Type | Meaning |
|---|---|---|
| path | TEXT PK | path |
| modified, size | INTEGER | validation stamp |
| quick_hash | TEXT | validation |
| jpeg | BLOB | encoded JPEG |

- Validated by size+mtime. `pruneThumbs()` drops rows whose path left `files`.
- ThumbnailStore (backend) opens the same `index.sqlite` as a second connection (WAL coexistence).

### 2.4 `file_trace` — scan forensics (0.9.4.78)

| Column | Type | Meaning |
|---|---|---|
| path | TEXT PK | path |
| admitted_ms, started_ms, finished_ms | INTEGER | UTC epoch ms |
| outcome | TEXT | analyzed / failed / else |

- Crash investigation. `finished_ms=0` means the worker died mid-unit.

### 2.5 `content_hash` — incremental dataset fingerprint cache (0.9.4.89, DB 1.0.6)

| Column | Type | Meaning |
|---|---|---|
| rel | TEXT PK | canonical relative path |
| size, modified | INTEGER | reuse when (size,modified) match |
| sha256 | TEXT | full-file SHA-256 |

- Lets a rescan re-hash only changed/new files when the file set changes.

### 2.6 `meta` — key/value

| key | Meaning |
|---|---|
| db_version | DB schema version (now 1.0.6) |
| engine_version | verdict-generation version (now 1.5.1) |
| sampling_generation | video sampling generation (now "3") |
| analyze_frontier | B-slice resume frontier |
| dataset_fp_cache | whole-dataset fingerprint cache (key|fp|files|bytes) |

## 3. video_cache.sqlite — video fingerprint cache (separate)

`src/video_fingerprint.cpp` opens it as its own DB.

### 3.1 `video_fingerprint_cache`

| Column | Type | Meaning |
|---|---|---|
| path | TEXT PK | path |
| size, modified | INTEGER | stamp |
| quick_hash | TEXT | stamp |
| duration | REAL | length |
| step_count | INTEGER | sample count |
| cache_version | INTEGER | cache format (v9) |
| payload | BLOB | frame fingerprints (base+crop) |

- Index: `idx_vfc_stamp(size,modified)`.

## 4. Image/video sharing model

| Store | Scope | File |
|---|---|---|
| files / matches / thumbs / content_hash / file_trace / meta | **shared images+videos** | `index.sqlite` |
| video_fingerprint_cache | **video-only** | `video_cache.sqlite` |
| matching candidate indexes (runtime) | split in memory | (not a file) |

So files, matches, and thumbnails share one DB (distinguished by `files.kind`), while video additionally keeps its own fingerprint-cache DB.

## 5. PRAGMA / concurrency

- `journal_mode=WAL`, `synchronous=NORMAL`, `busy_timeout=5000`, `temp_store=MEMORY`.
- WAL lets the engine (scan) connection and the GUI/ThumbnailStore connection coexist.

## 6. Version constants

| Constant | Value | Location |
|---|---|---|
| kDatabaseVersion | 1.0.6 | `src/database.h` |
| kEngineVersion | 1.5.1 | `src/media_search_engine.h` |
| kBenchmarkSchemaVersion | 9 | benchmark |
| kCacheFormatVersion | 9 | `src/video_fingerprint.h` |
| kSamplingGeneration | "3" | `src/media_search_engine.h` |

- Migrations are **additive** (columns/tables added; old code ignores them). There are no destructive changes.
- `db_version` is stamped to the current value on a successful open.

## 7. Planned pair-verification speedups (preliminary verdict)

> These are **preliminary verdicts**, to be confirmed/adjusted by measurements in `docs/architecture/database-verification-benchmark.en.md`.
> Figures are derived from the 0.9.4.91 log (177,372 files; image verify `verifyKeyMs` 170 s, `verifyDecodeMs` 1095 s, `verifyFrameSsimMs` 0.48 s, worker-summed).

| ID | Item | Mechanism | Preliminary verdict | Risk |
|---|---|---|---|---|
| V-1 | Remove the 64 KiB quickHash read from the verify cache key | Use `FileState.quickHash` (DB) or just (size,modified) → zero file reads | **CANDIDATE (low risk)** — removes most of verifyKeyMs (~13%) | low (key integrity only) |
| V-2 | Enlarge the verify buffer cache | `kVerifyCacheMax` 32 → 512–2048 (~12 KB/entry) | **CANDIDATE (low risk)** — higher hit rate lowers decode misses | low; effect data-dependent |
| V-3 | Reduce decode cost | reduced-resolution decode / derive fixed from aspect / faster interpolation | **CONDITIONAL** — up to 31% of decode | verdict-change risk → parity validation required |
| V-4 | Reuse the index-phase decode | Keep 64×64 (+aspect) buffers from indexing (memory/disk) | **UNDER REVIEW** — could remove most decode | high memory/disk cost |
| V-5 | Fewer candidate pairs | Tighten the L1 Hamming gate / cheap second filter | **CONDITIONAL** | accuracy validation required |
| V-6 | Overlap decode I/O | Async prefetch to cut high-latency drive waits | **UNDER REVIEW** | implementation complexity |
| V-7 | GPU SSIM for images | Generalize `ssimBatch` (48×48) to 32×32 + wiring | **LOW PRIORITY** — SSIM is 0.04% of verify | kernel generalization needed |

- Recommended order: **V-1 → V-2 → (V-4 or V-3)**.
- Every change must keep **verdicts unchanged** (engine 1.5.1). CPU/GPU numeric parity and stored-pair revalidation impact must be checked.
- Detailed numbers, run conditions, refutation reasons and revisit conditions go to build-history and the worklog Performance/Tuning Experiment Index (AGENTS item 9).

## 8. Measurement link

- Measurement plan/results/verdict: **`docs/architecture/database-verification-benchmark.en.md`** (KO: `.ko.md`).
- Experiment lineage/decisions: the **Performance / Tuning Experiment Index** in `docs/worklog/0.9.4.ko.md`.

## 9. Related documents

- `docs/architecture/storage-design.ko.md` — index location, transactions, thumbnail policy (higher level).
- `docs/architecture/video-gpu-hash.ko.md`, `docs/architecture/video-ssim-gpu.ko.md` — video hash/SSIM GPU paths.
- `docs/build-history/0.9.4.76.*` (crash frontier), `0.9.4.81.*` (B-slice frontier), `0.9.4.86.*`/`0.9.4.89.*` (dataset fingerprint cache).
