# ccx reference-capture driver. Run from PowerShell in the OpenCross root:
#
#   tools\ccx_capture.ps1 prep                 forge tmp\ref\save\save.dat (slot 2) and boot ccx into the bg_00 battle;
#                                              writes the base states tmp\ref\states\{field53_encounter,bg00_prestart,bg00_menu}.sav
#   tools\ccx_capture.ps1 <scene>              capture one scene into tmp\ref\<scene>\ (frames\, states\, manifest.txt)
#   tools\ccx_capture.ps1 all                  every scene in $Scenes
#
# Scenes: aqua_beam comet black_hole volcano uplift gravitonne aqua_ball (one PC element cast each, bg_00), serge_attack
# (weak Sea Swallow hit), bg00_zen0 (first 320 frames of the bg_00 battle), bg21_cam4 (bg_21 intro: the field encounter
# state with the battlefield word 0x800790FC poked to 21), golem / frogprince (summons).
#
# Mechanism: ccx -state <base> -mash <Button> 12 <frame> <frame+12> ... (one press per phase, -mashhold 4 frames) drives the
# PS1 battle menus by frame number; -stateevery writes DuckStation states (PS1 RAM + VRAM, read by tools\py\ccx_state.py),
# -dumpevery 1 writes the display as PNG. Every step is verified from RAM (ccx_state.py watch), never from the PNGs.
param(
  [Parameter(Mandatory = $true)][string]$Scene,
  [string]$CcxExe = "..\rde-reimpl\bin\ccx\ccx.exe",
  [string]$OwnerSaveCopy = "tmp\save.dat",
  # -Tag g2: a second forged save (grid row 0 = $Row0) with its own base states; scenes come from $TagScenes
  [string]$Tag = "",
  [string]$Row0 = "7,4,21,55,73,88,40,25"
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Ccx = (Resolve-Path (Join-Path $Root $CcxExe)).Path
$Ref = Join-Path $Root "tmp\ref"
$States = Join-Path $Ref "states"
$Py = Join-Path $Root "tools\py"
New-Item -ItemType Directory -Force $States | Out-Null

# Element menu: the level-1 list of Serge's row 0 (cells 1,29,36,12,19,32,2,43) opens on the 4th cell (column 5 of 10) and
# Down walks the row with wrap-around, so the Down count per element is fixed (verified by e05da / the pgdat pointer).
$Scenes = [ordered]@{
  volcano    = @{ elem = 12; downs = 0 }
  uplift     = @{ elem = 19; downs = 1 }
  gravitonne = @{ elem = 32; downs = 2 }
  aqua_ball  = @{ elem = 2;  downs = 3 }
  aqua_beam  = @{ elem = 1;  downs = 5 }
  comet      = @{ elem = 29; downs = 6 }   # Meteor Shower (JP Comet), pack white/37comet.prd
  black_hole = @{ elem = 36; downs = 7 }
  serge_attack = @{}
  bg00_zen0  = @{}
  bg21_cam4  = @{}
  golem      = @{ summon = 43; ring = 0x10; base = "bg00_menu" }      # yellow ring; Golem = Serge row 0 column 9 (Down x4)
  frogprince = @{ summon = 37; ring = 0x20; base = "bg00fp_menu" }    # blue ring; needs the save_fp forge (37 in column 9)
}

$SaveName = if ($Tag) { "save_$Tag" } else { "save" }
$Sfx = if ($Tag) { "_$Tag" } else { "" }
# Row 0 order from the menu's 4th entry (the list opens there and Down walks with wrap): 55 73 88 40 25 7 4 21
$TagScenes = [ordered]@{
  turn_red   = @{ elem = 55; downs = 0 }
  turn_white = @{ elem = 73; downs = 1 }
  heal       = @{ elem = 88; downs = 2 }
  salamander = @{ summon = 40; ring = 0x08; base = "bg00_menu$Sfx"; downs = 3 }
  photon_ray = @{ elem = 25; downs = 4 }
  fireball   = @{ elem = 7;  downs = 5 }
  ice_lance  = @{ elem = 4;  downs = 6 }
  earthquake = @{ elem = 21; downs = 7 }
  needle_y   = @{ elem = 510; downs = 5 }   # -Tag t3 -Row0 "510,332,1,29,36,12,19,32" (enemy tech cast by Serge)
  needle_b   = @{ elem = 332; downs = 6 }
  magma      = @{ elem = 10; downs = 0 }   # -Tag y3 -Row0 "1,29,36,10,19,32,2,43" (Magma Bomb, fog / facing after the cast)
}
if ($Tag) { $Scenes = $TagScenes }

function Invoke-Ccx([string]$LogDir, [string[]]$ExtraArgs, [string]$SaveDir = $SaveName) {
  if (Test-Path $LogDir) { Remove-Item -Recurse -Force $LogDir }
  # GameDir explicitly: an empty [CCX] GameDir means the folder ccx.exe is in (rde-reimpl\bin\ccx, which has no data\cdrom.dat)
  # and ccx stops on a modal "Can't start Chrono Cross" box - with -WindowStyle Minimized that looks like a hang.
  $args = @("-uncapped", "-set", "GPU/ResolutionScale=2", "-set", "CCX/Movies=false",
            "-set", ("CCX/GameDir=" + (Split-Path -Parent $Root)),
            "-set", ("CCX/SaveDir=" + (Join-Path $Ref $SaveDir)),
            "-set", "InputSources/XInput=false", "-set", "InputSources/SDL=false", "-set", "Main/StartFullscreen=false",
            "-logdir", $LogDir) + $ExtraArgs
  $p = Start-Process -FilePath $Ccx -ArgumentList ($args | ForEach-Object { if ($_ -match '[ =\\]') { '"' + $_ + '"' } else { $_ } }) `
       -WorkingDirectory (Split-Path $Ccx) -PassThru -Wait -WindowStyle Minimized
  if ($p.ExitCode -ne 0) { throw "ccx exited with $($p.ExitCode)" }
}
function Press([string]$Button, [int]$Frame) { return @("-mash", $Button, "12", "$Frame", "$($Frame + 12)") }
function Watch([string]$Dir, [string]$Fields) { & python (Join-Path $Py "ccx_state.py") watch --fields $Fields $Dir }
function Pick([string]$Dir, [string]$Fields, [string]$Expr) {
  # first state in $Dir whose watch row satisfies the python expression (names from $Fields)
  $code = "import sys,os; sys.path.insert(0, r'$Py'); import ccx_state as c`n" +
          "for p in c.list_states([r'$Dir']):`n    r = c.State(p).watch()`n    if ($Expr): print(p); break"
  return (& python -c $code)
}

# ---- prep: forge + boot ------------------------------------------------------------------------------------------------
if ($Scene -eq "prep") {
  New-Item -ItemType Directory -Force (Join-Path $Ref $SaveName) | Out-Null
  & python (Join-Path $Py "cc_save_forge.py") (Join-Path $Root $OwnerSaveCopy) (Join-Path $Ref "$SaveName\save.dat") --slot 2 `
      --room 53 --entrance 1 --party 0 --char "0:hp=1023,atk=1,def=127,hit=127,evd=0,mag=1,mdef=127" `
      --grid "0:0:$(if ($Tag) { $Row0 } else { "1,29,36,12,19,32,2,43" })" --grid "0:1:37,43,19,1"
  # Cold boot -> title (Cross passes the press-start wait without choosing) -> Down (Continue) -> Circle -> Circle (card 1)
  # -> slot grid: Right x2 = slot 2 -> Circle (load) -> room 53 entrance 1 -> the wandering monster touches Serge -> A3 #1,#0.
  $boot = Join-Path $Ref "boot$Sfx"
  Invoke-Ccx $boot (@("-frames", "2600", "-stateevery", "10", "1700", "2600",
                      "-mash", "Cross", "40", "60", "700") + (Press "Down" 800) + (Press "Circle" 860) + (Press "Circle" 1200) +
                      @("-mash", "Right", "30", "1500", "1560") + (Press "Circle" 1600))
  $enc = Pick (Join-Path $boot "states") "" "r['loc'] == 53 and r['btl_id'] == 1 and r['ovl_id'] == 6"
  $pre = Pick (Join-Path $boot "states") "" "r['ovl_id'] == 8 and r['btl_frame'] == 0 and r['u0_hp'] == 1023 and r['u0_sta'] == 70"
  $menu = Pick (Join-Path $boot "states") "" "r['ovl_id'] == 8 and r['tgt_064b'] == 1 and r['btl_frame'] >= 150"
  if (-not ($enc -and $pre -and $menu)) { throw "boot run did not reach the battle menu (see $boot\states)" }
  Copy-Item $enc (Join-Path $States "field53_encounter$Sfx.sav") -Force
  Copy-Item $pre (Join-Path $States "bg00_prestart$Sfx.sav") -Force
  Copy-Item $menu (Join-Path $States "bg00_menu$Sfx.sav") -Force
  Write-Output "base states: $enc / $pre / $menu"
  exit 0
}

function Write-Manifest([string]$Dir, [string[]]$Lines) {
  $lines = @("scene: $Scene", "date: $(Get-Date -Format s)", "ccx: $Ccx") + $Lines +
           @("", "watch (tools\py\ccx_state.py watch states):") +
           (Watch (Join-Path $Dir "states") "btl_frame,tgt_064b,act_kind,e05da,e3372,elem,fx_load,ovl,ovl0,u0_sta,u3_hp,u4_hp,u5_hp,loc,btl_id,area")
  Set-Content -Path (Join-Path $Dir "manifest.txt") -Value $lines -Encoding utf8
}

function Capture-Element([string]$Name, [int]$Elem, [int]$Downs) {
  # Stage 1 (from bg00_menu.sav): Attack -> target -> weak hit (gauge -1 -> 0) -> Down x2 = "Element" row of the HIT% box ->
  # Circle (element menu) -> Down x N -> Circle (target) -> Circle (cast). The cast pose starts ~45 frames after the last press.
  $s1 = Join-Path $Ref "s1_$Name"
  $args = @("-mashhold", "4", "-state", (Join-Path $States "bg00_menu$Sfx.sav"), "-frames", "700", "-stateevery", "10", "600", "700") +
          (Press "Circle" 40) + (Press "Circle" 80) + (Press "Circle" 200) + @("-mash", "Down", "20", "300", "340") + (Press "Circle" 360)
  if ($Downs -gt 0) { $args += @("-mash", "Down", "20", "400", "$(400 + 20 * $Downs)") }
  $args += (Press "Circle" 540) + (Press "Circle" 590)
  Invoke-Ccx $s1 $args
  $pre = Join-Path $s1 "states\state_000620.sav"
  $chk = & python -c "import sys; sys.path.insert(0, r'$Py'); import ccx_state as c; r = c.State(r'$pre').watch(); print(r['e05da'])"
  if ([int]$chk -ne $Elem) { throw "stage 1 of $Name selected element $chk, wanted $Elem" }
  # Stage 2: 360 frames from the pre-cast state, every frame as PNG, a state every 5 frames.
  $out = Join-Path $Ref $Name
  Invoke-Ccx $out @("-state", $pre, "-frames", "360", "-dumpevery", "1", "-stateevery", "5", "0", "360")
  Write-Manifest $out @("element: $Elem (pgdat), Serge row 0, Down x$Downs in the level-1 list", "base: $pre (bg00_menu.sav + stage 1)",
    "encounter: opa_b02f (room 53) entrance 1, A3 #1 field 0 -> scene 7 (Sand Squirt x2 + Komodo Pup), stage bg_00",
    "frames: frames\frame_NNNNNN.png = run frame N (640x432); the cast pose starts at run frame ~20 (tgt_064b=5), act_kind=1 and the pack load state 0x10002 at run frame 30, the effect overlay word at 0x801C5E00 from frame 35 (battle tick = run frame + the btl_frame of row 0 in the watch table)",
    "states: states\state_NNNNNN.sav every 5 frames (RAM/VRAM: ccx_state.py)")
}

switch ($Scene) {
  "all" { foreach ($k in $Scenes.Keys) { & $PSCommandPath -Scene $k -CcxExe $CcxExe -OwnerSaveCopy $OwnerSaveCopy -Tag $Tag -Row0 $Row0 }; exit 0 }
  { $Scenes.Contains($_) -and $Scenes[$_].Contains("elem") } {
    Capture-Element $Scene $Scenes[$Scene].elem $Scenes[$Scene].downs
  }
  { $Scenes.Contains($_) -and $Scenes[$_].Contains("summon") } {
    # Summons are offered by the element menu only when all three ring slots (u32 0x800DFF98/9C/A0) carry the
    # summon's colour. Instead of three same-colour casts the ring is poked in an uncompressed copy of the menu state (= the
    # state after three such casts as far as the ring goes). frogprince needs its own forge/boot (tmp\ref\save_fp: Frog
    # Prince instead of Golem in column 9: a grid cell poked after battle start is refused, the cell flags are cached).
    $sc = $Scenes[$Scene]
    if ($Scene -eq "frogprince" -and -not (Test-Path (Join-Path $States "bg00fp_menu.sav"))) {
      New-Item -ItemType Directory -Force (Join-Path $Ref "save_fp") | Out-Null
      & python (Join-Path $Py "cc_save_forge.py") (Join-Path $Root $OwnerSaveCopy) (Join-Path $Ref "save_fp\save.dat") --slot 2 `
          --room 53 --entrance 1 --party 0 --char "0:hp=1023,atk=1,def=127,hit=127,evd=0,mag=1,mdef=127" `
          --grid "0:0:1,29,36,12,19,32,2,37" --grid "0:1:43,19,1"
      $boot = Join-Path $Ref "boot_fp"
      Invoke-Ccx $boot (@("-frames", "2500", "-stateevery", "10", "2000", "2500", "-mash", "Cross", "40", "60", "700") +
                        (Press "Down" 800) + (Press "Circle" 860) + (Press "Circle" 1200) + @("-mash", "Right", "30", "1500", "1560") + (Press "Circle" 1600)) "save_fp"
      $menu = Pick (Join-Path $boot "states") "" "r['ovl_id'] == 8 and r['tgt_064b'] == 1 and r['btl_frame'] >= 150"
      if (-not $menu) { throw "frogprince boot did not reach the battle menu" }
      Copy-Item $menu (Join-Path $States "bg00fp_menu.sav") -Force
    }
    $unc = Join-Path $Ref "unc_$Scene"
    Invoke-Ccx $unc @("-state", (Join-Path $States ($sc.base + ".sav")), "-set", "Main/SaveStateCompression=Uncompressed", "-frames", "3", "-stateevery", "1", "1", "2")
    $poked = Join-Path $States ($sc.base + "_" + $Scene + ".sav")
    $ring = "0x{0:X}:4" -f $sc.ring
    & python (Join-Path $Py "ccx_state.py") poke (Join-Path $unc "states\state_000001.sav") $poked "0x800DFF98=$ring" "0x800DFF9C=$ring" "0x800DFFA0=$ring"
    $s1 = Join-Path $Ref "s1_$Scene"
    $sd = if ($sc.Contains("downs")) { $sc.downs } else { 4 }
    Invoke-Ccx $s1 (@("-mashhold", "4", "-state", $poked, "-frames", "700", "-stateevery", "10", "600", "700") +
                    (Press "Circle" 40) + (Press "Circle" 80) + (Press "Circle" 200) + @("-mash", "Down", "20", "300", "340") + (Press "Circle" 360) +
                    @("-mash", "Down", "20", "400", "$(400 + 20 * $sd)") + (Press "Circle" 540) + (Press "Circle" 590))
    $pre = Join-Path $s1 "states\state_000620.sav"
    $chk = & python -c "import sys; sys.path.insert(0, r'$Py'); import ccx_state as c; r = c.State(r'$pre').watch(); print(r['e05da'])"
    if ([int]$chk -ne $sc.summon) { throw "stage 1 of $Scene selected element $chk, wanted $($sc.summon) (ring not accepted?)" }
    $out = Join-Path $Ref $Scene
    Invoke-Ccx $out @("-state", $pre, "-frames", "360", "-dumpevery", "1", "-stateevery", "5", "0", "360")
    Write-Manifest $out @("summon: element $($sc.summon), ring poked to 0x$('{0:X}' -f $sc.ring) x3 in $poked", "base: $pre")
  }
  "serge_attack" {
    $s1 = Join-Path $Ref "s1_serge_attack"
    Invoke-Ccx $s1 (@("-mashhold", "4", "-state", (Join-Path $States "bg00_menu.sav"), "-frames", "230", "-stateevery", "10", "100", "230") +
                    (Press "Circle" 40) + (Press "Circle" 80))
    $pre = Join-Path $s1 "states\state_000150.sav"   # HIT% box up, Serge standing at the target
    $out = Join-Path $Ref $Scene
    Invoke-Ccx $out (@("-mashhold", "4", "-state", $pre, "-frames", "200", "-dumpevery", "1", "-stateevery", "2", "30", "130") + (Press "Circle" 40))
    Write-Manifest $out @("action: weak attack (row 1 of the HIT% box) with the Sea Swallow; Circle at run frame 40, the hit lands at ~50 (u3_hp 13 -> 11, u0_gauge 0)",
      "base: $pre", "encounter: as the element scenes (bg_00)")
  }
  "bg00_zen0" {
    $out = Join-Path $Ref $Scene
    Invoke-Ccx $out @("-state", (Join-Path $States "bg00_prestart.sav"), "-frames", "320", "-dumpevery", "1", "-stateevery", "4", "0", "320")
    Write-Manifest $out @("base: bg00_prestart.sav (battle.prg loaded, tick counter 0x800E0518 = 0); battle tick = run frame - 10")
  }
  "bg21_cam4" {
    # The field encounter state (A3 fired, enct playing) with the battlefield word 0x800790FC poked from 0 to 21 (= `A3 #1, #21`):
    # btlinit then loads bg_21 and seeds the ring from btbgdat row 21. Needs an uncompressed state for the poke.
    $unc = Join-Path $Ref "unc_enc"
    Invoke-Ccx $unc @("-state", (Join-Path $States "field53_encounter.sav"), "-set", "Main/SaveStateCompression=Uncompressed", "-frames", "3", "-stateevery", "1", "1", "2")
    $poked = Join-Path $States "field53_enc_bg21.sav"
    & python (Join-Path $Py "ccx_state.py") poke (Join-Path $unc "states\state_000001.sav") $poked "0x800790FC=21:2"
    $run = Join-Path $Ref "run_bg21p"
    Invoke-Ccx $run @("-state", $poked, "-frames", "460", "-stateevery", "10", "100", "460")
    $pre = Pick (Join-Path $run "states") "" "r['ovl_id'] == 8 and r['area'] == 21 and r['btl_frame'] == 0 and r['u0_hp'] == 1023 and r['u0_sta'] == 70"
    if (-not $pre) { throw "bg_21 battle did not start from the poked state" }
    Copy-Item $pre (Join-Path $States "bg21_prestart.sav") -Force
    $out = Join-Path $Ref $Scene
    Invoke-Ccx $out @("-state", (Join-Path $States "bg21_prestart.sav"), "-frames", "200", "-dumpevery", "1", "-stateevery", "2", "80", "200")
    Write-Manifest $out @("base: bg21_prestart.sav (poked encounter: area 21 -> bg_21, ring = btbgdat row 21); battle tick = run frame - 10, states every 2 frames from run frame 80 (tick 70) to 200")
  }
  default { throw "unknown scene $Scene (prep, all, $($Scenes.Keys -join ', '))" }
}
