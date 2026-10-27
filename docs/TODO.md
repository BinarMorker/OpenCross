# Open issues and known bugs

The working list for the engine: behaviour bugs found by reading the code, fidelity gaps against the original game, open comparisons that need a capture, and code hygiene. Each entry names the file to look at. Remove an entry in the same change that fixes it; keep the sections.

## 1. Bugs (behaviour)

Menus, shops and saves (`src/ui/`, `src/save/`):

- `shop_logic.cpp` `forge`: the materials are taken before `add_item`; with no free bag slot the piece is not added, the materials are gone and no gold is charged (returns `Full`).
- `shop_logic.cpp` `trade_commit`: ignores the result of `add_item`; at 99 held or with no free slot the offered elements are taken and the goods are lost (`add_to` returns true without raising a count of 99).
- `game_state.cpp` `equip`: ignores the result of `add_item` for the replaced piece, so it is lost when the bag has no free slot; `place` and `remove` silently cap element copies at 99 the same way.
- `game_state.cpp` `use_element`: accepts heal size 3 (heals 0 HP) and still consumes a copy when HP is below max; no check for a target at 0 HP.
- `game_state.cpp` `sort_elements`: mode `InnateFirst` puts the colour opposite to the member's innate in group 0, while `score()` treats the matching colour as +10 and the opposite as -10.
- `app_sim.cpp`: a script save request opens the menu through `debug_show(Save, 1)`, so a save point can only reach the Slot 1 list; Cancel there lands on the Save / Load choice. Slot 2 and the OpenCross page are unreachable from a save point.
- `cc_save.h` `progression()` returns a u8 for the u16 variable 0; `preview_of` (`main_menu_save.cpp`) reads the u16, so `GameState::location_key()` can never reach the 999 threshold. The threshold table is duplicated in `game_state.cpp` and `save_layout.h`.
- `main_menu_save.cpp` `load_previews` skips files numbered 15 and above; the title `SlotList::collect` has no such filter, so a card can list different files in the two places.
- `cc_save.cpp` `pack_record`: `SaveChar::name` keeps at most 6 symbols although non-JP name entry allows 12; the full name survives only in `ext_save.dat` or the OpenCross page.
- `shop_screen.cpp` trading post: `goods_cur` indexes entries including holes while the Down clamp uses `row.count` (holes excluded); a hole in the middle of a row makes the last entries unreachable or lands on goods 0.
- `party_editor.h` pick-mode swap: the lock bit of the other slot's member is not checked, so a locked member can be moved out of its slot.
- `main_menu_main.cpp`: OK on an unavailable roster row plays sfx 8 and then 0x18. `roster_no` (the "proceed?" cursor) is reset only in `open()`.
- `game_flow.cpp` `FlowMachine::on_load`: an unknown location logs a warning but still sets the Field state; no room is loaded.
- The status card shows the raw record stats (cap 99) while the equip screen shows record plus gear (caps 999 and 99); not checked against RDE.

Battle (`src/battle/`, `src/game/battle/`, `src/game/game_session.cpp`):

