#pragma once
// The two party-member screens of the memory menu. Pure logic, header-only so the UI library (ui::PartyScreen) can use it
// without linking oc_game.
//
// PartyGrid: the portrait grid both overlays share. Grid = the recruited characters that are not excluded, ascending ids, 4
// columns, 11 rows, 5 visible rows (cursor row 0..4) and a scroll of 0..6 rows that animates 2 px per frame for 17 frames (18
// frames, input ignored). Left / right wrap over the 4 columns, up / down move the row and scroll at the visible edge (sfx 1 on
// every press, also at the ends).
//
// PartyEditor (PARTY_SCREEN / Tele-Porter):
// Mode A (slots): left / right wrap over the 3 slots (sfx 1); confirm on an empty slot or on a slot whose character is not locked
// enters mode B (sfx 8), a locked one plays 0x18; cancel leaves (sfx 6) unless a slot is empty while the number of filled slots
// differs from the grid size (sfx 0x18).
// Mode B (grid): confirm on a character that is not in the party puts it into the slot, on a character of another slot swaps the two
// and moves the slot cursor to that other slot (both sfx 8, the grid stays open), on the slot's own character or an empty cell plays
// 0x18; cancel returns to mode A (sfx 6). Leaving compacts the party (drops the empty slots, keeps the order).
//
// ReliefEditor (Relief Charm): the grid only. Confirm on a character other than the current relief member sets it (sfx 8, the
// screen stays open, the cursor stays); on the current relief member or an empty cell sfx 0x18; cancel leaves unconditionally
// (sfx 6). The relief member cannot be cleared here. Entry clears a relief member that is no longer recruited; the cursor starts
// on cell 0.
#include <algorithm>
#include <array>
#include <vector>

#include "core/types.h"

namespace oc::game {

enum class PartyKey { Left, Right, Up, Down, Confirm, Cancel };

class PartyGrid {
 public:
  using Key = PartyKey;
  static constexpr int kColumns = 4, kVisibleRows = 5, kMaxScroll = 6, kCells = 44;
  // PS1 px per grid row; the scroll animation steps 2 px per frame for 17 frames.
  static constexpr int kRowPitch = 0x22, kScrollFrames = 0x11;

  // One 60 Hz frame of the grid scroll: 17 frames of 2 px, then one frame to finish.
  void tick() {
    if (scroll_dir_ == 0) return;
    if (scroll_left_ == 0) {
      scroll_dir_ = 0;
      return;
    }
    yscroll_ += scroll_dir_ == 1 ? -2 : 2;
    --scroll_left_;
  }

  int column() const { return col_; }
  // Visible row 0..4.
  int row() const { return row_; }
  // First visible grid row 0..6.
  int scroll() const { return scroll_; }
  bool scrolling() const { return scroll_dir_ != 0; }
  // PS1 y offset of grid row 0: 0 at entry, -0x22 per row scrolled down; animates while scrolling.
  int yscroll() const { return yscroll_; }
  // Grid cell index (= index into roster()).
  int cursor() const { return (scroll_ + row_) * kColumns + col_; }
  int cell(int index) const { return index >= 0 && index < static_cast<int>(roster_.size()) ? roster_[static_cast<size_t>(index)] : -1; }
  const std::vector<int>& roster() const { return roster_; }

 protected:
  void reset(std::vector<int> roster) {
    std::sort(roster.begin(), roster.end());
    roster.erase(std::unique(roster.begin(), roster.end()), roster.end());
    if (roster.size() > kCells) roster.resize(kCells);
    roster_ = std::move(roster);
    col_ = row_ = scroll_ = 0;
    scroll_dir_ = scroll_left_ = 0;
    yscroll_ = 0;
  }
  // Left / right / up / down in the grid; returns the sound (1, also at the ends), 0 for other keys. The scroll counter moves
  // at once; only its pixel offset animates.
  int move(Key key) {
    switch (key) {
      case Key::Left: col_ = (col_ + kColumns - 1) % kColumns; return 1;
      case Key::Right: col_ = (col_ + 1) % kColumns; return 1;
      case Key::Up:
        if (row_ > 0) --row_;
        else if (scroll_ > 0) --scroll_, scroll_dir_ = 2, scroll_left_ = kScrollFrames;
        return 1;
      case Key::Down:
        if (row_ < kVisibleRows - 1) ++row_;
        else if (scroll_ < kMaxScroll) ++scroll_, scroll_dir_ = 1, scroll_left_ = kScrollFrames;
        return 1;
      default: return 0;
    }
  }

  std::vector<int> roster_;
  int col_ = 0, row_ = 0, scroll_ = 0;
  int scroll_dir_ = 0, scroll_left_ = 0, yscroll_ = 0;
};

class PartyEditor : public PartyGrid {
 public:
  enum class Mode { Slots, Pick };

