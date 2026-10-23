#pragma once
// Cheats (Esc overlay "Cheats" tab): the toggles live in platform::Cheats ([Cheats] in opencross.ini, all off by default, saving
// is never locked by them). This file holds the tab, the battle-side translation and the RDE "encounters off" tables.
#include <functional>
#include <span>

#include "battle/battle.h"
#include "platform/config.h"

namespace oc::game {

// The live set as the battle core's bypass flags (BattleView pushes it into Battle::set_cheats every update).
battle::Battle::Cheats battle_cheats();

// The tab body (call inside the registered tab's draw). Returns true when a toggle changed (the caller saves the ini).
bool draw_cheats_tab(platform::Config& cfg);

// ---- RDE "encounters off". `story(addr)` reads a byte of the story-flag memory by its original console address and returns -1 when
// the port does not know it; an unknown byte makes the rule err on the side of the fight.
using StoryByte = std::function<int(unsigned addr)>;
// The story bytes the tables read are script variables, not save-image bytes: the field VM keeps the global variable array (ids =
// byte offsets from the array's base address, saved with the game). `vars` = FieldVm::variables(); an address in the 0x400-byte
// window from the base is the low byte (even address) / high byte (odd) of slot (addr - base) / 2; anything else returns -1.
int story_byte_from_vars(std::span<const u16> vars, unsigned addr);
// A3 / C9 with the op's raw battle id `id` (operand & 0x7FF) in `map` (the script / save map id) run by entity `ent`: true = RDE
// would patch the op into the auto-win (the A3 exception table, the C9 id list).
bool encounter_auto_won(int map, int ent, int id, bool c9, const StoryByte& story);
// Op D9 (character model swap) skipped while encounters are off (needs a particular story flag bit set).
bool model_swap_skipped(int map, int ent, const StoryByte& story);

}  // namespace oc::game
