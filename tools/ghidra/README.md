# Ghidra scripts

Headless Ghidra scripts used to read the Radical Dreamers Edition (RDE) exe and the PS1 game code. See [`docs/validation.md`](../../docs/validation.md) for the full workflow. The output is decompiled Square Enix code: keep it outside the repository and never commit it.

| Script | Arguments | What it does |
| --- | --- | --- |
| `ExportAll.java` | `<outdir>` | Writes `decomp.c` (every function decompiled), `strings.txt` (with referencing functions), `imports.txt` and `functions.txt` (address, name, size, caller count). |
| `RdeDecompTargets.java` | `<targets.txt> <out.c>` | Creates a function at each hex address in `targets.txt` if missing and decompiles all of them into `out.c`. For one-off lookups after an export. |
| `DumpSymbols.java` | `<outdir>` | Writes `<program>.csv` (`address,name,kind,source,namespace`) for every symbol of the current program. Run it with `-process -recursive` to cover every program in a project. `source` shows which names were user-defined. |
| `ApplySymbols.java` | `<symbols.txt>` | Applies `name = 0xADDR;  # function\|label` lines: renames existing functions, creates missing ones (kind function), otherwise adds labels; all USER_DEFINED. Prints counts. |
| `Ps1Seed.java` | `<entry PC>` | For a raw PS1 import: creates functions at the entry point and at every `jal` target, because the Binary loader finds no entry points of its own. |
| `Ps1MainExport.java` | `<symbols.txt> <outdir>` | Applies `name = 0xADDR;` symbols (main exe range `0x80010000`..`0x800AA000`) and writes one `name__ADDR.c` per function. |

`Ps1MainExport.java` expects the PS1 main exe imported raw, header stripped, `BinaryLoader`, `MIPS:LE:32:default`, base `0x80010000`.

How the RDE exe runs the PS1 game (interpreter, hooks, guards, overlays, file input) is described in [`docs/rde_internals.md`](../../docs/rde_internals.md); the hook catalogue `symbols\rde_hooks.csv` and the overlay catalogue `symbols\overlays.csv` are described in [`symbols/README.md`](symbols/README.md).

## Symbol lists

`symbols\chronocross_1010.txt` is the project's own name list for the RDE exe (v1.0.1.0): the hook registrar, resume entry, master installer, the `install_hooks_<addr>` sub-installers and one `hook_<category>_<guest pc>_<obs|rep|skip|redir|early>` per hook handler (about 1,800 names) plus the RDE functions whose role is described. The PS1 programs have their own lists, `symbols\ps1_main.txt`, `ps1_field.txt`, `ps1_battle.txt`, `ps1_btlinit.txt`, `ps1_btlexit.txt`, `ps1_menu.txt` and `ps1_effects_<overlay>.txt`; which program each applies to is in [`symbols/README.md`](symbols/README.md). The lists are maintained by the project; add lines as functions are identified. Library, compiler and Ghidra-generated names are not included. `ghidra_setup.ps1` applies the matching file after import and before `ExportAll.java` (`ps1_main.txt` for the main exe); use `-Symbols <file>` to override.

## One command

`tools\ghidra_setup.ps1` downloads Ghidra and a Temurin JDK 21 into `tools\ghidra\work\` (git-ignored), stages the scripts in a folder without spaces, and runs import, auto-analysis and `ExportAll.java`:

```powershell
.\tools\ghidra_setup.ps1 -SetupOnly
.\tools\ghidra_setup.ps1 -Ps1Exe "C:\path\to\game\data\cdrom.dat" -OutDir D:\private\decomp
.\tools\ghidra_setup.ps1 -GameDir "C:\path\to\game" -OutDir D:\private\decomp   # hours
```

`-Ps1Exe` takes `cdrom.dat` or an extracted `SLPS_023.64` (with or without its header); `-Symbols <file>` replaces the default symbol list for whichever target is imported.

Overlays: `-Ps1Exe <cdrom.dat> -Overlays field,battle,darkfeit` (or `-Overlays all`) extracts each `prog/<name>.prg`, imports it raw (`MIPS:LE:32:default`) at its base from `symbols\overlays.csv` into the same project, applies `ps1_<overlay>.txt` or `ps1_effects_<overlay>.txt` when one exists (menu overlays use `ps1_menu.txt`), and exports to `<OutDir>\ps1\<overlay>\`. The main exe is skipped unless `-IncludeMain` is added.

## Manual headless run

Use PowerShell (cmd.exe breaks on `(x86)` paths) and keep the script folder, project folder and every argument free of spaces and parentheses; Ghidra 11+ cannot load scripts from a path with spaces.

```powershell
$env:JAVA_HOME = "C:\jdk-21"
# first import + analysis + export
& "C:\ghidra\support\analyzeHeadless.bat" C:\work\proj rde `
    -import C:\work\CHRONOCROSS.exe -overwrite -scriptPath C:\ghidra_scripts `
    -postScript ExportAll.java C:\work\out
# later, against the existing project (no re-analysis)
& "C:\ghidra\support\analyzeHeadless.bat" C:\work\proj rde `
    -process CHRONOCROSS.exe -noanalysis -scriptPath C:\ghidra_scripts `
    -postScript RdeDecompTargets.java C:\work\targets.txt C:\work\out.c
```

PS1 main exe import options: `-import slps.bin -loader BinaryLoader -processor MIPS:LE:32:default -loader-baseAddr 0x80010000`.
