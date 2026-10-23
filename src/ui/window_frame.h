// Skinned window frame of the Settings "Custom Window Frame" preview.
// Pure geometry: the quads (canvas units, 1280 x 960) of one window of a skin sheet wNN.png / wN.tim - the 7 x 4 fill grid, the corner
// pieces, the top / bottom / left / right edge strips and the page leaf - so the menu can draw them through its own sprite path and a
// unit test can check the layout. Same piece model as the message window (DialogueBox::draw_tiles) and the key-item list panel.
#pragma once

#include <vector>

namespace oc::ui {

struct WinQuad {
  // Position and size in canvas units, then the normalised sheet uv.
  float x = 0, y = 0, w = 0, h = 0;
  float u0 = 0, v0 = 0, du = 0, dv = 0;
};

// quads are in draw order: fill grid, corners + top / bottom edges, left edge, right corners + right edge.
// leaf is the page leaf (close triangle), bottom-right; its u0 depends on the phase given to build().
// left / top / right / bottom bound the fill grid (canvas).
struct SkinWindow {
  std::vector<WinQuad> quads;
  WinQuad leaf;
  float left = 0, top = 0, right = 0, bottom = 0;
};

// `skin` = texture index 0..15 (w01..w15, w17; byte 0 -> 3, 1..3 -> 0..2, 4..14 -> same). `leaf_phase` 0..4 (u0 .5 + .125 min(phase, 3)).
SkinWindow build_skin_window(int skin, int leaf_phase);

// Rest x of the grid's first column per skin.
float skin_window_x(int skin);

}  // namespace oc::ui
