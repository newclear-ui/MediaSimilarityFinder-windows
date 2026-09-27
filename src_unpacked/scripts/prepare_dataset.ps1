# MediaSimilarityFinder — D8b standard dataset generator
#
# Writes the deterministic benchmark fixture into the destination root.
# Nothing here reads the clock, the environment, or a random source: every
# byte is a pure function of (seed, x, y), so two runs produce byte-identical
# files and therefore an identical dataset fingerprint.
#
# Usage:
#   .\scripts\prepare_dataset.ps1 -Root ..\test_sample_img_vid -Scale small
#   .\scripts\prepare_dataset.ps1 -Root ..\test_sample_img_vid -Scale full
#   .\scripts\prepare_dataset.ps1 -Root ..\test_sample_img_vid -FingerprintOnly
#
# -FingerprintOnly skips generation and only reports the fingerprint of an
# already-prepared root, which is how you validate a fixture that came from
# somewhere else (for example a different checkout on another drive).
#
# IMPORTANT: this script deletes and recreates the root. Any file kept inside
# the dataset root (a README, a manifest, a stray note) becomes part of the
# dataset manifest and changes the fingerprint, so the root must contain
# media and nothing else. Documentation and the expected fingerprint live in
# src_unpacked/docs/test_sample_img_vid.md and
# src_unpacked/docs/build-history/0.9.4.22.*.md.

