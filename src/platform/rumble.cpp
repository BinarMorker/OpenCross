#include "platform/rumble.h"

#include <algorithm>

#include "core/log.h"

namespace oc::platform {

RumbleOut rumble_map(int small, int big) {
  RumbleOut out;
  if (big > 0) {
    const float strength = static_cast<float>(std::min(big, 255)) * 0.003725490067f + 0.05f;
    out.low = static_cast<std::uint16_t>(std::min(strength, 1.0f) * 65535.0f);
  }
  if (small > 0) out.high = 0xFFFF;
  return out;
}

Rumble& Rumble::instance() {
  static Rumble rumble;
  return rumble;
}

void Rumble::contribute(const void* source, int small, int big) {
  for (Source& src : sources_)
    if (src.id == source) {
      src.small = small;
      src.big = big;
      return;
    }
  sources_.push_back({source, small, big});
}

void Rumble::pulse() {
  pulse_ = 0xE0;
  pulse_acc_ = 0;
  log::debug("rumble: big {} small 0 frames {} (vibration switched on)", 0xE0, 0xE0 / 10 + 1);
}

// The pulse falls by 10 per 60 Hz frame whether or not the option is on (it ends by itself).
RumbleOut Rumble::flush(bool enabled, double dt) {
  int small = 0, big = 0;
  if (enabled) {
    for (const Source& src : sources_) {
      small += src.small;
      big += src.big;
    }
    if (pulse_ > 0) big += pulse_;
    small = small != 0 ? 1 : 0;
    big = std::min(big, 255);
  }
  sources_.clear();
  if (pulse_ > 0) {
    pulse_acc_ += dt * 60.0;
    const int steps = static_cast<int>(pulse_acc_);
    pulse_acc_ -= steps;
    pulse_ = std::max(0, pulse_ - 10 * steps);
  }
  small_ = small;
  big_ = big;
  return rumble_map(small, big);
}

}  // namespace oc::platform