- `battle.cpp` `make_equip`: when a gear table exists but neither the weapon nor the armour id resolves, the function returns before `equip.acc` is set, so the Dopplegang accessory check (0xFC) and the special-status accessories (0xF9..0xFB) see no accessories.
- `battle.cpp` God Mode / Battle Boost cleanse: a member in the frozen state (0x20000000) fails `alive()` and the loop skips them before the negative statuses are cleared, although that bit is in the cleanse mask.
- `battle.cpp` `settle()` with a pending Pip evolution: `can_act` is false for everyone while the evolution is pending, so the view's second `settle()` can return without advancing time. Possible stall; not reproduced.
- `battle.cpp` ring start: an out-of-range battlefield operand leaves the field ring empty, while `app_battle.cpp` and `battle_view.cpp` fall back to the scene's stage. The two disagree.
- `battle.cpp` Chrono Cross colour row test uses `cell & 0x7FFF == 0x7E`; cell ids elsewhere use `& 0x3FFF`.
- `game_session.cpp`: trapped elements are added with `id & 0xFF`, stolen loot and drops with `& 0x7F`; harmless only while stock ids stay below 0x80.
- `battle.cpp`: kill drops index `drops[drop_row]` unmasked, the pilfer path masks `drop_row & 0x3FFF`.
- `battle.cpp` `init_enemy` never sets `gauge_max`, so enemies keep the default 7.
- `battle.cpp`: a colourless cast pushes -1 into the field ring and shifts the oldest colour out.
- `battle.cpp` `time_advance` recovers PC stamina with no flu factor while `step_after_pc_action` applies x300/400; Defend's +10 also ignores flu. One of the two is wrong.
- `battle.cpp` enemy name suffix: the comment says A..C, the code assigns `copy + 1`, so a group of six gets 1..6.
- `new_game.cpp` versus `battle.cpp` `unlock_grid`: a new game unlocks `el_mask` rows 0..19 for the 20 opening stars, a battle star-up at 20 stars applies rows 0..20. Possibly intended; unverified.

Field (`src/game/`):

- `field_world_sim.cpp` `step_player`: `trail_push(0, lead_start_)` runs for any controlled entity while `lead_start_valid_` is set only for the leader, so ring 0 takes a non-leader's spots when a script controls another entity.
- `field_world_audio.cpp` `play_footstep`: the left/right choice uses `feet & 1` while the sound is gated on bits 4 / 5 and the own-sound index uses `(feet >> 4) - 1`. Possibly intended.
- `field_world_draw.cpp` `ent_not_drawn` tests flags 0x20 and 0x4000 only; its comment and the shadow mask 0x10C420 also name 0x8000.
- `field_world_sim.cpp`: pad walking is not halved on walk-mesh tiles with flag 0x1000; only the terrain push is (`rules/field_actors.md` 2.1 says so).

## 2. Fidelity gaps (not modelled)

- **Encounter variants.** `symtbl` gives every battle id up to 8 enemy groups and 89 of the 94 used ids differ between them, but `make_field_battle_args` (`app_battle.cpp`) always uses variant 0, so random encounters never vary. The original rolls the variant against a weight table (`formats/battle_data.md` 6).
- Field script: the ops listed as stubs in `formats/fieldscript.md` 11.4 (FE 51, 66, 73..7E and the others named there); PAC ops with confidence `?` in `src/battle/pac_opcodes.inc`; a menu kind closed at once in `app_sim.cpp`; one element mode with no effect in `battle.cpp`; animation-sequence opcodes that stop the sequence in `actor.cpp`.
- Audio: busy bits for sound command 9D, sound command 21 eviction beyond the mask key, E1 / E2 random pitch, D4..D7 sidechains, music banks outside the SFX image; sequence opcode 0A is approximated as a plain load (`src/audio/akao_player.cpp`).
- Battle presentation approximations, each marked in its file: the camera curve's interior keys (`battle_camera.cpp`), spell-light depth (`battle_spell_lights.cpp`), the subtractive base plate drawn as a multiply (`battle_view_draw.cpp`), the water layer exact only under an additive layer with nothing behind it (`mesh_pipeline.h`), the `lasdes2` (526) death dissolve units and particle motion (`effects/bespoke/death_dissolve.cpp`), the miki haze capture not consumed by the battle host (`bespoke.h`), `ripple` (524) private ops unverified.
- Slow-motion `k` pacing assumes one vsync per PS1 frame (2 ticks per `k + 1` video frames) while the PS1 stretch depends on load; affects several summons, Bomber hop, Meteor Shower, Tornado and the Dash & Slash return leap (`battle_view_fx_state.cpp`). Volcano-class per-iteration overlay steps differ the same way.
- Audit (`ocbattle fx-audit`): mesh-texture-missing 4, mesh-texture-partial 4, mesh-part-missing 11, blank 13, never-ends 2, no-pack 4, no-sequence 5, sheet-unresolved 2, model-missing 1; bespoke module-not-run 3, private-ops-unhandled 4; sequence ops 28, 29, 52, 54 and 6A are not carried out in some packs.

