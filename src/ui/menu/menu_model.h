#pragma once
// Hook for the 3D character model of the Status screen (the member's model rotates on the right).
// oc_ui cannot depend on the mesh pipeline / Actor (oc_game depends on oc_ui), so the host supplies a MenuModelView
// (ui/menu/status_model.h in oc_menu_model is the implementation) and MainMenu calls it after its sprite pass.
#include "render/gpu_context.h"

namespace oc::ui {

struct MenuModelSlot {
  int char_id = 0;        // character record id (0 Serge .. 43), the model is fieldobj/player/<folder>
  float cx = 0;           // window pixels: horizontal centre of the figure
  float feet_y = 0;       // window pixels: ground point below the figure
  float height = 0;       // window pixels: head-to-feet size of the figure
  float yaw_turns = 0;    // rotation about the vertical axis in turns (0 = facing the camera)
  bool hd = true;         // 4x textures + linear filtering
  bool low_hp = false;    // hurt pose (curHP < maxHP / 8 and no status & 0x39)
  float dt = 0;           // seconds since the previous draw (animation clock)
  int pose = 0;           // menu pose clip 0..2 (square cycles it); the hurt pose 3 is chosen from low_hp
  int weapon_part = -1;   // part of the weapon pack = weapon record tier & 0xF, -1 = no weapon
};

class MenuModelView {
 public:
  virtual ~MenuModelView() = default;
  // Called with no SpriteBatch pass open, once per frame while the Status screen is shown.
  virtual void draw(const render::Frame& frame, float vw, float vh, const MenuModelSlot& slot) = 0;
};

}  // namespace oc::ui
