#include "ui/monster_pick.h"

namespace oc::ui {

namespace {
// Storage of the list (the 130-entry limit is checked after each pool word, so the last word may add up to two more).
constexpr size_t kListSlots = 0x84;
// Rows of the list window; the bottom visible row.
constexpr int kLastRow = MonsterPicker::kVisibleRows - 1;
constexpr int kLastColumn = MonsterPicker::kColumns - 1;
// Scroll steps per row; the scroll up starts one short (its counter counts down past 0).
constexpr int kScrollUpStart = 12;
}  // namespace

void MonsterPicker::begin(const std::array<u16, 16>& pool, int round, Table& table) {
  list_.clear();
  for (int word = 0; word < 16 && static_cast<int>(list_.size()) < kListCap; ++word)
    for (int bit = 0; bit < 16; ++bit)
      if ((pool[static_cast<size_t>(word)] >> bit & 1) && list_.size() < kListSlots) list_.push_back(word * 16 + bit);
  table_ = &table;
  round_ = round;
  for (int k = round * kPicks; k >= 0 && k < static_cast<int>(table.size()); ++k) table[static_cast<size_t>(k)] = kNone;
  taken_.assign(kListSlots, false);
  picks_ = {kNone, kNone, kNone};
  for (size_t pick_index = 0; pick_index < picks_.size(); ++pick_index)
    for (int position = 0; position < kPreselectRange; ++position)
      if (entry(position) >= 0 && free_position(position)) {
        picks_[pick_index] = static_cast<u8>(position);
        taken_[static_cast<size_t>(position)] = true;
        break;
      }
  for (const u8 position : table)
    if (position != kNone && position < kListSlots) taken_[position] = true;
  mode_ = Mode::Slots;
  slot_ = col_ = row_ = top_ = 0;
  scroll_ = step_ = 0;
  done_ = false;
}

int MonsterPicker::entry(int position) const {
  return position >= 0 && position < static_cast<int>(list_.size()) ? list_[static_cast<size_t>(position)] : -1;
}

bool MonsterPicker::taken(int position) const {
  return position >= 0 && position < static_cast<int>(taken_.size()) && taken_[static_cast<size_t>(position)];
}

bool MonsterPicker::free_position(int position) const {
  if (table_)
    for (const u8 used : *table_)
      if (used == position) return false;
  for (const u8 used : picks_)
    if (used == position) return false;
  return true;
}

// The bottom row scrolls the list down one row (when there is one), any other row moves the cursor down; a move clears the input so
// the second pass does nothing more.
void MonsterPicker::move_down(bool first_pass, u16& repeat) {
  if (row_ == kLastRow) {
    if (top_ < kMaxTop) {
      scroll_ = 1;
      if (++step_ >= kRowPitch) step_ = 0, scroll_ = 0, ++top_;
    }
    return;
  }
  repeat = 0;
  if (first_pass) ++row_;
}

void MonsterPicker::move_up(bool first_pass, u16& repeat) {
  if (row_ == 0) {
    if (top_ != 0) {
      step_ = kScrollUpStart;
      scroll_ = 2;
      --top_;
      --step_;
    }
    return;
  }
  repeat = 0;
  if (first_pass) --row_;
}

int MonsterPicker::frame(u16 repeat, u16 pressed) {
  if (done_) return 0;
  int sfx = 0;
  if (mode_ == Mode::Slots) {
    if (repeat & kLeft) sfx = 1, slot_ = slot_ == 0 ? kLastColumn : slot_ - 1;
    if (repeat & kRight) sfx = 1, slot_ = slot_ == kLastColumn ? 0 : slot_ + 1;
    if (pressed & kConfirm) sfx = 8, mode_ = Mode::List;
    if (pressed & kCancel) {
      sfx = 8;
      for (int k = 0; k < kPicks; ++k) (*table_)[static_cast<size_t>(round_ * kPicks + k)] = picks_[static_cast<size_t>(k)];
      done_ = true;
    }
    return sfx;
  }
  for (int pass = 0; pass < 2; ++pass) {
    const bool first = pass == 0;
    if (scroll_ == 1) {
      if (++step_ >= kRowPitch) step_ = 0, scroll_ = 0, ++top_;
      continue;
    }
    if (scroll_ == 2) {
      if (step_ < 1) step_ = 0, scroll_ = 0;
      --step_;
      continue;
    }
    // Left on column 0 of a scrolled list jumps to column 2 and goes on as up; anything else continues with right / down, then up.
    bool up = false;
    if ((repeat & kLeft) && first) {
      if (col_ != 0) {
        sfx = 1, --col_;
      } else if (top_ == 0) {
        if (row_ != 0) sfx = 1, col_ = kLastColumn, --row_;
      } else {
        sfx = 1, col_ = kLastColumn, up = true;
      }
    }
    if (!up) {
      bool down = (repeat & kDown) != 0;
      if ((repeat & kRight) && first) {
        if (col_ != kLastColumn) {
          sfx = 1, ++col_;
        } else if (top_ == kMaxTop) {
          if (row_ != kLastRow) sfx = 1, col_ = 0, ++row_;
        } else {
          sfx = 1, col_ = 0, down = true;
        }
      }
      if (down) {
        if (row_ != kLastRow || top_ < kMaxTop) sfx = 1;
        const bool scrolls = row_ == kLastRow && top_ < kMaxTop;
        move_down(first, repeat);
        if (scrolls) continue;
      }
      if (!(repeat & kUp)) continue;
    }
    if (row_ != 0 || top_ != 0) sfx = 1;
    move_up(first, repeat);
  }
  if (scroll_ != 0) return sfx;
  if (pressed & kConfirm) {
    const int position = cursor();
    if (entry(position) >= 0 && !taken(position)) {
      sfx = 8;
      const u8 old = picks_[static_cast<size_t>(slot_)];
      if (old != kNone && old < taken_.size()) taken_[old] = false;
      picks_[static_cast<size_t>(slot_)] = static_cast<u8>(position);
      taken_[static_cast<size_t>(position)] = true;
    } else {
      sfx = 0x18;
    }
  }
  if (pressed & kCancel) sfx = 8, mode_ = Mode::Slots;
  return sfx;
}

}  // namespace oc::ui
