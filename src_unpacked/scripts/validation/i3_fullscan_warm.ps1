# I-3 condition B: warm / repeat-scan condition.
#
# Condition A (run_fullscan.ps1) used a fresh process per measurement, so the
# process-global 32-entry verify cache was always cold. That maximises decodeBoth
# activity.
#
# This script covers the other condition the directive asks for: several scans in
# ONE process, which is what a GUI session does when the user clicks Scan more
# than once. Runs 1..N-1 are used; run 0 is discarded as warm-up, matching the
# project's existing convention in dataset_baseline.cpp (its D3 block starts at
# index 1 for the same reason).
#
# Expectation, stated before looking at the numbers: the warm cache serves more
# lookups, decodeBoth runs less often, and therefore the I-2 effect on total wall
# time is SMALLER than in condition A. That is not a failure.

param(
    [ValidateSet('cpu','gpu')] [string]$Flavour = 'cpu',
    [int]$RunsPerProcess = 5,
    [string]$Dataset = 'C:\project\test_sample_img_vid',
    [string]$OutDir   = 'C:\project\validation\results_warm'
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$bins = @{
    'base' = "C:\project\validation\v0.9.4.35\src_unpacked\build-windows-$Flavour\Release\msf_dataset_baseline.exe"
    'cand' = "C:\project\src_unpacked\build-windows-$Flavour\Release\msf_dataset_baseline.exe"
}
$appPrefix = "C:\project\validation\appdir\i3w_${Flavour}"
$rows = @()

# Interleave whole processes so the machine-level drift is shared by both.
for ($p = 1; $p -le 3; $p++) {
    foreach ($k in @('base','cand')) {
        $ver = if ($k -eq 'base') { 'v0.9.4.35' } else { 'v0.9.4.36' }
        $out = & $bins[$k] $Dataset "${appPrefix}_$k" $RunsPerProcess 2>&1
        $text = $out | Out-String
        $ms = [regex]::Matches($text, 'run=(\d+) wall_ms=([0-9.]+) scanned=(\d+) analyzed=(\d+) groups=(\d+)')
        # run 0 is the discarded warm-up; runs 1..N-1 are the repeat-scan condition
        for ($i = 1; $i -lt $ms.Count; $i++) {
            $g = $ms[$i]
            $rows += [pscustomobject]@{
                Proc = $p; Run = [int]$g.Groups[1].Value; Version = $ver; Flavour = $Flavour; Slot = $k
                HostWallMs = [double]$g.Groups[2].Value
                Scanned = [int]$g.Groups[3].Value
                Analyzed = [int]$g.Groups[4].Value
                Groups = [int]$g.Groups[5].Value
            }
        }
        Write-Host ("process {0} {1} {2} collected {3} repeat-scan rows" -f $p, $ver, $Flavour, ($ms.Count-1))
    }
}
$csv = "$OutDir\raw_warm_${Flavour}.csv"
$rows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8

function Median([double[]]$v) { $s=$v|Sort-Object; $n=$s.Count; if($n%2 -eq 1){[double]$s[[int]($n/2)]} else {([double]$s[$n/2-1]+[double]$s[$n/2])/2.0} }
$b = $rows | Where-Object { $_.Slot -eq 'base' }
$c = $rows | Where-Object { $_.Slot -eq 'cand' }
$bm = Median @($b.HostWallMs); $cm = Median @($c.HostWallMs)
Write-Host ""
Write-Host "=== I-3 CONDITION B (warm/repeat scan, $Flavour, $($b.Count) rows each) ==="
Write-Host ("baseline  median = {0,9:N1}  [min {1:N1} max {2:N1}]" -f $bm, ($b.HostWallMs|Measure-Object -Minimum).Minimum, ($b.HostWallMs|Measure-Object -Maximum).Maximum)
Write-Host ("candidate median = {0,9:N1}  [min {1:N1} max {2:N1}]" -f $cm, ($c.HostWallMs|Measure-Object -Minimum).Minimum, ($c.HostWallMs|Measure-Object -Maximum).Maximum)
Write-Host ("delta = {0:N1} ms   reduction = {1:P2}" -f ($cm-$bm), (($bm-$cm)/$bm))
Write-Host ("group parity: baseline={0} candidate={1} match={2}" -f (($b.Groups|Sort-Object -Unique) -join ','), (($c.Groups|Sort-Object -Unique) -join ','), ((($b.Groups|Sort-Object -Unique) -join ',') -eq (($c.Groups|Sort-Object -Unique) -join ',')))
Write-Host "raw -> $csv"
