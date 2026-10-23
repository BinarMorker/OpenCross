#pragma once
// CPU reference rasteriser for DrawQuads: nearest-texel sampling, the GPU blend arithmetic of fx_math.h. Used by `ocprd preview`
// and the tests; the GPU path (gpu_effects.h) draws the same quads with particles.h.
#include "game/effects/emitter_system.h"

namespace oc::effects {

class SoftCanvas {
 public:
  SoftCanvas(int w, int h) : w_(w), h_(h), rgba_(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0) {
    for (size_t i = 3; i < rgba_.size(); i += 4) rgba_[i] = 255;
  }
  int width() const { return w_; }
  int height() const { return h_; }
  const Bytes& rgba() const { return rgba_; }
  Bytes& rgba() { return rgba_; }

  // Alpha-over of an RGBA8 image at (x, y); alpha 0 texels are skipped, others replace (the backdrop layers are opaque).
  void blit(const u8* img, int iw, int ih, int x, int y);
  // One quad, painter style (call in sort_draw_order order). Returns the number of pixels written.
  int draw(const DrawQuad& quad);

 private:
  int w_, h_;
  Bytes rgba_;
};

}  // namespace oc::effects
