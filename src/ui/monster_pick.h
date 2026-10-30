#pragma once
// Grand Slam monster picker rules (field FE 75 + FE 61): the player picks three creatures of the Dopplegang pool for one round of the
// monster-party battles. Pure logic, no rendering.
//
// List: the pool slots whose bits are set (word w bit n = slot w * 16 + n), ascending, at most 130; shown 3 per row, 42 rows, 6 visible,
// 13 px per row. The round table (9 list positions, 3 per round, 0xFF = none) lives for the session: opening round r clears its entries
// from r * 3 on, so the creatures of the earlier rounds stay taken. The three picks start on the first three free list positions
// (searched among positions 0..128).
// A list position is "taken" while it is one of the round table's entries or one of the current picks.
//
// Slot mode: left / right wrap over the three picks (sfx 1); confirm opens the list (sfx 8); cancel ends the picker (sfx 8): the picks
// go into the round table and the three pool slots are the battle's party.
// List mode (one 60 Hz frame = two passes over the input; left / right only in the first): the cursor moves over columns and rows,
// wrapping column 0 <-> 2 into the previous / next row; past the visible rows the list scrolls one row in 13 px steps (2 per frame, the
// passes), ignoring the input meanwhile. Confirm on a free entry makes it the slot's pick (the old one is free again, sfx 8), on a taken
// or empty one plays 0x18; cancel returns to slot mode (sfx 8). After a scroll up the step counter rests at -1 (the list 1 px lower)
// until the next scroll down.
#include <array>
#include <vector>

#include "core/types.h"

namespace oc::ui {

class MonsterPicker {
 public:
  static constexpr int kColumns = 3, kVisibleRows = 6, kRows = 42, kMaxTop = kRows - kVisibleRows, kRowPitch = 13;
  static constexpr int kListCap = 0x82, kPreselectRange = 0x81, kRounds = 3, kPicks = 3;
  static constexpr u8 kNone = 0xFF;
  using Table = std::array<u8, kRounds * kPicks>;

  // Input bits of one frame (the repeating pad for the directions, the pressed edges for confirm / cancel).
  enum Pad : u16 { kLeft = 1, kRight = 2, kUp = 4, kDown = 8, kConfirm = 0x10, kCancel = 0x20 };
  enum class Mode { Slots, List };

  // Opens round `round` (0..2) on the pool words and the session's round table (entries from round * 3 on are cleared).
  void begin(const std::array<u16, 16>& pool, int round, Table& table);
  // One 60 Hz frame. Returns the sound effect (0 none, 1 cursor, 8 confirm / cancel, 0x18 refused).
  int frame(u16 repeat, u16 pressed);
  // True once cancel in slot mode has written the picks.
  bool done() const { return done_; }

  Mode mode() const { return mode_; }
  int slot() const { return slot_; }
  int column() const { return col_; }
  int row() const { return row_; }
  int top() const { return top_; }
  // Pixel offset of list row 0 from its rest position: -(step + top * 13).
  int list_offset() const { return -(step_ + top_ * kRowPitch); }
  bool scrolling() const { return scroll_ != 0; }
  int round() const { return round_; }
  // Pool slot of a list position (-1 = empty).
  int entry(int position) const;
  bool taken(int position) const;
  // List position of pick `slot` (-1 = none) and its pool slot (-1 = none).
  int pick(int slot) const { return picks_[static_cast<size_t>(slot)] == kNone ? -1 : picks_[static_cast<size_t>(slot)]; }
  int pick_slot(int slot) const { return entry(pick(slot)); }
  // The list position under the list cursor.
  int cursor() const { return (top_ + row_) * kColumns + col_; }

 private:
  bool free_position(int position) const;
  void move_down(bool first_pass, u16& repeat);
  void move_up(bool first_pass, u16& repeat);

  std::vector<int> list_;
  std::vector<bool> taken_;
  Table* table_ = nullptr;
  std::array<u8, kPicks> picks_{kNone, kNone, kNone};
  int round_ = 0;
  Mode mode_ = Mode::Slots;
  int slot_ = 0, col_ = 0, row_ = 0, top_ = 0;
  // 0 still, 1 scrolling down, 2 scrolling up; the step counter.
  int scroll_ = 0, step_ = 0;
  bool done_ = false;
};

}  // namespace oc::ui
