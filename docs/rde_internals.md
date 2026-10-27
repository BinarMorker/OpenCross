# How the Radical Dreamers Edition runs the PS1 game

This is a reading guide for your own Ghidra import of `CHRONOCROSS.exe` (v1.0.1.0, x86 PE). The Radical Dreamers Edition (RDE) is not a port of Chrono Cross. It is a PlayStation runtime: it loads the unmodified Japanese PS1 binaries from `cdrom.dat`, runs them on a MIPS interpreter, and attaches about two thousand x86 handlers ("hooks") to chosen PS1 program-counter values. Everything RDE adds (HD art, translated text, speed modes, remastered music, movies, extra save data) is done by those handlers or by host code they call. Function names below are from `tools\ghidra\symbols\chronocross_1010.txt` ("the symbol list"), which also has the addresses; apply it first (`tools\ghidra_setup.ps1 -GameDir ...` does). The catalogues are `tools\ghidra\symbols\rde_hooks.csv` and `overlays.csv`, described in that folder's README.

## 1. The two kinds of code

The exe has two layers. The PS1 runtime is a MIPS interpreter, a high-level emulation of the BIOS, memory-card and file services, a GPU path feeding Direct3D 11, and a sound thread. The feature layer (HD textures, text, menus and so on) is written for the PC and reached only through hooks.

A third piece sits between the layers: function-level recompiled ports. About 1,490 PS1 functions (840 main executable, 446 battle overlay, 202 field overlay) were converted by machine to native x86 and registered as hooks that replace the original function. They are faithful, not feature code, and can be skipped when hunting for behaviour changes. Signs of one: the original register spills still go to the guest stack, every guest store is wrapped in a check of the cache-isolate bit of the coprocessor-0 status register, every load and store is a virtual call through a memory-region table, and each port charges one fixed cycle cost. Two exceptions carry feature logic: some render ports call the fine-Z recorder (`hd_record_fine_z`), and some battle ports translate tagged polygon-code words.

## 2. The MIPS interpreter and guest state

The interpreter is a table-driven loop: it fetches a guest word, indexes a 64-entry function-pointer table by the primary opcode, and runs the handler. The guest CPU state lives in globals: 32 general registers in order (r0 to r31), then HI and LO, the program counter, a cycle counter and a small coprocessor-0 block. In decompiled handlers the registers are referenced by fixed offsets from the register base, in the usual MIPS order (each is one word). A handler returning a result writes v0. One otherwise unused primary opcode is RDE's BIOS trap (section 5).

Guest RAM is 8 MB, four times the retail console's 2 MB. The guest address space is the console's cached kernel segment (its uncached mirror is accepted too). A host pointer to a guest address is the guest-RAM base plus the address's offset inside that segment, so a global in the exe's data section inside that block is a guest variable; Ghidra shows it as a plain `DAT_` label. The extra 6 MB beyond the retail 2 MB does not exist on a real console; RDE uses it for an extended heap, larger buffers and a host-to-guest mailbox.

Guest loads and stores from host code go through a region table: an array of objects indexed by the address shifted right by 19, each with virtual byte, halfword and word read and write methods. Read an indirect call through it as a load or store at a guest address.

## 3. Hooks

A hook binds a guest PC to a handler. The registrar is `register_hook` (guest PC, handler). It stores the handler in a page table indexed by the PC: each 128-byte page of guest code has a block of 32 slots, one per instruction word, so the interpreter's per-instruction check is a single lookup. Registering a second handler on a PC that already has one turns that slot into a chain dispatcher that keeps the handlers in a map and runs them in turn.

Installation happens once, at boot, before any guest code runs. The master installer `install_all_hooks` calls several sub-installers, named `install_hooks_<addr>` in the symbol list (function entries are the sub-installers; label entries are positions inside the same very large functions). 143 hooks are registered not by straight-line code but from static tables of (PC, handler) pairs that an installer walks; the `installer` column shows `table@<addr>` for those and the containing installer's address otherwise.

Overlays share addresses, so each handler guards itself and returns at once when the guard fails; the guest then runs its original instruction. The guard column records what each handler checks:

