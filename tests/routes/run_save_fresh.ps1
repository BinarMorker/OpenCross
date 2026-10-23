# Fresh save.dat: an EMPTY save folder (tmp\save_fresh) -> the boot creates save.dat like RDE's first launch ->
# route `save_fresh` (save.txt) writes file 2 through the menu -> save_check.py / cc_save.py read it -> route `continue_fresh` continues from it.
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dir = Join-Path $root 'tmp\save_fresh'
Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dir | Out-Null
& (Join-Path $PSScriptRoot 'run.ps1') save_fresh
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$dat = Join-Path $dir 'save.dat'
if (-not (Test-Path $dat)) { 'FAIL: no save.dat created'; exit 1 }
python -I (Join-Path $root 'tools\py\cc_save.py') slots $dat
python -I (Join-Path $root 'tools\py\cc_save.py') checksum $dat
python -I (Join-Path $root 'tools\py\save_check.py') $dat
& (Join-Path $PSScriptRoot 'run.ps1') continue_fresh
exit $LASTEXITCODE
