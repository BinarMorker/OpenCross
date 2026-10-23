// Menu rule tests on synthetic data: grid allocation legality, stock accounting, auto-fill, the three sort orders,
// equipment compatibility / stat previews, element use and the options word.
#include <cstdio>

#include <cmath>
#include <string>
#include <vector>

#include "ui/menu/game_state.h"
#include "ui/menu/hand.h"
#include "ui/menu/ps1_art.h"

using namespace oc;
using namespace oc::ui;

namespace {
int failures = 0;
#define CHECK(x)                                              \
  do {                                                        \
    if (!(x)) {                                               \
      std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); \
      ++failures;                                             \
    }                                                         \
  } while (0)

struct Fixture {
  data::GameTables tables;
  MenuRules rules;
  GameState gs;

  void el(int id, int level, int colour, u8 window, u8 flag0 = 0x01, u8 flag1 = 0, u8 flag3 = 0) {
    auto& element = tables.elements[static_cast<size_t>(id)];
    element.raw[0] = flag0;
    element.raw[1] = flag1;
    element.raw[3] = flag3;
    element.raw[5] = static_cast<u8>(colour);
    element.raw[6] = static_cast<u8>(level);
    element.raw[0x12] = window;
    // Eligible for everybody.
    rules.pg[static_cast<size_t>(id)].raw.fill(0xFF);
  }

  Fixture() {
    tables.elements.resize(0x90);
    rules.pg.resize(0x90);
    auto& char_set = tables.char_sets["char"];
    // Characters 0 and 1 share Serge's shape 03FC x3, 03F8, 01F0, 01E0 x3 (48 cells).
    const u16 masks[8] = {0x3FC, 0x3FC, 0x3FC, 0x3F8, 0x1F0, 0x1E0, 0x1E0, 0x1E0};
    for (int character = 0; character < 2; ++character)
      for (int row = 0; row < 8; ++row) {
        char_set.rec[static_cast<size_t>(character)].raw[static_cast<size_t>(0x30 + 2 * row)] = static_cast<u8>(masks[row]);
        char_set.rec[static_cast<size_t>(character)].raw[static_cast<size_t>(0x31 + 2 * row)] = static_cast<u8>(masks[row] >> 8);
      }
    char_set.rec[1].raw[1] = 0x20;
    // Character 0 keeps its innate colour in the save.
    gs.slot.chars[0].colour_bit = 0x08;
    rules.tables = &tables;
    gs.rules = &rules;
    gs.slot.set_party({0, 1, 0xFF});
    gs.slot.set_recruited(0, true);
    gs.slot.set_recruited(1, true);
    // Unlocked: row 0 columns 2..5, row 1 columns 2..3, row 2 column 2.
    gs.slot.chars[0].unlocked[0] = 0x3C;
    gs.slot.chars[0].unlocked[1] = 0x0C;
    gs.slot.chars[0].unlocked[2] = 0x04;
    gs.slot.chars[0].max_hp = 100;
    gs.slot.chars[0].cur_hp = 40;
    gs.slot.chars[0].atk = 10;
    gs.slot.chars[0].hit = 30;
    gs.slot.chars[0].def = 5;
    // 1 lv1 any row, 2 lv3 rows 2..4 only, 3 consumable stack lv1 (own row only), 4 fixed, 5 innate-colour-only (white),
    // 6 not for character 0, 7 lv2 with an auto-fill bonus.
    el(1, 1, 0x80, 0x77);
    el(2, 3, 0x80, 0x11);
    el(3, 1, 0x04, 0x00, 0x14);
    el(4, 1, 0x08, 0x77, 0x01, 0x02);
    el(5, 1, 0x08, 0x77, 0x01, 0, 0x80);
    el(6, 1, 0x20, 0x77);
    // Character 0 is excluded from element 6.
    rules.pg[6].raw[0] = 0xFE;
    el(7, 2, 0x10, 0x66);
    rules.pg[7].raw[0xC] = 5;
  }
  void stock(int id, int qty) {
    for (auto& stack : gs.slot.elements)
      if (stack.id == 0) {
        stack.id = static_cast<u16>(id);
        stack.qty = static_cast<u8>(qty);
        return;
      }
  }
};

