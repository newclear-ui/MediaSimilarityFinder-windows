# E-1 planner-input analysis.
#
# Question: which measured variables actually explain the decoded-vs-requested
# gap and the decode cost? This decides the planner input set (Gate C).
# Rules applied, per directive section 10: keep a variable only if it explains
# decode time or end-to-end time. Drop the rest.

$rows = @(
 @{n='mpeg4_640x360_05fps_030s';  c='mpeg4'; w=640; h=360; fps=5.000; dur=30.0;  planN=16; dec=150; emit=15; keys=13; decMs=18.322;  convMs=1.954; openMs=0.537; seekMs=0.002; sweepMs=21.488}
 @{n='mpeg4_640x360_25fps_005s';  c='mpeg4'; w=640; h=360; fps=25.000; dur=5.0;   planN=6;  dec=125; emit=6;  keys=11; decMs=13.904;  convMs=0.842; openMs=0.798; seekMs=0.002; sweepMs=15.909}
 @{n='mpeg4_640x360_25fps_030s';  c='mpeg4'; w=640; h=360; fps=25.000; dur=30.0;  planN=16; dec=750; emit=16; keys=63; decMs=81.172;  convMs=2.089; openMs=0.623; seekMs=0.002; sweepMs=86.085}
 @{n='mpeg4_640x360_25fps_300s';  c='mpeg4'; w=640; h=360; fps=25.000; dur=300.0; planN=39; dec=7500;emit=39; keys=625;decMs=768.491; convMs=5.232; openMs=1.254; seekMs=0.002; sweepMs=790.293}
 @{n='mpeg4_640x360_60fps_030s';  c='mpeg4'; w=640; h=360; fps=60.000; dur=30.0;  planN=16; dec=1798;emit=16; keys=150;decMs=195.324; convMs=1.997; openMs=0.739; seekMs=0.002; sweepMs=201.609}
 @{n='mpeg4_1920x1080_25fps_030s'; c='mpeg4'; w=1920;h=1080;fps=25.000; dur=30.0;  planN=16; dec=750; emit=16; keys=63; decMs=747.765; convMs=6.907; openMs=1.299; seekMs=0.008; sweepMs=762.797}
 @{n='h264_640x360_25fps_030s';   c='h264'; w=640; h=360; fps=25.000; dur=30.0;  planN=16; dec=750; emit=16; keys=15; decMs=144.040; convMs=2.314; openMs=2.017; seekMs=0.030; sweepMs=151.688}
 @{n='ffv1_640x360_25fps_030s';   c='ffv1'; w=640; h=360; fps=25.000; dur=30.0;  planN=16; dec=750; emit=16; keys=63; decMs=1117.644;convMs=8.497; openMs=0.620; seekMs=0.065; sweepMs=1133.582}
)

function Interval($d) { if($d -le 10){1} elseif($d -le 60){2} elseif($d -le 240){4} elseif($d -le 960){8} elseif($d -le 3840){16} else {32} }

Write-Output "=== H1: decoded/requested ratio is explained by fps x interval(duration) ==="
Write-Output ("{0,-28} {1,8} {2,8} {3,9} {4,9} {5,8}" -f 'file','fps','interval','pred','measured','err%')
$h1err=@()
foreach($r in $rows){
  $iv = Interval $r.dur
  $pred = $r.fps * $iv
  $meas = $r.dec / $r.emit
  $err = 100.0*($meas-$pred)/$pred
  $h1err += [math]::Abs($err)
  "{0,-28} {1,8:N1} {2,8} {3,9:N1} {4,9:N1} {5,8:N1}" -f $r.n,$r.fps,$iv,$pred,$meas,$err
}
Write-Output ("  mean |error| = {0:N2} %   -> hypothesis holds: ratio is NOT an independent variable" -f ($h1err|Measure-Object -Average).Average)
Write-Output ""

