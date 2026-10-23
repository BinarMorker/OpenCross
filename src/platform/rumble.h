#pragma once
// Controller rumble: the PS1 pad motor model plus RDE's conversion to the host pad.
//
// Retail: every frame the running sequence tasks add their small-motor flag (0 / 1, summed, non-zero = 1) and big-motor strength
// (0..255, summed, clamped) into the pad packet's two motor bytes, which are sent and cleared each frame. Nothing adds while the
// Vibration Function option (options bit 0x400) is off. RDE turns a change of the two bytes into XInputSetState: left motor =
// (big / 268.4 + 0.05) * 65535 (0 when big is 0), right motor = 65535 when small is set.
#include <cstdint>
#include <vector>

namespace oc::platform {

// SDL_RumbleGamepad strengths: `low` = the low-frequency (large) motor, `high` = the high-frequency (small) motor.
struct RumbleOut {
  std::uint16_t low = 0;
  std::uint16_t high = 0;
  bool operator==(const RumbleOut&) const = default;
};

// RDE's byte -> motor conversion (pure).
RumbleOut rumble_map(int small, int big);

class Rumble {
 public:
  static Rumble& instance();

  // A source (a sequence player) adds its sum for the current tick. A source keeps only its latest tick of the frame (several sim ticks
  // can run between two presented frames); the sources' values add like the tasks' do.
  void contribute(const void* source, int small, int big);
  // The vibration-toggle test pulse: big 0xE0 falling by 10 per 1/60 s (vibration switched on in the pause screen).
  void pulse();
  // One presented frame: combines what was contributed (nothing when `enabled` is false), advances the pulse by `dt` seconds, clears the
  // contributions. Returns the motor state to output.
  RumbleOut flush(bool enabled, double dt);
  // The last combined bytes (small 0 / 1, big 0..255) - for logs and tests.
  int small() const { return small_; }
  int big() const { return big_; }

 private:
  struct Source {
    const void* id;
    int small, big;
  };
  std::vector<Source> sources_;
  int pulse_ = 0;
  double pulse_acc_ = 0;
  int small_ = 0, big_ = 0;
};

}  // namespace oc::platform
