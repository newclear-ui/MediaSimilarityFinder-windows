#requires -Version 7.0
<#
.SYNOPSIS
  docs/version-index.md 안의 전체 버전 인덱스 표를 build-history 파일에서 재생성한다.

.DESCRIPTION
  docs/build-history/0.9.4.<N>.ko.md 의 첫 줄(H1)에서 제목을 뽑아
  마커 사이의 표만 교체한다. 마커 밖의 내용(llms 원문 보존 절, 기준선 메타 등)은 건드리지 않는다.
  여러 번 실행해도 결과가 같다(idempotent).

.EXAMPLE
  pwsh -NoProfile -File src_unpacked/scripts/gen_version_index.ps1
  pwsh -NoProfile -File src_unpacked/scripts/gen_version_index.ps1 -Check
#>
param(
  [switch]$Check   # 표를 쓰지 않고, 현재 표가 최신인지만 검사
)
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $scriptDir
$docs = Join-Path $root 'docs'
$histDir = Join-Path $docs 'build-history'
$indexPath = Join-Path $docs 'version-index.md'

$begin = '<!-- BEGIN GENERATED VERSION INDEX -->'
$end = '<!-- END GENERATED VERSION INDEX -->'

$files = Get-ChildItem $histDir -Filter '0.9.4.*.ko.md' | ForEach-Object {
  if ($_.Name -match '^0\.9\.4\.(\d+)\.ko\.md$') {
    [pscustomobject]@{ Num = [int]$Matches[1]; Name = $_.Name; Full = $_.FullName }
  }
} | Sort-Object Num

if (-not $files) { throw 'no build-history files found' }

$rows = New-Object System.Collections.Generic.List[string]
foreach ($f in $files) {
  $h = (Get-Content $f.Full -TotalCount 1)
  $title = $h -replace '^#\s+', ''
  $title = $title -replace '^(MediaSimilarityFinder\s+)?v?\d+\.\d+\.\d+(?:\.\d+)?\s*[\u2014\u2013-]\s*', ''
  $title = $title.Trim()
  if ([string]::IsNullOrWhiteSpace($title)) { $title = '(제목 없음)' }
  $title = $title.Replace('|', '\|')
  $ver = "0.9.4.$($f.Num)"
  $rows.Add("| [$ver](build-history/$($f.Name)) | $title |")
}

$text = [System.IO.File]::ReadAllText($indexPath)
$nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }

$block = @()
$block += $begin
$block += ''
$block += "## 전체 버전 인덱스 (0.9.4.0 – 0.9.4.$($files[-1].Num))"
$block += ''
$block += '> `docs/build-history/` 의 각 버전 파일 제목에서 자동 생성한다. 직접 편집하지 않는다.'
$block += '> 재생성: `pwsh -NoProfile -File scripts/gen_version_index.ps1` (검사만: `-Check`).'
$block += '> 영문판 제목이 필요하면 같은 경로의 `.en.md` 를 연다.'
$block += ''
$block += '| 버전 | 제목 |'
$block += '|---|---|'
$block += $rows
$block += ''
$block += $end
$newBlock = ($block -join $nl)

if ($text.Contains($begin) -and $text.Contains($end)) {
  $i = $text.IndexOf($begin)
  $j = $text.IndexOf($end) + $end.Length
  $updated = $text.Substring(0, $i) + $newBlock + $text.Substring($j)
} else {
  $anchor = '## 0.9.4.x 이력'
  $k = $text.IndexOf($anchor)
  if ($k -lt 0) { throw 'anchor section not found' }
  $updated = $text.Substring(0, $k) + $newBlock + $nl + $nl + $text.Substring($k)
}

if ($Check) {
  if ($text -ceq $updated) {
    Write-Output "OK  version index is up to date  rows=$($files.Count)"
    exit 0
  }
  Write-Output "STALE  version index differs from build-history  expected_rows=$($files.Count)"
  exit 1
}

if ($text -cne $updated) {
  [System.IO.File]::WriteAllText($indexPath, $updated, (New-Object System.Text.UTF8Encoding($false)))
  Write-Output "UPDATED  rows=$($files.Count)  bytes=$((Get-Item $indexPath).Length)"
} else {
  Write-Output "UNCHANGED  rows=$($files.Count)"
}
