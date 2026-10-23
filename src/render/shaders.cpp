#include "render/shaders.h"

#include "core/log.h"

namespace oc::render {

SDL_GPUShader* create_shader(GpuContext& ctx, SDL_GPUShaderStage stage, const ShaderCode& code, const ShaderDesc& desc,
                             const char* name) {
  const SDL_GPUShaderFormat accepted = SDL_GetGPUShaderFormats(ctx.device());
  SDL_GPUShaderCreateInfo info{};
  if ((accepted & SDL_GPU_SHADERFORMAT_SPIRV) && code.spirv_size > 0) {
    info.code = code.spirv;
    info.code_size = code.spirv_size;
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.entrypoint = "main";
  } else if ((accepted & SDL_GPU_SHADERFORMAT_DXIL) && code.dxil_size > 0) {
    info.code = code.dxil;
    info.code_size = code.dxil_size;
    info.format = SDL_GPU_SHADERFORMAT_DXIL;
    info.entrypoint = "main";
  } else if ((accepted & SDL_GPU_SHADERFORMAT_MSL) && code.msl_size > 0) {
    info.code = code.msl;
    info.code_size = code.msl_size;
    info.format = SDL_GPU_SHADERFORMAT_MSL;
    info.entrypoint = "main0";
  } else {
    log::error("shader '{}': no embedded format accepted by the device", name);
    return nullptr;
  }
  info.stage = stage;
  info.num_samplers = desc.samplers;
  info.num_storage_textures = 0;
  info.num_storage_buffers = desc.storage_buffers;
  info.num_uniform_buffers = desc.uniform_buffers;
  SDL_GPUShader* shader = SDL_CreateGPUShader(ctx.device(), &info);
  if (!shader) log::error("shader '{}': SDL_CreateGPUShader: {}", name, SDL_GetError());
  return shader;
}

}  // namespace oc::render
