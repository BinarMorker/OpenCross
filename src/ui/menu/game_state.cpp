#include "ui/menu/game_state.h"

#include <algorithm>
#include <bit>

#include "vfs/vfs.h"

namespace oc::ui {

namespace {

// Copies of a consumable a grid cell can hold.
constexpr int kStackMax = 5;

int opposite_bit(int colour_bit) {
  switch (colour_bit) {
    case 0x04: return 0x40;
    case 0x40: return 0x04;
    case 0x08: return 0x10;
    case 0x10: return 0x08;
    case 0x20: return 0x80;
    case 0x80: return 0x20;
    default: return 0;
  }
}
// Colour mask -> mask of the opposite colours (sorting mode 0 maps the innate bits this way).
int opposite_mask(int colour_mask) {
  int opposite = 0;
  for (int bit = 0x04; bit <= 0x80; bit <<= 1)
    if (colour_mask & bit) opposite |= opposite_bit(bit);
  return opposite;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------- rules
bool MenuRules::load_pg(const vfs::Vfs& vfs) {
  pg.clear();
  auto bytes = vfs.read("cdrom/battle/data/menu_pg.bin");
  if (!bytes) return false;
  pg.resize(bytes->size() / 16);
  for (size_t i = 0; i < pg.size(); ++i) std::copy_n(bytes->begin() + static_cast<std::ptrdiff_t>(i * 16), 16, pg[i].raw.begin());
  return true;
}

bool MenuRules::el_fixed(int id) const {
  if (id >= 0x80) return true;
  const data::ElemRec* record = elem(id);
  return record && (record->raw[1] & 0x02) != 0;
}
bool MenuRules::el_stack(int id) const {
  const data::ElemRec* record = elem(id);
  return record && (record->raw[0] & 0x30) == 0x10;
}
bool MenuRules::el_innate_only(int id) const {
  const data::ElemRec* record = elem(id);
  return record && (record->raw[3] & 0x80) != 0;
}
int MenuRules::el_level(int id) const {
  const data::ElemRec* record = elem(id);
  return record ? record->level() : 0;
}
int MenuRules::el_colour(int id) const {
  const data::ElemRec* record = elem(id);
  return record ? record->colour_bit() : 0;
}
bool MenuRules::window_ok(int id, int row) const {
  const data::ElemRec* record = elem(id);
  if (!record) return false;
  const int level = record->level(), hi = record->raw[0x12] >> 4, lo = record->raw[0x12] & 15;
  return level - hi <= row + 1 && row + 1 <= level + lo;
}

// ---------------------------------------------------------------------------------------------------------- basics
std::vector<int> GameState::party() const {
  std::vector<int> out;
  for (u8 id : slot.party())
    if (id < save::kNumChars) out.push_back(id);
  return out;
}

std::vector<int> GameState::roster() const {
  std::vector<int> out;
  for (int character = 0; character < save::kNumChars; ++character)
    if (slot.recruited(character)) out.push_back(character);
  return out;
}

void GameState::set_option(u16 mask, bool on) {
  u16 bits = options();
  bits = on ? static_cast<u16>(bits | mask) : static_cast<u16>(bits & ~mask);
  slot.head[0x5A0] = static_cast<u8>(bits & 0xFF);
  slot.head[0x5A1] = static_cast<u8>(bits >> 8);
}

int GameState::stamina_recovery(int character) const {
  const data::StatRec* record = rec(character);
  int total = record ? record->stamina_rate() : 0;
  if (rules && rules->tables)
    for (int k = 2; k < kEquipSlots; ++k) {
      const int id = equipped(character, k);
      const data::GearRec* gear = id > 0 ? rules->tables->gear_rec(id) : nullptr;
      if (gear && (gear->flags() & data::gearflag::kStamina)) total += gear->mag();
    }
  return total;
}

const data::StatRec* GameState::rec(int character) const {
  if (!rules || !rules->tables || character < 0 || character >= data::kNumChars) return nullptr;
  // The record set of the state (the dream's char20.bin until FE 3F, else char.bin; SaveSlot::dream_records): the +0x30 level masks
  // place the grid cells, +3 / +0x1F the growth column and stamina rate.
  auto it = rules->tables->char_sets.find(slot.dream_records ? "char20" : "char");
  if (it == rules->tables->char_sets.end()) it = rules->tables->char_sets.find("char");
  return it == rules->tables->char_sets.end() ? nullptr : &it->second.rec[static_cast<size_t>(character)];
}

int GameState::innate(int character) const {
  if (character < 0 || character >= save::kNumChars) return 0;
  if (character == 0 || character == 43) return slot.chars[static_cast<size_t>(character)].colour_bit;
  const data::StatRec* record = rec(character);
  return record ? record->innate() : 0;
}

std::vector<int> GameState::key_items() const {
  std::vector<int> out;
  for (int id = 1; id <= 64; ++id)
    if (has_key_item(id)) out.push_back(id);
  return out;
}

// ---------------------------------------------------------------------------------------------------------- stock
bool GameState::add_to(save::ItemStack* stacks, size_t count, int id) {
  if (id <= 0) return false;
  for (size_t i = 0; i < count; ++i)
    if (stacks[i].id == id && stacks[i].qty > 0) {
      if (stacks[i].qty < 99) ++stacks[i].qty;
      return true;
    }
  for (size_t i = 0; i < count; ++i)
    if (stacks[i].id == 0 || stacks[i].qty == 0) {
      stacks[i].id = static_cast<u16>(id);
      stacks[i].qty = 1;
      return true;
    }
  return false;
}

bool GameState::take_from(save::ItemStack* stacks, size_t count, int id) {
  if (id <= 0) return false;
  for (size_t i = 0; i < count; ++i)
    if (stacks[i].id == id && stacks[i].qty > 0) {
      if (--stacks[i].qty == 0) stacks[i].id = 0;
      return true;
    }
  return false;
}

int GameState::element_count(int id) const {
  int total = 0;
  for (const auto& entry : slot.elements)
    if (entry.id == id) total += entry.qty;
  return total;
}
int GameState::item_count(int id) const {
  int total = 0;
  for (const auto& entry : slot.items)
    if (entry.id == id) total += entry.qty;
  return total;
}
bool GameState::add_element(int id) { return add_to(slot.elements.data(), slot.elements.size(), id); }
bool GameState::take_element(int id) { return take_from(slot.elements.data(), slot.elements.size(), id); }
bool GameState::add_item(int id) { return add_to(slot.items.data(), slot.items.size(), id); }
bool GameState::take_item(int id) { return take_from(slot.items.data(), slot.items.size(), id); }

// ---------------------------------------------------------------------------------------------------------- grid
int GameState::cell_index(int character, int row, int col) const {
  const data::StatRec* record = rec(character);
  if (!record || character >= save::kNumChars || row < 0 || row >= kRows || col < 0 || col >= kCols) return -1;
  const u16 mask = record->level_mask(row);
  if (!((mask >> col) & 1)) return -1;
  int cell = 0;
  for (int i = 0; i < row; ++i) cell += std::popcount(static_cast<unsigned>(record->level_mask(i)));
  cell += std::popcount(static_cast<unsigned>(mask & ((1u << col) - 1)));
  return cell < 48 ? cell : -1;
}

bool GameState::cell_available(int character, int row, int col) const {
  return cell_exists(character, row, col) && ((slot.chars[static_cast<size_t>(character)].unlocked[static_cast<size_t>(row)] >> col) & 1) != 0;
}
int GameState::cell_element(int character, int row, int col) const {
  const int cell = cell_index(character, row, col);
  return cell < 0 ? 0 : slot.chars[static_cast<size_t>(character)].grid[static_cast<size_t>(cell)] & 0x1FF;
}
int GameState::cell_count(int character, int row, int col) const {
  const int cell = cell_index(character, row, col);
  return cell < 0 ? 0 : slot.chars[static_cast<size_t>(character)].equipped_qty[static_cast<size_t>(cell)];
}
bool GameState::row_has_open_cell(int character, int row) const {
  for (int col = 0; col < kCols; ++col)
    if (cell_available(character, row, col)) return true;
  return false;
}
int GameState::first_open_row(int character) const {
  for (int row = 0; row < kRows; ++row)
    if (row_has_open_cell(character, row)) return row;
  return 0;
}
int GameState::copies_placed(int character, int id) const {
  int total = 0;
  for (int row = 0; row < kRows; ++row)
    for (int col = 0; col < kCols; ++col)
      if (cell_element(character, row, col) == id) ++total;
  return total;
}

Why GameState::can_place(int character, int row, int col, int id) const {
  if (cell_index(character, row, col) < 0) return Why::NoCell;
  if (!cell_available(character, row, col)) return Why::Locked;
  const int occ = cell_element(character, row, col);
  if (occ && rules->el_fixed(occ)) return Why::Fixed;
  if (!rules->is_element(id)) return Why::NotEligible;
  if (const PgRow* pg_entry = rules->pg_row(id); pg_entry && !pg_entry->eligible(character)) return Why::NotEligible;
  if (rules->el_innate_only(id) && rules->el_colour(id) != innate(character)) return Why::NotEligible;
  if (!rules->window_ok(id, row)) return Why::Window;
  int have = element_count(id);
  if (occ == id) have += rules->el_stack(id) ? std::max(1, cell_count(character, row, col)) : 1;
  if (have < 1) return Why::NoStock;
  return Why::Ok;
}

Why GameState::can_remove(int character, int row, int col) const {
  if (cell_index(character, row, col) < 0) return Why::NoCell;
  const int occ = cell_element(character, row, col);
  if (occ == 0) return Why::Empty;
  if (rules->el_fixed(occ)) return Why::Fixed;
  if (!cell_available(character, row, col)) return Why::Locked;
  return Why::Ok;
}

Why GameState::place(int character, int row, int col, int id) {
  const Why why = can_place(character, row, col, id);
  if (why != Why::Ok) return why;
  const int cell = cell_index(character, row, col);
  save::SaveChar& ch = slot.chars[static_cast<size_t>(character)];
  const int occ = cell_element(character, row, col);
  if (occ) {
    const int copies = rules->el_stack(occ) ? std::max(1, static_cast<int>(ch.equipped_qty[static_cast<size_t>(cell)])) : 1;
    for (int i = 0; i < copies; ++i) add_element(occ);
  }
  const int want = rules->el_stack(id) ? kStackMax : 1;
  int taken = 0;
  while (taken < want && take_element(id)) ++taken;
  ch.grid[static_cast<size_t>(cell)] = static_cast<u16>(id);
  if (rules->el_stack(id)) ch.equipped_qty[static_cast<size_t>(cell)] = static_cast<u8>(taken);
  return Why::Ok;
}

Why GameState::remove(int character, int row, int col) {
  const Why why = can_remove(character, row, col);
  if (why != Why::Ok) return why;
  const int cell = cell_index(character, row, col);
  save::SaveChar& ch = slot.chars[static_cast<size_t>(character)];
  const int occ = cell_element(character, row, col);
  const bool stack = rules->el_stack(occ);
  const int copies = stack ? std::max(1, static_cast<int>(ch.equipped_qty[static_cast<size_t>(cell)])) : 1;
  for (int i = 0; i < copies; ++i) add_element(occ);
  ch.grid[static_cast<size_t>(cell)] = 0;
  if (stack) ch.equipped_qty[static_cast<size_t>(cell)] = 0;
  return Why::Ok;
}

Why GameState::swap(int character, int ra, int ca, int rb, int cb) {
  const int ka = cell_index(character, ra, ca), kb = cell_index(character, rb, cb);
  if (ka < 0 || kb < 0) return Why::NoCell;
  if (ka == kb) return Why::Same;
  if (!cell_available(character, ra, ca) || !cell_available(character, rb, cb)) return Why::Locked;
  const int ia = cell_element(character, ra, ca), ib = cell_element(character, rb, cb);
  if ((ia && rules->el_fixed(ia)) || (ib && rules->el_fixed(ib))) return Why::Fixed;
  if ((ia && !rules->window_ok(ia, rb)) || (ib && !rules->window_ok(ib, ra))) return Why::Window;
  save::SaveChar& ch = slot.chars[static_cast<size_t>(character)];
  std::swap(ch.grid[static_cast<size_t>(ka)], ch.grid[static_cast<size_t>(kb)]);
  std::swap(ch.equipped_qty[static_cast<size_t>(ka)], ch.equipped_qty[static_cast<size_t>(kb)]);
  return Why::Ok;
}

int GameState::clear_all(int character) {
  int cleared = 0;
  for (int row = 0; row < kRows; ++row)
    for (int col = 0; col < kCols; ++col)
      if (cell_available(character, row, col) && cell_element(character, row, col) > 0 && cell_element(character, row, col) < 0x80 &&
          remove(character, row, col) == Why::Ok)
        ++cleared;
  return cleared;
}

int GameState::score(int character, int id) const {
  if (element_count(id) <= 0) return -200;
  const int placed = copies_placed(character, id);
  if (placed >= 2) return -100;
  int total = 0;
  if (const PgRow* pg_entry = rules->pg_row(id)) total = placed == 0 ? pg_entry->bonus_none() : pg_entry->bonus_one();
  const int innate_colours = innate(character) & 0xFC, element_colour = rules->el_colour(id);
  if (innate_colours & element_colour) total += 10;
  if (opposite_mask(innate_colours) & element_colour) total -= 10;
  return total;
}

int GameState::fill_cells(int character, bool fallback) {
  // Distinct stocked element ids of level >= 1 (the per-level buckets of the original).
  std::vector<int> ids;
  for (const auto& entry : slot.elements)
    if (entry.qty > 0 && rules->is_element(entry.id) && rules->el_level(entry.id) >= 1 && std::find(ids.begin(), ids.end(), entry.id) == ids.end())
      ids.push_back(entry.id);
  int placed = 0;
  for (int row = kRows - 1; row >= 0; --row)
    for (int col = 0; col < kCols; ++col) {
      if (!cell_available(character, row, col) || cell_element(character, row, col) != 0) continue;
      std::vector<int> levels{row + 1};
      if (fallback)
        for (int distance = 1; distance <= 8; ++distance) {
          levels.push_back(row + 1 + distance);
          levels.push_back(row + 1 - distance);
        }
      bool done = false;
      for (int level : levels) {
        // (-score, id) so a stable sort gives descending score.
        std::vector<std::pair<int, int>> cand;
        for (int id : ids)
          if (rules->el_level(id) == level) cand.emplace_back(-score(character, id), id);
        std::stable_sort(cand.begin(), cand.end(), [](auto& lhs, auto& rhs) { return lhs.first < rhs.first; });
        for (auto& [neg, id] : cand) {
          const int need = rules->el_stack(id) ? kStackMax : 1;
          if (element_count(id) < need || copies_placed(character, id) >= 2) continue;
          if (place(character, row, col, id) == Why::Ok) {
            ++placed;
            done = true;
            break;
          }
        }
        if (done) break;
      }
    }
  return placed;
}

int GameState::auto_fill(int character, bool reset) {
  if (character < 0 || character >= save::kNumChars) return 0;
  if (reset) clear_all(character);
  // Pass 1 fills exact levels only, pass 2 the nearest levels.
  int placed = fill_cells(character, false);
  placed += fill_cells(character, true);
  return placed;
}

int GameState::auto_fill_party(bool reset) {
  int placed = 0;
  for (int character : party()) placed += auto_fill(character, reset);
  return placed;
}

void GameState::sort_elements(SortMode mode, int character) {
  struct SortEntry {
    int id, qty, slot;
    int group, sub;
  };
  std::vector<SortEntry> entries;
  const int opposite = opposite_mask(innate(character) & 0xFC);
  for (size_t i = 0; i < slot.elements.size(); ++i) {
    const auto& stack = slot.elements[i];
    if (stack.id == 0 || stack.qty == 0) continue;
    SortEntry entry{stack.id, stack.qty, static_cast<int>(i), 0, 0};
    const int id = stack.id;
    switch (mode) {
      case SortMode::InnateFirst: {
        const int col = rules->el_colour(id);
        if (id >= 0x01 && id <= 0x30) {
          if (col & opposite) entry.group = 0, entry.sub = id;
          else if (col & ~opposite) entry.group = 1, entry.sub = id;
          else entry.group = 2, entry.sub = entry.slot;
        } else {
          entry.group = 3, entry.sub = id;
        }
        break;
      }
      case SortMode::AttackFirst:
        if (id >= 1 && id <= 0x30) entry.group = 0, entry.sub = -id;
        else entry.group = 1, entry.sub = id;
        break;
      case SortMode::HealFirst:
        if (id == 0x80) entry.group = 0;
        else if (id >= 0x55 && id <= 0x6B) entry.group = 1, entry.sub = id;
        else if (id >= 0x31 && id <= 0x54) entry.group = 2, entry.sub = id;
        else if (id >= 0x01 && id <= 0x30) entry.group = 3, entry.sub = -id;
        else if (id >= 0x6C && id <= 0x7D) entry.group = 4, entry.sub = -id;
        else entry.group = 5, entry.sub = id;
        break;
    }
    entries.push_back(entry);
  }
  std::stable_sort(entries.begin(), entries.end(),
                   [](const SortEntry& lhs, const SortEntry& rhs) { return lhs.group != rhs.group ? lhs.group < rhs.group : lhs.sub < rhs.sub; });
  slot.elements.fill({});
  for (size_t i = 0; i < entries.size(); ++i) {
    slot.elements[i].id = static_cast<u16>(entries[i].id);
    slot.elements[i].qty = static_cast<u8>(entries[i].qty);
  }
}

// ---------------------------------------------------------------------------------------------------------- use
std::vector<int> GameState::usable_elements() const {
  std::vector<int> out;
  for (size_t i = 0; i < slot.elements.size(); ++i) {
    const auto& stack = slot.elements[i];
    const PgRow* pg_entry = rules->pg_row(stack.id);
    if (stack.qty > 0 && stack.id > 0 && pg_entry && pg_entry->usable()) out.push_back(static_cast<int>(i));
  }
  return out;
}

Why GameState::use_element(int id, int target) {
  const PgRow* pg_entry = rules->pg_row(id);
  if (!pg_entry || !pg_entry->usable() || element_count(id) <= 0 || target < 0 || target >= save::kNumChars) return Why::NoStock;
  save::SaveChar& ch = slot.chars[static_cast<size_t>(target)];
  if (pg_entry->heals()) {
    static const int kHeal[4] = {50, 100, 200, 0};
    if (ch.cur_hp >= ch.max_hp) return Why::Full;
    ch.cur_hp = static_cast<u16>(std::min<int>(ch.max_hp, ch.cur_hp + kHeal[pg_entry->heal_size()]));
  } else {
    const data::ElemRec* record = rules->elem(id);
    const u32 mask = record ? record->status_lo() & 0xFF : 0;
    if ((ch.status & mask) == 0) return Why::Full;
    ch.status = static_cast<u8>(ch.status & ~mask);
  }
  take_element(id);
  return Why::Ok;
}

// ---------------------------------------------------------------------------------------------------------- equipment
int GameState::equipped(int character, int eslot) const {
  if (character < 0 || character >= save::kNumChars) return 0;
  const save::SaveChar& ch = slot.chars[static_cast<size_t>(character)];
  if (eslot == 0) return ch.weapon;
  if (eslot == 1) return ch.armour;
  if (eslot >= 2 && eslot < 5) return ch.accessory[static_cast<size_t>(eslot - 2)];
  return 0;
}

Stats GameState::stats_with(int character, int eslot, int item) const {
  Stats result;
  if (character < 0 || character >= save::kNumChars) return result;
  const save::SaveChar& ch = slot.chars[static_cast<size_t>(character)];
  auto id_at = [&](int equip_slot) { return equip_slot == eslot ? item : equipped(character, equip_slot); };
  auto gear = [&](int id) -> const data::GearRec* { return id > 0 && rules->tables ? rules->tables->gear_rec(id) : nullptr; };
  const data::GearRec* wp = gear(id_at(0));
  const data::GearRec* ar = gear(id_at(1));
  // Accessory sums (offence class 0x20, defence class 0x40).
  int o4 = 0, o5 = 0, o6 = 0, d4 = 0, d5 = 0, d6 = 0;
  for (int k = 2; k < 5; ++k)
    if (const data::GearRec* accessory = gear(id_at(k))) {
      if (accessory->flags() & data::gearflag::kOffence) o4 += accessory->raw[4], o5 += accessory->raw[5], o6 += accessory->raw[6];
      if (accessory->flags() & data::gearflag::kDefence) d4 += accessory->raw[4], d5 += accessory->raw[5], d6 += accessory->raw[6];
    }
  result.hp = ch.max_hp;
  result.atk = ch.atk + o4 + (wp ? wp->raw[4] : 0);
  result.mag = ch.mag + o5 + (wp ? wp->raw[5] : 0);
  result.hit = (2 * (ch.hit + o6 / 3)) / 3 + (wp ? wp->raw[6] / 3 : 0);
  result.def = ch.def + d4 + (ar ? ar->raw[4] : 0);
  result.mdef = ch.mdef + d5 + (ar ? ar->raw[5] : 0);
  result.evd = ch.evd + d6 + (ar ? ar->raw[6] : 0);
  return result;
}

int GameState::accessory_conflict(int character, int eslot, int item) const {
  if (eslot < 2 || item <= 0 || !rules->tables) return 0;
  const data::GearRec* gear = rules->tables->gear_rec(item);
  if (!gear) return 0;
  int others = 0;
  for (int k = 2; k < 5; ++k) {
    if (k == eslot) continue;
    if (const data::GearRec* other = rules->tables->gear_rec(equipped(character, k)); other && equipped(character, k) > 0) others |= other->slot_mask();
  }
  return gear->slot_mask() & others;
}

std::vector<EquipCand> GameState::equip_candidates(int character, int eslot) const {
  std::vector<EquipCand> out;
  if (eslot < 0 || eslot >= kEquipSlots || !rules->tables) return out;
  const int mode = std::min(eslot, 2);
  if (eslot >= 2 && equipped(character, eslot) != 0) out.push_back({-1, 0, 0, true});
  for (size_t i = 0; i < slot.items.size(); ++i) {
    const auto& stack = slot.items[i];
    if (stack.id == 0 || stack.qty == 0) continue;
    const data::ItemInfo* info = rules->tables->item(stack.id);
    if (!info || info->kind() != mode) continue;
    EquipCand candidate;
    candidate.stock_slot = static_cast<int>(i);
    candidate.item = stack.id;
    candidate.qty = stack.qty;
    candidate.ok = info->can_equip(character) && accessory_conflict(character, eslot, stack.id) == 0;
    out.push_back(candidate);
  }
  return out;
}

bool GameState::can_equip(int character, int eslot, int item) const {
  if (character < 0 || character >= save::kNumChars || eslot < 0 || eslot >= kEquipSlots || !rules->tables) return false;
  if (item == 0) return eslot >= 2 && equipped(character, eslot) != 0;
  const data::ItemInfo* info = rules->tables->item(item);
  if (!info || info->kind() != std::min(eslot, 2) || !info->can_equip(character)) return false;
  if (accessory_conflict(character, eslot, item) != 0) return false;
  return item_count(item) > 0;
}

bool GameState::equip(int character, int eslot, int item) {
  if (!can_equip(character, eslot, item)) return false;
  save::SaveChar& ch = slot.chars[static_cast<size_t>(character)];
  const int old = equipped(character, eslot);
  if (item) take_item(item);
  if (old) add_item(old);
  if (eslot == 0) ch.weapon = static_cast<u16>(item);
  else if (eslot == 1) ch.armour = static_cast<u16>(item);
  else ch.accessory[static_cast<size_t>(eslot - 2)] = static_cast<u16>(item);
  return true;
}

void GameState::unequip_accessories(int character) {
  for (int k = 2; k < 5; ++k)
    if (equipped(character, k)) equip(character, k, 0);
}

int GameState::location_key(u32 scenario) {
  static constexpr int kStart[30] = {0,  8,  16, 24,  26,  28,  30,  36,  52,  64,  72,  80,  88,  92, 100,
                                     108, 112, 116, 124, 128, 140, 144, 156, 164, 172, 184, 195, 202, 209, 999};
  int key = 0;
  for (int start : kStart)
    if (static_cast<int>(scenario) >= start) ++key;
  return std::max(1, key);
}

std::string GameState::location_name(const import::LangTables& lt, import::Lang lang, u32 scenario, std::string* second) {
  constexpr char kLf = char(10), kCr = char(13);
  std::string text;
  if (const import::TextTable* table = const_cast<import::LangTables&>(lt).table("savetbl"))
    if (const std::string* found = table->get(static_cast<u64>(location_key(scenario)), lang)) text = *found;
  text.erase(std::remove(text.begin(), text.end(), kCr), text.end());
  const auto trim = [](std::string value) {
    while (!value.empty() && (value.back() == ' ' || value.back() == kLf)) value.pop_back();
    size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == kLf)) ++begin;
    return value.substr(begin);
  };
  const size_t nl = text.find(kLf);
  if (second) *second = nl == std::string::npos ? std::string() : trim(text.substr(nl + 1));
  return trim(nl == std::string::npos ? text : text.substr(0, nl));
}

}  // namespace oc::ui
