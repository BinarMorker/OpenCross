#pragma once
// Field lighting. The retail field has no directional lights: every actor / room model carries one flat RGB multiplier
// (128 = neutral) that eases toward a per-room 16-entry table, indexed by the low nibble of the walk-mesh flag word of the
// triangle it stands on. The table is written by script op 0x67 (SET_SHADOW_COLOR, 16 x {r, g, b, pad}).
// Water is a separate per-face rule of the model renderer: faces below a model-space level use the water colour (op 0x87)
// instead of the light colour.
#include <array>
#include <span>

#include "core/types.h"

namespace oc::game {

// Per-actor smoothing state (128 = neutral). Value-initialised = neutral.
struct ActorLight {
  std::array<int, 3> cur = {128, 128, 128};
};

// Everything the mesh pipeline needs to shade one actor (parallel to the actor list of RoomView::draw_actors).
struct ActorLook {
  // tint: RoomLighting::tint. water_level: model y (+y down); >= 1e8 = dry.
  float tint[3] = {1, 1, 1};
  float water_level = 1e9f;
  float water_tint[3] = {0.25f, 0.25f, 1.4f};
};

class RoomLighting {
 public:
  static constexpr int kEntries = 16;
  // Retail eases 16 per 30 Hz frame; 60 Hz ticks use 8.
  static constexpr int kStepPerTick = 8;

  // Room change: no table = neutral everywhere.
  void clear();
  // Op 0x67 payload: 16 entries of 4 bytes (the 4th is unused).
  void set_table(std::span<const u8> rgbx16);
  bool has_table() const { return has_table_; }

  // The table colour for a triangle flag word (128 = neutral); neutral without a table.
  std::array<int, 3> target(u32 attr_flags) const;

  // Room entry / actor spawn: the current colour jumps to the target.
  void snap(ActorLight& light, u32 attr_flags, bool neutral = false) const;
  // One 60 Hz tick: the current colour eases toward the target by kStepPerTick per channel.
  // `neutral` = the actor's "unlit" flag: the target is 128 on every channel.
  void tick(ActorLight& light, u32 attr_flags, bool neutral = false) const;

  // Colour multiplier handed to MeshPipeline::DrawParams::tint: cur * entity_rgb / 128 / 128 per channel
  // (entity_rgb = ENTITY_RGB A1, 128 = neutral; model colour byte = cur * rgb >> 7, 128 = 1.0).
  static std::array<float, 3> tint(const ActorLight& light, const std::array<int, 3>& entity_rgb);
  static std::array<float, 3> tint(const ActorLight& light, const std::array<u8, 3>& entity_rgb) {
    return tint(light, std::array<int, 3>{entity_rgb[0], entity_rgb[1], entity_rgb[2]});
  }

  // The light colour of a point on triangle flags `attr_flags` without smoothing (tools / tests / probes).
  std::array<float, 3> light_for(u32 attr_flags) const;

  // Water rule: an actor on a triangle with flag bits 0x20000 / 0x40000 draws faces below the level (model y, +y down,
  // feet = 0; one level per slot 0 / 1 / 2 for 0x20000, 0x40000, both; default 10) in the op 0x87 colour.
  static constexpr u32 kWaterBits = 0x60000;
  static constexpr float kDefaultWaterLevel = 10.0f;
  // 0, 1, 2; -1 = dry.
  static int water_slot(u32 attr_flags) { return static_cast<int>(((attr_flags & kWaterBits) >> 17) - 1); }

 private:
  std::array<std::array<u8, 3>, kEntries> table_{};
  bool has_table_ = false;
};

}  // namespace oc::game
