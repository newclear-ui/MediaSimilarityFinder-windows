#requires -Version 7.0
<#
.SYNOPSIS
  MediaSimilarityFinder 문서 일관성 검사.

.DESCRIPTION
  다음을 검사한다.
    1. ko/en 쌍  (예외: docs/architecture/storage-design.md 는 ko 의 영문판으로 인정)
    2. docs/ 내 상대 링크 무결성 (fenced code block 제외)
    3. docs/state/current.* 존재 및 BUILD_STATUS 버전과 일치
    4. docs/llms.txt 크기 상한 (24KB)
    5. docs/state/current.* 줄 수 상한 (80줄)

.EXAMPLE
  pwsh -NoProfile -File src_unpacked/scripts/check_docs.ps1
#>
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $scriptDir          # src_unpacked
$docs = Join-Path $root 'docs'
$fail = 0

function Check([string]$name, [bool]$ok, [string]$detail = '') {
  if ($ok) { Write-Output "PASS  $name  $detail" }
  else { Write-Output "FAIL  $name  $detail"; $script:fail++ }
}

# ---------------------------------------------------------------- 1) ko/en 쌍
#  예외: 영문판 파일명이 xx.en.md 가 아니라 xx.md 인 문서는 아래 Test-Path 로 자동 허용된다
#        (예: docs/architecture/storage-design.md)
$all = Get-ChildItem $docs -Recurse -File -Filter *.md
$ko = $all | Where-Object { $_.Name -like '*.ko.md' } | ForEach-Object { $_.FullName -replace '\.ko\.md$', '' }
$en = $all | Where-Object { $_.Name -like '*.en.md' } | ForEach-Object { $_.FullName -replace '\.en\.md$', '' }
$missingEn = $ko | Where-Object {
  ($en -notcontains $_) -and (-not (Test-Path ($_ + '.md')))
}
Check 'ko/en 쌍' ($missingEn.Count -eq 0) "missing=$($missingEn.Count)"
$missingEn | ForEach-Object { Write-Output "        missing EN: $($_.Replace($docs + '\',''))" }

# ---------------------------------------------------------------- 2) 링크
$broken = New-Object System.Collections.Generic.List[string]
foreach ($f in $all) {
  $text = [System.IO.File]::ReadAllText($f.FullName)
  $text = [regex]::Replace($text, '(?ms)^```.*?^```', '')
  foreach ($m in [regex]::Matches($text, '\]\(([^)\s]+)\)')) {
    $t = $m.Groups[1].Value
    if ($t -match '^(https?://|mailto:|#|<)') { continue }
    $t = ($t -split '#')[0]
    if ([string]::IsNullOrWhiteSpace($t)) { continue }
    $p = if ($t.StartsWith('/')) { Join-Path $root ($t.TrimStart('/')) } else { Join-Path $f.DirectoryName $t }
    if (-not (Test-Path $p) -and -not (Test-Path ($p + '.md'))) {
      $broken.Add("$($f.FullName.Replace($root + '\','')) -> $t")
    }
  }
}
Check '상대 링크' ($broken.Count -eq 0) "broken=$($broken.Count)"
$broken | Select-Object -First 10 | ForEach-Object { Write-Output "        $_" }

# ---------------------------------------------------------------- 3) 상태 문서 버전 일치
$buildStatus = Join-Path $root 'BUILD_STATUS.md'
$stateKo = Join-Path $docs 'state\current.ko.md'
$stateEn = Join-Path $docs 'state\current.en.md'
Check 'state 문서 존재' ((Test-Path $stateKo) -and (Test-Path $stateEn))

if ((Test-Path $buildStatus) -and (Test-Path $stateKo)) {
  $bs = (Get-Content $buildStatus -TotalCount 5) -join "`n"
  $st = [System.IO.File]::ReadAllText($stateKo)
  $bsVer = if ($bs -match 'Current development version:\s*\*\*([0-9.]+)\*\*') { $Matches[1] } else { $null }
  $stVer = if ($st -match '버전:\s*\*\*([0-9.]+)\*\*') { $Matches[1] } else { $null }
  Check 'state 버전 = BUILD_STATUS' ($bsVer -and $stVer -and $bsVer -eq $stVer) "build=$bsVer state=$stVer"
}

# ---------------------------------------------------------------- 4) llms.txt 크기
$llms = Join-Path $docs 'llms.txt'
if (Test-Path $llms) {
  $len = (Get-Item $llms).Length
  Check 'llms.txt <= 24KB' ($len -le 24576) "bytes=$len"
}

# ---------------------------------------------------------------- 5) state 줄 수
foreach ($p in @($stateKo, $stateEn)) {
  if (Test-Path $p) {
    $n = (Get-Content $p).Count
    Check "state $(Split-Path $p -Leaf) <= 80줄" ($n -le 80) "lines=$n"
  }
}

