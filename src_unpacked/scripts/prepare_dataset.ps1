# MediaSimilarityFinder — D8a standard dataset generator
#
# Writes the deterministic benchmark fixture into the destination root.
# Nothing here reads the clock, the environment, or a random source: every
# byte is a pure function of (seed, x, y), so two runs produce byte-identical
# files and therefore an identical dataset fingerprint.
#
# Usage:
#   .\scripts\prepare_dataset.ps1 -Root ..\test_sample_img_vid
#   .\scripts\prepare_dataset.ps1 -Root ..\test_sample_img_vid -FingerprintOnly
#
# -FingerprintOnly skips generation and only reports the fingerprint of an
# already-prepared root, which is how you validate a fixture that came from
# somewhere else (for example a different checkout on another drive).
#
# IMPORTANT: this script deletes and recreates the root. Any file kept inside
# the dataset root (a README, a manifest, a stray note) becomes part of the
# dataset manifest and changes the fingerprint, so the root must contain
# media and nothing else. This file and the expected fingerprint live
# elsewhere, in src_unpacked/docs/build-history/0.9.4.21.*.md.

[CmdletBinding()]
param(
    [string]$Root = "..\test_sample_img_vid",
    [switch]$FingerprintOnly
)

$ErrorActionPreference = 'Stop'

# Composition is part of the dataset identity. Changing any number here
# changes the fingerprint on purpose, so they live in one place.
$ExactGroupCount = 12      # independent duplicate groups
$ExactPerGroup   = 4       # byte-identical members inside each group
$VariedCount     = 12      # individually varied images
$Width           = 8
$Height          = 8

function New-DeterministicBmp {
    param(
        [string]$Path,
        [int]$Seed
    )
    $w = $Width; $h = $Height
    $row = $w * 3
    $img = $row * $h
    $fileSize = 54 + $img

    $bytes = New-Object 'System.Collections.Generic.List[byte]'
    $header = New-Object byte[] 54
    $header[0] = 0x42; $header[1] = 0x4D
    $header[2] = [byte]($fileSize -band 0xFF)
    $header[3] = [byte](($fileSize -shr 8) -band 0xFF)
    $header[10] = 54      # pixel data offset
    $header[14] = 40      # DIB header size
    $header[18] = [byte]$w
    $header[22] = [byte]$h
    $header[26] = 1       # planes
    $header[28] = 24      # bits per pixel
    $header[34] = [byte]($img -band 0xFF)
    $header[35] = [byte](($img -shr 8) -band 0xFF)
    foreach ($b in $header) { $bytes.Add($b) }

    # Pure integer determinism. No RNG, no time, no environment.
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $v = [byte]((($x * 2 + $y * 3 + $Seed) * 7) % 256)
            $bytes.Add($v); $bytes.Add($v); $bytes.Add($v)
        }
    }
    [System.IO.File]::WriteAllBytes($Path, $bytes.ToArray())
}

$rootPath = [System.IO.Path]::GetFullPath($Root)
Write-Output "dataset_root=$rootPath"

# The dataset identity file is a SIBLING of the root, never inside it. A file
# under the root joins the manifest and changes the fingerprint it is meant to
# describe, so keeping it outside makes that mistake impossible.
$identityPath = $rootPath.TrimEnd('\', '/') + '.fingerprint.json'
if (Test-Path -LiteralPath $identityPath) { Remove-Item -LiteralPath $identityPath -Force }

if (-not $FingerprintOnly) {
    if (Test-Path -LiteralPath $rootPath) {
        Remove-Item -LiteralPath $rootPath -Recurse -Force
    }
    New-Item -ItemType Directory -Path (Join-Path $rootPath 'images\exact') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $rootPath 'images\varied') -Force | Out-Null

    # Identical members inside a group share one seed, so a group is a true
    # byte-identical duplicate set. Different groups use different seeds.
    for ($g = 0; $g -lt $ExactGroupCount; $g++) {
        $groupSeed = 17 + $g * 13
        for ($m = 0; $m -lt $ExactPerGroup; $m++) {
            $name = ('dup{0:d2}_{1:d2}.bmp' -f $g, $m)
            New-DeterministicBmp -Path (Join-Path $rootPath "images\exact\$name") -Seed $groupSeed
        }
    }

    # Each varied image gets its own seed, so no two are byte-identical.
    for ($i = 0; $i -lt $VariedCount; $i++) {
        $name = ('v{0:d2}.bmp' -f $i)
        New-DeterministicBmp -Path (Join-Path $rootPath "images\varied\$name") -Seed (101 + $i * 29)
    }

    $expected = $ExactGroupCount * $ExactPerGroup + $VariedCount
    Write-Output "dataset_generated_files=$expected"
}

# Count what is actually on disk (never what was intended).
$actual = @(Get-ChildItem -LiteralPath $rootPath -Recurse -File -ErrorAction SilentlyContinue)
$totalBytes = ($actual | Measure-Object -Property Length -Sum).Sum
Write-Output "dataset_file_count=$($actual.Count)"
Write-Output "dataset_total_bytes=$totalBytes"
if ($FingerprintOnly) {
    Write-Output "dataset_fingerprint_hint=run gpu_timing/CTest tool to read the recorded fingerprint"
}
