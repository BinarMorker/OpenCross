// Field -> battle hand-over on the battle side. Retail has no screen warp: the field plays its encounter
// effect 'enct' (FieldWorld, effect/particle.prd > fenc, 105 ticks) and only then starts the battle, which copies the
// displayed frame to VRAM (0x1C0, 0x100) and clears the screen to white (ClearImage(0, 0, 0x280, 0xE0, white)); the battle.prd > miya
// `fade` emitter (flat quad, colour 4090 / 4096 falling 34 per tick, LIFE 120, timeline wait 80) then fades the white out over the
// battle's own opening camera. That fade is white_ticks_ (BattleView::tick / draw_scene).
#include "game/battle/battle_view.h"
#include "render/gpu_context.h"

namespace oc::game {

// Called by the host on the frame the battle starts (after the field's 'enct' task ended): the screen is white from here.
// In retail, battle tick 0 (btl_frame 0) is run frame ~121 while the field picture is still on screen (its last 7 ticks
// dim to black, frames 122..128): the battle is already 7 ticks old when the first battle frame shows. The host starts it at the end of the
// field's hold, so those ticks are run here (camera intro, units, the `fade` quad's clock).
constexpr int kHoldOverlapTicks = 1;

void BattleView::begin_transition(render::GpuContext&, render::TexRef carry, int main_clock) {
  free_transition();
  white_ticks_ = 0;
  hand_carry_ = carry;
  hand_tick_ = -1;
  rde_hand_ = static_cast<bool>(carry);
  rde_m0_ = main_clock;
  for (int i = 0; i < kHoldOverlapTicks; ++i) tick();
}

// Nothing to step: the white fade runs with the battle's ticks (no field frame is held, nothing stalls the battle).
void BattleView::update_transition(float) {}

// The white is drawn by the battle scene itself (draw_scene); nothing is drawn over the field.
void BattleView::draw_transition(const render::Frame&, bool) {}

void BattleView::free_transition() {
  trans_run_ = trans_wait_ = false;
  trans_px_.clear();
  trans_.set = {};
  trans_.tex.destroy();
}

}  // namespace oc::game
