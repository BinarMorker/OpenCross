# Battle audio capture: one ccx run from a base state with a press script, recording the SPU (-dumpaudio) and the CCX_TRACEPC sound requests.
# usage: tools\ccx_audio_capture.ps1 -Out tmp\x2c\fireball -State bg00_menu_g2 -Frames 1100 -Cast 5      (element from Serge's list: Down x5, see ccx_capture.ps1 $TagScenes)
#        tools\ccx_audio_capture.ps1 -Out ... -State X -Frames N -Press "Circle,12,40,52;Down,20,300,340"   (raw -mash list: button,hold,start,end)
# Output: <Out>\audio.raw(+.frames), ccx.log (TRACEPC lines), states\ (-StateEvery period,start,end; used to resolve file pointers to tags), log\.  Muted unless -NoMute.
param([Parameter(Mandatory=$true)][string]$Out, [string]$State="bg00_menu_g2", [int]$Frames=1100, [int]$Cast=-1, [int]$Lead=0, [string]$Press="", [switch]$NoMute, [string]$StateEvery="40,40,1100",
      [string]$Pcs="800368c4,8004a118,8004a168,8004f5c8,8004e9d0,800b9ddc,800b9f90")
$ErrorActionPreference="Stop"
$Root=(Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Ccx=(Resolve-Path (Join-Path $Root "..\rde-reimpl\bin\ccx\ccx.exe")).Path
$OutD=Join-Path $Root $Out; New-Item -ItemType Directory -Force $OutD | Out-Null
$log=Join-Path $OutD "log"; if(Test-Path $log){Remove-Item -Recurse -Force $log}
$sav= if(Test-Path $State){(Resolve-Path $State).Path}else{Join-Path $Root "tmp\ref\states\$State.sav"}
$a=@("-uncapped","-set","GPU/ResolutionScale=1","-set","CCX/Movies=false","-set",("CCX/GameDir="+(Split-Path -Parent $Root)),
  "-set",("CCX/SaveDir="+(Join-Path $Root "tmp\ref\save_g2")),"-set","InputSources/XInput=false","-set","InputSources/SDL=false",
  "-set","Main/StartFullscreen=false","-logdir",$log,"-mashhold","4","-state",$sav,"-frames","$Frames","-dumpaudio",(Join-Path $OutD "audio.raw"))
if(-not $NoMute){$a+=@("-set","Audio/OutputMuted=true")}
if($StateEvery){$p,$s0,$e0=$StateEvery -split ","; $a+=@("-stateevery",$p,$s0,$e0)}
$m=@()
if($Cast -ge 0){   # Capture-Element press sequence of ccx_capture.ps1
  $m+="Circle,12,40,52","Circle,12,80,92","Circle,12,200,212","Down,20,300,340","Circle,12,360,372"
  if($Cast -gt 0){$m+="Down,20,400,$(400+20*$Cast)"}
  $m+="Circle,12,540,552","Circle,12,590,602"
}
foreach($p in ($Press -split ";" | Where-Object {$_})){$m+=$p}
foreach($p in $m){ $b,$h,$s,$e=$p -split ","; $a+=@("-mash",$b,$h,"$([int]$s+$Lead)","$([int]$e+$Lead)") }   # -Lead: frames to wait before the first press (chained runs)
$env:CCX_TRACEPC=$Pcs
$q=$a | ForEach-Object { if($_.Contains(' ') -or $_.Contains('=') -or $_.Contains([string][char]92)){'"'+$_+'"'}else{$_} }
$pr=Start-Process -FilePath $Ccx -ArgumentList $q -WorkingDirectory (Split-Path $Ccx) -PassThru -Wait -WindowStyle Minimized -RedirectStandardOutput (Join-Path $OutD "stdout.txt") -RedirectStandardError (Join-Path $OutD "stderr.txt")
Copy-Item (Join-Path (Split-Path $Ccx) "ccx-data\duckstation.log") (Join-Path $OutD "ccx.log") -Force   # TRACEPC lines land here (not stdout); runs of other agents rotate it, so copy at once
"exit $($pr.ExitCode)"
