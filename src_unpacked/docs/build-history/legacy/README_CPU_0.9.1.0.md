# Media Similarity Finder 0.9.1.0 — CPU Initial Build

This release is the first Windows-oriented CPU initial build preparation.

## Runtime policy

- CPU backend is the primary execution path.
- CUDA/NVIDIA backend source remains in the project and is **not removed**.
- If CUDA is unavailable, CMake automatically falls back to the CPU backend.
- Qt6 GUI remains optional at configure time; on a Windows machine with Qt6 installed it is built.
- FFmpeg remains optional at configure time.

## Versioning

- `0.9.1.x`: CPU initial-build series
- `0.9.2.x`: GPU/NVIDIA expansion series
- `1.0.0`: CPU + GPU target completion
- `1.0.1+`: post-1.0 bug/feature maintenance

## Windows build

See `BUILD_WINDOWS_CPU.md` and `scripts/build_windows_cpu.ps1`.

## Current validation

The supplied Linux development environment has no Qt6 or NVIDIA CUDA Toolkit. Therefore the CPU Core was built and tested here, but a native Windows GUI/CUDA build is not claimed as verified.
