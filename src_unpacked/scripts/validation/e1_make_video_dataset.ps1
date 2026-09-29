# E-1 representative video dataset generator (v0.9.4.38)
#
# Creates a SEPARATE reference dataset. The standard image dataset
# (C:\project\test_sample_img_vid) is never touched.
#
# Reproducibility: -fflags +bitexact -map_metadata -1 removes the encoder and
# creation-time metadata that docs/test_sample_img_vid.md:116-129 identifies as
# the reason video was excluded from the standard dataset. This is a mitigation,
# not a guarantee -- the dataset is a measurement reference, not a hashed fixture.
#
# Codec availability is limited by the bundled FFmpeg 9.0.1, which has NO software
# encoder for H.264/HEVC/VP9/AV1. Verified: only h264_mf (MediaFoundation) works.
#   mpeg4 -> software, inter-frame
#   h264  -> h264_mf, inter-frame, the codec F targets
#   ffv1  -> software, INTRA-ONLY, so every frame is a keyframe (GOP contrast)
# HEVC / VP9 / AV1 are recorded as unavailable, not silently skipped.

param(
    [string]$Out = 'C:\project\validation\video_dataset',
    [string]$Ffmpeg = 'C:\project\src_unpacked\vcpkg_installed\x64-windows\tools\ffmpeg\ffmpeg.exe'
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Ffmpeg)) { throw "ffmpeg not found: $Ffmpeg" }
New-Item -ItemType Directory -Path $Out -Force | Out-Null

# name, codec, container, size, fps, seconds
$specs = @(
    @{ n='mpeg4_640x360_25fps_005s';  c='mpeg4';   m='mp4'; w=640;  h=360; fps=25; sec=5   }
    @{ n='mpeg4_640x360_25fps_030s';  c='mpeg4';   m='mp4'; w=640;  h=360; fps=25; sec=30  }
    @{ n='mpeg4_640x360_25fps_300s';  c='mpeg4';   m='mp4'; w=640;  h=360; fps=25; sec=300 }
    @{ n='h264_640x360_25fps_030s';   c='h264_mf'; m='mp4'; w=640;  h=360; fps=25; sec=30  }
    @{ n='ffv1_640x360_25fps_030s';   c='ffv1';    m='mkv'; w=640;  h=360; fps=25; sec=30  }
    @{ n='mpeg4_1920x1080_25fps_030s';c='mpeg4';   m='mp4'; w=1920; h=1080;fps=25; sec=30  }
    @{ n='mpeg4_640x360_05fps_030s';  c='mpeg4';   m='mp4'; w=640;  h=360; fps=5;  sec=30  }
    @{ n='mpeg4_640x360_60fps_030s';  c='mpeg4';   m='mp4'; w=640;  h=360; fps=60; sec=30  }
)

$results = @()
foreach ($s in $specs) {
    $path = Join-Path $Out "$($s.n).$($s.m)"
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    $args = @(
        '-hide_banner','-loglevel','error','-y',
        '-fflags','+bitexact',
        '-f','lavfi','-i',"testsrc=size=$($s.w)x$($s.h):rate=$($s.fps):duration=$($s.sec)",
        '-c:v',$s.c,
        '-map_metadata','-1',
        '-flags:v','+bitexact',
        $path
    )
    & $Ffmpeg @args 2>&1 | Out-Null
    if ((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 0) {
        $sz = (Get-Item -LiteralPath $path).Length
        Write-Host ("  OK      {0,-32} {1,10:N0} bytes" -f $s.n, $sz)
        $results += [pscustomobject]@{ name=$s.n; codec=$s.c; container=$s.m; w=$s.w; h=$s.h; fps=$s.fps; sec=$s.sec; bytes=$sz; ok=$true }
    } else {
        Write-Host ("  FAILED  {0,-32} ({1})" -f $s.n, $s.c)
        $results += [pscustomobject]@{ name=$s.n; codec=$s.c; container=$s.m; w=$s.w; h=$s.h; fps=$s.fps; sec=$s.sec; bytes=0; ok=$false }
    }
}

$results | Export-Csv -LiteralPath (Join-Path $Out 'manifest.csv') -NoTypeInformation -Encoding UTF8
$ok = ($results | Where-Object { $_.ok }).Count
Write-Host ""
Write-Host "generated $ok / $($results.Count) into $Out"
Write-Host "UNAVAILABLE in this FFmpeg build (documented, not silently skipped): libx264 libx265 libvpx-vp9 libaom-av1  (no software encoder); hevc_mf and av1_* fail at runtime"