void test_grid() {
  Fixture fixture;
  GameState& game_state = fixture.gs;
  // Ordinal mapping over the set mask bits.
  CHECK(game_state.cell_index(0, 0, 2) == 0);
  CHECK(game_state.cell_index(0, 0, 0) == -1);
  CHECK(game_state.cell_index(0, 1, 2) == 8);
  CHECK(game_state.cell_index(0, 4, 4) == 31);
  CHECK(game_state.cell_index(0, 7, 8) == 47);
  CHECK(game_state.first_open_row(0) == 0);
  CHECK(game_state.row_has_open_cell(0, 2) && !game_state.row_has_open_cell(0, 3));
  fixture.stock(1, 2);
  fixture.stock(2, 1);
  fixture.stock(3, 7);
  fixture.stock(4, 1);
  fixture.stock(5, 1);
  fixture.stock(6, 1);
  CHECK(game_state.can_place(0, 0, 0, 1) == Why::NoCell);
  // The cell exists but is not unlocked.
  CHECK(game_state.can_place(0, 0, 6, 1) == Why::Locked);
  // None owned.
  CHECK(game_state.can_place(0, 0, 2, 7) == Why::NoStock);
  // The element's eligibility table excludes the character.
  CHECK(game_state.can_place(0, 0, 2, 6) == Why::NotEligible);
  CHECK(game_state.can_place(0, 0, 2, 0x90) == Why::NotEligible);
  // Level 3 with window 0x11: rows 2..4 only.
  CHECK(game_state.can_place(0, 0, 2, 2) == Why::Window);
  CHECK(game_state.can_place(0, 1, 2, 2) == Why::Ok);
  CHECK(game_state.can_place(0, 2, 2, 2) == Why::Ok);
  // The stack goes in its own row; window 0 means exactly its level.
  CHECK(game_state.can_place(0, 0, 2, 3) == Why::Ok);
  CHECK(game_state.can_place(0, 1, 2, 3) == Why::Window);
  // Innate-only: white matches character 0.
  CHECK(game_state.can_place(0, 0, 2, 5) == Why::Ok);
  fixture.tables.char_sets["char"].rec[1].raw[1] = 0x20;
  game_state.slot.chars[1].unlocked[0] = 0x3C;
  // Character 1 is red.
  CHECK(game_state.can_place(1, 0, 2, 5) == Why::NotEligible);

  // Place a plain element: the stock drops; replacing returns the old copy.
  CHECK(game_state.place(0, 0, 2, 1) == Why::Ok);
  CHECK(game_state.cell_element(0, 0, 2) == 1 && game_state.element_count(1) == 1);
  CHECK(game_state.place(0, 0, 2, 5) == Why::Ok);
  CHECK(game_state.cell_element(0, 0, 2) == 5 && game_state.element_count(1) == 2 && game_state.element_count(5) == 0);
  CHECK(game_state.remove(0, 0, 2) == Why::Ok && game_state.element_count(5) == 1 && game_state.cell_element(0, 0, 2) == 0);
  CHECK(game_state.remove(0, 0, 2) == Why::Empty);

  // A stack takes up to 5 and stores the count; removing returns them all.
  CHECK(game_state.place(0, 0, 3, 3) == Why::Ok);
  CHECK(game_state.element_count(3) == 2 && game_state.cell_count(0, 0, 3) == 5);
  CHECK(game_state.remove(0, 0, 3) == Why::Ok && game_state.element_count(3) == 7);

  // Fixed (flag 0x200) and tech (>= 0x80) occupants can be neither replaced nor removed.
  // Cell 2 = row 0 column 4.
  game_state.slot.chars[0].grid[2] = 4;
  CHECK(game_state.can_remove(0, 0, 4) == Why::Fixed);
  CHECK(game_state.can_place(0, 0, 4, 1) == Why::Fixed);
  game_state.slot.chars[0].grid[2] = 0xA0;
  CHECK(game_state.can_remove(0, 0, 4) == Why::Fixed);
  game_state.slot.chars[0].grid[2] = 0;

  // Swap: the window of both elements is checked at the new rows.
  CHECK(game_state.place(0, 0, 2, 1) == Why::Ok);
  CHECK(game_state.place(0, 1, 2, 2) == Why::Ok);
  // Element 2 does not work in row 0.
  CHECK(game_state.swap(0, 0, 2, 1, 2) == Why::Window);
  CHECK(game_state.cell_element(0, 0, 2) == 1 && game_state.cell_element(0, 1, 2) == 2);
  fixture.stock(7, 1);
  CHECK(game_state.place(0, 0, 3, 7) == Why::Ok);
  CHECK(game_state.swap(0, 0, 2, 0, 3) == Why::Ok);
  CHECK(game_state.cell_element(0, 0, 2) == 7 && game_state.cell_element(0, 0, 3) == 1);
  CHECK(game_state.swap(0, 0, 2, 0, 2) == Why::Same);

  // Clear all returns everything; techs stay.
  CHECK(game_state.place(0, 0, 4, 3) == Why::Ok);
  // A tech in row 0 column 5; clearing returns 1, 2, 7 and the stack.
  game_state.slot.chars[0].grid[3] = 0xA1;
  CHECK(game_state.clear_all(0) == 4);
  CHECK(game_state.element_count(1) == 2 && game_state.element_count(2) == 1 && game_state.element_count(3) == 7 && game_state.element_count(7) == 1);
  CHECK(game_state.slot.chars[0].grid[3] == 0xA1);
}