  // `roster`: the grid characters (any order; sorted ascending). `party`: character id per slot, -1 / 0xFF = empty; packed (empty
  // slots and duplicates dropped); members stay even when they are not in the grid (an excluded member keeps its slot, as on the
  // PS1). `locked`: 64-bit bitmap, bit c = character c cannot be moved out of its slot.
  void begin(std::vector<int> roster, const std::array<int, 3>& party, std::array<u32, 2> locked = {0, 0}) {
    reset(std::move(roster));
    locked_ = locked;
    party_ = {-1, -1, -1};
    int count = 0;
    for (int id : party)
      if (id >= 0 && id < kCells && std::find(party_.begin(), party_.end(), id) == party_.end()) party_[static_cast<size_t>(count++)] = id;
    mode_ = Mode::Slots;
    slot_ = 0;
    done_ = false;
  }

  // Applies one key; returns the sound effect id (0 none, 1 cursor, 8 select, 6 back, 0x18 refused). Ignored while the grid scrolls.
  // A swap moves the slot cursor to the slot the picked character came from.
  int press(Key key) {
    if (done_ || scroll_dir_ != 0) return 0;
    if (mode_ == Mode::Slots) {
      switch (key) {
        case Key::Left: slot_ = (slot_ + 2) % 3; return 1;
        case Key::Right: slot_ = (slot_ + 1) % 3; return 1;
        case Key::Up:
        case Key::Down: return 0;
        case Key::Confirm: {
          const int member = party_[static_cast<size_t>(slot_)];
          if (member >= 0 && locked(member)) return 0x18;
          mode_ = Mode::Pick;
          return 8;
        }
        case Key::Cancel:
          if (filled() < 3 && filled() != static_cast<int>(roster_.size())) return 0x18;
          done_ = true;
          return 6;
      }
      return 0;
    }
    switch (key) {
      case Key::Confirm: {
        const int member = cell(cursor());
        if (member < 0) return 0x18;
        const auto it = std::find(party_.begin(), party_.end(), member);
        if (it == party_.end()) {
          party_[static_cast<size_t>(slot_)] = member;
          return 8;
        }
        const int other = static_cast<int>(it - party_.begin());
        if (other == slot_) return 0x18;
        std::swap(party_[static_cast<size_t>(other)], party_[static_cast<size_t>(slot_)]);
        slot_ = other;
        return 8;
      }
      case Key::Cancel: mode_ = Mode::Slots; return 6;
      default: return move(key);
    }
  }

  bool done() const { return done_; }
  Mode mode() const { return mode_; }
  int slot() const { return slot_; }
  // -1 = empty (not compacted until done).
  const std::array<int, 3>& slots() const { return party_; }
  bool in_party(int id) const { return id >= 0 && std::find(party_.begin(), party_.end(), id) != party_.end(); }
  bool locked(int id) const { return id >= 0 && id < 64 && ((locked_[static_cast<size_t>(id >> 5)] >> (id & 31)) & 1) != 0; }
  // The character whose stats / name the screen shows: mode A the cursor slot's member, mode B the cursor cell (kept while the
  // grid scrolls); -1 = none.
  int shown() const { return mode_ == Mode::Slots ? party_[static_cast<size_t>(slot_)] : cell(cursor()); }
  std::vector<int> result() const {
    std::vector<int> out;
    for (int id : party_)
      if (id >= 0) out.push_back(id);
    return out;
  }
  int filled() const { return static_cast<int>(std::count_if(party_.begin(), party_.end(), [](int id) { return id >= 0; })); }

 private:
  std::array<int, 3> party_{-1, -1, -1};
  std::array<u32, 2> locked_{0, 0};
  Mode mode_ = Mode::Slots;
  int slot_ = 0;
  bool done_ = false;
};

class ReliefEditor : public PartyGrid {
 public:
  // `roster` as PartyEditor; `relief` = the relief member (-1 / 0xFF = none), already cleared by the caller when not recruited.
  // The shown character starts as the relief member, else record 0.
  void begin(std::vector<int> roster, int relief) {
    reset(std::move(roster));
    relief_ = relief >= 0 && relief < kCells ? relief : -1;
    shown_ = relief_ >= 0 ? relief_ : 0;
    done_ = false;
  }

  int press(Key key) {
    if (done_ || scroll_dir_ != 0) return 0;
    switch (key) {
      case Key::Confirm: {
        const int member = cell(cursor());
        if (member < 0 || member == relief_) return 0x18;
        relief_ = member;
        return 8;
      }
      case Key::Cancel: done_ = true; return 6;
      default: return move(key);
    }
  }
  // One frame: the scroll, then the shown character follows the cursor cell while not scrolling (-1 = empty cell).
  void tick() {
    PartyGrid::tick();
    if (!scrolling()) shown_ = cell(cursor());
  }

  bool done() const { return done_; }
  // -1 = none.
  int relief() const { return relief_; }
  int shown() const { return shown_; }

 private:
  int relief_ = -1, shown_ = 0;
  bool done_ = false;
};

}  // namespace oc::game
