param([string]$Dir = 'C:\project\validation\results_cpu10',
      [string]$Label = 'CPU')
$rows = @()
foreach ($slot in 'base','cand') {
  Get-ChildItem -LiteralPath $Dir -Filter "*_${slot}.json" | ForEach-Object {
    $j = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
    $a = $j.analyze
    $rows += [pscustomobject]@{
      Slot = $slot
      Miss = [double]$a.verifyDecodeMisses
      VerifyDecode = [double]$a.verifyDecodeMs
      DecodeTotal = [double]$a.decodeTotalMs
      Open = [double]$a.decodeOpenMs
      Probe = [double]$a.osFileOpenProbeMs
      ProbeN = [double]$a.osFileOpenProbeCount
      Copy = [double]$a.decodeCopyMs
      Metadata = [double]$a.decodeMetadataMs
      ComInit = [double]$a.decodeComInitMs
      Resize = [double]$a.decodeResizeMs
      Convert = [double]$a.decodeConvertMs
    }
  }
}
function Median([double[]]$v) { $s=$v|Sort-Object; $n=$s.Count; if($n%2 -eq 1){[double]$s[[int]($n/2)]} else {([double]$s[$n/2-1]+[double]$s[$n/2])/2.0} }
$b = $rows | Where-Object { $_.Slot -eq 'base' }
$c = $rows | Where-Object { $_.Slot -eq 'cand' }
$miss = Median @($b.Miss)

Write-Output "=== $Label : decode-stage decomposition, medians over $(($b).Count) runs each ==="
Write-Output "misses per run = $miss   (identical in both)"
Write-Output ""
Write-Output ("{0,-26} {1,11} {2,11} {3,11} {4,9}" -f 'bucket','v0.9.4.35','v0.9.4.36','delta','per miss')
foreach ($f in 'VerifyDecode','DecodeTotal','Open','Probe','Metadata','ComInit','Resize','Convert','Copy') {
  $mb = Median @($b.$f); $mc = Median @($c.$f)
  "{0,-26} {1,11:N1} {2,11:N1} {3,11:N1} {4,9:N4}" -f $f, $mb, $mc, ($mc-$mb), (($mc-$mb)/$miss)
}
$pb = Median @($b.ProbeN); $pc = Median @($c.ProbeN)
Write-Output ""
Write-Output ("{0,-26} {1,11} {2,11}" -f 'osFileOpenProbeCount', $pb, $pc)
$dV = (Median @($b.VerifyDecode)) - (Median @($c.VerifyDecode))
$dP = (Median @($b.Probe)) - (Median @($c.Probe))
Write-Output ""
Write-Output "=== honest split of the verify-decode saving ==="
"{0,-40} {1,10:N1} ms" -f 'total verifyDecodeMs saving', $dV
"{0,-40} {1,10:N1} ms  ({2:P1})" -f '  of which removed D1 reference probes', $dP, ($dP/$dV)
"{0,-40} {1,10:N1} ms  ({2:P1})" -f '  of which other shared work avoided', ($dV-$dP), (1-($dP/$dV))
