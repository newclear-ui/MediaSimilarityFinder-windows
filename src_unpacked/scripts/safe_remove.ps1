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
    $recycleBin.MoveHere($item, 0x14)  # FOF_NOCONFIRMATION | FOF_SILENT | FOF_ALLOWUNDO
    $deadline = (Get-Date).AddSeconds(20)
    while ((Test-Path -LiteralPath $item) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
    }
    if (Test-Path -LiteralPath $item) {
        throw "failed_to_recycle=$item (still present; nothing was hard-deleted)"
    }
    $moved++
    Write-Output "recycled=$item"
}

Write-Output "recycled_count=$moved"
Write-Output ("recycled_bytes={0}" -f $bytes)
Write-Output 'note=Recycle Bin is NOT empty of previous items; user must empty it when ready.'
