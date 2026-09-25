param(
    [string]$VcpkgRoot = $(if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'C:\src\vcpkg' }),
    [string]$BuildDir = 'build-windows-cpu',
    [int]$BuildParallelism = 1
)
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot

if (-not (Test-Path $VcpkgRoot)) { throw "VCPKG_ROOT not found: $VcpkgRoot" }
$localVcpkgInstalled = Join-Path $projectRoot 'vcpkg_installed'
$tripletRoot = Join-Path $localVcpkgInstalled 'x64-windows'
$env:VCPKG_INSTALLED_DIR = $localVcpkgInstalled
$env:VCPKG_TARGET_TRIPLET = 'x64-windows'

# Manifest mode: vcpkg.json is the single dependency source for CPU and CUDA builds.
& "$VcpkgRoot\vcpkg.exe" install --triplet x64-windows
if ($LASTEXITCODE -ne 0) { throw 'vcpkg dependency installation failed.' }

$args = @(
    '-S',$projectRoot, '-B',$BuildDir,
    '-G','Visual Studio 18 2026','-A','x64',
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake",
    "-DVCPKG_INSTALLED_DIR=$localVcpkgInstalled",
    '-DVCPKG_TARGET_TRIPLET=x64-windows',
    "-Dunofficial-sqlite3_DIR=$tripletRoot\share\unofficial-sqlite3",
    "-DQt6_DIR=$tripletRoot\share\Qt6",
    "-DMSF_FFMPEG_ROOT=$tripletRoot",
    '-DMSF_ENABLE_CUDA=OFF',
    '-DMSF_ENABLE_FFMPEG=ON',
    '-DMSF_BUILD_GUI=ON',
    '-DMSF_BUILD_TESTS=ON'
)
& cmake @args
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

& cmake --build $BuildDir --config Release --parallel $BuildParallelism
if ($LASTEXITCODE -ne 0) { throw 'Windows Release build failed.' }

& ctest --test-dir $BuildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }

Write-Host 'CPU Windows build completed successfully.'
Write-Host "Executable: $BuildDir\Release\MediaSimilarityFinder.exe"
