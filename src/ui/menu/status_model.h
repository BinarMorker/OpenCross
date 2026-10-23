#pragma once
// Status-screen 3D model: the member's MENU model (cdrom menu/player/<folder>.ome: cpt entry 3 = Mdl,
// entry 5 = Atim pair, HD 4x texture hd/menu/player/<folder>.tim) in its pose clip, with the equipped weapon (weapon pack battle/wepon/*.bin,
// part = weapon record tier) attached at the model's weapon attach point, turning about the vertical axis; drawn with MeshPipeline after
// the menu's sprite pass.
#include <map>
#include <memory>
#include <string>

#include "game/actor.h"
#include "game/battle/battle_pose.h"
#include "ui/menu/menu_model.h"
#include "vfs/vfs.h"

namespace oc::ui {

class StatusModelView : public MenuModelView {
 public:
  StatusModelView(render::MeshPipeline& meshes, const vfs::Vfs& vfs) : meshes_(meshes), vfs_(vfs) {}
  void draw(const render::Frame& frame, float vw, float vh, const MenuModelSlot& slot) override;
  // Drops the GPU models before the pipeline is destroyed.
  void release();

 private:
  struct Entry {
    std::shared_ptr<game::ModelHandle> body;
    game::UnitPose pose;
    std::shared_ptr<game::ModelHandle> weapon;  // current weapon part
    int weapon_part = -2;
    int attach = -1;  // body attach point used for the weapon
    float y_min = 0, y_max = 0;  // bind-pose extent (model units, y down)
    float accum = 0;
    int pose_id = -1;
    bool logged = false;
  };
  // Cached per (character, art mode).
  Entry* entry(int char_id, bool hd_art);
  void set_weapon(Entry& model, int char_id, int part);
  render::MeshPipeline& meshes_;
  const vfs::Vfs& vfs_;
  std::map<int, std::unique_ptr<Entry>> cache_;  // key = char_id * 2 + hd_art
  std::map<std::string, std::shared_ptr<game::ModelHandle>> weapons_;  // "<pack>#<part>"
  std::vector<render::Mat4> palette_;
};

}  // namespace oc::ui
