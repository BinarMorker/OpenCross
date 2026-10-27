# Menu rules

Behaviour of the menus, shops, name entry, save and load screens, party editors and the pause screen: the rules, filters, formulas and limits the code enforces. Screen inventory and the flow machine: `architecture.md` section 7. Menu tables, the sell price and the forge fee: `formats/battle_data.md` sections 5.1, 11.3, 11.4. Save container, slot choice, New Game+: `formats/save.md`. Fonts, window skins, UI art: `formats/text.md`. Field script variables: `formats/fieldscript.md` section 10.8. Player walk / run and analog movement: `rules/field_actors.md` section 2. None of that is repeated here.

Code: `src/ui/menu/` (main menu, `GameState`), `src/ui/shop/`, `src/ui/title/`, `src/ui/party_screen.*`, `src/ui/pause_screen.*`, `src/game/game_flow.*`, `src/game/party_editor.h`, `src/app/app_sim.cpp`.

Conventions. "sfx N" is the menu sound id the code plays (1 cursor, 5 saved or sold, 6 cancel, 8 confirm, 0xC grid row change, 0x17 grid change, 0x18 refused, 0x1B traded, 0x48 equipment change, 0xBF forged or broken down). A refusal is only sfx 0x18 unless a message is named. Item stacks hold at most 99 (a further add succeeds silently without raising the count); the element stock has 192 slots, the gear and material stock 384. `party()` is the menu party: up to 3 valid ids in slot order, with a roster stand-in (section 1) replacing slot 0. Offsets such as "+0x12" are record byte offsets from `formats/battle_data.md`.

## 1. Main menu graph and input

Source: `main_menu.cpp`, `main_menu_main.cpp`.

Screens: Main (list, member pick, roster), Status, ElementChoice (Use / Allocate), ElementUse, ElementGrid, Equip, KeyItems, Settings, Save (Save / Load).

| List row | Opens | Exit cursor on return |
|---|---|---|
| 0 Status | member pick, then Status | 0 |
| 1 Elements | ElementChoice | 1 |
| 2 Equip. | member pick, then Equip | 2 |
| 3 Items | KeyItems | 3 |
| 4 Customize | Settings | 4 |
| 5 Save/Load | Save (Load is always available; Save is gated inside, section 10) | 5 |

* **List.** Up / Down wrap over 6 rows (sfx 1). OK opens (sfx 8). Cancel closes the menu (the other exit is a confirmed Load, section 10): it stores the row in the cursor memory and sets closed (sfx 6).
* **Cursor memory.** On `open()` the list starts on the remembered row when options bit 0x01 is set, else on row 0. The remembered row is written only by Cancel on the list; the exit cursors above are not stored.
* **ElementChoice.** Two rows, Use and Allocate; Up / Left and Down / Right both toggle (wrap). Use opens ElementUse. Allocate returns to the member pick with the target set to ElementGrid. Cancel returns to the list on row 1.
* **Member pick.** Shows the party (at most 3 portraits). The pick cursor starts on the current member clamped to the party size. Left / Up step back and Right / Down forward, wrapping. Left on the first member opens the roster instead of wrapping. OK opens the target screen with that member; Cancel returns to the list.
* **Roster.** Lists all 44 characters (`save::kNumChars`), 17 rows visible (kRosterRows), scrolling one row at a time without wrap. It slides in and out over 20 ticks (kRosterSlide); input waits while it slides. Only available characters (recruited and not in the exclusion bitmap) show a row. OK on an unavailable row is refused (sfx 0x18); OK on an available one opens the target screen with that character standing in for party slot 0 (`stand_in`). Right or Cancel slides it out and puts the pick cursor on member 0. On the Elements-grid and Equip picks, Triangle on an available row asks "proceed?" (Left / Right; the cursor is on Yes when the menu opens and keeps its last position afterwards): Yes empties the removable cells of that character's grid (sfx 0x17) or unequips all three accessories (sfx 0x48); No leaves it unchanged. Leaving a screen that was opened from the roster returns to the roster on the same row, not to the list.
* **Member switch.** L1 / R1 cycle through `party()` (wrap, sfx 1) on Status, Equip and the Elements grid. On Status and Equip a stand-in takes slot 0 of the cycle; on the grid L1 / R1 are ignored while a stand-in is active.
* **Input lockouts.** The first 4 ticks after `open()` ignore all input. Directions repeat after 350 ms every 70 ms (`DirectionRepeat`); a stick counts as a direction past 0.6, L2 / R2 past 0.5 of travel. The grid and the roster have their own locks (section 5). A message (`say`) clears after 150 updates, a save result after 240.
* **Entering a screen** resets its cursors: Status shows the profile; ElementUse list and target at 0; the grid screen `grid_reset`; Equip slot 0 on the slot column; KeyItems 0; Settings row 0 with the skin picker closed; Save starts on Load when saving is blocked, else on Save.
* **What closes the menu.** Cancel on the list; a confirmed Load (section 10). After Cancel the host fades the field in over 0x10 ticks and a menu opened by a script sets script variable 0x4C = 1 when a file was written since `open()`, else 0; a Load does neither.
* **Opening.** The Menu key (pad North) opens it when the field is idle, no menu or battle is active and global flag bit 12 (menu key disallowed) is clear. The screen fades to black over 0.5 s first, then opens under the veil. A script MainMenu request opens it without the fade. A script Save request opens the menu straight on the save list of Slot 1, always with saving allowed.

