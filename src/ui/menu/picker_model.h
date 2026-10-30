#pragma once
// The Grand Slam picker's three creatures (ui::MonsterPickScreen): the battle model of each pick's pool slot (the Dopplegang creature
// table's model, scaled by its scale word; 0 = not drawn) in its clip 1, half a keyframe per model tick, swaying about the vertical
// axis by 128 sin(16 t) (4096 = a turn) at t model ticks (one per two 60 Hz frames). Each box is its own projection: centre
// (64 + 96 box, 112), distance 256 to the screen, the creature's feet 4096 in front of the eye.
#include <array>
#include <memory>
#include <vector>

#include "game/actor.h"
#include "game/battle/battle_pose.h"
#include "render/mesh_pipeline.h"
#include "ui/monster_pick_screen.h"
#include "vfs/vfs.h"

namespace oc::ui {

class PickerModelView : public MonsterModelView {
 public:
  PickerModelView(render::MeshPipeline& meshes, const vfs::Vfs& vfs) : meshes_(meshes), vfs_(vfs) {}
  void draw(const render::Frame& frame, float vw, float vh, const MonsterModelSlots& slots) override;

 private:
  struct Entry {
    std::shared_ptr<game::ModelHandle> body;
    game::UnitPose pose;
    // Model ticks taken.
    u32 ticks = 0;
  };
  // Per box: the pool slot shown and its model (a new pick restarts the clip).
  Entry* entry(int box, int pool_slot);
  render::MeshPipeline& meshes_;
  const vfs::Vfs& vfs_;
  std::array<int, 3> shown_{-1, -1, -1};
  std::array<std::unique_ptr<Entry>, 3> boxes_;
  std::vector<render::Mat4> palette_;
};

}  // namespace oc::ui
