# ccx audio+PC-trace recorder. usage: tools\ccx_sfx.ps1 -Out tmp\x2b\NAME -State bg00_menu -Frames 330 -Mash "Circle:40,Circle:80,Circle:190"
# Writes <Out>\audio.raw(+.frames), <Out>\stdout.txt (TRACEPC lines), <Out>\log\ (hook-events.log). Window is minimized, -mute set.
param([Parameter(Mandatory=$true)][string]$Out, [string]$State="bg00_menu", [int]$Frames=330, [string]$Mash="", [switch]$NoMute, [string]$StateEvery="",
      [string]$Pcs="800368c4,8004a118,8004a168,8004f5c8,8004e9d0,800b9ddc,800b9f90")
$ErrorActionPreference="Stop"
$Root=(Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Ccx=(Resolve-Path (Join-Path $Root "..\rde-reimpl\bin\ccx\ccx.exe")).Path
$OutD=Join-Path $Root $Out; New-Item -ItemType Directory -Force $OutD | Out-Null
$log=Join-Path $OutD "log"; if(Test-Path $log){Remove-Item -Recurse -Force $log}
$sav= if(Test-Path $State){$State}else{Join-Path $Root "tmp\ref\states\$State.sav"}
$a=@("-uncapped","-set","GPU/ResolutionScale=1","-set","CCX/Movies=false","-set",("CCX/GameDir="+(Split-Path -Parent $Root)),
  "-set",("CCX/SaveDir="+(Join-Path $Root "tmp\ref\save")),"-set","InputSources/XInput=false","-set","InputSources/SDL=false",
  "-set","Main/StartFullscreen=false","-logdir",$log,"-mashhold","4","-state",$sav,"-frames","$Frames","-dumpaudio",(Join-Path $OutD "audio.raw"))
if(-not $NoMute){$a+=@("-set","Audio/OutputMuted=true")}
if($StateEvery){$sp,$s0,$e0=$StateEvery -split ","; $a+=@("-stateevery",$sp,$s0,$e0)}
foreach($m in ($Mash -split "," | Where-Object {$_})){ $b,$f=$m -split ":"; $a+=@("-mash",$b,"12",$f,"$([int]$f+12)") }
$env:CCX_TRACEPC=$Pcs
$q=$a | ForEach-Object { if($_ -match '[ =\\]'){'"'+$_+'"'}else{$_} }
. (Join-Path $PSScriptRoot "quiet_start.ps1")
$p=Start-Quiet $Ccx ($q -join " ") (Split-Path $Ccx) (Join-Path $OutD "stdout.txt") (Join-Path $OutD "stderr.txt"); $p.WaitForExit()   # minimised without activation
Copy-Item (Join-Path (Split-Path $Ccx) "ccx-data\duckstation.log") (Join-Path $OutD "ccx.log") -Force   # TRACEPC lines land here; other agents rotate it, copy at once
"exit $($p.ExitCode)"
