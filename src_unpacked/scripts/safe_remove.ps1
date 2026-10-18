# MediaSimilarityFinder - safe removal (Recycle Bin only)
#
# Workspace rule: no file or folder may ever be hard-deleted. Anything that has
# to go away is moved to the Recycle Bin and the user empties it. This script
# therefore has no hard-delete code path at all: there is no -Force, no -Recurse
# + delete, and no Remove-Item call. Misuse should fail, not silently succeed.
#
# Usage:
#   .\scripts\safe_remove.ps1 -Path ..\backup\old.zip
#   .\scripts\safe_remove.ps1 -Path ..\some\folder
#   .\scripts\safe_remove.ps1 -Path a.zip,b.zip,..\tmp-folder
#
# The Recycle Bin is addressed through the Shell.Application COM interface
# (Namespace(10).MoveHere), which is the same mechanism Explorer uses for
# "Move to Recycle Bin". A hard-delete fallback is deliberately absent: if the
# shell cannot accept the item the script fails loudly instead of destroying it.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string[]]$Path
)

$ErrorActionPreference = 'Stop'

$resolved = @()
foreach ($p in $Path) {
    if (-not (Test-Path -LiteralPath $p)) {
        Write-Output "skip_absent=$p"
        continue
    }
    $resolved += (Resolve-Path -LiteralPath $p).Path
}

if ($resolved.Count -eq 0) {
    Write-Output 'nothing_to_remove'
    exit 0
}

$shell = New-Object -ComObject Shell.Application
$recycleBin = $shell.Namespace(10)
if ($null -eq $recycleBin) {
    throw 'Recycle Bin is not available. Refusing to delete: hard delete is not permitted.'
}

$moved = 0
$bytes = 0L
$scriptStart = Get-Date
foreach ($item in $resolved) {
    $leaf = Split-Path -Leaf $item
    if (Test-Path -LiteralPath $item -PathType Container) {
        $sum = (Get-ChildItem -LiteralPath $item -Recurse -File -Force -ErrorAction SilentlyContinue |
            Measure-Object -Property Length -Sum).Sum
    } else {
        $sum = (Get-Item -LiteralPath $item -Force).Length
    }
    if ($null -ne $sum) { $bytes += [int64]$sum }

    # MoveHere is asynchronous; poll briefly so the reported count is honest.
    # Flags must include FOF_ALLOWUNDO (0x40).
    $recycleBin.MoveHere($item, 0x54)  # FOF_NOCONFIRMATION | FOF_SILENT | FOF_ALLOWUNDO
    $deadline = (Get-Date).AddSeconds(20)
    while ((Test-Path -LiteralPath $item) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
    }
    if (Test-Path -LiteralPath $item) {
        throw "failed_to_recycle=$item (still present; nothing was hard-deleted)"
    }
    # Fail-closed verification: a vanished source is NOT proof of recycling.
    # Proven 2026-10-18: in a non-interactive session the shell can remove the
    # source without writing any $I/$R bin record. Only an $I record naming
    # this exact path counts as "recycled"; anything else aborts loudly.
    $sid = ((& whoami /user /FO LIST 2>$null | Select-String 'SID') -replace '.*(S-1-5-21-[\d-]+).*','$1').Trim()
    if (-not $sid) { $sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value }
    $binRoot = Join-Path 'C:\$Recycle.Bin' $sid
    $found = $false
    $verifyDeadline = (Get-Date).AddSeconds(30)
    while (-not $found -and (Get-Date) -lt $verifyDeadline) {
        foreach ($meta in (Get-ChildItem -LiteralPath $binRoot -Force -Filter '$I*' -ErrorAction SilentlyContinue)) {
            if ($meta.LastWriteTime -lt $scriptStart) { continue }
            $raw = [IO.File]::ReadAllBytes($meta.FullName)
            if ($raw.Length -lt 28) { continue }
            $path = [Text.Encoding]::Unicode.GetString($raw, 28, $raw.Length - 28).TrimEnd([char]0)
            if ($path -ieq $item) { $found = $true; break }
        }
        if (-not $found) { Start-Sleep -Milliseconds 500 }
    }
    if (-not $found) {
        throw ('not_in_recycle_bin=' + $item + ' (source is gone but no $I record names it; treat contents as lost and stop)')
    }
    $moved++
    Write-Output "recycled=$item"
}

Write-Output "recycled_count=$moved"
Write-Output ("recycled_bytes={0}" -f $bytes)
Write-Output 'note=Recycle Bin is NOT empty of previous items; user must empty it when ready.'
