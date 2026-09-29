param([string]$Csv = 'C:\project\validation\results_cpu10\raw_cpu.csv')
$rows = Import-Csv -LiteralPath $Csv
$base = $rows | Where-Object { $_.Slot -eq 'base' } | Sort-Object { [int]$_.Run }
$cand = $rows | Where-Object { $_.Slot -eq 'cand' } | Sort-Object { [int]$_.Run }

function Median([double[]]$v) {
    $s = $v | Sort-Object
    $n = $s.Count
    if ($n % 2 -eq 1) { return [double]$s[[int]($n/2)] }
    return ([double]$s[$n/2-1] + [double]$s[$n/2]) / 2.0
}

Write-Output "Run  baselineWall candidateWall   ratio   baseVerifyDec candVerifyDec  ratio"
Write-Output "--------------------------------------------------------------------------------"
$wr = @(); $dr = @(); $wins = 0
foreach ($b in $base) {
    $i = [int]$b.Run
    $c = $cand | Where-Object { [int]$_.Run -eq $i }
    $bw = [double]$b.HostWallMs; $cw = [double]$c.HostWallMs
    $bd = [double]$b.VerifyDecodeMs; $cd = [double]$c.VerifyDecodeMs
    $wr += $cw / $bw
    $dr += $cd / $bd
    if ($cw -lt $bw) { $wins++ }
    "{0,-4} {1,12:N1} {2,13:N1} {3,8:P2} {4,13:N1} {5,13:N1} {6,8:P2}" -f `
        $i, $bw, $cw, ($cw/$bw), $bd, $cd, ($cd/$bd)
}
Write-Output ""
Write-Output "candidate faster in $wins / $($base.Count) paired runs"
Write-Output ""
$mw = Median $wr; $md = Median $dr
Write-Output ("paired median wall ratio      = {0:P2}  (reduction {1:P2})" -f $mw, (1-$mw))
Write-Output ("paired median verifyDec ratio  = {0:P2}  (reduction {1:P2})" -f $md, (1-$md))
Write-Output ""
Write-Output "invariant counters (must be identical across every run):"
foreach ($f in 'DecodeMisses','CacheHits','HitRate','Scanned','Analyzed','Groups','DecodeCalls','DecodeAspectCalls','VerifyCalls') {
    $all = ($rows | ForEach-Object { $_.$f } | Sort-Object -Unique) -join ','
    "  {0,-18} = {1}" -f $f, $all
}
Write-Output ""
Write-Output "osProbeCount (expected to halve, not a parity field):"
"  baseline  = " + (($base | ForEach-Object { $_.OsProbeCount } | Sort-Object -Unique) -join ',')
"  candidate = " + (($cand | ForEach-Object { $_.OsProbeCount } | Sort-Object -Unique) -join ',')
