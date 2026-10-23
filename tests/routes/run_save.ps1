# Save round trip: scratch save.dat (card 1 emptied) -> route `save` writes file 2 through the menu -> cc_save.py checks the file ->
# route `continue_save` continues from it.  .\tests\routes\run_save.ps1   ($env:OC_BUILD_DIR selects the build tree)
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$tree = if ($env:OC_BUILD_DIR) { $env:OC_BUILD_DIR } else { 'build\win-x64' }
$exe = Join-Path $root "$tree\bin\Debug\opencross.exe"
$scratch = Join-Path $root 'tmp\save_route.dat'
Remove-Item $scratch, "$scratch.bak" -ErrorAction SilentlyContinue
& $exe occ save-blank (Join-Path $root 'tmp\save.dat') $scratch | Out-Host
& (Join-Path $PSScriptRoot 'run.ps1') save
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$json = Join-Path $root 'tmp\save_route_dump.json'
python -I (Join-Path $root 'tools\py\cc_save.py') slots $scratch
python -I (Join-Path $root 'tools\py\cc_save.py') checksum $scratch
python -I (Join-Path $root 'tools\py\cc_save.py') dump $scratch 2 $json
python -I (Join-Path $PSScriptRoot 'check_save_dump.py') $json
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $PSScriptRoot 'run.ps1') continue_save
exit $LASTEXITCODE
