#pragma once
// [Video] Crt: a simple full-frame CRT post pass (scanlines, a faint RGB mask, a vignette). The whole frame up to the ImGui overlay is
// drawn into an off-screen target of the frame's size (begin), then drawn to the frame through the CRT shader (end); the overlay
// and the toasts follow on the frame itself, so they are not filtered.
#include "render/pipeline.h"
#include "render/render_target.h"
#include "render/texture.h"

namespace oc::render {

class CrtPass {
 public:
  CrtPass() = default;
  ~CrtPass() { destroy(); }
  CrtPass(const CrtPass&) = delete;
  CrtPass& operator=(const CrtPass&) = delete;

  bool init(GpuContext& ctx);
  void destroy();

  // Redirects the frame's drawing into the pass's target (created / resized to the frame's extent). False = could not (draw normally).
  bool begin(GpuContext& ctx, const Frame& frame);
  // Ends the redirect and draws the target to the frame through the CRT shader.
  void end(GpuContext& ctx, const Frame& frame);
  bool active() const { return active_; }
  // The next end() draws the monochrome ramp (the pause screen filter) instead of the CRT look: out = L * rgb with L the luminance
  // of the picture (RDE cycles that constant colour while the pause menu is up).
  void set_ramp(bool on, const float (&rgb)[3]) {
    ramp_ = on;
    for (int i = 0; i < 3; ++i) ramp_rgb_[i] = rgb[i];
  }

 private:
  GpuContext* ctx_ = nullptr;
  SDL_GPUShader* vs_ = nullptr;
  SDL_GPUShader* fs_ = nullptr;
  TexRef set_;
  Pipeline pipeline_;
  Sampler sampler_;
  RenderTarget target_;
  bool active_ = false;
  bool ramp_ = false;
  float ramp_rgb_[3] = {1, 1, 1};
};

}  // namespace oc::render
