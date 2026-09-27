param(
  [int]$Keep = 3
)
# D9b backup rotation: keep the $Keep most recent source backups, send the
# oldest to the recycle bin when a new one is added.
#
# The zip is produced by `git archive`, so its contents and structure are
# byte-identical to what GitHub serves for the same commit. Nothing is copied
# from the working tree, which means uncommitted edits can never leak into a
# backup and a backup can never disagree with the remote.
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$gitRoot = Split-Path -Parent $repoRoot
$backupDir = Join-Path $gitRoot "backup"
if (-not (Test-Path $backupDir)) { New-Item -ItemType Directory -Path $backupDir -Force | Out-Null }

# Refuse to back up anything the remote does not have. Without this check a
# local-only commit would produce a zip that silently diverges from GitHub.
$head = (& git -C $repoRoot rev-parse HEAD).Trim()
$remote = (& git -C $repoRoot rev-parse origin/main).Trim()
if ($head -ne $remote) {
  throw "HEAD ($head) differs from origin/main ($remote). Push first so the backup matches GitHub."
}
# `git status` prints nothing when the tree is clean, which becomes $null rather
# than an empty string, so join before trimming.
$dirty = ((& git -C $repoRoot status --porcelain --untracked-files=no) -join "`n").Trim()
if ($dirty) { throw "Uncommitted tracked changes present. Commit and push before making a backup." }

# Escape the dots: in a regex an unescaped '.' matches any character, which
# would truncate 0.9.4.25 to 0.9.4.
$version = (Select-String -Path (Join-Path $repoRoot "CMakeLists.txt") -Pattern 'project\([^\)]*VERSION (\d+\.\d+\.\d+\.\d+)' |
  Select-Object -First 1).Matches[0].Groups[1].Value
if (-not $version) { throw "Could not read the version from CMakeLists.txt." }
$zipName = "MediaSimilarityFinder-v$version-src.zip"
$zipPath = Join-Path $backupDir $zipName
# Exclude zips so a previous backup can never be nested inside the new one.
& git -C $gitRoot archive --format=zip --output="$zipPath" -v $head -- src_unpacked ':(exclude)src_unpacked/*.zip'
if ($LASTEXITCODE -ne 0) { throw "git archive failed: $LASTEXITCODE" }
Write-Host "Created $zipName from $head"

# Rotate: keep the newest $Keep, recycle the rest. Sorting on name works because
# the version segment is zero-padded and dotted, so lexical order is version order.
$all = Get-ChildItem $backupDir -Filter "MediaSimilarityFinder-v*-src.zip" | Sort-Object Name
if ($all.Count -gt $Keep) {
  foreach ($old in $all[0..($all.Count - $Keep - 1)]) {
    Add-Type -AssemblyName Microsoft.VisualBasic
    [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile(
      $old.FullName, [Microsoft.VisualBasic.FileIO.UIOption]::OnlyErrorDialogs,
      [Microsoft.VisualBasic.FileIO.RecycleOption]::SendToRecycleBin)
    Write-Host "Recycled $($old.Name)"
  }
}

Get-ChildItem $backupDir -Filter "*.zip" |
  ForEach-Object { "{0}  {1:N2} MB  {2}" -f $_.Name, ($_.Length / 1MB), (Get-Date $_.LastWriteTime -Format 'yyyy-MM-dd HH:mm') }
