#pragma once
// Graphics pipeline for the frame / target images: formats come from the GpuContext, viewport/scissor are per render pass.
#include <span>

#include "render/gpu_context.h"

namespace oc::render {

// Multiply: dst = src * dst; Subtract: dst = dst - src; Premultiplied: dst = src + dst * (1 - src alpha) (compositing an off-screen
// target that was drawn with Alpha over a transparent clear: its colour is already multiplied by its coverage); Invert: out = src *
// (1 - dst) (a white source turns the backdrop into its negative); AdditiveAlpha: dst = dst + src * src alpha (straight-alpha add),
// destination alpha kept (a premultiplied composite of the target then adds it over the picture).
enum class Blend { None, Alpha, Additive, Multiply, Subtract, Premultiplied, Invert, AdditiveAlpha };

struct VertexAttribute {
  u32 location = 0;
  SDL_GPUVertexElementFormat format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
  u32 offset = 0;
};

struct PipelineDesc {
  SDL_GPUShader* vertex = nullptr;
  SDL_GPUShader* fragment = nullptr;
  // 0 = no vertex buffer (a full-screen triangle from gl_VertexIndex).
  u32 vertex_stride = 0;
  std::span<const VertexAttribute> attributes;
  SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  SDL_GPUCullMode cull = SDL_GPU_CULLMODE_NONE;
  Blend blend = Blend::None;
  bool depth_test = false;
  bool depth_write = false;
};

class Pipeline {
 public:
  Pipeline() = default;
  ~Pipeline() { destroy(); }
  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;

  bool create(GpuContext& ctx, const PipelineDesc& desc);
  void destroy();

  SDL_GPUGraphicsPipeline* handle() const { return pipeline_; }

 private:
  SDL_GPUDevice* device_ = nullptr;
  SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
};

}  // namespace oc::render
