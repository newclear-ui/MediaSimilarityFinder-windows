# Media Similarity Finder v3.7.0

Integrated core for recursive media scanning, incremental indexing, image/video fingerprinting, candidate search, similarity grouping and SQLite persistence.

Build:
```bash
cmake -S . -B build -DMSF_ENABLE_CUDA=OFF -DMSF_ENABLE_FFMPEG=OFF -DMSF_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

For Windows, enable CUDA when the CUDA toolkit is installed. Qt GUI integration is maintained separately because the verification environment does not contain Qt6.