[CmdletBinding()]
param(
    [string]$Root = "..\test_sample_img_vid",
    # small = D8a v1, the minimal reproducibility/parity fixture.
    # full  = D8b v2, adds the walk axis and the decode axis so walker queue
    #         and GPU batch share can be measured at a realistic scale.
    [ValidateSet('small', 'full')]
    [string]$Scale = 'full',
    [switch]$FingerprintOnly
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# v1 composition (D8a) — unchanged, so the recorded D8a fingerprint's files
# still exist byte-for-byte as a parity regression anchor.
# ---------------------------------------------------------------------------
$ExactGroupCount = 12
$ExactPerGroup   = 4
$VariedCount     = 12
$SmallW = 8; $SmallH = 8

# ---------------------------------------------------------------------------
# v2 additions (D8b). Two deliberately asymmetric axes:
#   tree/  — many tiny files spread over many directories. Makes the walker's
#            enumeration cost real, so the queue can actually build depth.
#   bulk/ — realistically sized images. Makes the consumer's decode + crop +
#            color thumbnail cost real, so the consumer becomes the slow side.
# The queue grows only when consumer cost exceeds producer cost, so both axes
# are needed to observe anything.
# ---------------------------------------------------------------------------
$TreeTopDirs  = 10      # l00..l09
$TreeMidDirs  = 8       # m00..m07 under each top
$TreePerLeaf  = 30      # files per leaf directory
$BulkCount    = 240
$BulkW = 256; $BulkH = 192

function New-DeterministicBmp {
    param(
        [string]$Path,
        [int]$Seed,
        [int]$W = $SmallW,
        [int]$H = $SmallH
    )
    $row = $W * 3
    $img = $row * $H
    $fileSize = 54 + $img

    $bytes = New-Object 'System.Collections.Generic.List[byte]'
    $header = New-Object byte[] 54
    $header[0] = 0x42; $header[1] = 0x4D
    $header[2] = [byte]($fileSize -band 0xFF)
    $header[3] = [byte](($fileSize -shr 8) -band 0xFF)
    $header[10] = 54      # pixel data offset
    $header[14] = 40      # DIB header size
    $header[18] = [byte]($W -band 0xFF)
    $header[19] = [byte](($W -shr 8) -band 0xFF)
    $header[22] = [byte]($H -band 0xFF)
    $header[23] = [byte](($H -shr 8) -band 0xFF)
    $header[26] = 1       # planes
    $header[28] = 24      # bits per pixel
    $header[34] = [byte]($img -band 0xFF)
    $header[35] = [byte](($img -shr 8) -band 0xFF)
    foreach ($b in $header) { $bytes.Add($b) }

    # Pure integer determinism. No RNG, no time, no environment.
    for ($y = 0; $y -lt $H; $y++) {
        for ($x = 0; $x -lt $W; $x++) {
            $v = [byte]((($x * 2 + $y * 3 + $Seed) * 7) % 256)
            $bytes.Add($v); $bytes.Add($v); $bytes.Add($v)
        }
    }
    [System.IO.File]::WriteAllBytes($Path, $bytes.ToArray())
}

$rootPath = [System.IO.Path]::GetFullPath($Root)
Write-Output "dataset_root=$rootPath"
Write-Output "dataset_scale=$Scale"

# The dataset identity file is a SIBLING of the root, never inside it. A file
# under the root joins the manifest and changes the fingerprint it is meant to
# describe, so keeping it outside makes that mistake impossible.
$identityPath = $rootPath.TrimEnd('\', '/') + '.fingerprint.json'
if (Test-Path -LiteralPath $identityPath) { Remove-Item -LiteralPath $identityPath -Force }

if (-not $FingerprintOnly) {
    if (Test-Path -LiteralPath $rootPath) {
        Remove-Item -LiteralPath $rootPath -Recurse -Force
    }

    # --- v1 sections (identical in both scales) ---
    New-Item -ItemType Directory -Path (Join-Path $rootPath 'images\exact') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $rootPath 'images\varied') -Force | Out-Null
    for ($g = 0; $g -lt $ExactGroupCount; $g++) {
        $groupSeed = 17 + $g * 13
        for ($m = 0; $m -lt $ExactPerGroup; $m++) {
            $name = ('dup{0:d2}_{1:d2}.bmp' -f $g, $m)
            New-DeterministicBmp -Path (Join-Path $rootPath "images\exact\$name") -Seed $groupSeed
        }
    }
    for ($i = 0; $i -lt $VariedCount; $i++) {
        $name = ('v{0:d2}.bmp' -f $i)
        New-DeterministicBmp -Path (Join-Path $rootPath "images\varied\$name") -Seed (101 + $i * 29)
    }

    if ($Scale -eq 'full') {
        # --- walk axis: many directories, few files each ---
        for ($t = 0; $t -lt $TreeTopDirs; $t++) {
            for ($m = 0; $m -lt $TreeMidDirs; $m++) {
                $leafDir = Join-Path $rootPath ('tree\l{0:d2}\m{1:d2}' -f $t, $m)
                New-Item -ItemType Directory -Path $leafDir -Force | Out-Null
                for ($f = 0; $f -lt $TreePerLeaf; $f++) {
                    $name = ('l{0:d5}.bmp' -f ($t * 100000 + $m * 1000 + $f))
                    New-DeterministicBmp -Path (Join-Path $leafDir $name) -Seed (7 + $t * 37 + $m * 11 + $f)
                }
            }
        }

        # --- decode axis: realistically sized images ---
        New-Item -ItemType Directory -Path (Join-Path $rootPath 'bulk') -Force | Out-Null
        for ($i = 0; $i -lt $BulkCount; $i++) {
            $name = ('b{0:d4}.bmp' -f $i)
            New-DeterministicBmp -Path (Join-Path $rootPath "bulk\$name") -Seed (1009 + $i * 17) -W $BulkW -H $BulkH
        }
    }
}

# Count what is actually on disk (never what was intended).
$actual = @(Get-ChildItem -LiteralPath $rootPath -Recurse -File -ErrorAction SilentlyContinue)
$totalBytes = ($actual | Measure-Object -Property Length -Sum).Sum
Write-Output "dataset_file_count=$($actual.Count)"
Write-Output "dataset_total_bytes=$totalBytes"
Write-Output "dataset_dir_count=$(@(Get-ChildItem -LiteralPath $rootPath -Recurse -Directory -ErrorAction SilentlyContinue).Count)"
if ($FingerprintOnly) {
    Write-Output "run msf_dataset_report to read the recorded fingerprint"
}
