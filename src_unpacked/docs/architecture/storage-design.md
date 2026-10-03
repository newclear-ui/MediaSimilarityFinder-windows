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


## Detailed Log and Benchmark Storage Separation

GUI detailed logs belong to the real user workload. They are not a separate benchmark run.

The GUI must not create Benchmark Run/Stop/Pause UI merely to collect diagnostics.

The final durable GUI log path is an S4 implementation decision. The semantic rule is fixed: GUI logs are diagnostic evidence from real user work.

### Console benchmark

Application data root
└─ Benchmark/
   └─ Console/
      └─ suite-<suite-id>/
         ├─ suite.json
         ├─ runs.jsonl
         └─ summary.json

- runs.jsonl is the recovery source for per-file/per-mode evidence.
- summary.json is derived and never authoritative.
- Console benchmark results are retained for long-term comparison.
- Benchmark runtime/index/cache is isolated from the normal Search Index.
- GUI does not automatically read Console benchmark history.
- Console does not automatically ingest GUI detailed logs as benchmark history.

### Semantic boundary

GUI
  Search/Update
    └─ [상세 로그]
         └─ TelemetryRecorder / UserDiagnostic

CLI
  --benchmark
    └─ BenchmarkSession / BenchmarkRunner
         └─ TelemetryRecorder / Benchmark

Benchmark and Telemetry are not synonyms.
