param(
  [string]$DumpRoot = "D:\Temp\OpenCodeWork\dumps",
  [int]$DumpCount = 3,
  [int]$DumpType = 1
)
# One-click LocalDumps enablement for both product images (GUI + Backend).
# Mirrors docs/architecture/crash-response-runbook (DumpFolder/DumpCount 3/
# DumpType 1 = mini). Run ELEVATED: LocalDumps lives under HKLM.
$ErrorActionPreference = "Stop"
$principal = New-Object Security.Principal.WindowsPrincipal(
  [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw "Administrator rights required: re-run this script from an elevated PowerShell (LocalDumps is under HKLM)."
}
New-Item -ItemType Directory -Path $DumpRoot -Force | Out-Null
foreach ($image in @("MediaSimilarityFinder.exe", "MediaSimilarityFinderBackend.exe")) {
  $key = "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\$image"
  New-Item -Path $key -Force | Out-Null
  New-ItemProperty -Path $key -Name "DumpFolder" -Value $DumpRoot -PropertyType ExpandString -Force | Out-Null
  New-ItemProperty -Path $key -Name "DumpCount" -Value $DumpCount -PropertyType DWord -Force | Out-Null
  New-ItemProperty -Path $key -Name "DumpType" -Value $DumpType -PropertyType DWord -Force | Out-Null
  Get-ItemProperty -Path $key |
    Select-Object @{n="Image"; e={$image}}, DumpFolder, DumpCount, DumpType |
    Format-Table -AutoSize | Out-String -Width 200 | Write-Host
}
Write-Host "LocalDumps enabled for both images."