## 2. Settings screen and the options word

Source: `main_menu_settings.cpp`; options word and defaults: `game_state.h` (`opt::`), `game/new_game.h`.

The options word is the u16 at head bytes 0x5A0..0x5A1 (`formats/save.md` section 4). Up / Down wrap over 8 rows; Left clears the row's bit, Right sets it, OK toggles it (sfx 1 / 1 / 8); Cancel returns to the list on row 4.

| Row | Bit | Clear / set | Effect in code |
|---|---|---|---|
| Sound | 0x04 | Stereo / Monaural | `Mixer::set_mono` every frame |
| Window Frame | 0x08 | Default / Custom | dialogue and key-item list use the skin byte only when set, else skin 0 |
| Cursor Position | 0x01 | Default / Memorized | main list start row, battle command cursor, results cursor |
| Accuracy Selection | 0x02 | Manual / Auto | battle menu option |
| Controller | 0x100 | Default / Custom | toggles only (not modelled: nothing reads the bit) |
| [button] Pressed | 0x200 | Run / Walk | inverts the run button (`rules/field_actors.md` section 2.2) |
| Vibration Function | 0x400 | Off / On | rumble enable; also set by the title page and the pause screen |
| Left Stick Sensitivity | 0x800 | Digital / Analog | Analog makes a full stick tilt run (`rules/field_actors.md` section 2.2) |

* **Pad rows.** Rows 6 and 7 are greyed without a connected pad; Left / Right / OK on them play sfx 0x18 and change nothing. Rows 0..5 are always enabled.
* **Skin picker.** OK on Window Frame while it is Custom opens the picker instead of toggling. Left / Right step the skin byte 0..14 (wrap) to the next skin whose bit is set in the owned-frames mask (`slot.frames & 0x7FFF`); an empty mask makes the step a no-op. OK or Cancel closes the picker. The skin byte lives on `GameState::skin`, is not part of the slot image, and is set by the script (clamped 0..14).
* **Defaults.** A new game or New Game+ starts from the word in `formats/save.md` section 14.1, then sets bit 0x400 from the title vibration page (section 13). A loaded slot keeps its word.

## 3. Status screen

Source: `main_menu_main.cpp`, `game_state.cpp`.

OK toggles the profile panel and the equipment panel; Cancel returns to the list on row 0; L1 / R1 change member and return to the profile with the model camera reset.

