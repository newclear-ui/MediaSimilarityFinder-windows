# MediaSimilarityFinder 0.9.2.5 — Portable Index Storage

This build uses application-owned index storage for portable deployments.

## Storage layout

If the executable is located at:

`C:\MediaSimilarityFinder\`

indexes are stored under:

```text
C:\MediaSimilarityFinder\
├─ MediaSimilarityFinder.exe
├─ Index\
│  ├─ <stable-folder-id>\
│  │  ├─ metadata.json
│  │  ├─ index.sqlite
│  │  └─ video_cache.sqlite
│  └─ ...
└─ ...
```

A scanned folder such as `D:\Movie` is never used as an index storage location.
Each canonical scan root gets its own independent index directory. Re-scanning the same root reuses that index and therefore preserves incremental analysis.

## Portable behavior

- No installer is required.
- The application directory is the portable storage root.
- The target media folders are read/scanned but are not modified to store MediaSimilarityFinder data.
- GPU can be enabled or disabled from the GUI.
- CUDA remains optional at build time; CPU fallback remains available.

## Important deployment note

The portable package must be extracted to a writable directory. A future traditional installer should use a user-writable location such as `%LOCALAPPDATA%` for the Index directory rather than writing under `Program Files`.
