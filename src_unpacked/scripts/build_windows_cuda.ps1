param(
  [string]$VcpkgRoot = $env:VCPKG_ROOT,
  [string]$BuildDir = "build-windows-cuda"
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
if (-not $VcpkgRoot) { throw "VCPKG_ROOT is not set." }
$localVcpkgInstalled = Join-Path $projectRoot "vcpkg_installed"
$tripletRoot = Join-Path $localVcpkgInstalled "x64-windows"
$env:VCPKG_INSTALLED_DIR = $localVcpkgInstalled
& "$VcpkgRoot\vcpkg.exe" install --triplet x64-windows
cmake -S $projectRoot -B $BuildDir -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_INSTALLED_DIR="$localVcpkgInstalled" -DVCPKG_TARGET_TRIPLET=x64-windows `
  -Dunofficial-sqlite3_DIR="$tripletRoot\share\unofficial-sqlite3" `
  -DQt6_DIR="$tripletRoot\share\Qt6" `
  -DMSF_FFMPEG_ROOT="$tripletRoot" `
  -DMSF_ENABLE_CUDA=ON -DMSF_CUDA_ARCHITECTURES="75;86;89" `
  -DMSF_ENABLE_FFMPEG=ON -DMSF_BUILD_GUI=ON -DMSF_BUILD_TESTS=ON
cmake --build $BuildDir --config Release --parallel
ctest --test-dir $BuildDir -C Release --output-on-failure
Write-Host "CUDA build/test complete. Use $BuildDir\Release\MediaSimilarityFinder.exe --version to verify the executable."
