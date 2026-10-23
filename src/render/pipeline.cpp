#include "render/pipeline.h"

#include <vector>

#include "core/log.h"

namespace oc::render {

namespace {

SDL_GPUColorTargetBlendState blend_state(Blend blend) {
  SDL_GPUColorTargetBlendState state{};
  state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
  if (blend == Blend::None) return state;
  state.enable_blend = true;
  state.src_color_blendfactor = blend == Blend::Alpha      ? SDL_GPU_BLENDFACTOR_SRC_ALPHA
                                : blend == Blend::Multiply ? SDL_GPU_BLENDFACTOR_DST_COLOR
                                                           : SDL_GPU_BLENDFACTOR_ONE;
  state.dst_color_blendfactor = blend == Blend::Alpha      ? SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
                                : blend == Blend::Multiply ? SDL_GPU_BLENDFACTOR_ZERO
                                                           : SDL_GPU_BLENDFACTOR_ONE;
  state.color_blend_op = blend == Blend::Subtract ? SDL_GPU_BLENDOP_REVERSE_SUBTRACT : SDL_GPU_BLENDOP_ADD;
  state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  state.dst_alpha_blendfactor = state.dst_color_blendfactor;
  state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  if (blend == Blend::Invert) {
    state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  }
  if (blend == Blend::Premultiplied) {
    state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  }
  return state;
}

}  // namespace

bool Pipeline::create(GpuContext& ctx, const PipelineDesc& desc) {
  destroy();
  device_ = ctx.device();

  SDL_GPUVertexBufferDescription vertex_buffer{};
  vertex_buffer.slot = 0;
  vertex_buffer.pitch = desc.vertex_stride;
  vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  std::vector<SDL_GPUVertexAttribute> attributes;
  for (const VertexAttribute& attribute : desc.attributes) {
    SDL_GPUVertexAttribute out{};
    out.location = attribute.location;
    out.buffer_slot = 0;
    out.format = attribute.format;
    out.offset = attribute.offset;
    attributes.push_back(out);
  }

  const SDL_GPUColorTargetDescription color_target{ctx.color_format(), blend_state(desc.blend)};

  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = desc.vertex;
  info.fragment_shader = desc.fragment;
  if (desc.vertex_stride > 0) {
    info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes.data();
    info.vertex_input_state.num_vertex_attributes = static_cast<u32>(attributes.size());
  }
  info.primitive_type = desc.topology;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = desc.cull;
  info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  info.rasterizer_state.enable_depth_clip = true;
  info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  info.depth_stencil_state.enable_depth_test = desc.depth_test;
  info.depth_stencil_state.enable_depth_write = desc.depth_write;
  info.target_info.color_target_descriptions = &color_target;
  info.target_info.num_color_targets = 1;
  info.target_info.depth_stencil_format = ctx.depth_format();
  info.target_info.has_depth_stencil_target = true;

  pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &info);
  if (!pipeline_) {
    log::error("SDL_CreateGPUGraphicsPipeline: {}", SDL_GetError());
    return false;
  }
  return true;
}

void Pipeline::destroy() {
  if (device_ && pipeline_) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
  pipeline_ = nullptr;
  device_ = nullptr;
}

}  // namespace oc::render
