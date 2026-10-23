#pragma once
// BattleAudit: the counters behind the OC_BATTLE_AUDIT=1 per-tick flow checks (a stuck queue, a stuck UI wait).

namespace oc::game {

struct BattleAudit {
  // Consecutive ticks a queued group could not start, and a UI wait with nothing to wait for.
  int stall = 0, phase_ticks = 0;
  // The one-time table checks have run.
  bool tables_done = false;

  // One tick of the queued-group check: true on the tick the stall reaches 900.
  bool group_stalled(bool stuck) {
    if (!stuck) {
      stall = 0;
      return false;
    }
    return ++stall == 900;
  }

  // One tick of the idle-UI check: true on the tick the wait reaches 300.
  bool ui_wait_stuck(bool idle) {
    if (!idle) {
      phase_ticks = 0;
      return false;
    }
    return ++phase_ticks == 300;
  }
};

}  // namespace oc::game
