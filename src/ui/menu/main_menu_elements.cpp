#include "ui/menu/main_menu_impl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/menu/menu_common.h"
#include "ui/menu/ps1_art.h"

namespace oc::ui {

// A refused action only plays sfx 0x18: the PS1 menus show no text for it.
void MainMenu::Impl::update_use(const Nav& nav) {
  const std::vector<int> list = gs->usable_elements();
  const int count = static_cast<int>(list.size());
  if (!use_target) {
    if (nav.up && use_cur > 0) --use_cur, sfx(1);
    if (nav.down && use_cur + 1 < count) ++use_cur, sfx(1);
    scroll_to(use_cur, use_scroll, kVisibleRows);
    if (nav.cancel) back_to_main(1), sfx(6);
    else if (nav.ok && count > 0) use_target = true, target_cur = 0, sfx(8);
  } else {
    const int party_size = static_cast<int>(party().size());
    if (nav.up) target_cur = wrap(target_cur - 1, party_size), sfx(1);
    if (nav.down) target_cur = wrap(target_cur + 1, party_size), sfx(1);
    if (nav.cancel) use_target = false, sfx(6);
    else if (nav.ok && count > 0 && party_size > 0) {
      const int id = gs->slot.elements[static_cast<size_t>(list[static_cast<size_t>(use_cur)])].id;
      if (gs->use_element(id, party()[static_cast<size_t>(target_cur)]) == Why::Ok) {
        sfx(8);
        if (gs->element_count(id) == 0) use_target = false, use_cur = use_scroll = 0;
      } else {
        sfx(0x18);
      }
    }
  }
}

// ---- element grid
// Stock list of the grid editor: -1 = the "Remove" row, then the stock slots holding grid elements.
std::vector<int> MainMenu::Impl::grid_list() const {
  std::vector<int> rows{-1};
  for (size_t slot = 0; slot < gs->slot.elements.size(); ++slot) {
    const save::ItemStack& stack = gs->slot.elements[slot];
    if (stack.qty > 0 && rules->is_element(stack.id)) rows.push_back(static_cast<int>(slot));
  }
  return rows;
}

// An element the member cannot place on the selected row is listed dimmed: not eligible, wrong innate colour, outside its level
// window, or its lowest legal row lies below the member's deepest unlocked row.
bool MainMenu::Impl::element_dim(int character, int id, int row) const {
  if (const PgRow* pg = rules->pg_row(id); pg && !pg->eligible(character)) return true;
  if (rules->el_innate_only(id) && rules->el_colour(id) != gs->innate(character)) return true;
  if (!rules->window_ok(id, row)) return true;
  int deepest = -1;
  for (int open_row = 0; open_row < GameState::kRows; ++open_row)
    if (gs->row_has_open_cell(character, open_row)) deepest = open_row;
  return rules->elem(id) && rules->el_level(id) - (rules->elem(id)->raw[0x12] >> 4) > deepest + 1;
}

void MainMenu::Impl::move_cell_row(int step) {
  const int character = cur_char();
  for (int distance = 1; distance <= GameState::kRows; ++distance) {
    const int row = wrap(cell_row + step * distance, GameState::kRows);
    if (gs->row_has_open_cell(character, row)) {
      cell_row = row;
      if (!gs->cell_exists(character, cell_row, cell_col)) cell_col = first_col(cell_row);
      sfx(1);
      return;
    }
  }
}

void MainMenu::Impl::move_cell_col(int step) {
  const int character = cur_char();
  for (int distance = 1; distance <= GameState::kCols; ++distance) {
    const int col = wrap(cell_col + step * distance, GameState::kCols);
    if (gs->cell_exists(character, cell_row, col)) {
      cell_col = col;
      sfx(1);
      return;
    }
  }
}

void MainMenu::Impl::grid_member(int step) {
  cycle_member(step);
  focus = 0;
  carry = -1;
  swap_row = -1;
  cell_row = gs->first_open_row(cur_char());
  cell_col = first_col(cell_row);
}

// Popup actions: 0 Remove All, 1 Auto Allocate (recommended layout), 2 Fill Blanks; on the member or the whole party.
void MainMenu::Impl::run_popup_action() {
  const std::vector<int> targets = pop_party ? party() : std::vector<int>{cur_char()};
  for (int character : targets) {
    if (pop_act == 0) gs->clear_all(character);
    else gs->auto_fill(character, pop_act == 1);
  }
  sfx(0x17);
  list_cur = std::min(list_cur, static_cast<int>(grid_list().size()) - 1);
}

// Grid options popup: Menu (4 entries) -> Scope (member / party) -> Confirm (yes / no); entry 3 of the menu opens Sort instead.
void MainMenu::Impl::update_popup(const Nav& nav) {
  auto entries = [&] { return popup == Popup::Menu ? 4 : popup == Popup::Sort ? 3 : 2; };
  if (nav.up || nav.left) pop_cur = wrap(pop_cur - 1, entries()), sfx(1);
  if (nav.down || nav.right) pop_cur = wrap(pop_cur + 1, entries()), sfx(1);
  if (nav.cancel || nav.tri) {
    sfx(6);
    if (popup == Popup::Menu || popup == Popup::Sort) popup = Popup::None;
    else if (popup == Popup::Scope) popup = Popup::Menu, pop_cur = pop_act;
    else popup = Popup::Scope, pop_cur = pop_party ? 1 : 0;
    return;
  }
  if (!nav.ok) return;
  sfx(8);
  switch (popup) {
    case Popup::Menu:
      if (pop_cur == 3) popup = Popup::Sort, pop_cur = 0;
      else pop_act = pop_cur, popup = Popup::Scope, pop_cur = 0;
      break;
    case Popup::Scope:
      pop_party = pop_cur == 1;
      popup = Popup::Confirm;
      pop_cur = 0;
      break;
    case Popup::Confirm:
      if (pop_cur == 0) run_popup_action();
      popup = Popup::None;
      break;
    case Popup::Sort:
      gs->sort_elements(static_cast<SortMode>(pop_cur), cur_char());
      popup = Popup::None;
      list_cur = list_scroll = 0;
      break;
    default: break;
  }
}

// Grid editor: focus 0 = the stock list (OK picks up an element, or the Remove row, as `carry`), focus 1 = the grid (left / right
// change the level row, up / down the cell; OK places / removes, Square marks a cell then swaps with it, Triangle opens the popup).
void MainMenu::Impl::update_grid(const Nav& nav) {
  if (popup != Popup::None) return update_popup(nav);
  if (nav.l1) grid_member(-1);
  if (nav.r1) grid_member(1);
  if (nav.tri) {
    popup = Popup::Menu;
    pop_cur = 0;
    sfx(8);
    return;
  }
  const int character = cur_char();
  if (focus == 0) {
    const std::vector<int> list = grid_list();
    const int count = static_cast<int>(list.size());
    if (nav.up && list_cur > 0) --list_cur, sfx(1);
    if (nav.down && list_cur + 1 < count) ++list_cur, sfx(1);
    scroll_to(list_cur, list_scroll, kVisibleRows);
    if (nav.cancel) back_to_main(1), sfx(6);
    else if (nav.ok) {
      carry = list[static_cast<size_t>(list_cur)] < 0 ? 0 : gs->slot.elements[static_cast<size_t>(list[static_cast<size_t>(list_cur)])].id;
      focus = 1;
      sfx(8);
    }
  } else {
    if (nav.left) move_cell_row(-1);
    if (nav.right) move_cell_row(1);
    if (nav.up) move_cell_col(-1);
    if (nav.down) move_cell_col(1);
    if (nav.cancel) focus = 0, swap_row = -1, sfx(6);
    else if (nav.sq) {
      if (swap_row < 0) {
        swap_row = cell_row, swap_col = cell_col;
        sfx(8);
      } else {
        sfx(gs->swap(character, swap_row, swap_col, cell_row, cell_col) == Why::Ok ? 0x17 : 0x18);
        swap_row = -1;
      }
    } else if (nav.ok) {
      const Why result = carry == 0 ? gs->remove(character, cell_row, cell_col) : gs->place(character, cell_row, cell_col, carry);
      if (result == Why::Ok) {
        sfx(0x17);
        focus = 0;
        list_cur = std::min(list_cur, static_cast<int>(grid_list().size()) - 1);
      } else {
        sfx(0x18);
      }
    }
  }
}

// ---- Elements: use: three member panels (47, 27 + 132 i), dim portraits, list panel (253,27), description (253,332)
void MainMenu::Impl::build_use() {
  const auto list = gs->usable_elements();
  const int cnt = static_cast<int>(list.size());
  const auto members = party();
  // Widescreen: portraits, member panels and their cursors left; the element list and description right.
  group(Left);
  for (size_t i = 0; i < members.size() && i < 3; ++i) portrait_strip(members[i], 16.0f + 72.0f * static_cast<float>(i), grey(0x40));
  for (size_t i = 0; i < members.size() && i < 3; ++i) status_panel(members[i], 47.0f, 27.0f + 132.0f * static_cast<float>(i));
  if (use_target) {
    tri(30, 37.0f + 132.0f * static_cast<float>(target_cur), true);
    // Roll frame 3.
    pointer(38.0f - static_cast<float>(((frames % 23) * 0x3000) >> 16), 224.0f, true);
  }
  group(Right);
  Frame(186, 253, 27);
  // "Elements" + icon in the list frame's top gap.
  notch_caption(253, 27, 3);
  for (int row = 0; row < kVisibleRows; ++row) {
    const int idx = use_scroll + row;
    if (idx >= cnt) break;
    const auto& stack = gs->slot.elements[static_cast<size_t>(list[static_cast<size_t>(idx)])];
    const float y = 47.0f + 24.0f * static_cast<float>(row);
    plate(cur_char(), stack.id, 275, 42.0f + 24.0f * static_cast<float>(row));
    const std::u32string nm = element_name(stack.id);
    const float rel = fit_rel(nm, 140, 0.9f);
    TP(301.0f + (128.0f - twp(nm, rel)) * 0.5f, y, nm, idx == use_cur && !use_target ? ps1_colour(0x7A6AA5) : kText, rel);
    Digits(453, y, padded_number(std::min<int>(stack.qty, 99), 2), 16, kText);
  }
  const int total = std::max(cnt, kVisibleRows);
  if (use_scroll > 0) scroll_arrow(485, 35, true);
  if (use_scroll + kVisibleRows < total) scroll_arrow(485, 311, false);
  Frame(0x36, 253, 332, kWhite, nullptr, nullptr, true);
  if (cnt > 0) tri(251, 47.0f + 24.0f * static_cast<float>(use_cur - use_scroll), !use_target);
  if (cnt > 0 && use_cur < cnt) {
    const int id = gs->slot.elements[static_cast<size_t>(list[static_cast<size_t>(use_cur)])].id;
    element_info(id, 340, 372);
  }
  group(Centre);
  message(PX(268), PY(412));
}

// Row tint behind an element name: sprite 0x5FF + 4k + 3 = menutim_14_<t>_01 (184x24), t by colour k.
void MainMenu::Impl::element_row_tint(int id, float x, float y) {
  const int bit = rules->el_colour(id);
  int colour = -1;
  for (int bit_index = 2; bit_index < 8; ++bit_index)
    if (bit & (1 << bit_index)) colour = bit_index - 2;
  if (colour < 0) return;
  static const int kTile[6] = {9, 12, 10, 11, 13, 8};
  if (Tex* tex = plate_tex(kTile[colour])) Img(tex, PX(x), PY(y), PX(184), PY(24), 0, 0, 1, 1, grey(0x70));
}

// menutim_14_<tile>_01.png (184 x 24), or in classic art + Ps1ResMenus the PS1 colour-sheet frame 1 composed from menutim.prd.
Tex* MainMenu::Impl::plate_tex(int tile) {
  if (ps1_screen())
    if (Tex* ps1_tex = texs.ps1("ps1:plate" + std::to_string(tile), [&] { return ps1::plate(*vfs, tile, 1); })) return ps1_tex;
  char path[64];
  std::snprintf(path, sizeof path, "hd/menu/menutim/menutim_14_%02d_01.png", tile);
  return texs.load(path);
}

// Element info lines of the Elements screens: level / window / target at y0, two description lines at y1 (+16).
void MainMenu::Impl::element_info(int id, float y0, float y1, float pitch) {
  const int lv = rules->el_level(id);
  TP(288, y0, utf32(menu_str(0x801F29B0, "Lv")), kText, 0.85f);
  Digits(320, y0, padded_number(lv, 1), 16, kText);
  if (const data::ElemRec* record = rules->elem(id)) {
    const int lo = record->raw[0x12] & 0xF;
    TP(340, y0 - 3, utf32("+"), kText, 0.6f);
    TP(340, y0 + 3, utf32("-"), kText, 0.6f);
    Digits(348, y0, padded_number(lo, 1), 16, kText);
    const u8 target = record->raw[4];
    const u32 tbase = (target & 0x10) ? 0x801F29D0u : 0x801F29C8u;
    const u32 key = tbase + ((target & 0xC0) == 0xC0 ? 3u : (target & 0x80) ? 1u : 2u);
    TP(372, y0, utf32(menu_str(key, "")), kText, 0.8f);
  }
  for (int line = 1; line <= 2; ++line) {
    const std::string* description = lt.sysmes((0x1Au << 16) | static_cast<u32>(id * 3 + line), lang);
    if (description && !trim(*description).empty())
      TPfit(288, y1 + pitch * static_cast<float>(line - 1), utf32(trim(*description)), 190, kText, 0.8f);
  }
}

// ---- element grid: grid half on the left, stock list + description on the right
// Colour index of an element for the plates / minimap: 0..5 = green, white, black, red, yellow, blue; 7 = multi; -1 none.
// Fixed elements (pgdat flag 0x200) take the member's innate colour.
int MainMenu::Impl::colour_index(int character, int id) const {
  if (id <= 0) return -1;
  const int bit = rules->el_fixed(id) ? gs->innate(character) : rules->el_colour(id);
  if ((bit & 0xFC) == 0xFC) return 7;
  int colour = -1;
  for (int bit_index = 2; bit_index < 8; ++bit_index)
    if (bit & (1 << bit_index)) colour = bit_index - 2;
  return colour;
}

// 184x24 row plate, menutim_14_<tile>_01.
void MainMenu::Impl::plate(int character, int id, float x, float y) {
  const int colour = colour_index(character, id);
  if (colour < 0) return;
  static const int kTile[8] = {9, 12, 10, 11, 13, 8, 0, 38};
  if (Tex* tex = plate_tex(kTile[colour])) Img(tex, PX(x), PY(y), PX(184), PY(24), 0, 0, 1, 1, grey(0x80));
}

// Thin 173x24 frame around an empty slot (eleempty.png), both art modes.
void MainMenu::Impl::empty_box(float x, float y) {
  if (Tex* tex = texs.load("hd/menu/eleempty.png")) Img(tex, PX(x + 4), PY(y), PX(174), PY(25));
}

// 8 levels x 11 columns of 16x8 element tiles at pitch (18, 9).
void MainMenu::Impl::grid_minimap(int character, float px, float py, int sel_row, int hold_id) {
  // menuelem_<n> for colour index 0..5, 7 = multi (8).
  static const int kFile[8] = {3, 6, 4, 5, 7, 2, 8, 8};
  int lo = 99, hi = -99;
  if (hold_id > 0 && rules->elem(hold_id)) {
    const int tol = rules->elem(hold_id)->raw[0x12] & 0xF;
    lo = rules->el_level(hold_id) - 1 - tol, hi = rules->el_level(hold_id) - 1 + tol;
  }
  for (int row = 0; row < GameState::kRows; ++row)
    for (int col = 0; col < GameState::kCols; ++col) {
      if (!gs->cell_exists(character, row, col) || !gs->cell_available(character, row, col)) continue;
      const int id = gs->cell_element(character, row, col);
      // file 1 = empty cell.
      int file = 1;
      if (id > 0) {
        const int colour = colour_index(character, id);
        file = colour < 0 ? 1 : kFile[colour];
      }
      u32 tint = grey(0x30);
      if (hold_id > 0 && row >= lo && row <= hi) tint = ps1_colour(0xA04040);
      if (row == sel_row) tint = grey(0xA0);
      const float x = px + 18.0f * static_cast<float>(row), y = py + 9.0f * static_cast<float>(col);
      char path[48];
      std::snprintf(path, sizeof path, "hd/menu/menuelem/menuelem_%d.png", file);
      if (Tex* tex = texs.load(path)) Img(tex, PX(x), PY(y), PX(16), PY(8), 0, 0, 1, 1, tint);
    }
}

void MainMenu::Impl::build_grid() {
  const int character = cur_char();
  const auto list = grid_list();
  const int cnt = static_cast<int>(list.size());
  const bool pane_grid = focus == 1;
  // The inactive half keeps its last frame (list highlight off).
  const bool dimmed = pane_grid || popup != Popup::None;
  // Widescreen: portrait, member-switch glyphs and the grid panel left; stock list, description and popups right.
  group(Left);
  portrait_strip(character, 16.0f, grey(0x40));
  // Member-switch glyphs (L1 / R1) under the grid panel.
  btn(0x14, 0x195, 4);
  btn(0xC6, 0x195, 5);
  // ---- left: grid panel (frame 0x38 at (10,27)), level number, minimap, 11-row cell list of the selected level
  Frame(56, 10, 27);
  {
    // Caption "Element Level N" in the grid frame's top gap (cmnmes "Element Level"; ink at (19.7, 27), bn 0.31, bn in every language).
    HT(face_gn, 19.7f, 27.0f, capt("Element Level", "Element Level"), kText, 0.31f);
    // The level number is its own print at (0x66, 0x19) in kb x 0.625, offset by a per-language x
    // {-10, 5, 0x1B, -0x10, 0xB, -10}[L], so it follows the end of the translated caption (FR / IT / ES are longer).
    static const float k42EDE0[6] = {-10, 5, 0x1B, -0x10, 0xB, -10};
    HS(0x91, 0x66 + k42EDE0[lang_index()], 0x19, utf32(std::to_string(cell_row + 1)), kText);
  }
  int hold_id = 0;
  if (focus == 1 && carry > 0) hold_id = carry;
  grid_minimap(character, 50, 45, cell_row, hold_id);
  for (int i = 0; i < 11; ++i) {
    if (i >= GameState::kCols || !gs->cell_exists(character, cell_row, i) || !gs->cell_available(character, cell_row, i)) continue;
    const float xb = 22.0f, yi = 152.0f + 24.0f * static_cast<float>(i), yt = 157.0f + 24.0f * static_cast<float>(i);
    const int id = gs->cell_element(character, cell_row, i);
    const bool sel = pane_grid && popup == Popup::None && i == cell_col;
    u32 col = sel ? ps1_colour(0x7A6AA5) : ps1_colour(0x909090);
    if (id == 0) {
      empty_box(xb, yi);
      continue;
    }
    if ((carry > 0 && focus == 1 && !rules->window_ok(carry, cell_row)) || rules->el_fixed(id) || id >= 0x80) col = ps1_colour(0x404040);
    plate(character, id, xb, yi);
    const std::u32string nm = element_name(id);
    const float rel = fit_rel(nm, 124, 0.9f);
    TP(46.0f + (128.0f - twp(nm, rel)) * 0.5f, yt, nm, col, rel);
    if (id < 0x80) {
      const int level_delta = cell_row - (rules->el_level(id) - 1);
      if (level_delta != 0)
        TP(46.0f + 128.0f + 6.0f, yt, utf32(std::string(level_delta > 0 ? "+" : "-") + std::to_string(std::abs(level_delta))), col, 0.75f);
      if (rules->el_stack(id) && gs->cell_count(character, cell_row, i) > 0)
        TP(xb + 0x98, yt, utf32("x" + std::to_string(gs->cell_count(character, cell_row, i))), col, 0.8f);
    }
  }
  // Left-pointing cursor at (205, 157 + 24 i).
  if (pane_grid && popup == Popup::None) {
    const float y = 157.0f + 24.0f * static_cast<float>(cell_col);
    for (int i = 0; i < 12; ++i) {
      const float width_factor = 1.0f - std::fabs(static_cast<float>(i) + 0.5f - 6.0f) / 6.0f;
      rect(PX(205 + 23.0f * (1 - width_factor)), PY(y + static_cast<float>(i)), PX(23.0f * width_factor), PY(1) + 0.6f, rgb(255, 80, 80));
    }
  }
  // ---- right: stock list panel (frame 0x35 at (253,27))
  group(Right);
  Frame(53, 253, 27);
  // "Elements" + icon in the list frame's top gap.
  notch_caption(253, 27, 3);
  for (int i = 0; i < kVisibleRows; ++i) {
    const int idx = list_scroll + i;
    if (idx >= cnt) break;
    const float yp = 42.0f + 24.0f * static_cast<float>(i), yt = 47.0f + 24.0f * static_cast<float>(i);
    const bool sel = focus == 0 && popup == Popup::None && idx == list_cur;
    u32 col = sel ? ps1_colour(0x7A6AA5) : ps1_colour(0x909090);
    // The "Remove" pseudo row.
    if (list[static_cast<size_t>(idx)] < 0) {
      empty_box(275, yp);
      const std::u32string nm = utf32(menu_str(0x801F2A20, "Remove"));
      TP(301.0f + (128.0f - twp(nm, 0.9f)) * 0.5f, yt, nm, dimmed && !sel ? ps1_colour(0x404040) : col, 0.9f);
      continue;
    }
    const auto& stack = gs->slot.elements[static_cast<size_t>(list[static_cast<size_t>(idx)])];
    if (element_dim(character, stack.id, cell_row)) col = ps1_colour(0x404040);
    plate(character, stack.id, 275, yp);
    const std::u32string nm = element_name(stack.id);
    const float rel = fit_rel(nm, 124, 0.9f);
    TP(301.0f + (128.0f - twp(nm, rel)) * 0.5f, yt, nm, col, rel);
    Digits(453, yt, padded_number(std::min<int>(stack.qty, 99), 2), 16, col);
  }
  if (focus == 0 || popup != Popup::None) tri(251, 47.0f + 24.0f * static_cast<float>(list_cur - list_scroll), focus == 0 && popup == Popup::None);
  const int total = std::max(cnt, kVisibleRows);
  if (list_scroll > 0) scroll_arrow(485, 35, true);
  if (list_scroll + kVisibleRows < total) scroll_arrow(485, 311, false);
  // ---- description panel D = (280,336): frame 0x36 large at (253,332); text at x = 288, lines y = 344, 362, 380, 398
  Frame(0x36, 253, 332, kWhite, nullptr, nullptr, true);
  if (popup != Popup::None) {
    build_popup(character);
  } else {
    int id = 0;
    if (focus == 1) id = gs->cell_element(character, cell_row, cell_col);
    else if (list_cur > 0 && list_cur < cnt) id = gs->slot.elements[static_cast<size_t>(list[static_cast<size_t>(list_cur)])].id;
    if (id > 0 && id < 0x80) {
      element_info(id, 344, 380, 18);
      if (rules->el_innate_only(id)) {
        const int colour_mask = colour_index(character, id) >= 0 ? rules->el_colour(id) : 0;
        int ci = 0;
        for (int bit_index = 2; bit_index < 8; ++bit_index)
          if (colour_mask & (1 << bit_index)) ci = bit_index - 2;
        const std::string fmt = menu_str(0x801F2A88, "(Only if innate color %s)");
        const std::string cn = trim(menu_str(0x801F4A30 + 3u * static_cast<u32>(ci), ""));
        std::string caption = fmt;
        if (const size_t marker = caption.find("%s"); marker != std::string::npos) caption.replace(marker, 2, cn);
        TPfit(288, 362, utf32(caption), 190, kText, 0.74f);
      }
    } else if (id == 0 && focus == 0 && list_cur == 0 && cnt > 0) {
      TP(288, 344, utf32(menu_str(0x801F4758, "Remove chosen Element")), kText, 0.8f);
      TP(288, 362, utf32(menu_str(0x801F4772, "from the Element Grid.")), kText, 0.8f);
    }
  }
  group(Centre);
  message(PX(268), PY(412));
}

// Popup text in the description panel (lines y = 344 / 362 / 380 / 398, markers at x = 276 / 288 / 296).
void MainMenu::Impl::build_popup(int character) {
  auto line = [&](float x, float y, const std::string& text_line, u32 col = kText) {
    TPfit(x, y, utf32(text_line), 190.0f - (x - 288.0f), col, 0.8f);
  };
  const std::string name = import::u32_to_utf8(char_name(character));
  auto sel_tri = [&](float x, float y) { tri(x, y, true); };
  switch (popup) {
    case Popup::Menu:
      line(300, 344, menu_str(0x801F2A10, "Remove All"));
      line(300, 362, menu_str(0x801F2A2C, "Auto Allocate"));
      line(300, 380, menu_str(0x801F2A3C, "Fill Blanks"));
      line(300, 398, menu_str(0x801F49C8, "Sort Elements by..."));
      sel_tri(276, 344.0f + 18.0f * static_cast<float>(pop_cur));
      break;
    case Popup::Scope:
      line(304, 344, name + " " + trim(menu_str(0x801F48F8, "Only")));
      line(304, 380, trim(menu_str(0x801F492C, "Whole Party")));
      sel_tri(276, 344.0f + 36.0f * static_cast<float>(pop_cur));
      break;
    case Popup::Confirm:
      line(288, 344, menu_str(0x801F4890, "Alright to proceed?"));
      line(320, 380, menu_str(0x801F27E4, "Yes"));
      line(400, 380, menu_str(0x801F27EC, "No"));
      sel_tri(288.0f + 80.0f * static_cast<float>(pop_cur), 382);
      break;
    case Popup::Sort:
      line(288, 344, menu_str(0x801F49C8, "Sort Elements by..."));
      line(312, 362, trim(menu_str(0x801F49E2, "Innate Attribute Color First")));
      line(312, 380, trim(menu_str(0x801F49FC, "Attack First")));
      line(312, 398, trim(menu_str(0x801F4A16, "Healing & Supplemental First")));
      sel_tri(296, 362.0f + 18.0f * static_cast<float>(pop_cur));
      break;
    default: break;
  }
}

}  // namespace oc::ui
