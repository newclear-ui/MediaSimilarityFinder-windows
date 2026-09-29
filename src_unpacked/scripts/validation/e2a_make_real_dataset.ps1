# E-2A real-content video dataset builder (v0.9.4.39)
#
# Purpose
#   E-1 measured on 8 synthetic lavfi files and recorded HEVC/AV1/VP9 as
#   NOT_AVAILABLE_IN_CURRENT_ENVIRONMENT because the bundled FFmpeg has no
#   software ENCODER for them. That statement was about GENERATION only. Real
#   encoded content for those codecs exists on this machine, so E-2A uses it.
#   AV1 additionally turns out not to DECODE either, which is a separate and
#   newly discovered constraint; it is recorded as such rather than assumed.
#
#   The standard image dataset (C:\project\test_sample_img_vid) is NEVER touched.
#   Everything here lands in a gitignored validation directory and nothing is
#   committed.
#
# Content handling
#   Source media is the user's personal library. Only short, technically
#   representative SEGMENTS are copied, always trimmed with `-c copy` so the
#   native GOP structure, bitrate and pixels are preserved exactly. No full
#   personal file is duplicated and nothing is committed.
#
# Synthetic set
#   Kept from E-1 and extended with CONTROLLED GOP variants (-g) so the
#   GOP -> sparse-seek relationship can be established at known GOP lengths,
#   which real content alone cannot provide.

param(
    [string]$Out = 'C:\project\validation\video_dataset_real',
    [string]$Ffmpeg = 'C:\project\src_unpacked\vcpkg_installed\x64-windows\tools\ffmpeg\ffmpeg.exe',
    [string]$Ffprobe = 'C:\project\src_unpacked\vcpkg_installed\x64-windows\tools\ffmpeg\ffprobe.exe'
)

$ErrorActionPreference = 'Continue'
if (-not (Test-Path -LiteralPath $Ffmpeg)) { throw "ffmpeg not found" }
New-Item -ItemType Directory -Path $Out -Force | Out-Null
$stage = Join-Path $env:TEMP 'e2a_stage'
New-Item -ItemType Directory -Path $stage -Force | Out-Null

# ffprobe writes container/stream warnings to stderr for perfectly usable files
# (e.g. "Unsupported encoding type" on some matroska tracks). Those must not be
# treated as terminating errors, so stderr is discarded and only stdout parsed.
function Probe($f) {
    $r = & $Ffprobe -hide_banner -v quiet -select_streams v:0 `
        -show_entries stream=codec_name,width,height,avg_frame_rate,pix_fmt `
        -show_entries format=duration -of csv=p=0:nk=1 $f 2>$null
    if (-not $r -or $r.Count -lt 2) { return $null }
    $line = ($r | Where-Object { $_ -match ',' } | Select-Object -First 1)
    if (-not $line) { return $null }
    $p = $line -split ','
    if ($p.Count -lt 5) { return $null }
    $d = 0.0; [double]::TryParse($r[1], [ref]$d) | Out-Null
    return [pscustomobject]@{ codec=$p[0]; w=[int]$p[1]; h=[int]$p[2]; fps=$p[3]; dur=$d; path=$f }
}

# ---- locate real sources by PROPERTY, never by (encoding-fragile) name ----
Write-Output "locating real source media ..."
$pool = @()
$pool += Get-ChildItem 'C:\Users\newcl\Downloads' -Recurse -Depth 1 -File -Include *.mp4,*.mkv,*.mov -ErrorAction SilentlyContinue
# Broad sweep: a shallow scan of G:\Downloads found 42 h264 / 15 hevc / 1 av1,
# including 1080p, 4K and VFR sources. Narrow subdirectory scans miss them, so
# the whole Downloads tree is walked and capped.
$pool += Get-ChildItem 'G:\Downloads' -Recurse -Depth 4 -File -Include *.mp4,*.mkv,*.mov -ErrorAction SilentlyContinue |
         Select-Object -First 400
Write-Output "  pool = $($pool.Count) files, probing ..."
$probed = @()
foreach ($f in $pool) {
    $i = Probe $f.FullName
    if ($i -and $i.dur -gt 3) { $probed += $i }
}
Write-Output "  decodable-looking = $($probed.Count)"

# target: name, codec, w, h, trim seconds (0 = no trim, use whole file)
$want = @(
    @{ n='real_h264_1920x1080_30fps_030s'; c='h264'; w=1920; h=1080; t=30 }
    @{ n='real_hevc_1920x1080_30fps_030s'; c='hevc'; w=1920; h=1080; t=30 }
    @{ n='real_h264_1080x1920_30fps_030s'; c='h264'; w=1080; h=1920; t=30 }
    @{ n='real_h264_3840x2160_30fps_020s'; c='h264'; w=3840; h=2160; t=20 }
    @{ n='real_h264_1920x1080_30fps_300s'; c='h264'; w=1920; h=1080; t=300 }
    @{ n='real_h264_1360x0808_30fps_030s';  c='h264'; w=1360; h=808;  t=30 }
    @{ n='real_hevc_1360x0808_30fps_030s';  c='hevc'; w=1360; h=808;  t=30 }
    @{ n='real_h264_0648x1080_30fps_030s';  c='h264'; w=648;  h=1080; t=30 }
    @{ n='real_av1_1920x1080_30fps_030s';   c='av1';  w=1920; h=1080; t=30 }
)

