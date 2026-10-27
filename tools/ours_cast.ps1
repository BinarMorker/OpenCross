# Battle audio, our side of a cast scenario. usage: tools\ours_cast.ps1 -Out tmp\x2c\heal -Cast 88 -Tgt 0 [-Scene 7] [-Caster 0] [-Field -1] [-At 120] [-Frames 900]
# Deterministic 1/60 s ticks (--shotevery) so the mixer trace clock = game time; writes <Out>\ours.log and <Out>\ours.csv (sfx_cmp2.py).
param([Parameter(Mandatory=$true)][string]$Out,[int]$Cast,[int]$Tgt=3,[int]$Caster=0,[int]$Scene=7,[int]$Field=-1,[int]$At=120,[int]$Frames=900,[int]$Cast2=0,[int]$Cast2At=0,[int]$Cast2Tgt=-1)
$Root=(Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe=Join-Path $Root "build\win-x64\bin\Debug\opencross.exe"
New-Item -ItemType Directory -Force (Join-Path $Root $Out),(Join-Path $Root "tmp\x2c\shots") | Out-Null
$env:OC_SFX_TRACE="1"
$a=@("ocbattleview","$Scene","--cast","$Cast","--caster","$Caster","--casttgt","$Tgt","--castat","$At","--frames","$Frames","--shotevery","100000","--shotdir",(Join-Path $Root "tmp\x2c\shots"))
if($Cast2 -gt 0){$a+=@("--cast2","$Cast2","--cast2at","$Cast2At","--cast2tgt","$Cast2Tgt")}
if($Field -ge 0){$a+=@("--field","$Field")}
$lg=Join-Path $Root "$Out\ours.log"
$q=$a | ForEach-Object { if($_.Contains(" ")){'"'+$_+'"'}else{$_} }
Start-Process -FilePath $exe -ArgumentList $q -WorkingDirectory $Root -Wait -NoNewWindow -RedirectStandardError $lg -RedirectStandardOutput ($lg+".out")
Get-Content ($lg+".out") | Add-Content $lg
python (Join-Path $PSScriptRoot "py\sfx_cmp2.py") ours (Join-Path $Root "$Out\ours.log") (Join-Path $Root "$Out\ours.csv")
Copy-Item (Join-Path $Root "build\win-x64\bin\Debug\sfx_trace.wav") (Join-Path $Root "$Out\ours.wav") -Force   # the mixer capture of this run
