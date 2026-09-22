# Storage / Portable Index Design

## Current design

MediaSimilarityFinder does not write its search index into the folder being scanned.
All managed indexes are stored below the application's own `Index` directory.

```text
C:\MediaSimilarityFinder\
├─ MediaSimilarityFinder.exe
├─ Index\
│  ├─ <stable-folder-id>\
│  │  ├─ metadata.json
│  │  ├─ index.sqlite
│  │  └─ video_cache.sqlite
│  └─ ...
└─ Cache\
```

Each scan root is normalized to a canonical path and mapped to a stable folder ID.
The metadata record is verified before an existing index is reused.

## Important rule

The scan target itself remains free of MediaSimilarityFinder index files.
When the application directory is scanned, its own `Index` subtree is excluded.

## Transaction policy

Index changes made during an incremental scan are transactional. A cancelled scan
rolls back its changes. When a previously indexed file changes, its old fingerprint
is removed before re-analysis so a failed analysis cannot silently leave a stale
fingerprint in the committed index.

## Evolution

This document describes the current storage design. Version-specific changes are
recorded separately under `docs/build-history/`.
