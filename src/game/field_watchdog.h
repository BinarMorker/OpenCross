#pragma once
// FieldWatchdog: the softlock watchdog of the field. It counts the ticks in which nothing the player could be doing explains a script wait
// or an input lock, and logs the records that held on for longer than the limit. The caller supplies the per-tick facts and the text that
// explains one stuck record.
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"
#include "script/field_vm.h"

namespace oc::game {

struct FieldWatchdog {
  using DescribeFn = std::function<std::string(const script::FieldVM::StuckInfo&)>;

  // 30 s at 60 Hz
  u32 stuck_limit = 1800;
  // consecutive ticks with no player-driven reason to wait (menu, battle, movie, modal window)
  u32 quiet_ticks = 0;
  // consecutive quiet ticks with the player input locked by a script
  u32 lock_ticks = 0;
  // (room, ent, record, pc) -> tick count at the last report
  std::map<u64, u32> stuck_reported;
  bool sweep_probe = false;
  std::vector<std::string> stuck_log;

  // Room change: the counters and the report table start over; the limit, the log and the sweep flag stay.
  void reset();
  // One tick. `user_busy`: the player has a reason to wait; `input_locked`: a script holds the pad. `describe_detail` explains the entity-side
  // cause of one stuck record.
  void step(bool user_busy, bool input_locked, u64 tick_count, std::string_view room, const script::FieldVM& vm, const DescribeFn& describe_detail);
};

}  // namespace oc::game