void test_autofill() {
  Fixture fixture;
  GameState& game_state = fixture.gs;
  fixture.stock(1, 3);
  fixture.stock(7, 2);
  fixture.stock(2, 1);
  const int placed = game_state.auto_fill(0, true);
  // Exact levels: level 3 -> row 2, level 2 -> row 1 (two copies), level 1 -> row 0 (at most two copies per element).
  CHECK(placed == 5);
  CHECK(game_state.cell_element(0, 2, 2) == 2);
  CHECK(game_state.cell_element(0, 1, 2) == 7 && game_state.cell_element(0, 1, 3) == 7);
  CHECK(game_state.copies_placed(0, 1) == 2 && game_state.copies_placed(0, 7) == 2);
  for (int row = 0; row < 8; ++row)
    for (int column = 0; column < 10; ++column)
      // Locked cells are never filled.
      if (game_state.cell_element(0, row, column)) CHECK(game_state.cell_available(0, row, column));
  CHECK(game_state.element_count(1) == 1 && game_state.element_count(7) == 0 && game_state.element_count(2) == 0);
  // Fill-empty keeps what is there and adds nothing without stock.
  CHECK(game_state.auto_fill(0, false) == 0);
  CHECK(game_state.cell_element(0, 2, 2) == 2);
  // The recommended layout resets first.
  CHECK(game_state.auto_fill(0, true) == 5);
}

void test_sort() {
  Fixture fixture;
  GameState& game_state = fixture.gs;
  auto ids = [&] {
    std::vector<int> result;
    for (const auto& stack : game_state.slot.elements)
      if (stack.id) result.push_back(stack.id);
    return result;
  };
  for (int id : {0x80, 0x10, 0x03, 0x60, 0x35, 0x70, 0x55, 0x6C, 0x02, 0x40}) fixture.stock(id, 1);
  game_state.sort_elements(SortMode::AttackFirst, 0);
  CHECK((ids() == std::vector<int>{0x10, 0x03, 0x02, 0x35, 0x40, 0x55, 0x60, 0x6C, 0x70, 0x80}));
  game_state.sort_elements(SortMode::HealFirst, 0);
  // 0x80, 0x55..0x6B ascending, 0x31..0x54 ascending, 0x01..0x30 descending, 0x6C..0x7D descending.
  CHECK((ids() == std::vector<int>{0x80, 0x55, 0x60, 0x35, 0x40, 0x10, 0x03, 0x02, 0x70, 0x6C}));
  // Empty slots sink to the end.
  CHECK(game_state.slot.elements[10].id == 0);
  // Innate-first: character 0 is white (0x08), the mask maps it to black (0x10).
  fixture.el(2, 1, 0x10, 0x77);
  fixture.el(3, 1, 0x80, 0x77);
  fixture.el(0x10, 1, 0x04, 0x77);
  game_state.sort_elements(SortMode::InnateFirst, 0);
  CHECK((ids() == std::vector<int>{2, 3, 0x10, 0x35, 0x40, 0x55, 0x60, 0x6C, 0x70, 0x80}));
}

