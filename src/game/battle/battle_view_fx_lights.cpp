// BattleView: forwards of the spell-run lighting (battle_spell_lights.h).
#include "game/battle/battle_view.h"

namespace oc::game {

void BattleView::fx_spell_run_begin() {
  spell_.run_begin(fx_.get(), fx_lights_);
}

void BattleView::fx_spell_light_tick() {
  effects::ViewParams view;
  if (fx_) view = fx_view();
  if (spell_.tick(fx_.get(), view, fx_lights_, fx_ambient_.scur, fx_lit_, tick_count_)) stage_scene_.set_spell_lights(spell_.set);
}

}  // namespace oc::game
