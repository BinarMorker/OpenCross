#include "render/crt_pass.h"

#include "render/shaders.h"

namespace oc::render {

namespace {

#include "crt.vert.inc"

// Scanlines at the PS1's 240 lines (never finer than 2 output pixels), a faint RGB triad mask when the window is wide enough to show it,
// a soft vignette, a little gain to make up for the darkening.
#include "crt.frag.inc"

}  // namespace

bool CrtPass::init(GpuContext& ctx) {
  destroy();
  ctx_ = &ctx;
  vs_ = create_shader(ctx, SDL_GPU_SHADERSTAGE_VERTEX, crt_vert, ShaderDesc{0, 0, 0}, "crt.vert");
  fs_ = create_shader(ctx, SDL_GPU_SHADERSTAGE_FRAGMENT, crt_frag, ShaderDesc{1, 0, 1}, "crt.frag");
  if (!vs_ || !fs_ || !sampler_.create(ctx, true)) return false;
  PipelineDesc pd;
  pd.vertex = vs_;
  pd.fragment = fs_;
  pd.vertex_stride = 0;
  pd.blend = Blend::None;
  return pipeline_.create(ctx, pd);
}

void CrtPass::destroy() {
  active_ = false;
  pipeline_.destroy();
  target_.destroy();
  sampler_.destroy();
  if (ctx_) {
    if (vs_) SDL_ReleaseGPUShader(ctx_->device(), vs_);
    if (fs_) SDL_ReleaseGPUShader(ctx_->device(), fs_);
  }
  vs_ = fs_ = nullptr;
  set_ = {};
  ctx_ = nullptr;
}

bool CrtPass::begin(GpuContext& ctx, const Frame& frame) {
  active_ = false;
  if (!pipeline_.handle()) return false;
  if (!target_.valid() || target_.width() != frame.extent.width || target_.height() != frame.extent.height) {
    target_.destroy();
    if (!target_.create(ctx, frame.extent.width, frame.extent.height, true)) return false;
    set_ = target_.ref(sampler_);
  }
  Frame inner;
  ctx.begin_target(target_, frame, inner);
  active_ = true;
  return true;
}

void CrtPass::end(GpuContext& ctx, const Frame& frame) {
  if (!active_) return;
  active_ = false;
  ctx.end_target(target_);
  const float size[8] = {static_cast<float>(frame.extent.width), static_cast<float>(frame.extent.height), ramp_ ? 1.0f : 0.0f, 0.0f, ramp_rgb_[0], ramp_rgb_[1], ramp_rgb_[2], 1.0f};
  SDL_BindGPUGraphicsPipeline(ctx.pass(), pipeline_.handle());
  const SDL_GPUTextureSamplerBinding binding{set_.texture, set_.sampler};
  SDL_BindGPUFragmentSamplers(ctx.pass(), 0, &binding, 1);
  SDL_PushGPUFragmentUniformData(ctx.cmd(), 0, size, sizeof size);
  SDL_DrawGPUPrimitives(ctx.pass(), 3, 1, 0, 0);
}

}  // namespace oc::render
