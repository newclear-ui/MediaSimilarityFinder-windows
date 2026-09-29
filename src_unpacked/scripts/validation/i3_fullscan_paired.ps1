# I-3 full-scan end-to-end validation harness (v0.9.4.37)
#
# Measurement-only. Runs the REAL production scan driver
# (msf_dataset_baseline -> MediaSearchEngine::scan) for the v0.9.4.35 baseline
# and the v0.9.4.36 candidate, interleaved BCBCBCBCBC to remove time-axis bias.
#
# One process per measurement (runs=1). The verify buffer cache in
# image_verify.cpp is PROCESS-GLOBAL with 32 entries, so a fresh process is the
# only way to get a cold verify cache. That is the decode-active condition: every
# verify miss actually decodes, which is where the I-2 shared WIC source lands.
#
# This script does not modify the dataset and does not delete anything.

param(
    [ValidateSet('cpu','gpu')] [string]$Flavour = 'cpu',
    [int]$Repeats = 5,
    [string]$Dataset = 'C:\project\test_sample_img_vid',
    [string]$OutDir   = 'C:\project\validation\results'
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$bins = @{
    'base' = "C:\project\validation\v0.9.4.35\src_unpacked\build-windows-$Flavour\Release\msf_dataset_baseline.exe"
    'cand' = "C:\project\src_unpacked\build-windows-$Flavour\Release\msf_dataset_baseline.exe"
}
foreach ($k in $bins.Keys) {
    if (-not (Test-Path -LiteralPath $bins[$k])) { throw "missing binary: $($bins[$k])" }
}
if (-not (Test-Path -LiteralPath $Dataset)) { throw "missing dataset: $Dataset" }

# Separate app-dir prefixes so the two versions can never share an index.
$appPrefix = "C:\project\validation\appdir\i3_${Flavour}"

$rows = @()

function Invoke-Scan {
    param([string]$Exe, [string]$AppDir, [string]$JsonPath)

    $env:MSF_DUMP_JSON = $JsonPath
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $out = & $Exe $Dataset $AppDir 1 2>&1
    $sw.Stop()
    $env:MSF_DUMP_JSON = $null

    $text = ($out | Out-String)
    # "run=0 wall_ms=18381.3 scanned=3334 analyzed=3333 groups=156211 gpu_images=0"
    $m = [regex]::Match($text, 'run=0 wall_ms=([0-9.]+) scanned=(\d+) analyzed=(\d+) groups=(\d+)')
    if (-not $m.Success) { throw "could not parse run line for $Exe`n$text" }

    $j = Get-Content -LiteralPath $JsonPath -Raw | ConvertFrom-Json
    $a = $j.analyze
    # The D1 OS CreateFileW reference probe count is read from the text block;
    # it is the number of measurement-only probes, not product work.
    $pc = [regex]::Match($text, 'probe count (\d+)')

    [pscustomobject]@{
        hostWallMs   = [double]$m.Groups[1].Value
        summaryWallMs= [double]$j.summary.wallMs
        walkMs       = [double]$j.summary.walkMs
        imageStageMs = [double]$j.summary.imageStageMs
        analyzeMs    = [double]$j.summary.analyzeMs
        scanned      = [int]$m.Groups[2].Value
        analyzed     = [int]$m.Groups[3].Value
        groups       = [int]$m.Groups[4].Value
        verifyCalls      = [int]$a.verifyCalls
        verifyDecodeMisses = [int]$a.verifyDecodeMisses
        verifyCacheHits  = [int]$a.verifyCacheHits
        verifyHitRate    = [double]$a.verifyHitRate
        verifyMs      = [double]$a.verifyMs
        verifyDecodeMs= [double]$a.verifyDecodeMs
        verifyKeyMs   = [double]$a.verifyKeyMs
        decodeCalls   = [int]$a.decodeCalls
        decodeAspectCalls = [int]$a.decodeAspectCalls
        decodeTotalMs = [double]$a.decodeTotalMs
        osProbeCount  = if ($pc.Success) { [int]$pc.Groups[1].Value } else { -1 }
    }
}

# ---- warm-up: both binaries once, discarded, so neither pays a cold-start tax
#      that only one of them would pay inside the measured sequence.
Write-Host "warm-up (discarded) ..."
foreach ($k in @('base','cand')) {
    $null = Invoke-Scan -Exe $bins[$k] -AppDir "${appPrefix}_warm_$k" `
                         -JsonPath "$OutDir\warm_${Flavour}_$k.json"
}
Write-Host "warm-up done"
Write-Host ""

for ($i = 1; $i -le $Repeats; $i++) {
    # BCBCBCBCBC
    foreach ($k in @('base','cand')) {
        $ver = if ($k -eq 'base') { 'v0.9.4.35' } else { 'v0.9.4.36' }
        $r = Invoke-Scan -Exe $bins[$k] -AppDir "${appPrefix}_$k" `
                         -JsonPath "$OutDir\run_${Flavour}_${i}_$k.json"
        $rows += [pscustomobject]@{
            Run = $i; Version = $ver; Flavour = $Flavour; Slot = $k
            HostWallMs = [math]::Round($r.hostWallMs,1)
            SummaryWallMs = [math]::Round($r.summaryWallMs,1)
            AnalyzeMs = [math]::Round($r.analyzeMs,1)
            VerifyMs = [math]::Round($r.verifyMs,1)
            VerifyDecodeMs = [math]::Round($r.verifyDecodeMs,1)
            DecodeTotalMs = [math]::Round($r.decodeTotalMs,1)
            DecodeCalls = $r.decodeCalls
            DecodeAspectCalls = $r.decodeAspectCalls
            DecodeMisses = $r.verifyDecodeMisses
            CacheHits = $r.verifyCacheHits
            HitRate = [math]::Round($r.verifyHitRate,6)
            OsProbeCount = $r.osProbeCount
            Scanned = $r.scanned; Analyzed = $r.analyzed; Groups = $r.groups
        }
        Write-Host ("{0,-3} {1,-9} {2} host={3,8} summary={4,8} verifyDecode={5,7} misses={6} hits={7} groups={8}" -f `
            $i, $ver, $Flavour, [math]::Round($r.hostWallMs,1), [math]::Round($r.summaryWallMs,1), `
            [math]::Round($r.verifyDecodeMs,1), $r.verifyDecodeMisses, $r.verifyCacheHits, $r.groups)
    }
}

