# v3.27.0 Build Status

Date: 2026-09-02

Configuration tested:
- CMake: 3.31.6
- Compiler: GCC 14.2
- SQLite: 3.46.1
- FFmpeg: 7.1.5
- `MSF_BUILD_GUI=OFF`
- `MSF_ENABLE_CUDA=OFF`

Result:
- Build: PASS
- CTest: 21/21 PASS
- Real FFmpeg video test: PASS
- Candidate benchmark: PASS (1.60x measured indexed-query speedup on N=6000)
- Partial/offset temporal alignment: PASS (100%)
- Persistent video cache API/persistence-hit regression: PASS

Not tested because unavailable in the current environment:
- Qt6 Windows GUI runtime
- Windows WIC/shell behavior
- NVIDIA nvcc/CUDA runtime
- NVDEC
