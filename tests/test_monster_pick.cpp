// Grand Slam monster picker rules: the pool list, the round table across rounds, the preselection, the slot and list cursors with the
// wraps and the 13-step scrolls, picking (taken / empty refused), and the picks the commit writes.
#include <cstdio>

#include "ui/monster_pick.h"

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

using Pad = MonsterPicker::Pad;

std::array<u16, 16> pool_of(int count) {
  std::array<u16, 16> pool{};
  // Slots 3, 5, 7, ... (every other bit from 3).
  for (int k = 0; k < count; ++k) {
    const int slot = 3 + 2 * k;
    pool[static_cast<size_t>(slot >> 4)] |= static_cast<u16>(1u << (slot & 15));
  }
  return pool;
}

// Runs frames without input until the list stops scrolling.
void settle(MonsterPicker& picker) {
  for (int i = 0; i < 20 && picker.scrolling(); ++i) picker.frame(0, 0);
}

void test_list_and_round_table() {
  MonsterPicker::Table table;
  table.fill(0x22);
  MonsterPicker picker;
  picker.begin(pool_of(10), 0, table);
  // Round 0 clears the whole table and preselects positions 0..2.
  for (const u8 entry : table) CHECK(entry == MonsterPicker::kNone);
  CHECK(picker.entry(0) == 3 && picker.entry(1) == 5 && picker.entry(9) == 21 && picker.entry(10) == -1);
  CHECK(picker.pick(0) == 0 && picker.pick(1) == 1 && picker.pick(2) == 2);
  CHECK(picker.pick_slot(2) == 7);
  CHECK(picker.taken(0) && picker.taken(2) && !picker.taken(3));
  // Cancel in slot mode commits: the table holds the list positions.
  CHECK(picker.frame(0, Pad::kCancel) == 8);
  CHECK(picker.done());
  CHECK(table[0] == 0 && table[1] == 1 && table[2] == 2 && table[3] == MonsterPicker::kNone);
  // Round 1 keeps round 0's entries taken and preselects the next free ones.
  picker.begin(pool_of(10), 1, table);
  CHECK(table[0] == 0 && table[3] == MonsterPicker::kNone);
  CHECK(picker.pick(0) == 3 && picker.pick(1) == 4 && picker.pick(2) == 5);
  CHECK(picker.taken(0) && picker.taken(5) && !picker.taken(6));
  picker.frame(0, Pad::kCancel);
  // Replaying round 1 frees its own entries again.
  picker.begin(pool_of(10), 1, table);
  CHECK(picker.pick(0) == 3 && table[3] == MonsterPicker::kNone && table[0] == 0);
  // A pool too small for three picks leaves the last ones empty.
  MonsterPicker::Table small;
  MonsterPicker short_pool;
  short_pool.begin(pool_of(2), 0, small);
  CHECK(short_pool.pick(2) == -1 && short_pool.pick_slot(2) == -1);
}

void test_slot_mode() {
  MonsterPicker::Table table;
  MonsterPicker picker;
  picker.begin(pool_of(5), 0, table);
  CHECK(picker.frame(Pad::kLeft, 0) == 1);
  CHECK(picker.slot() == 2);
  picker.frame(Pad::kRight, 0);
  CHECK(picker.slot() == 0);
  picker.frame(Pad::kRight, 0);
  CHECK(picker.frame(0, Pad::kConfirm) == 8);
  CHECK(picker.mode() == MonsterPicker::Mode::List && picker.slot() == 1);
}

void test_list_moves_and_scroll() {
  MonsterPicker::Table table;
  MonsterPicker picker;
  picker.begin(pool_of(60), 0, table);
  picker.frame(0, Pad::kConfirm);
  // Left at the top-left corner does nothing; right walks the row and wraps into the next.
  CHECK(picker.frame(Pad::kLeft, 0) == 0);
  picker.frame(Pad::kRight, 0);
  picker.frame(Pad::kRight, 0);
  CHECK(picker.column() == 2);
  picker.frame(Pad::kRight, 0);
  CHECK(picker.column() == 0 && picker.row() == 1);
  CHECK(picker.frame(Pad::kLeft, 0) == 1);
  CHECK(picker.column() == 2 && picker.row() == 0);
  // Down to the last visible row, then one more scrolls: 13 steps, two per frame.
  for (int i = 0; i < 5; ++i) picker.frame(Pad::kDown, 0);
  CHECK(picker.row() == 5 && picker.top() == 0);
  picker.frame(Pad::kDown, 0);
  CHECK(picker.scrolling() && picker.list_offset() == -2);
  // Input is ignored while scrolling.
  picker.frame(Pad::kLeft, Pad::kConfirm);
  CHECK(picker.column() == 2 && picker.list_offset() == -4);
  settle(picker);
  CHECK(picker.top() == 1 && picker.row() == 5 && picker.list_offset() == -13);
  // Up at the first visible row scrolls back; the counter rests at -1 (the list one pixel lower).
  for (int i = 0; i < 5; ++i) picker.frame(Pad::kUp, 0);
  CHECK(picker.row() == 0);
  picker.frame(Pad::kUp, 0);
  CHECK(picker.top() == 0 && picker.scrolling());
  settle(picker);
  CHECK(picker.top() == 0 && picker.list_offset() == 1);
  // Cancel returns to slot mode.
  CHECK(picker.frame(0, Pad::kCancel) == 8);
  CHECK(picker.mode() == MonsterPicker::Mode::Slots && !picker.done());
}

void test_picking() {
  MonsterPicker::Table table;
  MonsterPicker picker;
  picker.begin(pool_of(5), 0, table);
  picker.frame(0, Pad::kConfirm);
  // Position 0 is slot 0's own pick: refused.
  CHECK(picker.frame(0, Pad::kConfirm) == 0x18);
  picker.frame(Pad::kDown, 0);
  // Position 3 (row 1, column 0) is free.
  CHECK(picker.frame(0, Pad::kConfirm) == 8);
  CHECK(picker.pick(0) == 3 && picker.taken(3) && !picker.taken(0));
  // Position 5 is empty (pool of 5): refused.
  picker.frame(Pad::kRight, 0);
  picker.frame(Pad::kRight, 0);
  CHECK(picker.cursor() == 5 && picker.frame(0, Pad::kConfirm) == 0x18);
  picker.frame(0, Pad::kCancel);
  picker.frame(0, Pad::kCancel);
  CHECK(picker.done() && table[0] == 3 && picker.pick_slot(0) == 9);
  // Nothing happens after the commit.
  CHECK(picker.frame(Pad::kLeft, Pad::kConfirm) == 0);
}

}  // namespace

int main() {
  test_list_and_round_table();
  test_slot_mode();
  test_list_moves_and_scroll();
  test_picking();
  if (failures) std::printf("%d failure(s)\n", failures);
  else std::printf("monster pick: all checks passed\n");
  return failures ? 1 : 0;
}
