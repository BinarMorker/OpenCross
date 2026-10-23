# Builds librempeg (GPL-3 FFmpeg fork with the Bink 2 decoder) into third_party\librempeg\{include,lib} (gitignored).
# Needs Git for Windows (bash), GNU make (make / mingw32-make) and NASM; point -NasmDir / -MakeDir at them if not on PATH.
# Takes a few minutes (a clone plus a ~6 MB-source static build, 4 libs).
param(
    [string] $NasmDir = $env:NASM_DIR,
    [string] $MakeDir = $env:MAKE_DIR,
    [string] $Url = 'https://github.com/librempeg/librempeg'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ toolset not found' }
foreach ($line in (cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set")) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
}
if ($NasmDir) { $env:PATH = "$NasmDir;$env:PATH" }
if ($MakeDir) { $env:PATH = "$env:PATH;$MakeDir" }
$ErrorActionPreference = 'Continue'  # git/make write progress to stderr
& "$env:ProgramFiles\Git\bin\bash.exe" "$PSScriptRoot\build-librempeg.sh" $root $Url
if ($LASTEXITCODE -ne 0) { throw "librempeg build failed ($LASTEXITCODE)" }
Write-Host 'librempeg done.'