## 3. Open comparisons (need a capture against the PS1 or RDE)

- Pilfer hits about 32 ticks early (cast lead-in); Pilfer / Boxer timing not checked frame by frame.
- Bulbs: the cutscene cuts to the PC idle camera after the seed shower.
- Late PC placement for boss intros is derived from code only; FATE `i04` leg is drawn in front of the camera; Fire Dragon 453 looks mirrored in one pair; Bunyip dome lacks the light colour.
- Hit timing unchecked: Multistab-style multi-react numbers, Gnaw Bones, Sic 'Em, Bug-Swatter, Slash All.
- Dopplegang: the slot >= 220 guard and the list-window row pitch / columns are code-only (needs a forged Sprigg save; `--party 26` starts Sprigg with an empty grid).
- Colour / brightness notes unconfirmed on bg_21, bg_72, bg_55/56/96-98 floors and several effects; tech emitter counts for 174, 199, 203, 229; JP party panel and Trading Post layout; settings window clamping on small monitors.
- Message window: the grow takes 16 ticks in ours versus about 18 frames on the PS1; number-entry acceptance and close latency unchecked.
- Follower trail distance behind the leader is shorter than the original; `tel_b22e` boat bobs in ours only; the 73 other D6 (attached-entity) cutscene-ride rooms are not eye-checked.
- User-guide rows without glyphs (Space / K on the keyboard page, Pause / Change Lead Character on the pad page, speed / F3-F5 rows). The 500 ms Start hold that opens the settings window is untested on a real pad.
- Items / element grid after loading an OpenCross save in RDE unchecked.
- Earthquake se61 rumble brighter on PS1; fireball se60 first 200 ms louder in ours; `ocbattleseq sfxscan` flags three never-ending sounds (transition pair 0x23 / 0x24, bg_10 se35).
- The status card versus equip screen stat display (section 1) against RDE.

## 4. Code hygiene

- Dead code: `best` / `best_d2` in `FieldWorld::step_triggers` (`field_world_collide.cpp`); `yaw_from_script_` is set and never read (`field_world_tick.cpp`); the unused shadow path in `RoomView::draw` (`Shadow` span, `kShadowDepthBias`, `room_view.*`); `saving_locked_` has no caller of `set_saving_locked` (`field_world.h`).
- Stale comments: `step_shadows` says "twice per tick" (it eases 2 per tick, once); `step_cull` says "once per rendered frame" (it runs every tick); `advance_anim` says the cross-fade source keeps looping (only the 0x8000 blend case does).
- `src/script/field_opcodes.inc`: about 170 rows carry raw names (`ENT_FLAG38_BIT4_SET_CLEAR`, `SET_GLOBAL_FLAG_12`, ...) while `formats/fieldscript.md` uses descriptive ones; renaming the table to the doc's names would make `occ script-disasm` and the watchdog output readable.
- `tools/py/cc_fieldscript.py`: the opcode table's notes column names third-party tools.

## 5. Tests and tooling

- Route `dialogue` fails at tick 1571 waiting for room `opn_a01i` (route line 25); it failed the same way before the documentation pass.
- `battle_hud` on a Debug build runs every screenshot 11 ticks later than on Release because the field-to-battle load is time-sliced in real time; the route content is identical. A Debug baseline would settle whether that is build-config only.
- Standalone `[Debug] Battle=` routes (`battle_run`, `battle_chain`, `battle_dash`, `battle_dash2`, `battle_defend`, `results_heal`, `smoke_battle`) are clock-seeded unless the `.env` sets `OC_BATTLE_SEED`, so they are not deterministic.
- `tests/routes/keymap.txt` has flipped line endings in the working copy (a pre-existing local change).
- The `sweep` route ini has no `[Paths] SaveDir`, so a sweep run writes autosaves to the real save folder; add a scratch save dir to it before running (the seeded comparison sweeps used `tmp\`).