void test_equip() {
  Fixture fixture;
  GameState& game_state = fixture.gs;
  fixture.tables.item_info.resize(256);
  fixture.tables.gear.resize(256);
  auto item = [&](int id, int kind, u32 mask_lo) {
    fixture.tables.item_info[static_cast<size_t>(id)].cat = static_cast<u8>(kind);
    fixture.tables.item_info[static_cast<size_t>(id)].equip_lo = mask_lo;
  };
  item(1, 4, 0xFFFF);
  // Item 32 is a weapon for character 0, item 33 one for character 1 only.
  item(32, 0, 0x1);
  item(33, 0, 0x2);
  item(150, 1, 0x3);
  item(194, 2, 0x3);
  item(195, 2, 0x3);
  item(196, 2, 0x3);
  fixture.tables.gear[32].raw[4] = 20;
  fixture.tables.gear[32].raw[6] = 9;
  fixture.tables.gear[150].raw[4] = 7;
  // Item 194 is an offence accessory (adds byte 4 to ATK) in conflict group 1; item 195 is in the same group.
  fixture.tables.gear[194].raw[8] = 0x20;
  fixture.tables.gear[194].raw[4] = 4;
  fixture.tables.gear[194].raw[10] = 0x01;
  fixture.tables.gear[195].raw[10] = 0x01;
  fixture.tables.gear[196].raw[8] = 0x40;
  fixture.tables.gear[196].raw[4] = 3;
  for (int id : {1, 32, 33, 150, 194, 195, 196}) game_state.add_item(id);

  auto cands = game_state.equip_candidates(0, 0);
  // The material and the armour are never candidates; character 0 cannot use item 33; armour does not fit the weapon slot.
  CHECK(cands.size() == 2);
  CHECK(cands[0].item == 32 && cands[0].ok);
  CHECK(cands[1].item == 33 && !cands[1].ok);
  CHECK(!game_state.can_equip(0, 0, 33));
  CHECK(!game_state.can_equip(0, 0, 150));

  CHECK(game_state.stats(0).atk == 10 && game_state.stats(0).def == 5);
  CHECK(game_state.equip(0, 0, 32));
  CHECK(game_state.equipped(0, 0) == 32 && game_state.item_count(32) == 0);
  CHECK(game_state.stats(0).atk == 30);
  CHECK(game_state.stats(0).hit == (2 * 30) / 3 + 9 / 3);
  CHECK(game_state.equip(0, 1, 150) && game_state.stats(0).def == 12);
  // A preview changes nothing.
  CHECK(game_state.stats_with(0, 0, 0).atk == 10 && game_state.equipped(0, 0) == 32);
  // Accessories: the offence class adds byte 4 to ATK; the same conflict group is refused.
  CHECK(game_state.equip(0, 2, 194) && game_state.stats(0).atk == 34);
  bool saw195 = false, ok195 = true;
  for (const auto& candidate : game_state.equip_candidates(0, 3))
    if (candidate.item == 195) saw195 = true, ok195 = candidate.ok;
  CHECK(saw195 && !ok195);
  CHECK(!game_state.can_equip(0, 3, 195));
  CHECK(game_state.can_equip(0, 3, 196));
  // The remove row exists only for a filled accessory slot, never for weapon / armour.
  CHECK(game_state.equip_candidates(0, 2).front().stock_slot == -1);
  CHECK(game_state.equip_candidates(0, 4).front().stock_slot != -1);
  CHECK(game_state.equip_candidates(0, 0).front().stock_slot != -1);
  CHECK(!game_state.can_equip(0, 0, 0) && !game_state.can_equip(0, 1, 0));
  game_state.unequip_accessories(0);
  CHECK(game_state.equipped(0, 2) == 0 && game_state.item_count(194) == 1);
  // Stamina recovery (status screen "STAMINA RECOVERY"): record +1F plus the MAG byte of stamina accessories.
  fixture.tables.char_sets["char"].rec[0].raw[0x1F] = 10;
  CHECK(game_state.stamina_recovery(0) == 10);
  item(197, 2, 0x3);
  fixture.tables.gear[197].raw[8] = static_cast<u8>(data::gearflag::kStamina & 0xFF);
  fixture.tables.gear[197].raw[9] = static_cast<u8>(data::gearflag::kStamina >> 8);
  fixture.tables.gear[197].raw[5] = 5;
  game_state.add_item(197);
  CHECK(game_state.equip(0, 3, 197) && game_state.stamina_recovery(0) == 15);
  game_state.unequip_accessories(0);
  CHECK(game_state.stamina_recovery(0) == 10);
  // Swapping weapons returns the old one to the stock.
  game_state.add_item(32);
  CHECK(game_state.equip(0, 0, 32) && game_state.item_count(32) == 1);
}

