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
//   Space (not the speed keys), or on the frame the pad goes from some button held to none held (LT / RT / L3 / R3 do not
//   count), at any time: a press during a fade is consumed when the counter reaches the waiting step. A connected pad that is
//   left alone never sets it, so the controller page waits like the keyboard page. The game frame is not drawn under the
//   guide (black).
// - The glyph overlays, every row as RDE: the keyboard page shows RDE's two key sets (platform/keymap.h; arrows / WASD move, Enter / X
//   confirm, Backspace / Left Shift cancel and run, F / R key items, I / Tab menu, F1 / F2 speed modes; an unbound key moves its row
//   half a step right) and the fixed keys Space (pause), K (change lead character), F3 / F4 / F5, Esc. The pad page is a fixed picture:
//   A / B / X / Y for the field and battle rows, d-pad + left stick, Start, Back, LT, RT, L3, R3, L3 + R3, LT + L3.
//   The screen is not part of the PS1 game, so both art modes draw the same pages.
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
  // 1 = the controller page (fading in or waiting), 2 = the keyboard page (cross-fading in, waiting or fading out), 0 = closed.
  int page() const { return done_ ? 0 : counter_ <= 15 ? 1 : 2; }
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
  // Some latching pad button was held on the previous frame.
  bool pad_held_ = false;
};

}  // namespace oc::ui
