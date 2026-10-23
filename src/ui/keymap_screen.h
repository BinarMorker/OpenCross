#pragma once
// The "user guide" (controls / key map) screen, shown by New Game between the title menu and the name entry.
//
// - Trigger: the title script's start-game op. The emulation pauses and only the guide is drawn until it closes, then the op
//   runs (-> name entry). No once-only flag: every New Game shows it.
// - Pages: two full-screen images from hd.dat with the text baked in: `userguide/steampad_<lang>.png` ("Controller Controls",
//   plus Xbox glyphs from `ui/gamepad_assign.png`, 14 x 4 cells of 96 px, the glyph rows are the odd rows) and
//   `userguide/steamkey_<lang>.png` ("Keyboard Controls", plus the player's key bindings drawn from `ui/keyboard_<l>.png`,
//   16 x 7 cells of 48 px, cell chosen by virtual key). Layout in the 1620 x 1215 image space, scaled by width / 1620.
//   Both pages always (no pad / keyboard mode test). Languages: en fr it du es jp (image suffix), e f i g s j (keyboard sheet).
// - Timing (one counter step per presented frame): page 1 fades in over 15 frames (alpha k / 15) and waits at
//   k = 15 for the "next" latch; 15..29 cross-fades page 2 in over page 1 (page 1 stays opaque); waits at k = 30; 30..44 fades
//   page 2 out to black ((45 - k) / 15); k = 45 closes. The latch is set by any key press with a keyboard glyph or
//   Space (not the speed keys), or by the release of a pad button (not LT / RT / L3 / R3), at any time: a press during a fade
//   is consumed when the counter reaches the waiting step. The game frame is not drawn under the guide (black).
// - The glyph overlays show OpenCross's own bindings (title/nav.h, app/app_sim.cpp): arrows / WASD move, Enter / Z confirm,
//   X cancel + Shift run, F key items, Tab menu, Esc options; pad A / B / Y / Start, d-pad + left stick. Guide rows for
//   features OpenCross does not implement (pause, change lead, speed modes, F3 / F4 / F5) stay empty, like an unbound key.
//   The screen is not part of the PS1 game, so both art modes draw the same pages.
#include <array>

#include "import/text.h"
#include "platform/window.h"
#include "ui/title/canvas.h"

namespace oc::ui {

class KeyMapScreen {
 public:
  // Loads the pages for `lang` (cached per language) and restarts the counter. Returns false when the page images are missing
  // (then done() is true at once and nothing is drawn).
  bool begin(UiKit& kit, import::Lang lang);
  // One presented frame; `dt` in seconds (the counter steps once per 1/60 s).
  void update(const platform::Input& in, float dt);
  bool done() const { return done_; }
  void draw(DrawList& dl, UiKit& kit) const;

 private:
  void step();
  render::TexRef pad_page_ = {}, key_page_ = {}, pad_glyphs_ = {}, key_glyphs_ = {};
  int loaded_lang_ = -1;
  int counter_ = 0;
  float acc_ = 0;
  // The "next" latch.
  bool next_ = false;
  bool done_ = true;
  std::array<bool, platform::Input::kButtons> pad_prev_{};
};

}  // namespace oc::ui