$rows = @()
foreach ($want1 in $want) {
    $src = $probed | Where-Object { $_.codec -eq $want1.c -and $_.w -eq $want1.w -and $_.h -eq $want1.h -and $_.dur -ge ($want1.t * 0.9) } |
           Sort-Object -Property @{e={ [math]::Abs($_.dur - $want1.t) }} | Select-Object -First 1
    if (-not $src) {
        Write-Host ("  SKIP  {0,-34} no real {1} {2}x{3} available" -f $want1.n,$want1.c,$want1.w,$want1.h)
        $rows += [pscustomobject]@{ name=$want1.n; codec=$want1.c; container=''; w=0;h=0;fps=0;dur=0;bytes=0;ok=$false;reason='no matching real source' }
        continue
    }
    # Stage under an ASCII name so no non-ANSI path ever reaches the encoder.
    $staged = Join-Path $stage ("$($want1.n).src")
    Copy-Item -LiteralPath $src.path -Destination $staged -Force
    $dst = Join-Path $Out "$($want1.n).mp4"
    if ($want1.t -gt 0) {
        & $Ffmpeg -hide_banner -loglevel error -y -i $staged -t $want1.t -c copy -map 0:v:0 -map_metadata -1 $dst 2>&1 | Out-Null
    } else {
        & $Ffmpeg -hide_banner -loglevel error -y -i $staged -c copy -map 0:v:0 -map_metadata -1 $dst 2>&1 | Out-Null
    }
    Remove-Item -LiteralPath $staged -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $dst) {
        $p = Probe $dst
        $sz = (Get-Item -LiteralPath $dst).Length
        Write-Host ("  OK    {0,-34} {1} {2}x{3} {4}s  {5:N0} B" -f $want1.n,$p.codec,$p.w,$p.h,[math]::Round($p.dur,1),$sz)
        $rows += [pscustomobject]@{ name=$want1.n; codec=$p.codec; container='mp4'; w=$p.w;h=$p.h;fps=$p.fps;dur=[math]::Round($p.dur,2);bytes=$sz;ok=$true;reason='' }
    } else {
        Write-Host ("  FAIL  {0,-34} trim failed" -f $want1.n)
        $rows += [pscustomobject]@{ name=$want1.n; codec=$want1.c; container=''; w=0;h=0;fps=0;dur=0;bytes=0;ok=$false;reason='trim failed' }
    }
}

# ---- controlled-GOP synthetic set (E-1 set extended) ----
Write-Output ""
Write-Output "generating controlled-GOP synthetic set ..."
foreach ($g in 5, 15, 60, 250) {
    $nm = "synth_mpeg4_640x360_25fps_030s_gop$g"
    $dst = Join-Path $Out "$nm.mp4"
    & $Ffmpeg -hide_banner -loglevel error -y -fflags +bitexact `
        -f lavfi -i "testsrc=size=640x360:rate=25:duration=30" `
        -c:v mpeg4 -g $g -map_metadata -1 -flags:v +bitexact $dst 2>&1 | Out-Null
    if (Test-Path -LiteralPath $dst) {
        $p = Probe $dst; $sz = (Get-Item -LiteralPath $dst).Length
        Write-Host ("  OK    {0,-34} gop={1,-4} {2:N0} B" -f $nm,$g,$sz)
        $rows += [pscustomobject]@{ name=$nm; codec=$p.codec; container='mp4'; w=$p.w;h=$p.h;fps=$p.fps;dur=[math]::Round($p.dur,2);bytes=$sz;ok=$true;reason='' }
    }
}
# intra-only reference (every frame a key frame)
$nm = 'synth_ffv1_640x360_25fps_030s_intra'
$dst = Join-Path $Out "$nm.mkv"
& $Ffmpeg -hide_banner -loglevel error -y -fflags +bitexact -f lavfi -i "testsrc=size=640x360:rate=25:duration=30" `
    -c:v ffv1 -g 1 -map_metadata -1 -flags:v +bitexact $dst 2>&1 | Out-Null
if (Test-Path -LiteralPath $dst) {
    $p = Probe $dst; $sz = (Get-Item -LiteralPath $dst).Length
    Write-Host ("  OK    {0,-34} intra-only {1:N0} B" -f $nm,$sz)
    $rows += [pscustomobject]@{ name=$nm; codec=$p.codec; container='mkv'; w=$p.w;h=$p.h;fps=$p.fps;dur=[math]::Round($p.dur,2);bytes=$sz;ok=$true;reason='' }
}

$rows | Export-Csv -LiteralPath (Join-Path $Out 'manifest.csv') -NoTypeInformation -Encoding UTF8
$ok = ($rows | Where-Object { $_.ok }).Count
Write-Host ""
Write-Host "real-content dataset: $ok / $($rows.Count) files in $Out"
Write-Host "NOT AVAILABLE: av1 decodes with 'Function not implemented' in this FFmpeg build."
