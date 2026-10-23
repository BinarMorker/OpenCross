// FieldWatchdog: the per-tick counters and the stuck-record reports.
#include "game/field_watchdog.h"

#include <algorithm>
#include <format>

#include "core/log.h"

namespace oc::game {

void FieldWatchdog::reset() {
  stuck_reported.clear();
  quiet_ticks = lock_ticks = 0;
}

// Softlock watchdog: reports the cause of a hang ("it just waits"): a record that has yielded on one instruction, or a
// polling loop of a class-3/4 script, for stuck_limit ticks with nothing the player could be doing (menu, battle, movie, a modal window,
// a transition), and a player input lock held that long. Both builds; OC_STUCK_TICKS=n changes the limit (the sweeps use a short one).
void FieldWatchdog::step(bool user_busy, bool input_locked, u64 tick_count, std::string_view room, const script::FieldVM& vm, const DescribeFn& describe_detail) {
  quiet_ticks = user_busy ? 0 : quiet_ticks + 1;
  lock_ticks = !user_busy && input_locked ? lock_ticks + 1 : 0;
  if (tick_count % 30 != 0 || quiet_ticks < std::min<u32>(stuck_limit, 600)) return;
  for (const auto& stuck : vm.stuck_records(stuck_limit)) {
    const u64 key = (static_cast<u64>(std::hash<std::string_view>{}(room)) << 32) ^ (static_cast<u64>(stuck.ent) << 24) ^
                    (static_cast<u64>(stuck.rec) << 16) ^ stuck.pc;
    u32& last = stuck_reported[key];
    // Already reported: report again when it doubles.
    if (last != 0 && stuck.ticks < last * 2) continue;
    last = stuck.ticks;
    const std::string detail = describe_detail(stuck);
    std::string msg = std::format("softlock watchdog: room {} {}{} (tick {})", room, script::FieldVM::describe(stuck), detail, tick_count);
    log::warn("{}", msg);
    stuck_log.push_back(std::move(msg));
  }
  // A long scripted scene without a message, menu or battle is rare but exists (credits, panning cutscenes): twice the wait limit.
  if (lock_ticks >= 2 * stuck_limit && lock_ticks % (2 * stuck_limit) < 30) {
    std::string msg = std::format("softlock watchdog: room {} player input locked by scripts for {} ticks (tick {}; a long cutscene or a hang); records:\n{}",
                                  room, lock_ticks, tick_count, vm.describe_records());
    log::warn("{}", msg);
    stuck_log.push_back(std::move(msg));
  }
}

}  // namespace oc::game
