#pragma once
// The dial needle of the menus / name entry / shops: handle.png (405 x 58, tip at the left) is the needle quad of 98 x 14 PS1 px
// (0.242 PS1 px per image px, tip at P = -314 image px from the pivot) turned by `a` (4096 = 2 pi, theta = (a + 1024) pi / 2048).
// SpriteBatch has no rotation, so the rotated needle is resampled (bilinear) into a square RGBA image whose
// centre is the pivot; the caller draws it centred on the pivot at (2 half_w, 2 half_h) canvas px.
#include <vector>

#include "core/types.h"

namespace oc::ui {

struct HandImage {
  u32 w = 0, h = 0;
  float half_w = 0, half_h = 0;  // canvas px
  std::vector<u8> rgba;
};

// sx / sy = canvas px per PS1 px of the screen (menus 2.5 x 2.1429, name entry 2.5 x 2.2222).
HandImage render_hand(const u8* src_rgba, u32 sw, u32 sh, int angle, float sx, float sy);
// Needle angle `a` whose TIP points along the canvas direction (dx, dy) from the pivot.
int hand_angle_toward(float dx, float dy, float sx, float sy);

}  // namespace oc::ui
