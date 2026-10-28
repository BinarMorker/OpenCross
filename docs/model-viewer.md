# Model viewer

The native model viewer helps modders test character model and texture changes
more easily. It lets you inspect field characters, battle players and battle
monsters and compare modified assets with the originals without loading a save
or navigating to a specific scene. It uses the same model importers, pose
evaluator and GPU mesh renderer as OpenCross.

## Start

Build the modified OpenCross source using the normal Windows build instructions.
The viewer is included in the resulting executable, not a second application.

```powershell
.\opencross.exe modelview
.\opencross.exe modelview --game-dir "C:\Games\ChronoCross"
.\opencross.exe modelview --model fieldobj/player/selju/selju
.\opencross.exe modelview --sd
.\opencross.exe modelview --help
```

`ocmodelview` is an alias. Game-directory discovery uses the explicit argument,
then `OPENCROSS_GAMEDIR`, then the existing configuration/default install search.
Model names are member stems without `cdrom/` or `.obj`, as shown in the list.
No game assets are included with the tool.

## Inspect a model

Use **Search model path** and **Group** to select a model. Different costumes and
file variants appear as separate entries. The catalog combines the base and HD
override archives and removes duplicate paths.

Drag with the left mouse button outside the control panel to orbit; use the
wheel to zoom. **Front**, **Side**, **Back** and **Reset framing** provide repeatable
views. Framing uses the union of the original and modified rest-pose bounds.
Animation root motion may leave those bounds; select rest pose to frame again.

The **Clip** list contains raw imported animation clips. Choose **Rest pose**,
or play/pause a clip, adjust speed and scrub its **Frame** slider. The viewer
evaluates the existing pose API directly at the selected time; it does not
advance a second independent simulation. It does not execute animation-sequence
scripts, scene scripts, attack choreography, attached weapons or effect packs.

## Compare original and modified

The **Modified** checkbox switches A/B without changing camera or playback time.
Space toggles playback, Tab switches A/B and R reloads when the UI is not
capturing keyboard input. All actions also have visible controls.

Both versions use the same selected art style. **HD art** pairs cdrom2 models
and base textures where present, plus matching hd.dat textures. SD uses the
disc art set. The original side uses a separate VFS without overlays and its
upload preparation bypasses PNG replacement. The modified side reads existing
enabled mods and their priority/load-on rules from `opencross.ini`.

Changing **HD art** reloads both sides. **Reload models and mods** rescans loose
mods and rebuilds GPU models after waiting for in-flight work; clip/time reset
to rest pose. The viewer does not persist mod settings, configuration or save
data. To change enabled mods permanently, use the regular game's Mods tab,
close it, then reopen the viewer so the updated config is read.

Clip comparison matches source and index, not the clip's array position. If a
matching clip is missing/ambiguous, that side shows rest pose with a warning.
Different clip lengths also produce a warning and use the same playback time.

## Lighting and diagnostics

**Controlled lighting** uses the model's stored normals, an ambient term and a
fixed model-space key light. Disable it to inspect the texture without dynamic
shading. Orbiting changes the camera, not the light direction. This setup is for
comparison; it does not reproduce the lighting of a particular map or battle.
Painted shadows in textures remain visible in both modes.

**Show initially hidden parts** helps inspect multi-part models without running
the game scripts that normally control their visibility. The panel reports
vertex/triangle, mesh-part, joint and clip counts, logical resource paths,
missing optional packs and importer errors. External same-name EX/AT0 packs are
loaded where compatible; absent or rejected packs do not hide internal clips.

The current mesh pipeline supports up to 64 joint matrices. Models exceeding
that limit are rejected with an error, rather than uploaded with invalid joint
references. Missing/corrupt assets likewise remain visible as errors in the UI.

## Tests

The CMake test targets `test_model_viewer`, `test_model_viewer_load` and
`test_model_viewer_pose` use synthetic inputs and require no game data.

```powershell
.\build.ps1 debug -Test
```

For a local development build, the new helper/loader tests can also be compiled
with the source files listed in the contribution's validation report. Test
original-reference isolation separately from the art-style selection: an HD
reference must not accidentally read the disc mesh or a mod's PNG.

Before merging, perform a Windows GPU smoke test:

1. Open without a save; inspect field Serge, one battle player and a monster.
2. Select a multi-part model and verify hidden-part controls and cleanup.
3. Compare a known modified model against the correct unmodified art style.
4. Test orbit, zoom, narrow viewport framing, play/pause, scrubbing and A/B.
5. Switch SD/HD, reload a changed mod and inspect a missing/invalid asset.
6. Verify save/config/mod preference files remain untouched.
7. Close while playback is active; check GPU validation messages/resource release.
