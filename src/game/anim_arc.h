#pragma once
#include <cmath>

namespace oc::game {

// The retail rotation pre-pass of every keyframe and clip-blend interpolation, one channel in degrees: when the two values are a
// turn or more apart, each one of magnitude >= a turn is reduced to |v| mod 360 (sign kept); then, ONLY when they are more than
// 270 degrees apart, `to` is moved by a turn toward `from`. Between 180 and 270 degrees apart the interpolation goes the LONG way
// round (the animation data relies on it).
inline void retail_arc(float& from, float& to) {
  if (std::fabs(to - from) >= 360.0f) {
    for (float* value : {&from, &to})
      if (std::fabs(*value) >= 360.0f) *value = std::copysign(std::fmod(std::fabs(*value), 360.0f), *value);
  }
  const float diff = to - from;
  if (std::fabs(diff) > 270.0f) to += diff < 0 ? 360.0f : -360.0f;
}

}  // namespace oc::game