# ---------------------------------------------------------------- 6) 빌드 번호 일치
#  원본: CMakeLists.txt project(... VERSION ...)  → BUILD_STATUS → state → matrix
$cmakePath = Join-Path $root 'CMakeLists.txt'
$cmakeVer = $null
if (Test-Path $cmakePath) {
  $cm = [System.IO.File]::ReadAllText($cmakePath)
  if ($cm -match 'project\s*\(\s*MediaSimilarityFinder\s+VERSION\s+([0-9.]+)') { $cmakeVer = $Matches[1] }
}
$bsVer = $null
if (Test-Path $buildStatus) {
  $bst = [System.IO.File]::ReadAllText($buildStatus)
  if ($bst -match 'Current development version:\s*\*\*([0-9.]+)\*\*') { $bsVer = $Matches[1] }
}
$stateVers = @{}
foreach ($lang in @('ko', 'en')) {
  $p = Join-Path $docs "state\current.$lang.md"
  if (Test-Path $p) {
    $t = [System.IO.File]::ReadAllText($p)
    if ($t -match '(?:버전|Version):\s*\*\*([0-9.]+)\*\*') { $stateVers[$lang] = $Matches[1] }
  }
}
Check 'CMake = BUILD_STATUS 버전' ($cmakeVer -and $bsVer -and $cmakeVer -eq $bsVer) "cmake=$cmakeVer build=$bsVer"
Check 'state 버전 = CMake' (($stateVers['ko'] -eq $cmakeVer) -and ($stateVers['en'] -eq $cmakeVer)) "ko=$($stateVers['ko']) en=$($stateVers['en']) cmake=$cmakeVer"

# matrix 는 기준선 버전을 최소 1회 포함해야 한다 (갱신 누락 감지)
foreach ($lang in @('ko', 'en')) {
  $p = Join-Path $docs "node-status-gate-matrix.$lang.md"
  if ((Test-Path $p) -and $cmakeVer) {
    $t = [System.IO.File]::ReadAllText($p)
    Check "matrix.$lang = 현재 버전" ($t.Contains($cmakeVer)) "want=$cmakeVer"
  }
}

# ---------------------------------------------------------------- 7) 엔진/DB/cache 상수 = 문서 표기
$consts = @{}
$srcMap = @{
  'engine' = @('src\media_search_engine.h', 'kEngineVersion\s*=\s*"([0-9.]+)"')
  'db'     = @('src\database.h',             'kDatabaseVersion\s*=\s*"([0-9.]+)"')
  'cache'  = @('src\video_fingerprint.h',    'kCacheFormatVersion\s*=\s*(\d+)')
}
foreach ($k in $srcMap.Keys) {
  $p = Join-Path $root $srcMap[$k][0]
  if (Test-Path $p) {
    $t = [System.IO.File]::ReadAllText($p)
    if ($t -match $srcMap[$k][1]) { $consts[$k] = $Matches[1] }
  }
}
Check '소스 상수 검출' ($consts.Count -eq 3) "engine=$($consts['engine']) db=$($consts['db']) cache=$($consts['cache'])"

if ($consts.Count -eq 3) {
  $structText = [System.IO.File]::ReadAllText((Join-Path $docs 'STRUCTURE.md'))
  Check 'STRUCTURE DB/engine/cache' (
    $structText.Contains("kDatabaseVersion $($consts['db'])") -and
    $structText.Contains("engine $($consts['engine'])") -and
    $structText.Contains("cache $($consts['cache'])")
  ) "db=$($consts['db']) engine=$($consts['engine']) cache=$($consts['cache'])"

  $llmsText = [System.IO.File]::ReadAllText((Join-Path $docs 'llms.txt'))
  Check 'llms.txt DB/engine/cache' (
    $llmsText.Contains("DB $($consts['db'])") -and
    $llmsText.Contains("engine $($consts['engine'])") -and
    $llmsText.Contains("cache $($consts['cache'])")
  ) "db=$($consts['db']) engine=$($consts['engine']) cache=$($consts['cache'])"
}

# ---------------------------------------------------------------- 8) version-index 존재
$vindex = Join-Path $docs 'version-index.md'
if (Test-Path $vindex) {
  $vi = [System.IO.File]::ReadAllText($vindex)
  $rows = ([regex]::Matches($vi, '(?m)^\| 0\.9\.4\.\d+ \|')).Count
  Check 'version-index 표 40행 이상' ($rows -ge 40) "rows=$rows"
}

Write-Output ''
if ($fail -gt 0) { Write-Output "RESULT: FAILED ($fail)"; exit 1 }
Write-Output 'RESULT: OK'
exit 0
