#include "game/effects/fx_math.h"

#include <algorithm>
#include <cmath>

namespace oc::effects {

namespace {
const std::array<int, 4096>& sin_table() {
  static const std::array<int, 4096> table = [] {
    std::array<int, 4096> values{};
    for (int i = 0; i < 4096; ++i) values[static_cast<size_t>(i)] = static_cast<int>(std::lround(std::sin(i * 6.283185307179586 / 4096.0) * 4096.0));
    return values;
  }();
  return table;
}
}  // namespace

int rsin(int angle) { return sin_table()[static_cast<size_t>(angle & 0xFFF)]; }
int rcos(int angle) { return sin_table()[static_cast<size_t>((angle + 1024) & 0xFFF)]; }

int ratan2(int y, int x) {
  if (x == 0 && y == 0) return 0;
  return static_cast<int>(std::lround(std::atan2(static_cast<double>(y), static_cast<double>(x)) * 4096.0 / 6.283185307179586));
}

int ease(int phase, int mode) {
  phase = (phase & 0x1000) ? 0x1000 - (phase & 0xFFF) : (phase & 0xFFF);
  switch (mode) {
    case 1: return rsin(phase >> 2);
    case 2: return 0x1000 - rcos(phase >> 2);
    case 3:
      if (phase <= 0x800) return rsin(phase / 2) >> 1;
      return 0x1000 - (rsin(phase / 2) >> 1);
    case 4: return 0x800 - rcos(phase / 2) / 2;
    default: return phase;
  }
}

Mat3 Mat3::operator*(const Mat3& other) const {
  Mat3 result;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      float sum = 0;
      for (int k = 0; k < 3; ++k) sum += m[static_cast<size_t>(i * 3 + k)] * other.m[static_cast<size_t>(k * 3 + j)];
      result.m[static_cast<size_t>(i * 3 + j)] = sum;
    }
  return result;
}

Mat3 Mat3::from_angles(int ax, int ay, int az, bool zyx) {
  auto sin_of = [](int angle) { return static_cast<float>(rsin(angle)) / 4096.0f; };
  auto cos_of = [](int angle) { return static_cast<float>(rcos(angle)) / 4096.0f; };
  Mat3 rx, ry, rz;
  rx.m = {1, 0, 0, 0, cos_of(ax), -sin_of(ax), 0, sin_of(ax), cos_of(ax)};
  ry.m = {cos_of(ay), 0, sin_of(ay), 0, 1, 0, -sin_of(ay), 0, cos_of(ay)};
  rz.m = {cos_of(az), -sin_of(az), 0, sin_of(az), cos_of(az), 0, 0, 0, 1};
  return zyx ? rz * ry * rx : rx * ry * rz;
}

Mat3 Mat3::scaled_columns(float sx, float sy, float sz) const {
  Mat3 result = *this;
  for (int i = 0; i < 3; ++i) {
    result.m[static_cast<size_t>(i * 3 + 0)] *= sx;
    result.m[static_cast<size_t>(i * 3 + 1)] *= sy;
    result.m[static_cast<size_t>(i * 3 + 2)] *= sz;
  }
  return result;
}

Mat3 Mat3::scaled_rows(float sx, float sy, float sz) const {
  Mat3 result = *this;
  const float scale[3] = {sx, sy, sz};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) result.m[static_cast<size_t>(i * 3 + j)] *= scale[i];
  return result;
}

int modulate(int texel, int colour) { return std::min(255, texel * colour / 128); }

int blend_channel(int mode, int back, int front) {
  switch (mode) {
    case 0: return front;
    case 1: return (back + front) / 2;
    case 2: return std::min(255, back + front);
    case 3: return std::max(0, back - front);
    case 4: return std::min(255, back + front / 4);
    default: return front;
  }
}

}  // namespace oc::effects
