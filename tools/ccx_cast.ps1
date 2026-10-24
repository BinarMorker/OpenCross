# ccx_cast.ps1 - capture the ccx (PS1 under DuckStation) reference of any battle effect.
# Output per element id: tmp\r34\<id>\ref\{states\state_NNNNNN.sav (every 5 frames over the effect), frames\frame_NNNNNN.png (every 4), manifest.txt}
#
#   tools\ccx_cast.ps1 batch  -Tag v1 -Ids 7,4                 up to 8 PC-castable elements / traps cast by Serge (forge + boot once, then one capture per id)
#   tools\ccx_cast.ps1 summon -Tag v2 -Ids 43                  summons: as batch plus the ring poke (colour x3) in an uncompressed copy of the menu state
#   tools\ccx_cast.ps1 tech   -Char 1 -Ids 163                 the owning character casts its techs (party slot 0 = Char; Tag defaults to t<Char>)
#   tools\ccx_cast.ps1 enemy  -Scene 84 [-Area 12] [-Frames 3000] [-Poke "0x800E1BA8=70:2"]
#                                                              enemy's OWN casts: poke battle id 0x200|scene into tmp\ref\states\field53_encounter_p.sav,
#                                                              pass 1 = one long run (states every 30), harvest -> tmp\r34\scene_<n>\actions.tsv,
#                                                              pass 2 = one capture per harvested (unit, element) from the pass-1 state before it
#   enemy mode, with -Poke: -MenuFrames N (first-menu search length, default 320) -MenuMash (press the mash button while searching: dialogue scenes); -Pick "3/447" only that unit/element pair; -KeepPass1 also keeps every pass-1 state
#   -PartyIds 0,1 (forged party, slot 0 = Char) and -ForgeArgs "--char|1:hp=100|--char|1:curhp=0" (| separated, extra cc_save_forge.py arguments: a KO'd ally for Revive 94 / 95)
#   options: -Jobs N (batch/summon/tech: N ccx instances at once, each with its own logdir + SaveDir copy), -Only <id> (one id of the
#            -Ids row), -Frames N (capture length), -Window N (enemy pass 2 length, default 500), -Rebuild (redo forge + boot),
#            -KeepPass1, -Mash <button> (enemy mode, default Circle), -MaxCaps N (enemy mode: stop after N captures)
#   tools\ccx_cast.ps1 attack -Tag a1 [-Keys "Circle:40,Circle:80,Circle:200,Circle:300,Cross:420"] [-Frames 560]: Serge's physical attack from the
#            bg_00 menu base (Attack, target, the HIT% picks of -Keys, Cross = end the turn); every state (every 4) and frame (every 4) kept -> <out>\attack_<Tag>\ref
#   -Disc2 (enemy mode) switches the poked encounter state to disc 2 first (scenes 342-344, 458-471, 509)
#   -P2Every N -P2Lo A -P2Hi B (enemy pass 2: a state every N frames over run frames A..B, default 5 / 0 / Window; use with -NoPrune for a dense slice)
#   -P2Dump N (enemy pass 2: a frame PNG every N frames, default 4)
#
# Down-count rule: Serge's / the character's row 0 is padded to its length N (cc_save_forge.py rowlen); the element list opens on entry 3
# (0-based) and Down walks with wrap, so id at index k needs (k - 3) mod N Downs. If stage 1 selects another id the opening index is
# re-derived from the RAM reading (e05da) and stage 1 repeated once. Nothing is taken from the PNGs; every step is read from RAM.
param(
  [Parameter(Mandatory = $true, Position = 0)][ValidateSet("batch", "summon", "tech", "enemy", "attack")][string]$Mode,
  [string]$Tag = "", [string]$Ids = "", [int]$Char = 0, [int]$Scene = -1, [int]$Area = -1,
  [int]$Frames = 0, [int]$Window = 500, [int]$Lead = 150, [string]$Poke = "", [int]$Jobs = 1, [int]$Only = -1,
  [switch]$Rebuild, [switch]$NoPrune, [switch]$KeepPass1, [string]$Mash = "Circle", [int]$MaxCaps = 99,
  [string]$PartyIds = "", [string]$ForgeArgs = "", [string]$MenuPoke = "", [string]$PrePoke = "", [string]$Pick = "", [switch]$SkipExisting, [int]$MenuFrames = 320, [switch]$MenuMash, [int]$ElemShift = 0, [int]$P2Every = 5, [int]$P2Dump = 4, [int]$P2Lo = 0, [int]$P2Hi = 0, [switch]$MenuAny, [switch]$ElemHit, [int]$P1Every = 30, [int]$ElemDowns = 2, [int]$ElemUps = 0, [string]$VarPoke = "", [switch]$Disc2, [string]$Keys = "", [string]$GameDir = "", [string]$CcxExe = "..\rde-reimpl\bin\ccx\ccx.exe", [string]$OwnerSaveCopy = "tmp\save.dat",
  [string]$EncState = "tmp\ref\states\field53_encounter_p.sav", [string]$EncSave = "tmp\ref\save_p"
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
# Run from a .claude\worktrees\<x> checkout, the capture tree, the saves / states and ccx come from MAIN (the OpenCross checkout holding tmp\ and ..\rde-reimpl)
$Main = $Root; if ($Root -match '^(.*?)\\\.claude\\worktrees\\[^\\]+$') { $Main = $Matches[1] }
$Ccx = (Resolve-Path (Join-Path $Main $CcxExe)).Path
$Py = Join-Path $Root "tools\py"
$Out = if ($env:OC_CCX_OUT) { $env:OC_CCX_OUT } else { Join-Path $Main "tmp\r34" }   # OC_CCX_OUT = another capture root (never overwrite existing capture folders)
New-Item -ItemType Directory -Force $Out | Out-Null
$IdList = @(); if ($Ids) { $IdList = @($Ids -split "," | ForEach-Object { [int]$_ }) }
if ($Mode -eq "tech" -and -not $Tag) { $Tag = "t$Char" }
if ($Mode -ne "enemy" -and -not $Tag) { throw "-Tag is required" }
if ($Mode -notin @("enemy", "attack") -and ($IdList.Count -lt 1 -or $IdList.Count -gt 8)) { throw "-Ids: 1..8 element ids" }

. (Join-Path $PSScriptRoot "quiet_start.ps1")
function Log([string]$m) { Write-Host ("[{0}] {1}" -f (Get-Date -Format HH:mm:ss), $m) }

# One ccx run. Own -logdir, own SaveDir copy (parallel-safe), minimised, below-normal priority (never steals focus / CPU from the owner's game).
function Invoke-Ccx([string]$LogDir, [string[]]$ExtraArgs, [string]$SaveSrc) {
  if (Test-Path $LogDir) { Remove-Item -Recurse -Force $LogDir }
  $sv = "$LogDir.sv"
  if (Test-Path $sv) { Remove-Item -Recurse -Force $sv }
  New-Item -ItemType Directory -Force $sv | Out-Null
  Copy-Item (Join-Path $SaveSrc "*") $sv -Recurse -Force
  $a = @("-uncapped", "-set", "GPU/ResolutionScale=2", "-set", "CCX/Movies=false",
         "-set", ("CCX/GameDir=" + $(if ($GameDir) { $GameDir } else { Split-Path -Parent $Main })), "-set", ("CCX/SaveDir=" + $sv),
         "-set", "InputSources/XInput=false", "-set", "InputSources/SDL=false", "-set", "Main/StartFullscreen=false",
         "-set", "Audio/OutputMuted=true", "-logdir", $LogDir) + $ExtraArgs
  $q = $a | ForEach-Object { if ($_ -match '[ =\\]') { '"' + $_ + '"' } else { $_ } }
  # Minimised without activation, muted, below-normal priority (tools\quiet_start.ps1).
  $p = Start-Quiet $Ccx ($q -join " ") (Split-Path $Ccx)
  $p.WaitForExit()
  Remove-Item -Recurse -Force $sv -ErrorAction SilentlyContinue
  if ($p.ExitCode -ne 0) { throw "ccx exited with $($p.ExitCode) ($LogDir)" }
}
# -ElemHit = Serge first attacks (weak hit, gauge -1 -> 0) and then casts the element under the opening cursor at the enemy (the stage-1 key
# script of the PC-cast modes, no Downs), before the Circle mash starts at pass-1 frame 700: the enemy scripts that react to an ELEMENT hit (slot 4: var 0x20 == 1).
function ElemHit-Args([int]$f0, [int]$end) {
  $a = @(); $S = $ElemShift   # -ElemShift N delays the whole key script (scenes whose first command menu opens later than scene 374's: Harle 419 ~ +50)
  $abs = @(@("Circle", 40 + $S), @("Circle", 80 + $S), @("Circle", 200 + $S), @("Circle", 400 + $S), @("Circle", 540 + $S), @("Circle", 590 + $S))
  foreach ($p in $abs) { $f = $p[1] - $f0; if ($f -ge 0 -and $f -lt $end) { $a += @("-mash", $p[0], "12", "$f", "$($f + 12)") } }
  foreach ($d in @(300, 340, 380, 420)[0..($ElemDowns - 1)]) { $f = $d + $S - $f0; if ($f -ge 0 -and $f -lt $end) { $a += @("-mash", "Down", "12", "$f", "$($f + 12)") } }
  for ($k = 0; $k -lt $ElemUps; $k++) { $f = 450 + $S + 20 * $k - $f0; if ($f -ge 0 -and $f -lt $end) { $a += @("-mash", "Up", "12", "$f", "$($f + 12)") } }
  $m0 = [Math]::Max(10, 700 + $S - $f0)
  if ($m0 -lt $end) { $a += @("-mash", $Mash, "30", "$m0", "$end") }
  return ,$a
}
function Press([string]$Button, [int]$Frame) { return @("-mash", $Button, "12", "$Frame", "$($Frame + 12)") }
function Py { & python @args }
function Field([string]$Sav, [string]$Name) {
  return (& python -c "import sys; sys.path.insert(0, r'$Py'); import ccx_state as c; print(c.State(r'$Sav').watch()['$Name'])")
}
function Pick([string]$Dir, [string]$Expr) {
  $code = "import sys,os; sys.path.insert(0, r'$Py'); import ccx_state as c`n" +
          "for p in c.list_states([r'$Dir']):`n    r = c.State(p).watch()`n    if ($Expr): print(p); break"
  return (& python -c $code)
}
function Poke-State([string]$StateIn, [string]$StateOut, [string[]]$Pokes, [string]$Work, [string]$SaveSrc) {
  # uncompressed copy (3 frames), then ccx_state.py poke
  $unc = Join-Path $Work "unc"
  Invoke-Ccx $unc @("-state", $StateIn, "-set", "Main/SaveStateCompression=Uncompressed", "-frames", "3", "-stateevery", "1", "1", "2") $SaveSrc
  Py (Join-Path $Py "ccx_state.py") poke (Join-Path $unc "states\state_000001.sav") $StateOut @Pokes | Out-Null
  Remove-Item -Recurse -Force $unc -ErrorAction SilentlyContinue
}
function Write-Manifest([string]$Dir, [string]$Elem, [string[]]$Lines) {
  $sd = Join-Path $Dir "states"
  $hdr = @("date: $(Get-Date -Format s)", "ccx: $Ccx", "element: $Elem", "mode: $Mode") + $Lines
  $m = @(Py (Join-Path $Py "ccx_state.py") manifest $sd $Elem)
  $cam = (Py (Join-Path $Py "ccx_state.py") campick $sd)
  $w = @(Py (Join-Path $Py "ccx_state.py") watch --fields "btl_frame,tgt_064b,act_kind,act_actor,tgt0,e05da,elem,fx_load,ovl,ovl0,party0,u0_hp,u3_hp,u4_hp,u5_hp,loc,btl_id,area" $sd)
  Set-Content -Path (Join-Path $Dir "manifest.txt") -Encoding utf8 `
     -Value ($hdr + $m + @("cam_pick: $cam", "frames: frames\frame_NNNNNN.png = run frame N (every 4); states\state_NNNNNN.sav = run frame N (every 5); run frame 0 = the base state of the stage-2 run (see base:)", "", "watch (tools\py\ccx_state.py watch states):") + $w)
}
function Prune-Capture([string]$Ref, [string]$Elem) {
  if ($NoPrune) { return $true }
  $sd = Join-Path $Ref "states"
  $win = (Py (Join-Path $Py "ccx_state.py") window $sd $Elem) -replace "[()' ]", "" -split ","
  Remove-Item (Join-Path $Ref "hook-events.log"), (Join-Path $Ref "hook-report.csv") -ErrorAction SilentlyContinue
  if ($win[0] -eq "None") { Log "WARNING: no effect window found in $sd (kept everything)"; return $false }
  $lo = [Math]::Max(0, [int]$win[0] - 20); $hi = [int]$win[1] + 20
  Py (Join-Path $Py "ccx_state.py") prune $sd $lo $hi | Out-Null
  return $true
}

# ---- forge + boot (the ccx_capture.ps1 'prep' pattern) -----------------------------------------------------------------
$Work = Join-Path $Out "_work\$Tag"
$States = Join-Path $Work "states"
$SaveDir = Join-Path $Work "save"
$FillPool = @(1, 29, 36, 12, 19, 32, 2, 43, 55, 73, 88, 40, 25)

function Row-List() {
  $n = [int](Py (Join-Path $Py "cc_save_forge.py") rowlen $Char 0)
  $row = @($IdList)
  foreach ($f in $FillPool) { if ($row.Count -ge $n) { break }; if ($IdList -notcontains $f) { $row += $f } }
  return ,$row
}
function Ensure-Base() {
  if ((Test-Path (Join-Path $States "bg00_menu.sav")) -and -not $Rebuild) { return }
  New-Item -ItemType Directory -Force $SaveDir, $States | Out-Null
  $row = Row-List
  Py (Join-Path $Py "cc_save_forge.py") (Join-Path $Main $OwnerSaveCopy) (Join-Path $SaveDir "save.dat") --slot 2 `
      --room 53 --entrance 1 --party $(if ($PartyIds) { $PartyIds } else { $Char }) @($ForgeArgs -split "[|]" | Where-Object { $_ }) --char "${Char}:hp=1023,atk=1,def=127,hit=127,evd=0,mag=1,mdef=127" `
      --grid "${Char}:0:$($row -join ',')"
  # Cold boot -> title (Cross) -> Down (Continue) -> Circle -> Circle (card 1) -> Right x2 = slot 2 -> Circle -> room 53 entrance 1 ->
  # the wandering monster touches the leader -> A3 #1,#0 -> bg_00 battle menu.
  $boot = Join-Path $Work "boot"
  Invoke-Ccx $boot (@("-frames", "2600", "-stateevery", "10", "1700", "2600", "-mash", "Cross", "40", "60", "700") +
                    (Press "Down" 800) + (Press "Circle" 860) + (Press "Circle" 1200) + @("-mash", "Right", "30", "1500", "1560") +
                    (Press "Circle" 1600)) $SaveDir
  $menu = Pick (Join-Path $boot "states") "r['ovl_id'] == 8 and r['tgt_064b'] == 1 and r['btl_frame'] >= 150"
  if (-not $menu) { throw "boot run did not reach the battle menu (see $boot\states)" }
  Copy-Item $menu (Join-Path $States "bg00_menu.sav") -Force
  if ($MenuPoke) {   # -MenuPoke "ADDR=VAL[:W];..." into the first-menu state (a KO'd ally: 0x800E0B60=0x80:4;0x800E0B68=0:2)
    $mp = Join-Path $Work "menu_poked.sav"
    Poke-State (Join-Path $States "bg00_menu.sav") $mp @($MenuPoke -split ";" | Where-Object { $_ }) (Join-Path $Work "pm") $SaveDir
    Copy-Item $mp (Join-Path $States "bg00_menu.sav") -Force
  }
  Remove-Item -Recurse -Force $boot
  Log "base state for tag ${Tag}: $menu"
}

# summon ring: slot value = 1 << (pgdat colour >> 2): green 1, white 2, black 4, red 8, yellow 0x10, blue 0x20
$SummonRing = @{ 37 = 0x20; 38 = 0x20; 39 = 0x08; 40 = 0x08; 41 = 0x01; 42 = 0x01; 43 = 0x10; 44 = 0x10; 45 = 0x02; 46 = 0x02; 47 = 0x04; 48 = 0x04 }

function Capture-Id([int]$Id) {
  $row = Row-List
  $N = $row.Count
  $idx = [Array]::IndexOf($row, $Id)
  $openFile = Join-Path $Work "open.txt"
  $open = if (Test-Path $openFile) { [int](Get-Content $openFile) } else { 3 }
  $base = Join-Path $States "bg00_menu.sav"
  if ($Mode -eq "summon") {
    if (-not $SummonRing.ContainsKey($Id)) { throw "no ring colour known for summon $Id" }
    $base = Join-Path $States "menu_ring_$Id.sav"
    $rg = "0x{0:X}:4" -f $SummonRing[$Id]
    Poke-State (Join-Path $States "bg00_menu.sav") $base @("0x800DFF98=$rg", "0x800DFF9C=$rg", "0x800DFFA0=$rg") (Join-Path $Work "p$Id") $SaveDir
  }
  $s1 = Join-Path $Work "s1_$Id"
  # HIT% box rows (cursor 0x800E064E): 0 weak, 1 medium, 2 heavy, 3 Element; Down stops at 2, Up wraps 0 -> 3, and a second cursor press
  # within ~40 frames of a move is dropped. After the weak hit the "Auto HIT%" option (0x800789C0 bit 0x02) moves the cursor to the strongest
  # affordable hit, which depends on the character: the default presses (Down at 300 + 340) reach Element for Serge, while Marcy (char 9) sits on
  # row 1 and stops on heavy (e05da 0). $upMode = the retry: Auto HIT% cleared in a poked copy of the base, so the cursor stays on row 0, and
  # one Up wraps it to Element.
  $upMode = $false
  $pre = ""
  for ($try = 0; $try -lt 3; $try++) {
    $pre = Join-Path $s1 "states\state_000620.sav"
    $downs = (($idx - $open) % $N + $N) % $N
    # Attack -> target -> weak hit (gauge -1 -> 0) -> Element row of the HIT% box -> Circle (element menu) -> Down x k -> Circle (target) -> Circle (cast)
    $from = $base
    $toElement = @("-mash", "Down", "40", "300", "350")
    if ($upMode) {
      $from = Join-Path $Work "menu_manual_hit_$Id.sav"
      $opt = [int](& python -c "import sys; sys.path.insert(0, r'$Py'); import ccx_state as c; print(c.State(r'$base').u8(0x800789C0))")
      Poke-State $base $from @(("0x800789C0=0x{0:X}:1" -f ($opt -band 0xFD))) (Join-Path $Work "pu$Id") $SaveDir
      $toElement = (Press "Up" 300)
    }
    $a = @("-mashhold", "4", "-state", $from, "-frames", "700", "-stateevery", "10", "600", "700") +
         (Press "Circle" 40) + (Press "Circle" 80) + (Press "Circle" 200) + $toElement + (Press "Circle" 360)
    if ($downs -gt 0) { $a += @("-mash", "Down", "20", "400", "$(400 + 20 * $downs)") }
    $a += (Press "Circle" 540) + (Press "Circle" 590)
    # -Keys "Right:565,Circle:590" extra presses after the cast confirm (Dopplegang 238 opens the creature list: entry 0 is the own form and refused; Right = entry 1)
    foreach ($k in @($Keys -split "," | Where-Object { $_ })) { $kb = $k -split ":"; $a += (Press $kb[0] ([int]$kb[1])) }
    Invoke-Ccx $s1 $a $SaveDir
    $chk = [int](Field $pre "e05da")
    if ($chk -eq $Id) { break }
    $j = [Array]::IndexOf($row, $chk)
    if ($chk -eq 0 -and -not $upMode) {
      $upMode = $true
      Log "id ${Id}: stage 1 never opened the element menu (e05da 0), retrying with Auto HIT% off and Up to the Element row"
    } elseif ($j -ge 0 -and $try -lt 2) {
      $open = (($j - $downs) % $N + $N) % $N
      Set-Content $openFile $open
      Log "id ${Id}: stage 1 selected $chk (row index $j) with $downs Downs -> opening index $open, retrying"
    } else { throw "stage 1 of $Id selected element $chk (e05da), wanted $Id" }
  }
  if ($Mode -eq "tech" -and [int](Field $pre "party0") -ne $Char) { throw "party0 is not character $Char" }
  if ($PrePoke) {   # -PrePoke "ADDR=VAL[:W];..." into the stage-1 end state (cast confirmed, not yet resolved) that the capture starts from: 0x800DF678=1:1 = Big Catch's line breaks
    $pp = Join-Path $Work "pre_poked_$Id.sav"
    Poke-State $pre $pp @($PrePoke -split ";" | Where-Object { $_ }) (Join-Path $Work "pp$Id") $SaveDir
    $pre = $pp
  }
  $ref = Join-Path $Out "$Id\ref"
  New-Item -ItemType Directory -Force (Split-Path $ref) | Out-Null
  $fr = if ($Frames -gt 0) { $Frames } elseif ($Mode -eq "summon") { 900 } else { 600 }
  Invoke-Ccx $ref @("-state", $pre, "-frames", "$fr", "-dumpevery", "4", "-stateevery", "5", "0", "$fr") $SaveDir
  $ok = Prune-Capture $ref "$Id"
  # the tech / element damage is applied at the action start (ccx 190: u3 HP 9 -> 4 at run frame ~35, before the pruned window), so the first
  # watch row of a pruned capture shows the HP AFTER the cast; ours_cast34 passed that row as --enemyhp and killed an already-hit target ("0 damage" of 190).
  # base_hp = the HP of the stage-2 start state; its row comes first in the manifest, so ours_cast34's first-row regex reads it.
  $bh = (Py (Join-Path $Py "ccx_state.py") watch --fields "u3_hp,u4_hp,u5_hp" $pre) | Select-Object -First 1
  Write-Manifest $ref "$Id" @("row 0 of character ${Char}: $($row -join ',') (index $idx, $downs Downs from opening index $open)",
      "base_hp: $bh", "base: $pre (stage 1 from $base)", "stage-1 e05da: $chk", "window found: $ok")
  Remove-Item -Recurse -Force $s1
  Log "captured $Id -> $ref"
}

if ($Mode -eq "attack") {
  Ensure-Base
  $ks = if ($Keys) { $Keys } else { "Circle:40,Circle:80,Circle:200,Circle:300,Cross:420" }
  $fr = if ($Frames -gt 0) { $Frames } else { 560 }
  $a = @("-mashhold", "4", "-state", (Join-Path $States "bg00_menu.sav"), "-frames", "$fr", "-dumpevery", "4", "-stateevery", "4", "0", "$fr")
  foreach ($k in @($ks -split "," | Where-Object { $_ })) { $kb = $k -split ":"; $a += (Press $kb[0] ([int]$kb[1])) }
  $ref = Join-Path $Out "attack_$Tag\ref"
  New-Item -ItemType Directory -Force (Split-Path $ref) | Out-Null
  Invoke-Ccx $ref $a $SaveDir
  Remove-Item (Join-Path $ref "hook-events.log"), (Join-Path $ref "hook-report.csv") -ErrorAction SilentlyContinue
  Log "captured attack ($ks) -> $ref"
  exit 0
}

if ($Mode -in @("batch", "summon", "tech")) {
  Ensure-Base
  $todo = if ($Only -ge 0) { @($Only) } else { $IdList }
  if ($Jobs -le 1 -or $todo.Count -le 1) {
    foreach ($id in $todo) { Capture-Id $id }
  } else {
    $running = @()
    foreach ($id in $todo) {
      while (@($running | Where-Object { -not $_.HasExited }).Count -ge $Jobs) { Start-Sleep -Seconds 2 }
      # Every parameter of this run is passed on (quoted: the checkout path has spaces), -Jobs / -Only replaced.
      $argl = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ('"' + $PSCommandPath + '"'), $Mode, "-Only", $id)
      foreach ($kv in $PSBoundParameters.GetEnumerator()) {
        if ($kv.Key -in @("Mode", "Jobs", "Only")) { continue }
        if ($kv.Value -is [System.Management.Automation.SwitchParameter]) { if ($kv.Value.IsPresent) { $argl += "-$($kv.Key)" }; continue }
        $argl += @("-$($kv.Key)", ('"' + [string]$kv.Value + '"'))
      }
      $job = Start-Process powershell -ArgumentList $argl -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $Work "job_$id.log")
      $null = $job.Handle   # keeps ExitCode readable after the exit
      $running += $job
    }
    $running | ForEach-Object { $_.WaitForExit() }
    foreach ($id in $todo) { Get-Content (Join-Path $Work "job_$id.log") | Write-Host }
    $failed = @($running | Where-Object { $_.ExitCode -ne 0 }).Count
    if ($failed -gt 0) { Log "$failed of $($todo.Count) jobs failed"; exit 1 }
  }
  exit 0
}

# ---- enemy mode --------------------------------------------------------------------------------------------------------
if ($Scene -lt 0) { throw "-Scene is required" }
$Work = Join-Path $Out "_work\scene_$Scene"
$SaveSrc = Join-Path $Main $EncSave
$SceneDir = Join-Path $Out "scene_$Scene"
New-Item -ItemType Directory -Force $Work, $SceneDir | Out-Null
$poked = Join-Path $Work "enc_poked.sav"
$pk = @("0x80075906={0}:2" -f (0x200 -bor $Scene))
if ($Area -ge 0) { $pk += "0x800790FC=${Area}:2" }
Poke-State (Join-Path $Main $EncState) $poked $pk $Work $SaveSrc
if ($Disc2) {   # the disc-2 bosses (ccx_registry.py --disc2): cdrom2.fnd at 0x80780000, u8 0x800720FE = 2, script var 0x42 = 1
  $c2 = "import sys,zipfile; sys.path.insert(0, r'$Py'); import ccx_state as cs; p = r'$poked'; d = bytearray(open(p, 'rb').read()); ro = cs.State(p).ram_off; f = zipfile.ZipFile(r'$(Join-Path (Split-Path -Parent $Main) 'data\cdrom.dat')').read('cdrom2.fnd'); d[ro + 0x780000:ro + 0x780000 + len(f)] = f; d[ro + 0x720FE] = 2; d[ro + 0xEBAEA] = 1; open(p, 'wb').write(d)"
  & python -c $c2
}
if ($Poke -or $VarPoke) {
  # -Poke "ADDR=VAL[:W];..." is applied once the battle runs (btlinit would overwrite it in the field state): boot the poked encounter to the
  # first command menu, poke that state, and pass 1 starts from it (e.g. the Portalgheist door hp: 0x800E1BA8=70:2).
  $b0 = Join-Path $Work "toMenu"
  $mf = $MenuFrames
  $ba = @("-state", $poked, "-set", "Main/SaveStateCompression=Uncompressed", "-frames", "$mf", "-stateevery", "10", "100", "$mf")
  if ($MenuMash) { $ba += @("-mashhold", "4", "-mash", $Mash, "30", "10", "$mf") }   # scripted-dialogue scenes need presses before the first menu
  Invoke-Ccx $b0 $ba $SaveSrc
  $mcond = if ($MenuAny) { "r['ovl_id'] == 8 and r['btl_frame'] >= 100 and r['btl_frame'] < 100000" } else { "r['ovl_id'] == 8 and r['tgt_064b'] == 1 and r['btl_frame'] >= 100" }   # -MenuAny = the first running-battle state (scenes whose first menu comes after a long scripted intro)
  $mn = Pick (Join-Path $b0 "states") $mcond
  if (-not $mn) { throw "no battle menu reached from the poked encounter ($b0\states)" }
  $poked2 = Join-Path $Work "menu_poked.sav"
  $pl = @($Poke -split ";" | Where-Object { $_ })
  if ($VarPoke) {   # -VarPoke "0x448=1;0x80=3" = battle-script variables (u16 at [0x800E2FC8] + 0x84 + (id & 0xFFFE)), resolved from the menu state
    $vb = [uint32](& python -c "import sys; sys.path.insert(0, r'$Py'); import ccx_state as c; print(c.State(r'$mn').u32(0x800E2FC8))")
    foreach ($vp in ($VarPoke -split ";" | Where-Object { $_ })) { $kv = $vp -split "="; $pl += ("0x{0:X}={1}:2" -f ($vb + 0x84 + ([Convert]::ToInt32($kv[0], 16) -band 0xFFFE)), $kv[1]) }
  }
  Py (Join-Path $Py "ccx_state.py") poke $mn $poked2 $pl | Out-Null
  Remove-Item -Recurse -Force $b0
  Remove-Item -Force $poked
  $poked = $poked2
}
# NB: PowerShell variables are case-insensitive: the old $P1 (frames) was overwritten by $p1 (pass-1 dir) -> "-frames <path>" and
# "-stateevery 30 0 <path>" did not parse in ccx (frames unlimited, state end = UINT32_MAX): that was the 9.5 GB / 3771-state flood.
$P1Frames = if ($Frames -gt 0) { $Frames } else { 3000 }
$pass1 = Join-Path $Work "pass1"
function Guard-Disk([string]$Dir, [int]$CapMB) {
  $mb = [int]((Get-ChildItem $Dir -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum / 1MB)
  if ($mb -gt $CapMB) { throw "disk cap: $Dir is $mb MB (> $CapMB MB)" }
}
# mash period 30 divides the state interval (30), so a restart from a pass-1 state replays the same input phase
Log "pass 1: $P1Frames frames, compressed state every 30, no frames, mash $Mash"
$p1a = @("-mashhold", "4", "-state", $poked, "-frames", "$P1Frames", "-stateevery", "$P1Every", "0", "$P1Frames")
$p1a += $(if ($ElemHit) { ElemHit-Args 0 $P1Frames } else { @("-mash", $Mash, "30", "10", "$P1Frames") })
Invoke-Ccx $pass1 $p1a $SaveSrc
Remove-Item -Force $poked -ErrorAction SilentlyContinue   # the uncompressed poke copy is not needed once pass 1 has run
Guard-Disk $Work 1000
$tsv = Join-Path $SceneDir "actions.tsv"
Py (Join-Path $Py "ccx_state.py") harvest (Join-Path $pass1 "states") $tsv
$rows = @(Get-Content $tsv | Select-Object -Skip 1 | Where-Object { $_ } | ForEach-Object {
  $c = $_ -split "`t"; [pscustomobject]@{ Start = [int]$c[0]; End = [int]$c[1]; Unit = [int]$c[2]; Elem = [int]$c[3]; Target = [int]$c[4] } })
Log "harvested $($rows.Count) action runs -> $tsv"
# pass pairs: first run of each (unit, element); the pass-1 state to restart from is the 30-multiple at or before Start - Lead
$seen = @{}; $jobs2 = @()
foreach ($r in $rows) {
  $key = "$($r.Unit)/$($r.Elem)"
  if ($ElemHit -and $r.Unit -lt 3) { continue }   # the party's own element hit is not a capture target
  if ($SkipExisting -and (Test-Path (Join-Path $Out "$($r.Elem)_s${Scene}_u$($r.Unit)\ref\manifest.txt"))) { continue }   # never overwrite a folder an earlier run captured
  if ($Pick -and ($Pick -split ",") -notcontains $key) { continue }   # -Pick "3/447,4/12" = only these (unit/element) pairs
  if ($seen.ContainsKey($key) -or $jobs2.Count -ge $MaxCaps) { continue }
  $seen[$key] = $true
  $f0 = [Math]::Max(0, [int]([Math]::Floor(($r.Start - $Lead) / $P1Every)) * $P1Every)   # the pass-1 states sit every $P1Every frames (was a fixed 30: -P1Every 90 restarted from a state that did not exist)
  $jobs2 += [pscustomobject]@{ R = $r; F0 = $f0 }
}
# keep only the pass-1 states pass 2 restarts from
$need = @($jobs2 | ForEach-Object { "state_{0:D6}.sav" -f $_.F0 })
if (-not $KeepPass1) { Get-ChildItem (Join-Path $pass1 "states") -Filter *.sav | Where-Object { $need -notcontains $_.Name } | Remove-Item -Force }
$caps = 0; $report = @()
foreach ($j in $jobs2) {
  $r = $j.R; $f0 = $j.F0
  # enemy captures always carry the scene / unit suffix: the bare id folder is the PC-cast capture
  $name = "$($r.Elem)_s${Scene}_u$($r.Unit)"
  $from = Join-Path $pass1 ("states\state_{0:D6}.sav" -f $f0)
  $ref = Join-Path $Out "$name\ref"
  New-Item -ItemType Directory -Force (Split-Path $ref) | Out-Null
  Log "pass 2: element $($r.Elem) unit $($r.Unit) from pass-1 frame $f0 -> $ref"
  $p2a = @("-mashhold", "4", "-state", $from, "-frames", "$Window", "-dumpevery", "$P2Dump", "-stateevery", "$P2Every", "$P2Lo", "$(if ($P2Hi -gt 0) { $P2Hi } else { $Window })")
  $p2a += $(if ($ElemHit) { ElemHit-Args $f0 $Window } else { @("-mash", $Mash, "30", "10", "$Window") })
  Invoke-Ccx $ref $p2a $SaveSrc
  $rel = $r.Start - $f0
  $chk = Join-Path $ref ("states\state_{0:D6}.sav" -f $rel)
  $e = if (Test-Path $chk) { [int](Field $chk "e05da") } else { -1 }
  $u = if (Test-Path $chk) { [int](Field $chk "act_actor") } else { -1 }
  $repro = ($e -eq $r.Elem -and $u -eq $r.Unit)
  $null = Prune-Capture $ref "$($r.Elem)"
  Write-Manifest $ref "$($r.Elem)" @("enemy_unit: $($r.Unit) scene $Scene", "target_unit: $($r.Target)",
      "base: pass-1 state frame $f0 (scene poke 0x80075906 = 0x$('{0:X}' -f (0x200 -bor $Scene)), area $Area, extra pokes: $Poke)",
      "harvest: pass-1 start $($r.Start) end $($r.End); reproduction at run frame ${rel}: e05da=$e act_actor=$u -> $(if ($repro) { 'OK' } else { 'MISMATCH' })")
  $report += "$name`telem $($r.Elem) unit $($r.Unit) target $($r.Target) repro $(if ($repro) { 'OK' } else { 'MISMATCH' })"
  $caps++
}
Set-Content (Join-Path $SceneDir "captures.txt") -Value $report -Encoding utf8
if (-not $KeepPass1) { Remove-Item -Recurse -Force $Work }
Log "done: $caps captures"
