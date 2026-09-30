# Storage / Portable Index Design

## Current design

MediaSimilarityFinder does not write its search index into the folder being scanned.
All managed indexes are stored below the application's own `Index` directory.

```text
C:\MediaSimilarityFinder\
├─ MediaSimilarityFinder.exe
└─ Index\
   ├─ <stable-folder-id>\
   │  ├─ metadata.json
   │  ├─ index.sqlite
   │  │  ├─ files
   │  │  ├─ matches
   │  │  └─ thumbs
   │  └─ video_cache.sqlite
   └─ ...
```

Each scan root is normalized to a canonical path and mapped to a stable folder ID.
The metadata record is verified before an existing index is reused.

## Important rule

The scan target itself remains free of MediaSimilarityFinder index files.
When the application directory is scanned, its own `Index` subtree is excluded.

## Transaction and checkpoint policy

Index updates are transactional, but the current scan intentionally uses checkpoints:
completed work is committed periodically so a cancellation does not lose all progress.
On cancellation, the already committed checkpoints remain available; only the current
uncommitted transaction is rolled back on an actual failure.

Deleted-file cleanup is performed only after the directory walk completes successfully,
so a cancelled/incomplete walk does not incorrectly delete still-unseen index rows.

When a previously indexed file changes, its old database row is removed before
re-analysis and a skeleton row is written before fingerprint analysis. This prevents
stale fingerprints from remaining associated with a file while analysis is in progress.

## Thumbnail cache

`index.sqlite` also contains the persistent GUI thumbnail cache in the `thumbs` table.
Entries are keyed by path and validated against file mtime + size. Successful preview
decodes are stored as 192px JPEGs (quality 70) and can be reused on later scans without
consuming the per-tick GUI decode budget. The GUI opens a second SQLite connection to
the same index database; WAL mode allows this to coexist with the scan connection.

Orphaned thumbnail rows are pruned when a scan finishes.

## Evolution

This document describes the current storage design. Version-specific changes are
recorded separately under `docs/build-history/`.


## Benchmark storage separation

Benchmark artifacts are not part of normal search index storage.

Conceptual layout:

~~~text
Application data root
├─ Index/
│  └─ <root-id>/
│     ├─ metadata.json
│     ├─ index.sqlite
│     └─ video_cache.sqlite
└─ Benchmark/
   ├─ GUI/
   │  └─ <source-label>_<root-id-short>/
   │     ├─ auto.json
   │     ├─ cpu.json
   │     └─ gpu-max.json
   └─ Console/
      └─ suite-<suite-id>/
         ├─ suite.json
         ├─ auto.json
         ├─ cpu.json
         └─ gpu-max.json
~~~

- Benchmark storage remains physically/logically separate from Index.
- GUI retains only the latest result for each of the three modes.
- Console retains results cumulatively.
- The exact application-data base directory continues to follow the existing portable-aware path policy; this design does not create a second unrelated root policy.
- Benchmark folder names are human-identifiable but never use the raw full source path. Use a sanitized basename plus a short stable root id, while storing the canonical full sourceRoot in JSON.
- Benchmark-specific index/cache state must never be treated as the normal Search Index.
- GUI must never automatically load Console benchmark files.
- A benchmark must not leave DB/cache artifacts inside the scanned source folder.

## Benchmark Console storage/UI finalization — 2026-09-30

The benchmark storage layer is finalized around **per-file immediate persistence** and a terminal UI that is only a view over persisted records.

### Canonical Console layout

~~~text
Benchmark/Console/suite-<suite-id>/
    suite.json
    runs.jsonl
    summary.json
~~~

Each completed file/mode result is appended to runs.jsonl. The recommended logical sequence is AUTO, CPU-only, GPU-max for each file, followed by the next file.

The JSONL journal is the recovery source for interrupted runs. summary.json is generated/updated from the journal and must never be the only copy of per-file evidence.

### Terminal rendering contract

Interactive output is divided into:

1. fixed three-row execution header,
2. CURRENT FILE detail view,
3. compact completed-file history,
4. final or partial summary.

The fixed header never wraps. Long paths use middle ellipsis on screen only. Full values remain in JSON. Non-interactive output is line-oriented and uses the same persisted data model.

### GUI/Console retention

GUI keeps the latest result for each of AUTO / CPU-only / GPU-max per source folder. Console retains cumulative Suites and does not automatically delete prior benchmark evidence.

### Legacy preservation

Existing benchmark source/schema and the v0.9.4.43 baseline backup/tag are historical baselines. New storage/telemetry work must remain traceable to that legacy benchmark rather than deleting or silently rewriting its history.

