#pragma once
// Base of the overlays that skin a model and draw it themselves: host access to the skinned model (FxModels::skin) and attach points,
// the stock-draw hide, and the shared draw (model_draw.h).
#include <cmath>
#include <vector>

#include "game/effects/bespoke/model_draw.h"
#include "game/effects/bespoke/overlay_base.h"
#include "game/effects/sequence_player.h"

namespace oc::effects {

// scene_remap.cpp: unico's caster pick (the unit nearest to caster 0's side point), shared with tunico.
std::vector<int> fx_nearest_to_side(const FxModels& ms, const FxModel* caster0);

class MeshTech : public OverlayFx {
 protected:
  bool skin(const FxModel* model, FxSkinnedModel& sk) const { return model && ctx_.models && ctx_.models->skin && ctx_.models->skin(model->unit, sk); }
  // The attach point's world position as s32 out[3], used by the overlays as their low halves.
  bool attach(const FxModel* model, int point, int out[3]) const {
    Vec3 pos{};
    if (!model || !ctx_.models || !ctx_.models->attach || !ctx_.models->attach(model->unit, point, pos)) return false;
    for (size_t k = 0; k < 3; ++k) out[k] = static_cast<s16>(std::lround(pos[k]));
    return true;
  }
  // The overlay hides the stock draw after its own; `shadow` = its draw draws the unit shadow itself (no flag test).
  static void own_draw(FxModel* model, bool on, bool shadow = true) {
    if (!model) return;
    model->hidden = on;
    model->custom_draw = on;
    model->own_shadow = on && shadow;
  }
  void draw_model(const FxModel* model, const ModelDrawSpec& spec, std::vector<FxPrim>& out, u32& order) const {
    FxSkinnedModel sk;
    if (!skin(model, sk)) return;
    model_draw(sk, *model, spec, ctx_.view, out, order);
  }
  // The overlays' common init tail: runs entry 0x80 of the external anmseq table (the pack's last type-0x1A entry, assigned at load),
  // variant 0 as the host's op 0x43.
  static void start_caster_anim(FxModel* caster) {
    if (caster) caster->script_table.clear(), caster->script_entry = 0x80, caster->script_variant = 0;
  }
};

}  // namespace oc::effects
