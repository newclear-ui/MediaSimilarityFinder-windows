# v3.12.0 build status

Core verification performed in the available Linux build environment:

- CMake configure: PASS
- Core build: PASS
- CTest: 14/14 PASS
- Real FFmpeg video test: PASS
- SQLite persistence tests: PASS
- Incremental index tests: PASS

Environment limitations:
- Qt6 is not installed here, so the Windows GUI target could not be compiled in this environment.
- NVIDIA CUDA toolkit/nvcc is not installed here, so CUDA execution could not be verified.
- The GUI source is intended for Windows/Qt6 and is not represented as runtime-verified here.