* **Card values.** Name; HP as current / maximum, each shown as `min(value, 999)`; the current HP uses the warning colour when `cur_hp < max_hp / 8` (integer division). STR, ACC, MAG, RES, AGI, M.RES are the record fields `atk, hit, mag, def, evd, mdef`, each `min(value, 99)`. The record holds the character's own value: neither equipment nor a growth lookup is applied on this screen (the equip screen, section 4, adds gear). INNATE shows the colour disc of `innate(character)`: the record colour bit for characters 0 and 43, else the `char` record's innate byte (the dream record set while the dream state is active).
* **Stamina recovery** (Status and Elements-use panels). `min(rate + sum over accessory slots 2..4 of gear.mag where gear flag 0x80 (stamina) is set, 99)`; `rate` is byte +0x1F of the character record.
* **Ailment badges.** Four badges for record status bits 0x01, 0x10, 0x20, 0x08.
* **Grid minimap.** One tile per unlocked grid cell.
* **Profile text.** Table row keyed by the character's model byte when it is at most 0x31, else by the character id; at most 7 lines, trailing blank lines dropped. The 3D model is the menu model with the same key (model byte up to 0x32).
* **Pad camera.** Triangle toggles between the turning model and the pad camera; in pad mode Square steps the pose 0, 1, 2. The model logic ticks every second 60 Hz frame. Auto turn +8 per tick. Pad camera: Left / Right turn 16, Up / Down move 32 (distance clamped between the model's nearest distance and 0x960); with L2 held the directions pan by 8 (limit 0xAA); with R2 held Up / Down tilt by 16 (limit -0x200..0).

## 4. Equip screen

Source: `main_menu_equip.cpp`; rules `GameState::equip_candidates`, `can_equip`, `equip`, `stats_with`.

Slots 0 weapon, 1 armour, 2..4 accessories. The slot column (Up / Down wrap over 5): OK opens the candidate list, Cancel returns to the main list on row 2, Triangle unequips all three accessories at once (sfx 0x48, no prompt). L1 / R1 change member and close the list. The list: Up / Down without wrap, 11 rows visible (kVisibleRows), Cancel back to the slot column.

* **Candidate filter.** The list holds, in bag stock order, every stack with quantity > 0 whose item kind (low nibble of the item-info category) equals `min(slot, 2)` (0 weapon, 1 armour, 2 accessory). An accessory slot that is occupied gets a "Remove" row first (always selectable); weapon and armour have none.
* **Selectable.** A row is selectable when the equippable-by mask has the character's bit (ids below 64) and, for accessories, `gear.slot_mask & (OR of the slot masks of the other two equipped accessories) == 0` (the accessory conflict mask). Other rows stay listed in grey; OK on one plays sfx 0x18. Quantity shows as `min(qty, 99)`.
* **Preview.** For the highlighted selectable row the screen shows the new values in a raised or lowered colour next to the current ones. The Remove row previews with item 0.
* **Stat formulas** (`stats_with`; `ch` = record, `o` / `d` = sums over the equipped accessories with gear flag 0x20 (offence) / 0x40 (defence) of gear bytes +4 / +5 / +6, `wp` / `ar` = the weapon / armour gear records):

| Stat | Formula | Display cap |
|---|---|---|
| HP | `max_hp` | (not shown) |
| ATK | `ch.atk + o4 + wp[4]` | 999 |
| M.ATT (mag) | `ch.mag + o5 + wp[5]` | 999 |
| HIT% | `(2 * (ch.hit + o6 / 3)) / 3 + wp[6] / 3` | 99 |
| DEF | `ch.def + d4 + ar[4]` | 999 |
| M.DEF | `ch.mdef + d5 + ar[5]` | 999 |
| EVD% | `ch.evd + d6 + ar[6]` | 99 |

  All divisions are integer. A slot with no item contributes 0.
* **Equipping.** `equip` re-checks `can_equip` (including a bag count > 0 for the item), takes one copy from the bag, returns the replaced item to the bag, and writes the id into the character's weapon, armour or accessory word. The record stats are not rewritten; gear is added at display time. Success plays sfx 0x48 and closes the list. If the bag has no free slot for the replaced item it is lost (the add result is ignored).

## 5. Elements screen (grid editor)

Source: `main_menu_elements.cpp`; rules `game_state.cpp` (`can_place`, `can_remove`, `swap`, `auto_fill`, `sort_elements`), `MenuRules`.

**Geometry.** 8 rows (the element level, row r holds level r + 1) by 10 columns. A character has a cell where its record level mask (+0x30, one u16 per row) has the bit; cells are stored compactly in row-major order, at most 48. A cell is usable only when its bit is set in the character's unlocked mask for that row. L1 / R1 re-enter the screen with the next member (`grid_reset`).

**List.** Two label rows (0 and 1) followed by every occupied stock slot (id nonzero, quantity > 0) in stock order, 192 rows at most (so 190 elements). Rows 0 / 1 read Remove All / Remove; Right on the list (ignored while a cell is picked) shows the second pair, Auto Allocate / Fill Blanks; Right again moves to the grid; Left from the second pair returns to the first; Left from the first pair moves to the grid. 11 rows visible, Up / Down without wrap, Triangle opens Reorder.

**Focus model.** Focus is the list (0) or the grid (1). One pick is held at a time:
* An element or Remove picked on the list waits for a cell (`pick_element`). The cursor goes to the grid on the element's level row clamped into the character's unlocked rows (first open row .. deepest open row; sfx 0xC when the row changes); if the member may not hold the element, or no unlocked row lies within its reach, the pick is not taken (sfx 1 only). Reach is the low nibble of record byte +0x12. Remove and the row past the end of the list hold no element: they go to the grid on the current row and accept any cell.
* A cell picked on the grid first waits for a list row (`pick_cell`). On the list, OK with Remove or an element applies it to that cell (the label rows Remove All, Auto Allocate and Fill Blanks are refused); Left takes the picked cell back to the grid as a swap source (`swapping`); Cancel drops the pick.
* Swap: OK on a second cell swaps the two (`swap`); Cancel drops the swap and stays on the grid.
* Grid keys: Up / Down move along the row to the next unlocked cell (wrap); Left / Right change to the next lower / higher row that has an unlocked cell, wrapping. Wrapping past the bottom or top row moves to the list instead, unless an element pick or a swap is pending. A row change plays sfx 0xC. While an element is held (list pick, swap source or picked cell), a move onto a row outside its placement window is ignored. Cancel on the grid drops a swap, or a list pick (back to the list), else returns to the list. Cancel on the list returns to the main list on row 1 (to the roster if a stand-in is active).
* Applying (`apply_to_cell`, sfx 0x17 first): Remove empties the cell (on an empty cell it changes nothing and still hands the focus back); an element replaces the occupant, which goes back to the stock. An element that fails the window, eligibility or stock check plays sfx 0x18 and keeps the pick; a fixed occupant never gets this far, OK on a fixed cell is refused on the grid. When the element came from the list the focus returns to the list, when the cell came first it returns to the grid.

**Placement legality** (`can_place`, first failing check wins): no such cell; cell locked; occupant fixed; id is not an element (1..0x7F with a record) or the character may not hold it; outside the placement window; no copy in stock (the cell's own occupant counts back when it is the same element).
* **May hold.** The element's menu eligibility bit for the character (ids 0..47, `menu_pg` bytes 0..5; ids past the table are open to all), and for innate-only elements (record byte +3 bit 0x80) the element colour must equal the character's innate colour exactly.
* **Fixed.** Id >= 0x80 (techs and special cells), or element record byte +1 bit 0x02. Fixed cells cannot be picked, swapped, replaced or removed.
* **Placement window.** The element of level L fits row r (0-based) when `L - hi <= r + 1 <= L + lo`, `hi` and `lo` being the high and low nibble of record byte +0x12.
* **Stacks.** An element whose record byte +0 has `(b & 0x30) == 0x10` is a consumable stack: placing takes up to 5 copies (kStackMax) from the bag, as many as exist, and stores the count in the cell; removing or replacing returns the stored count (at least 1). Other elements use one copy.
* **Swap rules.** Both cells exist, are unlocked and are different; neither holds a fixed element; each element must satisfy the window of the other cell's row. An empty cell may take part. Counts travel with the ids. (Picking the same cell twice is treated as success, no change.)
* **Remove** (`can_remove`) fails on an empty cell, a fixed cell (checked before locked) and a locked cell.

**List dimming.** A listed element is dimmed when the character may not hold it; when a cell is picked and its row is outside the element's window (not while swapping); or when the element's reach (level row +- low nibble) misses every row between the first and deepest unlocked row of the character.

**Prompts** (question in the description panel; Cancel dismisses, sfx 6):
* Remove All: Yes / No (Left / Right), default No; Yes removes every element with 0 < id < 0x80 from unlocked cells of the member (fixed ones stay), sfx 0x17.
* Auto Allocate / Fill Blanks: choice "this member" (default) or "whole party" (Up / Down). The whole party is refused (sfx 0x18) while a stand-in is active. Auto Allocate clears the member(s) first, Fill Blanks only fills empty cells.
* Reorder (Triangle on the list): three orders, cursor starts on the first (Up / Down wrap); the stock is repacked from slot 0 with quantities kept.

**Reorder orders** (stable sort by group, then key):

| Order | Groups |
|---|---|
| Innate first | ids 1..0x30: group 0 if the element colour intersects the opposite colours of the member's innate colour (key id), else group 1 if it has any other colour (key id), else group 2 (key stock slot); all other ids group 3 (key id) |
| Attack first | ids 1..0x30 group 0 by descending id; the rest group 1 by id |
| Heal first | 0x80 group 0; 0x55..0x6B group 1 by id; 0x31..0x54 group 2 by id; 1..0x30 group 3 by descending id; 0x6C..0x7D group 4 by descending id; rest group 5 by id |

The opposite colour pairs are bits 0x04 <-> 0x40, 0x08 <-> 0x10, 0x20 <-> 0x80; the member's innate colour is the innate bit masked with 0xFC.

**Auto-allocate algorithm.**
1. Buckets: stock slots (first 190 occupied) with ids below 0x80, bucketed by element level (bucket = level - 1), in stock order.
2. Two passes, exact level first (`fallback` false), then fallback; each pass walks rows 7 down to 0, and each row for every member in party order (one member for "this member"), so members share the stock in that order.
3. For each empty unlocked cell, left to right: sort the row's bucket by score, descending (exchange sort, an entry moves forward only for a strictly higher score), then take the first stock slot that passes: id nonzero, member may hold it, element fits the cell row's window, stock quantity >= 1 (>= 5 for a stack), and fewer than 2 copies of it already placed on that member. The fallback pass also searches the buckets of rows +1, -1, +2, -2 ... around the cell row.
4. Score of an element: empty slot -200; two or more copies already placed -100; otherwise the `menu_pg` bonus for 0 copies (byte 0x0C) or 1 copy (byte 0x0D) placed, then -10 if the element colour intersects the opposite colour, else +10 if it intersects the member's innate colour (members whose innate colour has no single opposite get neither).

**Timing locks** (menu frames during which grid input is ignored): after opening, Remove All, Auto Allocate, Fill Blanks and Reorder 27 (kLockOpen); list to grid 18 (kLockToGrid); cancelled cell pick 9; list-side fill 17; grid-side fill 15; swap 10; column move 9; grid to list 4; row change 40 (kLockRow: a 30-frame slide, 15 PS1 px per frame out then in); a row change started from the list adds the 3-frame hand-over (kHandOver).

## 6. Use element from the menu

Source: `main_menu_elements.cpp` (`update_use`), `game_state.cpp` (`use_element`).

* **List filter.** Stock slots with quantity > 0 and id > 0 whose `menu_pg` byte 0x0B has bit 0x80 (usable). Up / Down without wrap, 11 rows visible, Cancel returns to the main list on row 1.
* **Target roster.** OK on a row opens the target choice over `party()` (Up / Down wrap, at most 3); Cancel returns to the list.
* **Heal** (`formats/battle_data.md` 5.1): heals the listed amount, capped at max HP. Refused when `cur_hp >= max_hp`: sfx 0x18, nothing consumed.
* **Cure:** the mask is the low byte of the status field of the element record (+0x18). Refused when `(status & mask) == 0`; otherwise those bits are cleared from the character's status byte.
* **Result.** On success one copy is consumed (sfx 8). When the last copy goes, the focus returns to the list with cursor and scroll reset; otherwise the target choice stays up. Nothing checks whether the target is at 0 HP.

## 7. Key items

Source: `main_menu_equip.cpp` (menu list), `keyitem_popup.*` and `game/field_world_party.cpp` (field list and use).

* **Menu list** (Items row). The owned ids in ascending order (ids 1..64 from the 64-bit key-item bitmap), 15 rows visible, Up / Down without wrap, Cancel returns to the main list on row 3. It is read-only: it shows the picture `menu/keyitem/NN` and the three description rows of the key-item table; nothing can be used here.
* **Field list** (Item key / pad West, no menu, battle or script active, field idle). A separate panel of 64 slots packed in the display order of a 64-entry id table read from the executable (ascending order when the table is missing), 2 columns by 6 rows, scroll 0..26 row pairs. Input starts on the 15th active frame. Left at column 0 and Right at column 1 continue on the previous / next row; Up / Down scroll at the edges; no wrap. Names come from the key-item system-message table of the text language.
* **Refused rows.** An empty slot, or a disabled row: ids 1 (Tele-Porter), 61 (Relief Charm) and 63 (Smith Spirit) are enabled only while script variable 0x46 == 1 and variable 0x3C == 1. Refusal is sfx 0x18; Cancel (or the list button) closes with sfx 6.
* **Using an item** closes the list (sfx 8) and writes variable 0x2A (USED_KEY_ITEM) = 0x400 + id; the room script reacts. Ids other than 1, 61, 63 only set the "key item shown" flag. Id 1 requests the party screen, 61 the Relief Charm screen (argument 1), 63 the smith shop with the shop id taken from variable 0x4A. A request while another menu is open is dropped.

## 8. Shops

Source: `shop_logic.cpp` (rules), `shop_screen.cpp` (flow).

Script requests: ElementShop opens the element shop, TradingPost the trading post, WeaponShop the blacksmith (forge, break down, sell); the Smith Spirit key item opens the blacksmith too. The argument is the shop row id. Sounds: 1 cursor, 8 confirm, 6 cancel, 0x18 denied, 5 bought or sold, 0xBF forged or broken down, 0x1B traded. Gold gains from selling and breaking down are clamped to 9999999 (kMaxGold) without a refusal.

**Flow.** Top list (Element: Buy, Sell; Smith: Forge, Break down, Sell; Up / Down wrap; Cancel closes the shop). Smith goes through a category list (weapon, armour, accessory = item kind 0, 1, 2). Then the item list (wrap, 11 rows visible, Cancel one level up), then quantity (Buy, Sell element, Sell gear) or a Yes / No confirm (Forge, Break down; default No). An empty list on entering is refused (sfx 0x18). Quantity: Right / Left +-1, Up / Down +-10, clamped to 1..max without wrap; OK commits at once (no confirm). After a change the list is rebuilt and the flow falls back one level when the list is empty.

**Element shop** (price: `formats/battle_data.md` 5.1, clamped >= 0):
* Buy list: the shop row (`el_shop`, ids up to the first 0). The Cheats option "every element in shops" appends every other sellable element with price > 0, by id.
* Buy max = `min(99 - owned, gold / price)`. OK on a row is refused if the price exceeds gold or the max is below 1. Buying checks owned + qty <= 99 (Full), `price * qty <= gold` (NoGold) and a free stock slot (else rolled back, Full). Cost = `price * qty`. A price of 0 cannot be bought.
* Sellable: the `menu_pg` no-shop flag clear (5.1), id > 0, not fixed. Sell list: bag elements in slot order, then elements that are only placed in some roster member's removable grid cells.
* Sell max = bag copies + removable placed copies over the whole roster. Selling takes bag copies first, then empties placed cells (a stack cell returns its copies to the bag first) and takes from the bag. Proceeds are `(price * qty) >> 2`, computed on the total, not per unit.

**Blacksmith.**
* Forge list: the shop's `waa_shop` row filtered to the category; the Cheats option widens it to every craftable, non-dummy (price > 0) piece of that category.
* Recipe: the nonzero counts for materials 1..31 in the forge table. Forge needs every count in the bag (else "short of materials", one message per category) and gold >= cost (else "not enough gold"); no recipe is silent. Cost: `formats/battle_data.md` 11.3. A forge is refused at 99 held. It takes the materials, adds the piece, then deducts gold. If the bag has no free slot for the piece the materials are already consumed and no gold is taken. The Cheats option "no crafting cost" skips the material and gold checks and takes nothing.
* Break down list: bag or worn gear of the category without item-info bit 0x20 that has a recipe. Refused (message) when only worn copies exist (bag count 0). It removes one bag copy and returns each recipe material one unit at a time; a unit that no longer fits (the material at 99 or no free slot) is paid as gold (11.3), summed per material as `price * over / 4` in integer arithmetic and shown in a second message line when the total is above 0.
* Sell list: bag gear of the category without item-info bit 0x80. Max = bag count (worn copies are never sold). Unit price: 11.4; proceeds = unit * qty. Materials (kind 4) are refused.

**Trading post** (own three-stage loop over the shop's `trade` row; `battle_data.md` section 8 for the row layout).
* Stage 0: greeting; OK enters the goods list, Cancel closes.
* Stage 1: the goods list: the row's entries up to the first goods id 0; an entry with level sum 0 is a hole (no goods, not counted). Up clamps at 0, Down at count - 1, no wrap. Names are prepared one row per frame and input waits until the list is ready. OK enters the offer, Cancel returns to the greeting.
* Offer list: every element stock slot with a nonzero id and without the `menu_pg` no-shop flag (192 rows at most, at least 11 rows), cursor row 0..10, scrolling by one; Down never moves onto an empty row. Rows the entry's colour mask refuses are dimmed. Cancel returns to the goods list (sfx 6) and the offer is cleared on the next visit.
* A press of OK (auto-repeating while held; sfx 8 on every press and repeat) adds one copy of the row's element to the offer when the entry's colour mask is 0 or shares a bit with the element's colour, the offered level sum is still below the entry's level sum, and the offered count differs from the stock count of that element. Level sum = the sum of element level * offered copies.
* When the sum reaches the required level sum the "Make the exchange?" question opens (default Yes; Up / Down toggle). OK is accepted only more than 30 frames after the question opened. Yes takes every offered copy from the stock and adds one unit of the goods item (sfx 0x1B); No (sfx 8) and Cancel (sfx 6) discard the offer. A single copy may overshoot the required sum. The offer list is not rebuilt after a trade.
* The goods are added with the normal stack add: at 99 held, or with no free slot, the elements are still taken and the goods are lost.

## 9. Name entry

Source: `ui/title/name_entry.cpp`, `name_codec.cpp`.

* **Keyboard.** Non-JP languages: a 10 x 14 Latin grid (upper and lower case, accented letters, `ß`, `#`, digits, `+-*@<>()[]:;,.!?` and inverted marks); a blank cell inserts a space. JP: three 10 x 10 pages (katakana, hiragana, alphanumeric) selected by buttons; blank is U+3000. Nothing else can be typed (no text input from the keyboard).
* **Length.** `max_name_length`: 12 on the Latin keyboard; 6 in JP, 4 for Viper (character id 5). The buffer is always that many cells, blank padded.
* **Navigation.** Up / Down / Left / Right move without wrap inside the grid; leaving the grid at an edge goes to the button column, Left from the buttons returns to the last grid column. The button column wraps. The Latin layout starts in the grid on cell (0, 0); the JP layout starts on the button column at the active page. L1 / R1 move the text cursor.
* **Entering.** OK in the grid writes the cell at the cursor and advances unless on the last cell (which is overwritten repeatedly). Cancel is Backspace: it blanks the cell at the cursor, then moves left. There is no way to leave the screen without a valid name. Pad Start / Space accepts the name from anywhere, as the Confirm button.
* **Buttons.** Latin: Change Name (back to the grid), Confirm, Default. JP: the three page buttons, Confirm, Default. Default restores the character's default name for the language (system-message table), padded to the length.
* **Rejection.** Trailing U+0020 and U+3000 are trimmed. The name is rejected, with a 0.45 s flash and sfx 0x18 and the screen stays, when it is empty or equals クロノ, くろの, "chrono" or "crono" (Latin letters case-insensitive). The reserved list applies to every character. Only trailing blanks are trimmed: leading blanks stay and count as part of the name.
* **Initial text.** The existing custom name of the character if there is one (rename), else the default name.
* **Storage.** The typed name is kept whole (UTF-32 in the flow's custom names, saved to `ext_save.dat` / the page file). The save record gets only what the game font can encode, at most 6 symbols (`pack_record`); characters the font lacks are skipped. On load, an `ext_save.dat` name replaces the record's when the languages match. A name counts as unset when empty, all blanks, containing an undecodable code, or (outside JP) containing a code point at or above U+3000.

## 10. Save and load screens

Source: `main_menu_save.cpp`; title side `ui/title/slot_list.*`.

**In-game flow** (`save_phase`):

| Phase | Screen |
|---|---|
| 0 | Save / Load choice. Up / Down toggle only when saving is allowed; OK on Save when blocked is refused. Cancel returns to the main list on row 5. |
| 2 | Target choice: OpenCross, Slot 1, Slot 2 (Up / Down wrap). Starts on OpenCross when the page holds a file, else on Slot 1. Cancel returns to phase 0. |
| 3 | "No save data" (Load on an empty target); confirm or 120 frames returns to phase 0. |
| 1 | The file grid or list. Cancel returns to phase 0. |

* **Save allowed.** The menu opens with saving allowed when script variable 0x3C == 1, and always from a script save point. Having used the debug tools only shows a warning line (a toast at a save point); it never blocks a save.
* **Card grid.** 15 boxes (8 + 7), one per file in the card's directory order (not by file number); each box shows its file's number. Files whose number is 15 or above are not shown. Left / Right step with wrap (Save on card 1 wraps within boxes 1..14); Up / Down jump 8 boxes (boxes 7 and 8 stay). Box 0 of card 1 is the auto-save: loadable, never a manual save target. Load starts on the first used box, Save on box 1 of card 1 (box 0 on card 2).
* **Page list.** 101 rows (0 = the page's auto-save, 1..100), 10 visible (kPageRows). Up / Down wrap one row, Left / Right move 10 rows without wrap. Row 0 is not a save target. Load starts on the first used row, Save on row 1.
* **Overwrite confirm.** OK on a used box or row opens Yes / No with the cursor on No (`confirm_cur` 1); any direction toggles; Cancel dismisses. The same prompt guards a load. OK on an empty box saves at once; OK on an empty box in Load is refused (sfx 0x18).
* **Where a save goes.** A used box overwrites its own file. An empty box creates a new file with the lowest unused number from 0..14 (0 is reserved on card 1). With no free number nothing is written (sfx 0x18 only). The card save also writes the `ext_save.dat` record (language, battle counter, custom names; `formats/save.md` section 9) and rewrites the whole `save.dat`. A page save writes the numbered file with the same extras. A full card or write failure shows the failure message and plays sfx 0x18; success plays sfx 5 and shows the saved message. The result sets `saved`, which scripted menus return as variable 0x4C = 1.
* **Load.** A confirmed load sets the load request and closes the menu; the host takes the request (card, slot, block, data) and starts the slot like Continue (fade 0.5 s, then section 13 flow).
* **Location caption.** Keyed by script variable 0 (head byte 0x40, u16): the key is the number of the 30 thresholds `{0, 8, 16, 24, 26, 28, 30, 36, 52, 64, 72, 80, 88, 92, 100, 108, 112, 116, 124, 128, 140, 144, 156, 164, 172, 184, 195, 202, 209, 999}` that the value has reached, at least 1; the caption has two lines.
* **Corrupted data.** The card read path decodes the block without testing the checksum or the save version (not modelled; `formats/save.md` section 3). A block that fails structurally (wrong size, packed stream overrun) is left out of the grid with a warning; a failing file on the title list is likewise skipped. Page files that fail validation are skipped at boot (`formats/save.md` section 13). Directory entries are accepted with a Japanese or US game name and a two-digit number.
* **Title Continue lists.** The choice (OpenCross / Slot 1 / Slot 2) and the page list behave as above. The card grid holds one box per readable file in directory order with no number limit and no auto-save rule; Left / Right wrap over the files, Up / Down jump 8 boxes when that box exists. A file is picked, not confirmed: OK starts the load (fade), Cancel on the grid or page list returns to the choice, Cancel on the choice leaves to the title with the cursor on Continue. An empty target shows "No save data" (120 frames or confirm). Continue+ is section 13.

## 11. Party editors

Source: `game/party_editor.h`, `ui/party_screen.cpp`; flow fallback `game_flow.cpp`.

**Grid** (shared): the recruited characters that are not in the exclusion bitmap (words 2..3 of the lock block, which holds two lock words then two exclusion words), ascending ids, at most 44, 4 columns, 5 visible rows, scroll 0..6 rows. Left / Right wrap over the columns, Up / Down move the row and scroll at the visible edge, sfx 1 on every press including the ends. A scroll takes 17 frames of 2 px; input is ignored meanwhile.

**Party editor** (script party screen / Tele-Porter, 3 slots). Members are packed on entry (empties and duplicates dropped). A member that is not in the grid (excluded) keeps its slot.
* Slot mode: Left / Right wrap over 3 slots. OK on an empty slot, or on a slot whose member is not locked, enters pick mode (sfx 8); a locked member is refused (0x18). Cancel leaves (sfx 6) unless a slot is empty while the number of filled slots differs from the grid size (sfx 0x18).
* Pick mode: OK on a character who is not in the party puts it into the slot (it replaces the slot's member); on a character of another slot swaps the two and moves the slot cursor to that other slot (both sfx 8, the grid stays open); on the slot's own character or an empty cell is refused. Cancel returns to slot mode.
* Lock bitmap: bit c of the two lock words means character c cannot be taken out of its slot; the party-screen path passes it, the flow fallback (used when the party screen is unavailable) passes none and lists every recruited character, excluded ones included.
* Leaving compacts the party (empties dropped, order kept). The host writes it to the state (0xFF for empty) and rebinds the party entities and models; the party screen first runs a 0x40-frame closing fade, the fallback closes at once.

**Relief editor** (Relief Charm). Grid only. OK on a character other than the current relief member sets it (sfx 8, the screen stays and the cursor stays); on the current relief member or an empty cell sfx 0x18; Cancel always leaves (sfx 6). The relief member cannot be cleared here. The stored member is head byte 0x59F; on entry it is cleared when it is 0x2C or above or not recruited. The cursor starts on cell 0 and the shown character starts as the relief member, else character 0.

## 12. Pause screen

Source: `ui/pause_screen.*`; opening and closing: `app/app_sim.cpp`.

* **Rows.** PAUSE bar; "Vibration Function: On / Off" always; "World Names: On / Off" only when the row condition holds.
* **World Names condition.** The field world is up, script variable 0 (scenario) >= 0x34 and variable 0x50 (map has a world name) is nonzero. The value starts from variable 0x52 != 0 and is written back to variable 0x52 (1 / 0) when the screen closes, only if the row was shown.
* **Input.** Left / Right (or OK) flip Vibration (sfx 1); Up / Down flip World Names when its row is shown (sfx 1). Start (Space / pad Start), Cancel or pad East close it (sfx 6). Vibration is written to options bit 0x400 every frame; switching it on gives a test rumble pulse.
* **Opening.** In the field: Start when `FieldWorld::pause_allowed()` and the menu is closed. Over a running battle: Start at any time except while the battle transition is active. Over a movie: Start. The keyboard's Space opens it on the press; the pad's Start opens it on release of a press shorter than 500 ms, a longer press opens the settings window instead.
* **Where pause is refused** (`pause_allowed`): during room init, a room transition or a jump; when the room's pause flag is clear (global flag kPauseAllowed, set at every room load and cleared by script) or the low byte of variable 0x36 is nonzero; on maps 8 and 9; while a menu, movie or battle hand-off is in progress. Open windows, choices, running cutscenes and the input lock do not refuse it.
* **What it freezes.** The pause screen takes all input: the field, battle, results and movie simulation stand still. The play-time counter is advanced before the pause check.
* **Tint.** A counter 0..479 advances by wall time at 60 per second and is kept across openings. `ramp = counter > 0xEF ? 480 - counter : counter`; `t = ramp / 240 * 0.53`; the picture is multiplied by `(1 - t, 0.47, 0.47 + t)`.

## 13. Title screen flow

Source: `game/game_flow.*`, `ui/title/title_screen.cpp`, `nav.h`.

* **Phases.** Fade in, "Press any button" prompt, menu, fade out (0.5 s each). With the title room behind (scene mode) the screen starts at the prompt. Any input (Confirm, Cancel, shoulder buttons, any key except Esc and F1..F12, any pad button except the stick clicks) leaves the prompt through black into the menu (sfx 8). Navigation repeats after 0.4 s every 0.08 s (digital directions only).
* **Rows.** New Game, Continue, Settings, Quit (Up / Down wrap). Settings reports at once and opens the options window. Quit fades and quits.
* **Continue enabled rule.** The row is enabled when the OpenCross page holds a file or either card holds a readable slot (`SlotList::any`, re-evaluated each time the title is entered). Disabled: sfx 0x18. Cancel on the menu returns to the prompt through black.
* **Vibration page.** New Game opens a page with On and Off (cursor on On; Up / Down toggle). OK stores the choice, reports New Game and fades out; Cancel returns to the row list on row 0. The choice becomes options bit 0x400 of the new game state; Continue never touches the options word.
* **New Game.** After the fade: the user guide (below), then name entry for character 0 (Serge) with the language's default name. A validated name starts a fresh game (Serge, Kid and a random third member from a 14-entry table; gold and stars per `new_game.h`; location room 22), plays movie row 2 and loads `opn_a01i` with room param 0.
* **User guide.** Two pages (controller, then keyboard) of baked images. Shown at every New Game, and on a script name entry (opcode FE 4A) when script variable 6 (MAP_ID) is 0 or 8. Skipped when the page images are missing. A 45-step counter at 60 steps per second runs the fade-in of page 1, a wait, the fade-in of page 2 over it, a wait and the fade-out of page 2 (15 steps each). Each wait ends on any key press except F1..F12, or on the frame the pad goes from some button or stick held to none (stick clicks and Start excluded).
* **Continue.** Slot list (section 10); a pick fades the screen out for 0.5 s and then restores the state: the state is replaced by the slot, the Time Shifter key item is granted, record names that are usable and `ext_save.dat` names (same language only) become the custom names, the location word selects the room (a word of 0 with no room name logs a warning and loads no room; a nonzero id is looked up in the disc index and returns to the title when no room is found), the saved leader position (script variables 0x12, 0x14) is restored and the room parameter is variable 0x3A. Cancel on the choice returns to the title menu with the cursor on Continue.
* **New Game+ variant.** The ending room's script (FE 84 + FE 61) shows variant 2 over the cleared game: rows "New Game +" and "Continue +" only, straight into the menu with no prompt and no cancel. New Game+ goes through the vibration page, then replaces the state in place (carry-over per `formats/save.md` section 14) and the ending script goes on. Continue+ opens the slot list in plus mode: a slot whose two summary flag bits are set (bit 0 completed, bit 1 already New Game+) is refused with sfx 0x18; a pick carries the cleared game into the loaded slot after the fade; the custom names stay those of the cleared game. Cancel on the Continue+ choice returns to the variant 2 list on Continue +.
