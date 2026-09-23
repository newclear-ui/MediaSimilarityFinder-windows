param(
  [string]$BuildDir = "build-windows-cuda",
  [string]$Configuration = "Release",
  [string]$OutputDir = "portable-release"
)
$ErrorActionPreference = "Stop"
$exe = Join-Path $BuildDir "$Configuration\MediaSimilarityFinder.exe"
if (-not (Test-Path $exe)) { throw "Executable not found: $exe" }
Remove-Item $OutputDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $OutputDir, (Join-Path $OutputDir "Index") -Force | Out-Null
Copy-Item $exe $OutputDir -Force
$qtBin = Split-Path $exe -Parent
if (Get-Command windeployqt.exe -ErrorAction SilentlyContinue) {
  & windeployqt.exe --release --no-translations (Join-Path $OutputDir "MediaSimilarityFinder.exe")
}
Get-ChildItem $qtBin -Filter *.dll -ErrorAction SilentlyContinue | Copy-Item -Destination $OutputDir -Force
# vcpkg Qt keeps plugins under <prefix>\Qt6\plugins, which plain DLL copying
# misses. Without Qt6\plugins the GUI failfasts in Qt6Core at startup
# (verified on 0.9.2.35); windeployqt covers this when present.
$qtPluginRoot = Join-Path (Split-Path -Parent $PSScriptRoot) "vcpkg_installed\x64-windows\Qt6"
if (Test-Path (Join-Path $qtPluginRoot "plugins")) {
  Copy-Item (Join-Path $qtPluginRoot "plugins") (Join-Path $OutputDir "Qt6\plugins") -Recurse -Force
}
if (Test-Path "README.md") { Copy-Item "README.md" $OutputDir -Force }
@{
  product = "MediaSimilarityFinder"
  version = "0.9.2.47"
  mode = "portable"
  indexRoot = "Index"
} | ConvertTo-Json | Set-Content (Join-Path $OutputDir "portable.json") -Encoding UTF8
$zip = "MediaSimilarityFinder-v0.9.2.47-Portable-Windows-x64.zip"
Compress-Archive -Path (Join-Path $OutputDir '*') -DestinationPath $zip -Force
Write-Host "Portable package created: $zip"
