#include "ui/menu/picker_model.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "battle/dopple_table.h"
#include "core/log.h"
#include "game/battle/battle_model_cache.h"
#include "game/battle/model_table.h"
#include "render/mat4.h"

namespace oc::ui {

namespace {
// Box centres (PS1 px), the projection-plane distance and the eye's distance from the creature's feet.
constexpr float kBoxX = 64, kBoxPitch = 96, kBoxY = 112, kProjection = 256, kDistance = 4096;
// The creature's motion: clip 1 at 128 / 256 keyframe per model tick; a model tick every second 60 Hz frame.
constexpr int kClip = 1, kSpeed = 128;
constexpr u32 kFramesPerTick = 2;
// The sway: amplitude 128 (4096 = a turn), phase +16 per model tick (the first tick already has its step).
constexpr float kSwayAmplitude = 128, kSwayStep = 16;
constexpr float kTurnDegrees = 360.0f / 4096.0f;
constexpr float kPi = 3.14159265358979323846f;
// At most this many model ticks are caught up in one draw.
constexpr u32 kMaxSteps = 8;
}  // namespace

PickerModelView::Entry* PickerModelView::entry(int box, int pool_slot) {
  auto& held = boxes_[static_cast<size_t>(box)];
  if (shown_[static_cast<size_t>(box)] == pool_slot) return held.get();
  shown_[static_cast<size_t>(box)] = pool_slot;
  held.reset();
  if (pool_slot < 0 || static_cast<size_t>(pool_slot) >= battle::kDoppleTable.size()) return nullptr;
  const size_t model = battle::kDoppleTable[static_cast<size_t>(pool_slot)].model;
  if (model >= game::kBattleModels.size() || game::kBattleModels[model].empty()) return nullptr;
  std::string err;
  auto asset = game::ModelCache::read_model(vfs_, std::string(game::kBattleModels[model]), &err);
  if (!asset) {
    log::warn("monster picker: model {} of pool slot {}: {}", game::kBattleModels[model], pool_slot, err);
    return nullptr;
  }
  held = std::make_unique<Entry>();
  held->body = game::Actor::make_resolved_model(meshes_, std::move(*asset));
  held->pose.set_model(&held->body->asset);
  held->pose.set_speed(kSpeed);
  held->pose.set_loop(true);
  held->pose.play(kClip, 0);
  return held.get();
}

void PickerModelView::draw(const render::Frame& frame, float vw, float vh, const MonsterModelSlots& slots) {
  const u32 model_ticks = slots.tick / kFramesPerTick;
  const float sway = kSwayAmplitude * std::sin(2.0f * kPi * kSwayStep * static_cast<float>(model_ticks + 1) / 4096.0f);
  bool begun = false;
  for (int box = 0; box < 3; ++box) {
    const int pool_slot = slots.pool_slot[static_cast<size_t>(box)];
    Entry* shown = entry(box, pool_slot);
    if (!shown || !shown->body || !shown->body->gpu) continue;
    if (shown->ticks > model_ticks) shown->ticks = model_ticks;
    for (u32 step = 0; shown->ticks < model_ticks && step < kMaxSteps; ++step, ++shown->ticks) shown->pose.tick();
    shown->ticks = model_ticks;
    const float scale = static_cast<float>(battle::kDoppleTable[static_cast<size_t>(pool_slot)].val) / 4096.0f;
    if (scale <= 0.0f) continue;
    shown->pose.palette(0.0f, palette_);
    const float centre_x = kBoxX + kBoxPitch * static_cast<float>(box);
    render::Mat4 proj;
    proj.at(0, 0) = 2.0f * slots.unit_x * kProjection / vw;
    proj.at(0, 2) = 2.0f * (slots.origin_x + slots.unit_x * centre_x) / vw - 1.0f;
    proj.at(1, 1) = 2.0f * slots.unit_y * kProjection / vh;
    proj.at(1, 2) = 2.0f * (slots.origin_y + slots.unit_y * kBoxY) / vh - 1.0f;
    proj.at(2, 2) = 0.5f;
    proj.at(3, 2) = 1.0f;
    proj.at(3, 3) = 0.0f;
    const render::Mat4 view = render::mat4_translation(0, 0, kDistance) * render::mat4_from_rt(assets::rotation_zyx({0, sway * kTurnDegrees, 0}), {0, 0, 0});
    render::MeshPipeline::DrawParams dp;
    dp.view_proj = proj * view;
    dp.view = view;
    dp.model = render::mat4_scale(scale);
    dp.palette = palette_;
    dp.linear = dp.hd = slots.hd;
    dp.ot_faces = true;
    if (!begun) {
      // Drawn over the picker's sprites in the same pass: start from a clean depth buffer.
      frame.clear_depth();
      meshes_.begin(frame, frame.full());
      begun = true;
    }
    meshes_.draw(*shown->body->gpu, dp);
  }
  if (begun) meshes_.end();
}

}  // namespace oc::ui
