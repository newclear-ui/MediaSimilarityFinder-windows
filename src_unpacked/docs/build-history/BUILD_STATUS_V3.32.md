# v3.32 Build Status

Date: 2026-09-02

## CPU validation
- CMake configure: PASS
- C++20 core build: PASS
- SQLite: PASS
- FFmpeg-enabled core build: PASS
- CTest: **24/24 PASS**
- Total CTest time: 17.29 s

## Added regression tests
- candidate_pairs_test: PASS
- media_pipeline_batch_test: PASS
- search_report_test: PASS

## Important limitations
- Qt6 is not installed: Windows GUI was not compiled or runtime-tested here.
- NVIDIA CUDA Toolkit/nvcc is not installed: CUDA compilation/runtime was not tested here.
- NVDEC was not tested.
