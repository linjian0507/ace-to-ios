# ace-to-ios-sync.ps1 — rebuild the public source snapshot from this working tree and push it.
#
#   powershell -File tools/ace-to-ios-sync.ps1 -Message "what was completed"
#
# It exports the tracked source of the current branch, keeps the Reno Ace device source that lives
# outside the common tree (devices/pclm10, minus prebuilt/ and vendor-src/), drops every binary and
# image, and commits/pushes to https://github.com/linjian0507/ace-to-ios (main).
# No Apple firmware and no vendor firmware is ever copied: only source and notes.
param(
    [Parameter(Mandatory = $true)][string]$Message,
    [string]$Source = "S:\opencode\hakios",
    [string]$Stage = "S:\opencode\ace-to-ios-push",
    [switch]$DryRun
)
$ErrorActionPreference = "Stop"
$tmp = Join-Path $env:TEMP ("ace-to-ios-" + [guid]::NewGuid().ToString("N"))
$tree = Join-Path $tmp "tree"
New-Item -ItemType Directory -Force -Path $tree | Out-Null
$tar = Join-Path $tmp "snapshot.tar"

Write-Host "exporting $Source ..."
git -C $Source archive --format=tar --output=$tar HEAD
if ($LASTEXITCODE -ne 0) { throw "git archive failed" }
tar -xf $tar -C $tree
if ($LASTEXITCODE -ne 0) { throw "tar extract failed" }

# the snapshot's own note survives a re-export
$note = Join-Path $Stage "ACE-TO-IOS.md"
if (Test-Path $note) { Copy-Item $note $tree -Force }

# no binaries, no images
Remove-Item -Recurse -Force (Join-Path $tree "prebuilt") -ErrorAction SilentlyContinue
Get-ChildItem -Path $tree -Recurse -File |
    Where-Object { $_.Extension -in '.apfs', '.img', '.bin', '.efi', '.fd', '.raw', '.ipsw', '.dmg', '.mdt', '.tlv' } |
    Remove-Item -Force

# the device source that lives outside the common tree
$dev = Join-Path $Source "devices\pclm10"
$dst = Join-Path $tree "devices\pclm10"
New-Item -ItemType Directory -Force -Path $dst | Out-Null
foreach ($dir in 'configs', 'drivers', 'notes', 'soc', 'u-boot') {
    $from = Join-Path $dev $dir
    if (Test-Path $from) { robocopy $from (Join-Path $dst $dir) /E /NFL /NDL /NJH /NJS /NP | Out-Null }
}
foreach ($f in 'README.md', 'device.conf', 'pull-firmware.sh', 'pull-firmware-root.sh', 'pull-soc.sh', 'soc.args') {
    $from = Join-Path $dev $f
    if (Test-Path $from) { Copy-Item $from $dst -Force }
}

# mirror into the snapshot, keeping its .git
robocopy $tree $Stage /MIR /XD .git /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed: $LASTEXITCODE" }
$LASTEXITCODE = 0

# the snapshot tracks the device source and its note even though the common .gitignore does not
$gi = Join-Path $Stage ".gitignore"
if (-not (Select-String -Path $gi -Pattern '^!/devices/pclm10/$' -Quiet)) {
    Add-Content $gi ""
    Add-Content $gi "# snapshot: the Reno Ace device source is tracked here"
    Add-Content $gi "!/devices/pclm10/"
    Add-Content $gi "/devices/pclm10/prebuilt/"
    Add-Content $gi "/devices/pclm10/vendor-src/"
    Add-Content $gi "!/ACE-TO-IOS.md"
}

git -C $Stage add -A
git -C $Stage add -f devices/pclm10 ACE-TO-IOS.md
if ($DryRun) {
    Write-Host "--- staged (dry run) ---"
    git -C $Stage status --short
    Remove-Item -Recurse -Force $tmp
    return
}
$env:GCM_INTERACTIVE = "never"
git -C $Stage -c credential.interactive=false commit -m $Message
if ($LASTEXITCODE -ne 0) { Write-Host "nothing to commit"; Remove-Item -Recurse -Force $tmp; return }
git -C $Stage -c credential.interactive=false push origin main
Write-Host "pushed: $(git -C $Stage rev-parse --short HEAD)"
Remove-Item -Recurse -Force $tmp
