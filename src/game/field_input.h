#pragma once
// FieldInput: the per-frame input snapshot of the field - the pad mask and analog sticks that drive the player, the PS1-style key word and
// latch the script key ops read, and the pending action press. update() of the field world fills it from the platform input.
#include <array>

#include "core/types.h"
#include "platform/window.h"

namespace oc::game {

struct FieldInput {
  // pad mask: bit0 up, 1 right, 2 down, 3 left
  u32 held_mask = 0;
  bool run_held = false;
  bool action_pending = false;
  // PS1-style key word for the key ops
  u16 pad_word = 0;
  u16 key_latch = 0;
  // FE 77: ANDed with the pad words; room init 0xFFFF
  u16 input_mask = 0xFFFF;
  float pad_x = 0, pad_y = 0;
  bool pad_analog = false;
  // right stick (up positive), outside the dead zone
  float pad_rx = 0, pad_ry = 0;
  bool pad_rstick = false;

  // Ticks each direction has been held (up, right, down, left).
  std::array<int, 4> held_ticks{};

  // The menu-style presses of this frame (keyboard or pad). `repeat`: the held directions whose auto-repeat fires this frame (held_mask
  // bits): after a direction has been held past the repeat delay it steps again every tick.
  struct Edges {
    bool confirm = false, up = false, down = false, left = false, right = false;
    u32 repeat = 0;
  };

  // Room change: no held direction, no pending action, the key mask open again.
  void reset();
  // Reads the frame's keyboard and pad state into the snapshot and returns the press edges; `ticks` = game ticks since the last call.
  Edges sample(const platform::Input& in, int ticks);
  u16 keys_held() const { return static_cast<u16>(pad_word & input_mask); }
  u16 latched_keys() const { return static_cast<u16>(key_latch & input_mask); }
  void clear_key_latch() { key_latch = 0; }
};

}  // namespace oc::game
