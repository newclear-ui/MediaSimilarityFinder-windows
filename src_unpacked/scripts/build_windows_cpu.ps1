param(
    [string]$VcpkgRoot = $(if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'C:\src\vcpkg' }),
    [string]$BuildDir = 'build-windows-cpu',
    [int]$BuildParallelism = 1,
    [string]$CTestExclude = ''
)
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot
# CMake's vcpkg toolchain re-runs the manifest install; it only sees overlays via env.
$env:VCPKG_OVERLAY_PORTS = (Join-Path $projectRoot 'ports')
$env:VCPKG_OVERLAY_TRIPLETS = (Join-Path $projectRoot 'triplets')

if (-not (Test-Path $VcpkgRoot)) { throw "VCPKG_ROOT not found: $VcpkgRoot" }
$localVcpkgInstalled = Join-Path $projectRoot 'vcpkg_installed'
$tripletRoot = Join-Path $localVcpkgInstalled 'x64-windows'
$env:VCPKG_INSTALLED_DIR = $localVcpkgInstalled
$env:VCPKG_TARGET_TRIPLET = 'x64-windows'

# Manifest mode: vcpkg.json is the single dependency source for CPU and CUDA builds.
& "$VcpkgRoot\vcpkg.exe" install --triplet x64-windows --overlay-triplets="$projectRoot\triplets" --overlay-ports="$projectRoot\ports"
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

# Tests generate media fixtures by calling the ffmpeg CLI. vcpkg installs the
# ffmpeg/ffprobe tools under tools\ffmpeg but does not add them to PATH.
$ffmpegTools = Join-Path $tripletRoot 'tools\ffmpeg'
if (Test-Path $ffmpegTools) {
    $env:PATH = "$ffmpegTools;$env:PATH"
    Write-Host "ffmpeg tools: $ffmpegTools"
}
$ctestArgs = @('--test-dir', $BuildDir, '-C', 'Release', '--output-on-failure')
if ($CTestExclude) { $ctestArgs += @('-E', $CTestExclude) }
& ctest @ctestArgs
if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }

Write-Host 'CPU Windows build completed successfully.'
Write-Host "Executable: $BuildDir\Release\MediaSimilarityFinder.exe"
