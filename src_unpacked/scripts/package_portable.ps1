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
# Project-local vcpkg prefix used by the current Windows build.
$vcpkgTripletRoot = Join-Path (Split-Path -Parent $PSScriptRoot) "vcpkg_installed\x64-windows"
Get-ChildItem $qtBin -Filter *.dll -ErrorAction SilentlyContinue | Copy-Item -Destination $OutputDir -Force

# Copy runtime DLLs from the project-local vcpkg installation. SQLite/FFmpeg
# are linked from this prefix, so the portable package must not depend on the
# developer/CI machine's PATH for these DLLs.
$vcpkgBin = Join-Path $vcpkgTripletRoot "bin"
if (Test-Path $vcpkgBin) {
  Get-ChildItem $vcpkgBin -Filter *.dll -ErrorAction SilentlyContinue |
    Copy-Item -Destination $OutputDir -Force
}

# ffmpeg/ffprobe are runtime fallbacks used by VideoDecoder and the resolution
# probe. The manifest enables both tools; copy them from vcpkg's tools prefix.
$vcpkgTools = Join-Path $vcpkgTripletRoot "tools"

$windeployqt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if (-not $windeployqt) {
  $windeployqt = Get-ChildItem $vcpkgTools -Filter "windeployqt.exe" -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1
}
if ($windeployqt) {
  $windeployqtPath = if ($windeployqt.PSObject.Properties.Name -contains "Source") { $windeployqt.Source } else { $windeployqt.FullName }
  & $windeployqtPath --release --no-translations (Join-Path $OutputDir "MediaSimilarityFinder.exe")
  if ($LASTEXITCODE -ne 0) { throw "windeployqt failed: $LASTEXITCODE" }
}

foreach ($toolName in @("ffmpeg.exe", "ffprobe.exe")) {
  $tool = Get-ChildItem $vcpkgTools -Filter $toolName -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if ($tool) {
    Copy-Item $tool.FullName (Join-Path $OutputDir $toolName) -Force
  } else {
    throw "$toolName was not found in project-local vcpkg tools."
  }
}
# vcpkg Qt keeps plugins under <prefix>\Qt6\plugins, which plain DLL copying
# misses. Without Qt6\plugins the GUI failfasts in Qt6Core at startup
# (verified on 0.9.2.35); windeployqt covers this when present.
$qtPluginRoot = Join-Path $vcpkgTripletRoot "Qt6"
if (Test-Path (Join-Path $qtPluginRoot "plugins")) {
  Copy-Item (Join-Path $qtPluginRoot "plugins") (Join-Path $OutputDir "Qt6\plugins") -Recurse -Force -Exclude *.pdb
}
if (Test-Path "README.md") { Copy-Item "README.md" $OutputDir -Force }
@{
  product = "MediaSimilarityFinder"
  version = "0.9.2.73"
  mode = "portable"
  indexRoot = "Index"
} | ConvertTo-Json | Set-Content (Join-Path $OutputDir "portable.json") -Encoding UTF8
$zip = "MediaSimilarityFinder-v0.9.2.73-Portable-Windows-x64.zip"
Compress-Archive -Path (Join-Path $OutputDir '*') -DestinationPath $zip -Force
Write-Host "Portable package created: $zip"