void test_use_and_options() {
  Fixture fixture;
  GameState& game_state = fixture.gs;
  // Usable heal, size 0 = +50.
  fixture.rules.pg[7].raw[0xB] = 0xC0;
  fixture.rules.pg[7].raw[0xA] = 0x00;
  fixture.stock(7, 1);
  CHECK(game_state.usable_elements().size() == 1);
  CHECK(game_state.use_element(7, 0) == Why::Ok && game_state.slot.chars[0].cur_hp == 90 && game_state.element_count(7) == 0);
  fixture.stock(7, 1);
  game_state.slot.chars[0].cur_hp = 100;
  CHECK(game_state.use_element(7, 0) == Why::Full && game_state.element_count(7) == 1);
  game_state.set_option(opt::kAutoAttack, true);
  game_state.set_option(opt::kAnalogStick, true);
  CHECK(game_state.option(opt::kAutoAttack) && game_state.option(opt::kAnalogStick) && !game_state.option(opt::kMono));
  game_state.set_option(opt::kAutoAttack, false);
  CHECK(!game_state.option(opt::kAutoAttack) && game_state.option(opt::kAnalogStick));
  CHECK(game_state.party().size() == 2 && game_state.roster().size() == 2);
}

// Original-art frame pieces (ps1::frame_pieces) and the dial needle angle (hand.h).
void test_ps1_art() {
  struct Piece {
    float dx, dy, dw, dh, sx, sy, sw, sh;
  };
  std::vector<Piece> pieces;
  ps1::frame_pieces([&](float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh) { pieces.push_back({dx, dy, dw, dh, sx, sy, sw, sh}); }, 10, 20, 100, 60, 2, 2);
  // Top, bottom, left, right edges + 4 corners.
  CHECK(pieces.size() == 8);
  float top_w = 0;
  for (const Piece& piece : pieces)
    if (piece.dy == 20 && piece.dh == 16 && piece.sx == 31) top_w += piece.dw;
  CHECK(std::fabs(top_w - (100 - 2 * 16)) < 0.01f);
  pieces.clear();
  ps1::frame_pieces([&](float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh) { pieces.push_back({dx, dy, dw, dh, sx, sy, sw, sh}); }, 10, 20, 100, 60, 2, 2, 30, 40);
  // The tab splits the top edge in two.
  CHECK(pieces.size() == 9);
  float gap_w = 0;
  for (const Piece& piece : pieces)
    if (piece.dy == 20 && piece.dh == 16 && piece.sx == 31) gap_w += piece.dw;
  CHECK(std::fabs(gap_w - (100 - 2 * 16 - 40)) < 0.01f);
  // needle: the tip points along the requested direction for every angle
  for (int angle = -2048; angle < 2048; angle += 97) {
    const float th = (static_cast<float>(angle) + 1024.0f) * 3.14159265f / 2048.0f;
    // The canvas position of the tip.
    const float dx = -std::cos(th) * 2.5f * 76.0f, dy = -std::sin(th) * 2.2222f * 76.0f;
    int toward = ui::hand_angle_toward(dx, dy, 2.5f, 2.2222f);
    CHECK(std::abs(toward - angle) <= 1 || std::abs(std::abs(toward - angle) - 4096) <= 1);
  }
  // Caption notch table (the main menu / shop draw the disc's wind pieces in classic art, with the same gaps as the RDE shapes).
  CHECK(ps1::caption_gap_end(49) == 79 && ps1::caption_gap_end(64) == 61 && ps1::caption_gap_end(82) == 48 && ps1::caption_gap_end(7) == 0);
  CHECK(std::string(ps1::face_name(0)) == "seljyu" && std::string(ps1::face_name(1)) == "kid" && ps1::face_name(50) == nullptr);
}
}  // namespace

int main() {
  test_ps1_art();
  test_grid();
  test_autofill();
  test_sort();
  test_equip();
  test_use_and_options();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("menu tests passed\n");
  return 0;
}
