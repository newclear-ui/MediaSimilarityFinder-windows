# Build Status 0.9.2.6

- Central Index: stable folder-ID fast path with metadata verification.
- SQLite: WAL/NORMAL/busy-timeout defaults and transactional scan updates.
- Candidate search: image and video fingerprints use separate candidate indexes, avoiding cross-media candidate generation.
- Regression: 26/26 tests PASS.
- Benchmark: N=6000, indexed query 300.445 ms, linear query 840.764 ms, 2.7984x.
- CUDA: toolkit unavailable in this build environment; existing CUDA architecture retained but not runtime-verified.
- Windows Qt GUI/CUDA: not locally verified in this Linux build environment.
