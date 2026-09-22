# GitHub Actions Windows CPU build — 0.9.1.0

This project now contains a reproducible GitHub Actions workflow for the first Windows x64 CPU build.

## Workflow

`.github/workflows/windows-cpu.yml`

The workflow:

1. Runs on GitHub's `windows-2025` x64 hosted runner.
2. Initializes the MSVC x64 environment.
3. Uses the runner's preinstalled vcpkg.
4. Installs manifest dependencies: SQLite3, FFmpeg and Qt6 `qtbase` for `x64-windows`.
5. Configures CMake with CUDA explicitly disabled.
6. Builds the Qt6 GUI and CPU Core in Release mode.
7. Runs CTest.
8. Verifies that `MediaSimilarityFinder.exe` was produced.
9. Uses `windeployqt` to collect Qt runtime files.
10. Copies vcpkg runtime DLLs and creates a Windows x64 ZIP artifact.

GitHub's current Windows hosted runner documentation lists `windows-2025` as an x64 Windows runner and the Windows 2025 image includes Visual C++ 2022, Windows SDK, CMake and vcpkg.

## How to use

1. Create a GitHub repository.
2. Extract this source tree into the repository root.
3. Commit and push it.
4. Open **Actions** → **MediaSimilarityFinder 0.9.1.0 - Windows CPU**.
5. Use **Run workflow**.
6. After completion, download the artifact named:

`MediaSimilarityFinder-v0.9.1.0-CPU-Windows-x64`

The artifact is the first Windows CPU package, not a CUDA build.

## Important limitation

The GitHub runner can compile and run automated tests, but an interactive Qt GUI session is not a substitute for a user's Windows desktop smoke test. Therefore successful CI means **Windows/MSVC build + automated regression tests + executable/package creation**; it does not mean that every GUI interaction has been manually verified.

CUDA source remains present. `MSF_ENABLE_CUDA=OFF` only selects the CPU path for 0.9.1.0. The planned NVIDIA line remains 0.9.2.x.
