# Windows x64 CUDA build — 0.9.2.2

Prerequisites: Visual Studio 2022 C++ workload, CMake, vcpkg, Qt6/FFmpeg/SQLite through vcpkg, and the NVIDIA CUDA Toolkit (`nvcc`).

Run `scripts/build_windows_cuda.ps1` from a VS 2022 developer PowerShell. The preset also exists as `windows-cuda`.

The default CUDA architectures are `75;86;89`; the RTX 3080 Ti uses compute capability 8.6, so SM 86 is included. Adjust `MSF_CUDA_ARCHITECTURES` if a different NVIDIA GPU is the target.

This build keeps the CPU backend. If CUDA runtime/device detection fails, the MediaPipeline uses the CPU pHash reference path. The CUDA regression test compares GPU hashes against the CPU reference when an NVIDIA device is present.