| Guard form | Meaning | Hooks |
| --- | --- | --- |
| `-` | none; main-executable code is always resident | 913 |
| `ovl@<A>==N` with A the mode-overlay load base | the first word there (the resident mode overlay's ID word) equals N: 8 battle, 6 field | 832 plain, more with extra conditions |
| `ovl@<A>==N` with A the menu-overlay load base | the first word there equals N: 10 main menu, 11 name entry, 12 shop | 122 plain, more with extra conditions |
| `insn@A==W` | the guest word at A (or at the hooked PC) equals W; used for overlays with no ID word | about 95 |
| further conditions | map or room number, caller (`RA`) value, a guest memory byte, a host flag, an argument register, language | the rest |

Forms combine, e.g. a field-overlay guard `&& map==<n>` limits a field hook to one map. The literal addresses are in the `guard` column of `rde_hooks.csv` and the load bases in `overlays.csv`.

The mode column says what the handler does to guest execution:

| Mode | Count | Meaning |
| --- | --- | --- |
| observer | 285 | do host-side work and return; the original instruction at that PC then runs normally |
| replace | 1627 | do the work and resume the guest somewhere else, usually at the return address, so the whole function is replaced; 1488 of them are the recompiled ports |
| skip | 84 | move the PC forward past some original instructions (a code patch written as x86), optionally after setting registers |
| redirect | 28 | move the PC to an unrelated address, optionally after setting registers |
| return-early | 10 | return from the current function immediately, with a value in v0 |

`guest_resume(new PC, cycles)` resumes the guest: it stores the PC, adds the cost, and jumps to another hook's handler if one is registered there, otherwise returns to the interpreter loop. A replace handler ends by tail-calling it (with the saved return address to return from the function, or an address inside it for a skip); an observer simply returns.

## 4. Overlays

The PS1 game is a main executable plus about a hundred overlay programs, `prog/<name>.prg` inside `cdrom.dat`, which the game loads on demand into a few fixed regions. `overlays.csv` lists each overlay's name, file, load base, size and ID word. Each load base (the `load_base` column) is one region; overlays in a region replace each other. The regions are:

| Region | Contents | Count |
| --- | --- | --- |
| main executable | not an overlay file; listed for completeness | 1 |
| mode overlays | the big overlays: field (ID 6), miyatest (7), battle (8), title (9) | 4 |
| effect overlays | battle effects (spells, techs, summons, boss effects) | 93 |
| menu overlays | menutest (10), namemenu (11), shopmenu (12) | 3 |
| post-battle overlay | btlexit (results) | 1 |
| sub-menu overlays | btlinit, memmenu, memmenu2, monmenu, playinit (sub-menus and battle start) | 5 |

The ID word is the first 32-bit word of the file. The post-battle and sub-menu overlays have none (shown as `-`; their first word is code), so their hooks use instruction checks. No guard tests an effect overlay's ID word; effects are reached through the battle and texture-upload hooks. Import an overlay into Ghidra as a raw binary at its base; `tools\ghidra_setup.ps1 -Overlays` automates it and applies the matching symbol list.

## 5. Boot and file input

RDE never runs a Sony BIOS. At start the exe clears guest RAM and VRAM, resets the CPU, and plants a minimal kernel image: the three BIOS call vectors (A0, B0, C0) hold trap opcodes. When the guest executes one, the interpreter dispatches by function number through three tables to C++ handlers; roughly a hundred and ten functions are implemented (libc, GPU command words, events, file and memory-card APIs) and the rest are silent no-ops. The main executable is read from `prog/slps_023.64` in the ZIP, its 2 KB PS-X EXE header supplies the entry PC, global pointer and stack pointer, and the body is copied to the executable's load address. `install_all_hooks` runs and the guest starts at its own entry point. There is no CD controller, DMA or interrupt-driven transfer.

File access works by switching the game into a development mode it still contains: besides the disc file system it has a "PC" one that opens files by path through the host-file calls of the development kit (init, open, read, seek, close). A few one-byte flag writes from observer hooks at boot select it. The five host-file stubs are then hooked by a table-driven installer (25 hooks) and replaced with handlers that resolve the guest's path to a ZIP member and copy it straight into guest RAM inside the read call, so every read completes synchronously. Details:

- The game itself loads `cdrom.fnd`, a table of 64-byte path records mapping file indexes to names; the exe never parses it.
- Archives are `cdrom.dat` (the merged tree of both discs: PS1 executables, overlays, index files), `cdrom2.dat` (an override layer tried first for a path, always in HD mode and for a few files in classic mode), `lang.dat` (translated battle events and text) and `movie.dat` (Bink movies). They are read with a libzip-style reader over ordinary file calls; nothing is memory-mapped.
- Disc changes are the game's own: for disc 2 the field overlay reloads the disc-2 index files from the root of `cdrom.dat`, and the disc number lives in a guest byte. `cdrom2.dat` has nothing to do with disc 2 despite the name.
- CD speed is faked. After the copy, a delay is computed from the length and a configured speed (battle loads are always paced as double speed). The game's CD-status poll is hooked: until the delay has elapsed, non-blocking polls see a busy drive, and blocking waits never stall.

Movies bypass the PS1 STR player: the field overlay's streaming routines are stubbed (category movie) and the movie number picks a `.bk2` file played by the Bink library. There is no XA or CD-DA.

## 6. Saves and memory cards

The memory cards are emulated at the BIOS level only, on two 128 KB host images. At boot `save_boot_init_cards` and `save_validate_card_member` read the card members from the save archive (`save_zip_read_member`); a missing or wrong-size member is replaced by a formatted card (`save_format_card`). The archive is `save.dat`, an encrypted ZIP with the two card images and a few extra members (extended data, achievements). `sys_get_save_folder` resolves the per-Steam-user folder under the user's documents and `sys_set_save_path` stores the path. Every guest write to a present card rewrites the whole archive at once (`save_zip_write_archive`, via a temporary file and rename). Hooks merge extended data around saves (`save_ext_read`) and auto-save on the field tick (`save_autosave_write`).

## 7. The host-side feature layer

HD textures. The renderer keeps VRAM as a 4x grid: each 16-bit halfword of the PS1's 1024 by 512 VRAM is a 4 by 4 block of texels, so a primitive sampling the original coordinates samples HD texels. `hd_pack_hd_tim` writes a 4x HD TIM into the cells of an original VRAM rectangle and marks the CLUT; `hd_vram_upload_tim` and `hd_vram_move` mirror the guest's own uploads and moves into the HD VRAM. The hd-texture-vram hooks call them on every texture or palette upload, with files from `data\hd`; the game's own primitives do the drawing.

HD backgrounds. Field backgrounds are loaded per map from `.chip2` files, requested from a hook in the field overlay's map loader; the render-precision hooks sort primitives by a finer depth than the ordering-table buckets allow.

The overlay canvas. UI, text and portraits are not drawn by the PS1 path. Hooks at the PS1 draw calls capture the primitive and queue a PC sprite or text entry in a pending list (font/text-render, hd-overlay-sprite and ui-pc-menu hooks, with helpers such as `hd_place_menu_part_text`, `hd_draw_save_slot_face` and `hd_register_menu_sprites`) or skip the original draw. Once per frame, at a begin-frame hook, the pending list is published to the render thread, which draws it with a sprite batch and a sprite font. Menus keep a retained list and invalidate it by rectangle when the guest clears an area, battle rebuilds every frame, and field message windows live in persistent slots. Text is looked up by guest string address and language (the text-translation hooks); layout helpers such as `hd_measure_text_ps1_px` work in PS1 pixels on the 640-wide display. The pause menu (`hd_draw_pause_menu`) is pure host code drawn in the same pass.

Audio. The main executable's sound API is forwarded by audio/bgm hooks to a host music engine that plays remastered `.sab` tracks instead of the SPU sequencer; sound effects still use the game's AKAO path. `sys_send_audio_message` posts commands to the audio thread, and `sys_pause_game` and `sys_suspend_sound_thread` stop it.

Timing and input. GPU and vblank waits are hooked so the guest never spins, speed modes reuse the game's leftover debug fast and slow branches, `sys_keyboard_to_pad_word` converts the keyboard to a PS1 pad word, and `sys_poll_hotkeys` handles the per-frame toggles.

## 8. Hook categories

The catalogue has 2,034 rows. By category (the `category` column of `rde_hooks.csv`):

| Category | Hooks | What it covers |
| --- | --- | --- |
| native-port | 1,488 | recompiled functions: 840 main, 446 battle, 202 field |
| font/text-render | 115 | PC fonts and windows replacing the PS1 text engine |
| hd-overlay-sprite | 72 | queue a PC sprite, skip the PS1 draw |
| text-translation | 40 | string lookup by address and language |
| other | 40 | host-file API stubs, entry-point work, CD-busy poll |
| hd-background | 37 | `.chip2` layers, menu backdrops |
| memory-expansion | 36 | extended heap, bigger buffers |
| hd-texture-vram | 35 | mirror VRAM uploads, load HD TIMs |
| ui-pc-menu | 24 | PC pause and settings menu |
| bugfix | 23 | per-map and event fixes |
| gpu-wait/timing | 21 | idle query, status polls, vblank waits |
| speed/boost | 15 | fast and slow modes |
| battle-feature | 15 | auto-battle and battle boost |
| audio/bgm | 14 | sound API to host engine |
| movie | 13 | movie playback |
| save/newgame+ | 11 | New Game+, auto-save, extended data |
| debug | 10 | development-mode switches |
| render-precision | 9 | fine-Z ordering |
| encounter | 7 | encounters off |
| input | 5 | keyboard merge, soft-reset skip |
| achievement | 4 | achievements |

Of the 546 non-port hooks, 285 are observers and 139 are replacements; the rest patch control flow. To find what RDE changed in a screen, filter the catalogue by guard (the overlay) and category, then open the handler named in `handler_symbol`. Unnamed handlers are `hook_<category>_<pc>_<mode>`.