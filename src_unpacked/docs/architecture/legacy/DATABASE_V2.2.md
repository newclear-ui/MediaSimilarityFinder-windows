# v2.2 Incremental Database

The first persistence layer is now attached to the clean v2.1 core.

Stored file state:
- absolute/normalized path
- file size
- modified timestamp
- quick hash placeholder

Classification:
- unchanged -> skip media analysis
- added -> analyze
- modified -> re-analyze
- deleted -> remove from persistent state

The storage interface is intentionally isolated so the current test backend
can later be replaced by SQLite without changing scanner/similarity APIs.
