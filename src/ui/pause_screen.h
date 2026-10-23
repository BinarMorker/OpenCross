#pragma once
// RDE pause screen: the frozen game picture under a magenta colour filter, a slanted bar "PAUSE" and below it the option row
// "Vibration Function : On" with the small left / right arrows at the bar's right end. Text = lang.dat ui_pausemenu.txt (ids 0 PAUSE,
// 1 Vibration Function, 3 Off, 4 On; id 2 World Names exists but the pause screen does not use it), bar art = ui.dat ui/pause.png,
// font ui_font_gnb (HD) in both art modes.
//   p.open(vibration);   per frame: sfx = p.input(dx, dy, toggle, close);   p.draw(...)
#include <array>
#include <chrono>
#include <memory>
#include <string>

#include "import/text.h"
#include "render/sprite_batch.h"
#include "render/text_batch.h"
#include "render/texture.h"
#include "ui/ps1_font.h"
#include "vfs/vfs.h"

namespace oc::ui {

class PauseScreen {
 public:
  PauseScreen() = default;
  ~PauseScreen() { shutdown(); }
  PauseScreen(const PauseScreen&) = delete;
  PauseScreen& operator=(const PauseScreen&) = delete;

  bool init(render::GpuContext& ctx, const vfs::Vfs& vfs, render::SpriteBatch& sprites, render::TextBatch& text);
  void shutdown();
  void set_language(import::Lang lang) { lang_ = lang; }

  void open(bool vibration);
  void close() { open_ = false; }
  bool active() const { return open_; }
  bool vibration() const { return vibration_; }
  // The filter colour of this frame: a counter 0..479 advances once per frame (60 per second here, kept across openings as RDE's
  // global counter is), ramp = counter < 240 ? counter : 480 - counter, t = ramp / 240 * 0.53, colour = (1 - t, 0.47, 0.47 + t).
  void tint(float (&rgb)[3]);

  // One frame of input (edges): dx or `toggle` flips the value, `close` leaves; `dy` is accepted for symmetry with the other panels
  // and ignored. Returns the menu sound effect id to play (0 none, 1 cursor / toggle, 6 cancel).
  int input(int dx, int dy, bool toggle, bool close);
  // Draws over the frame (inside no begin / end); `resume` continues the frame's sprite pass as the other panels do.
  void draw(const render::Frame& frame, float vw, float vh, bool resume = false);

 private:
  std::u32string ui_text(int id, const char32_t* fallback);

  render::GpuContext* ctx_ = nullptr;
  const vfs::Vfs* vfs_ = nullptr;
  render::SpriteBatch* sprites_ = nullptr;
  render::TextBatch* text_ = nullptr;
  render::Sampler linear_, nearest_;
  render::Texture white_, bar_;
  render::TexRef white_set_ = {}, bar_set_ = {};
  float bar_w_ = 0, bar_h_ = 0;
  // ui/ui_font_gnb.spritefont: the bold condensed face of the pause screen.
  std::shared_ptr<const import::SpriteFont> font_;
  render::FaceId face_ = render::kNoFace;
  // Canvas units per font texel: the cap height is 55 % of the bar height.
  float font_scale_ = 1.0f;
  import::LangTables lt_;
  import::Lang lang_ = import::Lang::En;
  bool open_ = false, vibration_ = true;
  // Frames.
  double counter_ = 0;
  std::chrono::steady_clock::time_point last_{};
};

}  // namespace oc::ui
