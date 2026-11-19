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
#  상한은 "LLM이 한 번에 훑을 수 있는 인덱스"를 유지하기 위한 가독성 목표다.
#  24KB -> 28KB (2026-11-19): ChatGPT 경로에 운영 제약(휴지통/임시경로/승인/문서규칙)을
#  포함시키면서 초과했다. 중복 서사를 늘리지 않는 선에서만 상향한다.
$llms = Join-Path $docs 'llms.txt'
if (Test-Path $llms) {
  $len = (Get-Item $llms).Length
  Check 'llms.txt <= 28KB' ($len -le 28672) "bytes=$len"
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

# ---------------------------------------------------------------- 8) version-index 존재·드리프트
$vindex = Join-Path $docs 'version-index.md'
if (Test-Path $vindex) {
  $vi = [System.IO.File]::ReadAllText($vindex)
  $rows = ([regex]::Matches($vi, '(?m)^\| 0\.9\.4\.\d+ \|')).Count
  Check 'version-index llms 원문 표 40행 이상' ($rows -ge 40) "rows=$rows"

  $genRows = ([regex]::Matches($vi, '(?m)^\| \[0\.9\.4\.\d+\]\(build-history/')).Count
  $histCount = (Get-ChildItem (Join-Path $docs 'build-history') -Filter '0.9.4.*.ko.md').Count
  Check 'version-index 전체 인덱스 = build-history 파일 수' ($genRows -eq $histCount) "rows=$genRows files=$histCount"
  if ($cmakeVer) {
    Check 'version-index 에 현재 버전' ($vi.Contains("build-history/$cmakeVer.ko.md")) "want=$cmakeVer"
  }
}

# ---------------------------------------------------------------- 9) 본문에 적힌 경로 참조 존재
#  STRUCTURE.md / llms.txt 처럼 링크가 아닌 백틱 경로로 안내하는 문서를 검사한다.
#  `x.ko/.en.md` 축약 표기는 두 파일로 펼쳐서 확인한다.
$refTargets = @(
  (Join-Path $root 'AGENTS.md'),
  (Join-Path $docs 'STRUCTURE.md'),
  (Join-Path $docs 'llms.txt'),
  (Join-Path $docs 'state\current.ko.md'),
  (Join-Path $docs 'state\current.en.md'),
  (Join-Path $docs 'node-status-gate-matrix.ko.md'),
  (Join-Path $docs 'node-status-gate-matrix.en.md')
)
$refChecked = 0
$refMissing = New-Object System.Collections.Generic.List[string]
foreach ($t in $refTargets) {
  if (-not (Test-Path $t)) { continue }
  $text = [System.IO.File]::ReadAllText($t)
  foreach ($m in [regex]::Matches($text, '((?:docs|scripts|src|gui|tests)/[A-Za-z0-9_./\-]+\.(?:md|txt|ps1|h|cpp))')) {
    $rel = $m.Groups[1].Value
    if ($rel -match '[<>*]') { continue }
    $paths = @()
    if ($rel -match '^(.*)\.ko/\.en\.(md|txt)$') {
      $paths += ($Matches[1] + '.ko.' + $Matches[2])
      $paths += ($Matches[1] + '.en.' + $Matches[2])
    } else {
      $paths += $rel
    }
    foreach ($r in $paths) {
      $refChecked++
      if (-not (Test-Path (Join-Path $root ($r -replace '/', '\')))) {
        $refMissing.Add("$(Split-Path $t -Leaf) -> $r")
      }
    }
  }
}
Check '본문 경로 참조' ($refMissing.Count -eq 0) "checked=$refChecked missing=$($refMissing.Count)"
$refMissing | Select-Object -First 8 | ForEach-Object { Write-Output "        $_" }

# ---------------------------------------------------------------- 10) 목차 최신
$genToc = Join-Path $scriptDir 'gen_toc.ps1'
if (Test-Path $genToc) {
  $tocOut = & pwsh -NoProfile -File $genToc -All -Check 2>&1
  $tocExit = $LASTEXITCODE
  $tocCount = @($tocOut | Select-String 'toc up to date').Count
  Check 'TOC 최신' ($tocExit -eq 0) "files=$tocCount"
  $tocOut | Select-String 'STALE' | ForEach-Object { Write-Output "        $($_.Line)" }
}

Write-Output ''
if ($fail -gt 0) { Write-Output "RESULT: FAILED ($fail)"; exit 1 }
Write-Output 'RESULT: OK'
exit 0
