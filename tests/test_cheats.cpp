// Cheats: the [Cheats] ini round trip (all off by default), the first-launch language mapping and RDE's encounter tables.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "game/cheats.h"
#include "platform/config.h"

using namespace oc;

namespace {
int failures = 0;
#define CHECK(x)                                              \
  do {                                                        \
    if (!(x)) {                                               \
      std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); \
      ++failures;                                             \
    }                                                         \
  } while (0)
}  // namespace

int main() {
  // --- ini: every toggle defaults off, keeps its value through save / load, and a key is only set when asked
  platform::Config config;
  const platform::Cheats& cheats = config.cheats;
  CHECK(!cheats.no_encounters && !cheats.battle_boost && !cheats.auto_battle && !cheats.god_mode && !cheats.no_clip && !cheats.max_stamina && !cheats.free_elements &&
        !cheats.always_hit && !cheats.max_money && !cheats.shop_all_elements && !cheats.shop_all_weapons && !cheats.run_speed && !cheats.always_loot && !cheats.instant_growth && !cheats.no_craft_cost);
  config.cheats.no_encounters = config.cheats.no_clip = config.cheats.always_loot = config.cheats.no_craft_cost = config.cheats.shop_all_weapons = true;
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "oc_test_cheats.ini";
  CHECK(config.save(path));
  platform::Config reloaded;
  CHECK(reloaded.load(path));
  std::filesystem::remove(path);
  CHECK(reloaded.cheats.no_encounters && reloaded.cheats.no_clip && reloaded.cheats.always_loot && reloaded.cheats.no_craft_cost && reloaded.cheats.shop_all_weapons);
  CHECK(!reloaded.cheats.god_mode && !reloaded.cheats.max_money && !reloaded.cheats.instant_growth);

  // --- [Ui] settings window geometry: default -1, round trip
  platform::Config ui_config;
  CHECK(ui_config.settings_x == -1 && ui_config.settings_y == -1 && ui_config.settings_w == -1 && ui_config.settings_h == -1 && ui_config.settings_tab.empty());
  ui_config.settings_x = 120, ui_config.settings_y = 80, ui_config.settings_w = 900, ui_config.settings_h = 640, ui_config.settings_tab = "Cheats";
  const std::filesystem::path f2 = std::filesystem::temp_directory_path() / "oc_test_ui.ini";
  CHECK(ui_config.save(f2));
  platform::Config ui_reloaded;
  CHECK(ui_reloaded.load(f2));
  std::filesystem::remove(f2);
  CHECK(ui_reloaded.settings_x == 120 && ui_reloaded.settings_y == 80 && ui_reloaded.settings_w == 900 && ui_reloaded.settings_h == 640 && ui_reloaded.settings_tab == "Cheats");

  // --- first launch language: en fr it de es -> same, ja -> jp, anything else en; the first supported preference wins
  using platform::language_from_locales;
  CHECK(language_from_locales({"fr"}) == "fr" && language_from_locales({"de-DE"}) == "de" && language_from_locales({"ES"}) == "es");
  CHECK(language_from_locales({"ja"}) == "jp" && language_from_locales({"it_IT"}) == "it" && language_from_locales({"en"}) == "en");
  CHECK(language_from_locales({"pt", "fr"}) == "fr");
  CHECK(language_from_locales({"zh"}) == "en" && language_from_locales({}) == "en");

  // --- Auto-won encounters, table A (no story byte known: -1)
  const game::StoryByte none = [](unsigned) { return -1; };
  using game::encounter_auto_won;
  // An ordinary A3 is won; so are id 0 and ids >= 0x57.
  CHECK(encounter_auto_won(0x100, 4, 0x20, false, none));
  CHECK(encounter_auto_won(0x100, 4, 0, false, none));
  CHECK(encounter_auto_won(0x100, 4, 0x57, false, none));
  // Map 0x213 always fights; id 3 at map 0x110 entity 0x16 runs but is won elsewhere; id 0xC at map 0x9E fights for any entity.
  CHECK(!encounter_auto_won(0x213, 4, 0x20, false, none));
  CHECK(!encounter_auto_won(0x110, 0x16, 3, false, none));
  CHECK(encounter_auto_won(0x110, 0x15, 3, false, none));
  CHECK(!encounter_auto_won(0x9E, 1, 0x0C, false, none));
  CHECK(!encounter_auto_won(0x32, 4, 1, false, none) && encounter_auto_won(0x32, 5, 1, false, none));
  // With the story byte's bit 2 set the fight is always won.
  CHECK(encounter_auto_won(0x32, 4, 1, false, [](unsigned address) { return address == 0x800EBC68 ? 4 : -1; }));
  CHECK(!encounter_auto_won(0x1A0, 8, 0x3B, false, none) && !encounter_auto_won(0x1A0, 0x0A, 0x3B, false, none));
  // An unknown story byte lets the fight run.
  CHECK(!encounter_auto_won(0x50, 1, 0x42, false, none));
  CHECK(!encounter_auto_won(0x50, 1, 0x42, false, [](unsigned address) { return address == 0x800EBB84 ? 1 : address == 0x800EBB6C ? 2 : -1; }));
  CHECK(encounter_auto_won(0x50, 1, 0x42, false, [](unsigned address) { return address == 0x800EBB84 ? 0 : address == 0x800EBB6C ? 2 : -1; }));
  // The story bytes come from the VM's global variable array (ids = byte offsets from its base).
  {
    std::vector<u16> vars(0x400, 0);
    CHECK(game::story_byte_from_vars(vars, 0x800EBB84) == 0 && game::story_byte_from_vars(vars, 0x800EBAA7) == -1 &&
          game::story_byte_from_vars(vars, 0x800EBEA8) == -1);
    // The low byte sits at the even address, the high byte at the odd one.
    vars[(0x800EBB84 - 0x800EBAA8) / 2] = 0x1234;
    CHECK(game::story_byte_from_vars(vars, 0x800EBB84) == 0x34 && game::story_byte_from_vars(vars, 0x800EBB85) == 0x12);
    const game::StoryByte fromvars = [&](unsigned address) { return game::story_byte_from_vars(vars, address); };
    // Id 0x42 at an ordinary map: story byte A = 1 and B = 2 -> the story battle runs; A = 0 -> won (skipped)
    vars[(0x800EBB84 - 0x800EBAA8) / 2] = 1, vars[(0x800EBB6C - 0x800EBAA8) / 2] = 2;
    CHECK(!encounter_auto_won(0x50, 1, 0x42, false, fromvars));
    vars[(0x800EBB84 - 0x800EBAA8) / 2] = 0;
    CHECK(encounter_auto_won(0x50, 1, 0x42, false, fromvars));
    // Id 0x01: a clear bit 2 of its story byte keeps the fight at map 0x32 / entity 4, a set one wins it
    CHECK(!encounter_auto_won(0x32, 4, 1, false, fromvars));
    vars[(0x800EBC68 - 0x800EBAA8) / 2] = 4;
    CHECK(encounter_auto_won(0x32, 4, 1, false, fromvars));
    // Id 0x45 depends on bit 6 of its story byte; table B (model swaps) on bit 7 of another.
    CHECK(encounter_auto_won(0x50, 1, 0x45, false, fromvars));
    vars[(0x800EBC6E - 0x800EBAA8) / 2] = 0x40;
    CHECK(!encounter_auto_won(0x50, 1, 0x45, false, fromvars));
    CHECK(!game::model_swap_skipped(0x9E, 5, fromvars));
    vars[(0x800EBBB0 - 0x800EBAA8) / 2] = 0x80;
    CHECK(game::model_swap_skipped(0x9E, 5, fromvars));
  }
  // C9: only 0x180 0x181 0x187 0x1B3 are won
  CHECK(encounter_auto_won(0x50, 1, 0x180, true, none) && encounter_auto_won(0x50, 1, 0x1B3, true, none));
  CHECK(!encounter_auto_won(0x50, 1, 0x182, true, none));

  // --- Table B (D9): needs bit 7 of its story byte
  const game::StoryByte b7 = [](unsigned address) { return address == 0x800EBBB0 ? 0x80 : -1; };
  // Unknown story byte: never skipped.
  CHECK(!game::model_swap_skipped(0x9E, 5, none));
  CHECK(game::model_swap_skipped(0x9E, 5, b7) && game::model_swap_skipped(0xAB, 9, b7));
  // Steps below 4 are never skipped.
  CHECK(!game::model_swap_skipped(0x9E, 3, b7));
  CHECK(game::model_swap_skipped(0x99, 5, b7) && !game::model_swap_skipped(0x99, 0x0C, b7));
  CHECK(!game::model_swap_skipped(0xAA, 0x0E, b7) && game::model_swap_skipped(0xAA, 6, b7));
  // Other maps: never skipped.
  CHECK(!game::model_swap_skipped(0xA0, 6, b7));

  if (failures) return 1;
  std::printf("test_cheats: ok\n");
  return 0;
}
