# MediaSimilarityFinder

## Current development version: 0.9.2.89

0.9.2.26 replaced the previous BK-tree candidate traversal with an exact 9-part multi-index hash for the normal Hamming-distance <=8 candidate stage. Full 64-bit Hamming verification preserves exact candidate semantics while substantially improving large-index scaling and reducing per-node allocation overhead.

Windows 11 x64 media duplicate/similarity search engine under active CPU/CUDA development.

### Current capabilities
- Incremental SQLite index with program-owned `Index` storage.
- Image/video fingerprinting and CandidateIndex acceleration.
- NVIDIA CUDA backend with CPU fallback.
- Resident real-time folder monitor with foreground-workload protection.
- Horizontally mirrored image/video similarity detection.
- Persistent video fingerprint cache.
- Image transformation-aware second-stage matching for center crops (4:3, 1:1, 9:16), including mirror variants.
- Video temporal second-stage crop-aware comparison API; expensive crop decoding is deferred until explicitly requested after the fast normal/mirror stage.
- Large-scale exact CandidateIndex acceleration using a 9-part pigeonhole/multi-index candidate filter for the normal D<=8 search range.
- Versioned persistent video fingerprint cache with prepared SQLite statements, WAL/busy-timeout settings, and stale algorithm-version invalidation.
- Optimized `candidatePairs()` generation using direct partition-bucket scanning with reusable deduplication state.
- Streaming Match delivery with optional full-result retention and configurable report-side match bounds.

### Development numbering
- `0.9.1.x`: CPU baseline
- `0.9.2.x`: GPU and advanced search development
- `1.0.0`: CPU + GPU complete target

Build history is maintained under `docs/build-history/` in Korean and English. Architecture documents are under `docs/architecture/`.
