#pragma once
// RDE's boot logo: before the PS1 core is started at all, CHRONOCROSS.exe draws `data\ui\logo.png` (SQUARE ENIX, 1280 x 960, black
// around the lettering) - or, in Japanese, `notice.png`, `notice2.png`, `logo.png` - one image per 180 presented frames: fade in over
// frames 0..29 (alpha i / 30), opaque 30..149, fade out over 150..179 (alpha 1 - (i - 150) / 30), over black. A key / mouse click /
// pad button jumps the counter to the next multiple of 180: the image ends at once, without its fade-out. When the counter reaches
// the end the core is released and the game boots; it is a once-per-process screen (never on a return to the title). No art-mode
// test: both art modes show it. The PS1 disc has no boot logo of its own (the opening room plays its FMV directly), so this is the
// only logo asset there is.
#include <cstddef>

namespace oc::ui {

class BootLogo {
 public:
  static constexpr int kFramesPerImage = 180;
  static constexpr int kFadeFrames = 30;

  // `images` = 1 (logo) or 3 (Japanese: notice, notice2, logo); 0 = no logo (inactive).
  void begin(int images) {
    images_ = images < 0 ? 0 : images;
    counter_ = 0;
    skip_ = false;
  }
  bool active() const { return counter_ < images_ * kFramesPerImage; }
  // Which image the current frame shows (0-based) and its alpha (vertex colour (1, 1, 1, a) over the black frame).
  int image() const { return counter_ / kFramesPerImage; }
  float alpha() const {
    const int frame = counter_ % kFramesPerImage;
    if (frame < kFadeFrames) return static_cast<float>(frame) / kFadeFrames;
    if (frame > kFramesPerImage - kFadeFrames - 1) return 1.0f - static_cast<float>(frame - (kFramesPerImage - kFadeFrames)) / kFadeFrames;
    return 1.0f;
  }
  // A key / button press: applied after the next frame's counter step, as RDE does.
  void request_skip() {
    if (active()) skip_ = true;
  }
  // One presented frame (RDE draws with the counter, then increments it, then applies a pending skip).
  void step() {
    if (!active()) return;
    ++counter_;
    if (skip_) {
      const int base = counter_ / kFramesPerImage * kFramesPerImage;
      if (counter_ != base) counter_ = base + kFramesPerImage;
      skip_ = false;
    }
  }
  int counter() const { return counter_; }

 private:
  int images_ = 0;
  int counter_ = 0;
  bool skip_ = false;
};

}  // namespace oc::ui
