param(
  [string]$VcpkgRoot = $(if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'C:\src\vcpkg' }),
  [string]$BuildDir = "build-windows-gpu",
  [string]$GpuBackend = "AUTO",
  [int]$BuildParallelism = 1
)
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot
if (-not (Get-Command nvcc -ErrorAction SilentlyContinue)) {
  # Shells started before the toolkit install lack CUDA on PATH even though
  # the machine-level CUDA_PATH exists. Fall back to it before giving up.
  $cudaHome = $env:CUDA_PATH
  if (-not $cudaHome) { $cudaHome = [Environment]::GetEnvironmentVariable("CUDA_PATH", "Machine") }
  if ($cudaHome -and (Test-Path (Join-Path $cudaHome "bin\nvcc.exe"))) {
    $env:PATH = (Join-Path $cudaHome "bin") + ";" + $env:PATH
  }
}
if (-not (Get-Command nvcc -ErrorAction SilentlyContinue)) { throw "nvcc not found. Install the NVIDIA CUDA Toolkit and ensure nvcc is on PATH." }
# CUDA MSBuild integration (CUDA XX.Y.targets) resolves CudaToolkitDir from the
# versioned CUDA_PATH_V* variables. Shells started before the toolkit install
# lack them even though machine-level values exist, so import any missing ones.
$machineEnv = [Environment]::GetEnvironmentVariables("Machine")
foreach ($name in @($machineEnv.Keys | Where-Object { $_ -like "CUDA_PATH*" })) {
  if (-not [Environment]::GetEnvironmentVariable($name)) {
    [Environment]::SetEnvironmentVariable($name, $machineEnv[$name])
  }
}
if (-not (Test-Path $VcpkgRoot)) { throw "VCPKG_ROOT not found: $VcpkgRoot" }
$localVcpkgInstalled = Join-Path $projectRoot "vcpkg_installed"
$tripletRoot = Join-Path $localVcpkgInstalled "x64-windows"
$env:VCPKG_INSTALLED_DIR = $localVcpkgInstalled
& "$VcpkgRoot\vcpkg.exe" install --triplet x64-windows
if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed: $LASTEXITCODE" }
# Node A: canonical GPU entry point (clean tree; the legacy build-windows-cuda
# tree is preserved, never renamed in place). MSF_ENABLE_CUDA remains as a
# deprecated alias; MSF_GPU_BACKEND pins the concrete backend for diagnostics.
cmake -S $projectRoot -B $BuildDir -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_INSTALLED_DIR="$localVcpkgInstalled" -DVCPKG_TARGET_TRIPLET=x64-windows `
  -Dunofficial-sqlite3_DIR="$tripletRoot\share\unofficial-sqlite3" `
  -DQt6_DIR="$tripletRoot\share\Qt6" `
  -DMSF_FFMPEG_ROOT="$tripletRoot" `
  -DMSF_ENABLE_GPU=ON "-DMSF_GPU_BACKEND=$GpuBackend" -DMSF_CUDA_ARCHITECTURES="75;86;89" `
  -DMSF_ENABLE_FFMPEG=ON -DMSF_BUILD_GUI=ON -DMSF_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
cmake --build $BuildDir --config Release --parallel $BuildParallelism
if ($LASTEXITCODE -ne 0) { throw "Release build failed: $LASTEXITCODE" }
ctest --test-dir $BuildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "CTest failed: $LASTEXITCODE" }
Write-Host "GPU build/test complete. Use $BuildDir\Release\MediaSimilarityFinder.exe --version to verify the executable."
