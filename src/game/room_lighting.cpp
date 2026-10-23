#include "game/room_lighting.h"

#include <algorithm>

namespace oc::game {

namespace {
int approach(int cur, int target, int step) {
  if (cur == target) return cur;
  if (cur < target) return std::min(cur + step, target);
  return std::max(cur - step, target);
}
}  // namespace

void RoomLighting::clear() {
  table_ = {};
  has_table_ = false;
}

void RoomLighting::set_table(std::span<const u8> rgbx16) {
  for (size_t i = 0; i < static_cast<size_t>(kEntries); ++i)
    for (size_t k = 0; k < 3; ++k) table_[i][k] = 4 * i + k < rgbx16.size() ? rgbx16[4 * i + k] : u8{128};
  has_table_ = true;
}

std::array<int, 3> RoomLighting::target(u32 attr_flags) const {
  if (!has_table_) return {128, 128, 128};
  const auto& entry = table_[attr_flags & 15];
  return {entry[0], entry[1], entry[2]};
}

void RoomLighting::snap(ActorLight& light, u32 attr_flags, bool neutral) const {
  light.cur = neutral ? std::array<int, 3>{128, 128, 128} : target(attr_flags);
}

void RoomLighting::tick(ActorLight& light, u32 attr_flags, bool neutral) const {
  const std::array<int, 3> target_rgb = neutral ? std::array<int, 3>{128, 128, 128} : target(attr_flags);
  for (size_t k = 0; k < 3; ++k) light.cur[k] = approach(light.cur[k], target_rgb[k], kStepPerTick);
}

std::array<float, 3> RoomLighting::tint(const ActorLight& light, const std::array<int, 3>& rgb) {
  std::array<float, 3> out{};
  for (size_t k = 0; k < 3; ++k) out[k] = static_cast<float>(light.cur[k]) / 128.0f * static_cast<float>(rgb[k]) / 128.0f;
  return out;
}

std::array<float, 3> RoomLighting::light_for(u32 attr_flags) const {
  const auto rgb = target(attr_flags);
  return {rgb[0] / 128.0f, rgb[1] / 128.0f, rgb[2] / 128.0f};
}

}  // namespace oc::game
