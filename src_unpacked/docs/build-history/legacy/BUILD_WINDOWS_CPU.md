# Windows x64 CPU Build — 0.9.1.0

## Prerequisites

Recommended:

- Windows 11 x64
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.20+
- Qt 6.x with MSVC 2022 kit
- vcpkg
- Git

FFmpeg is optional for the first GUI smoke test, but required for actual video analysis. SQLite is supplied through vcpkg.

## Configure

From a **Developer PowerShell for VS 2022**:

```powershell
$env:VCPKG_ROOT = 'C:\src\vcpkg'
& "$env:VCPKG_ROOT\bootstrap-vcpkg.bat"
& "$env:VCPKG_ROOT\vcpkg.exe" install sqlite3:x64-windows ffmpeg:x64-windows

cmake -S . -B build-windows-cpu `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DMSF_ENABLE_CUDA=OFF `
  -DMSF_ENABLE_FFMPEG=ON `
  -DMSF_BUILD_GUI=ON `
  -DMSF_BUILD_TESTS=ON
```

Qt6 must be discoverable by CMake, normally by setting `CMAKE_PREFIX_PATH` to the Qt MSVC installation if it is not already configured:

```powershell
-D CMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
```

(Use the actual installed Qt path.)

## Build

```powershell
cmake --build build-windows-cpu --config Release --parallel
ctest --test-dir build-windows-cpu -C Release --output-on-failure
```

## GUI smoke test

If Qt6 was found, the executable is:

```text
build-windows-cpu\Release\MediaSimilarityFinder.exe
```

Run `windeployqt` from the matching Qt MSVC installation to collect Qt runtime DLLs/plugins beside the executable. Copy the FFmpeg runtime DLLs from the vcpkg installation into the release directory as required by the selected vcpkg triplet.

## CPU-only guarantee

The recommended first build explicitly uses:

```text
MSF_ENABLE_CUDA=OFF
```

This does not delete or disable the CUDA source from the project permanently. It only selects the CPU backend for the initial Windows validation build.

## GitHub Actions (recommended for first online build)

The repository includes `.github/workflows/windows-cpu.yml`. Push the project to GitHub and run the workflow from **Actions → MediaSimilarityFinder 0.9.1.0 - Windows CPU → Run workflow**.

The workflow uses the Windows x64 `windows-2025` hosted runner, vcpkg manifest mode, MSVC x64, Qt6 `qtbase`, SQLite3 and FFmpeg. It builds with `MSF_ENABLE_CUDA=OFF`, runs CTest, deploys Qt runtime files with `windeployqt`, copies runtime DLLs, and uploads `MediaSimilarityFinder-v0.9.1.0-CPU-Windows-x64.zip` as a workflow artifact.
