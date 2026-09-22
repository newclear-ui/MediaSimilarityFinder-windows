# Media Similarity Finder 0.9.1.0 - CPU build status

## Current checkpoint
- Version: 0.9.1.0
- Target: Windows x64 CPU initial build
- CUDA source/backend: retained for future 0.9.2.x
- GUI source: retained and configured through Qt6

## CPU validation in current Linux development environment
- CMake configure: PASS
- C++20 CPU core build: PASS
- SQLite linkage: PASS
- CTest: 24/24 PASS
- FFmpeg native development libraries: unavailable in this environment; video tests use the existing ffmpeg/ffprobe fallback path
- Qt6: unavailable in this environment; Windows GUI was not compiled here
- NVIDIA CUDA/nvcc: unavailable; CUDA runtime was not tested

## Windows build status
The GitHub Actions workflow targets the GitHub-hosted `windows-2025` x64 runner with MSVC, vcpkg manifest dependencies, Qt6, FFmpeg and SQLite. A successful Windows runner execution is required before claiming the Windows executable has been built.

## Recent fix
The GUI executable now supports `--version` and `--help`, allowing the CI smoke check to terminate without opening an interactive window.
