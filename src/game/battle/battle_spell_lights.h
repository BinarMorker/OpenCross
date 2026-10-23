#pragma once
// SpellLights: the [Enhancements] spell-run lighting. Once per tick the running effect's bright additive primitives become point
// lights (units: DrawParams::lights, stage: StageScene::set_spell_lights), together with the pack's own PS1 light slots.
#include <array>
#include <span>
#include <string>
#include <unordered_map>

#include "game/effects/emitter_system.h"
#include "game/effects/light_extract.h"
#include "render/lights.h"

namespace oc::game {

class BattleFxRun;

// The caches / slot tracking belong to `run`; amb_dark = this run darkened the ambient (contrast fade of the slot lights); slot_gate eases
// to 1 while a pack's lit window is open; slot_base = the light table when the run started; slot_live = slot written during this run;
// set = the lights of this tick.
struct SpellLights {
  bool lights_on = false;
  effects::LightTracker tracker;
  effects::SpriteColourCache sprite_colours;
  std::unordered_map<const std::string*, std::array<float, 4>> mesh_colours;
  const void* run = nullptr;
  int last_ticks = 0;
  bool amb_dark = false;
  float slot_gate = 0.0f;
  effects::LightTable slot_base;
  std::array<bool, 3> slot_live{false, false, false};
  render::LightSet set;

  // A new BattleFxRun exists (called right after its creation, before it starts, so the slot baseline predates everything it writes).
  void run_begin(const BattleFxRun* fx, const effects::LightTable& lights);
  // The pack's sprites go away with it (the lights keep decaying from the tracker's own state).
  void clear_colour_caches();
  // One tick. `view` is the effect camera (read only with a running effect), `light_table` the PS1 light slots, `ambient` the scene ambient
  // multiplier (/ 128) per channel, `unit_lit` the per-unit lit flags. True when `set` changed and the stage must take it.
  bool tick(BattleFxRun* fx, const effects::ViewParams& view, const effects::LightTable& light_table, const float (&ambient)[3], std::span<const bool> unit_lit, int tick_count);
};

}  // namespace oc::game