$csv = "$OutDir\raw_${Flavour}.csv"
$rows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8
Write-Host ""
Write-Host "raw rows -> $csv"

function Median([double[]]$v) {
    $s = $v | Sort-Object
    $n = $s.Count
    if ($n % 2 -eq 1) { return [double]$s[[int]($n/2)] }
    return ([double]$s[$n/2-1] + [double]$s[$n/2]) / 2.0
}

$b = $rows | Where-Object { $_.Slot -eq 'base' }
$c = $rows | Where-Object { $_.Slot -eq 'cand' }
$bm = Median @($b.HostWallMs); $cm = Median @($c.HostWallMs)
$bs = Median @($b.SummaryWallMs); $cs = Median @($c.SummaryWallMs)
$bv = Median @($b.VerifyDecodeMs); $cv = Median @($c.VerifyDecodeMs)
$bd = Median @($b.DecodeTotalMs); $cd = Median @($c.DecodeTotalMs)

Write-Host ""
Write-Host "=== I-3 FULL-SCAN SUMMARY ($Flavour, runs=$Repeats, host wall ms) ==="
Write-Host ("baseline  median = {0,9:N1}   [min {1:N1} max {2:N1}]" -f $bm, ($b.HostWallMs | Measure-Object -Minimum).Minimum, ($b.HostWallMs | Measure-Object -Maximum).Maximum)
Write-Host ("candidate median = {0,9:N1}   [min {1:N1} max {2:N1}]" -f $cm, ($c.HostWallMs | Measure-Object -Minimum).Minimum, ($c.HostWallMs | Measure-Object -Maximum).Maximum)
Write-Host ("delta          = {0,9:N1} ms" -f ($cm - $bm))
Write-Host ("reduction      = {0,8:P2}" -f (($bm - $cm)/$bm))
Write-Host ""
Write-Host ("engine wall:   baseline {0:N1} -> candidate {1:N1}  ({2:P2})" -f $bs, $cs, (($bs-$cs)/$bs))
Write-Host ("verifyDecode:  baseline {0:N1} -> candidate {1:N1}  ({2:P2})" -f $bv, $cv, (($bv-$cv)/$bv))
Write-Host ("decodeTotal:   baseline {0:N1} -> candidate {1:N1}  ({2:P2})" -f $bd, $cd, (($bd-$cd)/$bd))
Write-Host ""
$bg = ($b.Groups | Sort-Object -Unique) -join ','
$cg = ($c.Groups | Sort-Object -Unique) -join ','
Write-Host "group parity: baseline={$bg} candidate={$cg} match=$($bg -eq $cg)"