Write-Output "=== H2: per-frame decode cost is explained by codec x resolution, not by fps or duration ==="
Write-Output ("{0,-28} {1,-6} {2,10} {3,14} {4,12} {5,12}" -f 'file','codec','px','decoded','decode_ms','ms/frame')
$grp=@{}
foreach($r in $rows){
  $pf = $r.decMs / $r.dec
  $key = "$($r.c) $($r.w)x$($r.h)"
  "{0,-28} {1,-6} {2,10} {3,14} {4,12:N1} {5,12:N4}" -f $r.n,$r.c,($r.w*$r.h),$r.dec,$r.decMs,$pf
  if(-not $grp.ContainsKey($key)){$grp[$key]=@()}
  $grp[$key]+=$pf
}
Write-Output ""
Write-Output "  grouped by codec x resolution (spread within a group = influence of fps/duration):"
foreach($k in ($grp.Keys|Sort-Object)){
  $v=$grp[$k]
  $mn=($v|Measure-Object -Minimum).Minimum; $mx=($v|Measure-Object -Maximum).Maximum
  $sp = if($mn -gt 0){ 100.0*($mx-$mn)/$mn } else { 0 }
  "    {0,-18} n={1}  min={2:N4} max={3:N4}  spread={4:N1} %" -f $k,$v.Count,$mn,$mx,$sp
}
Write-Output ""

Write-Output "=== H3: time decomposition -- which stage is worth optimising ==="
$totDec=0.0;$totConv=0.0;$totOpen=0.0;$totSeek=0.0;$totSweep=0.0
foreach($r in $rows){ $totDec+=$r['decMs']; $totConv+=$r['convMs']; $totOpen+=$r['openMs']; $totSeek+=$r['seekMs']; $totSweep+=$r['sweepMs'] }
"    decode   {0,9:N1} ms  {1,6:P2} of sweep" -f $totDec,($totDec/$totSweep)
"    convert  {0,9:N1} ms  {1,6:P2} of sweep" -f $totConv,($totConv/$totSweep)
"    open     {0,9:N1} ms  {1,6:P2} of sweep" -f $totOpen,($totOpen/$totSweep)
"    seek     {0,9:N1} ms  {1,6:P2} of sweep" -f $totSeek,($totSeek/$totSweep)
Write-Output "    -> conversion, open and seek are not worth planning against at this scale."
Write-Output ""

Write-Output "=== H4: GOP length from key packets (input for a sparse-seek cost model) ==="
Write-Output ("{0,-28} {1,8} {2,8} {3,10}" -f 'file','frames','keys','gop_est')
foreach($r in $rows){
  $g = $r.dec / $r.keys
  "{0,-28} {1,8} {2,8} {3,10:N1}" -f $r.n,$r.dec,$r.keys,$g
}
Write-Output "    NOTE ffv1 is intra-only, so every packet should be a key frame; the measured 63/750"
Write-Output "    shows AV_PKT_FLAG_KEY is NOT reliable for ffv1-in-matroska. GOP input therefore"
Write-Output "    needs a different source before it can drive a cost model."
Write-Output ""

Write-Output "=== H5: what a sparse-seek strategy would cost (MODEL, NOT MEASURED) ==="
Write-Output "    Assumes: seek to target requires decoding from the previous keyframe, so the"
Write-Output "    expected extra frames per sample is GOP/2. That assumption is UNVERIFIED here."
Write-Output ("{0,-28} {1,8} {2,8} {3,10} {4,12} {5,10}" -f 'file','seqFrm','smp','gop','sparseFrm','saving')
foreach($r in $rows){
  $g = $r.dec / $r.keys
  $sparse = $r.emit * $g / 2.0
  $save = 100.0*($r.dec - $sparse)/$r.dec
  "{0,-28} {1,8} {2,8} {3,10:N1} {4,12:N0} {5,9:N1}%" -f $r.n,$r.dec,$r.emit,$g,$sparse,$save
}
Write-Output "    This is arithmetic on measured inputs, NOT a measured end-to-end result."
Write-Output "    It must be implemented (E-2) and measured before any of it is claimed."
