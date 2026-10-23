#pragma once
// Shader programs embedded at build time in every format a backend may ask for (src/render/CMakeLists.txt): SPIR-V (Vulkan),
// DXIL (Direct3D 12) and MSL (Metal). create_shader picks the one the device accepts.
#include "render/gpu_context.h"

namespace oc::render {

struct ShaderCode {
  const u8* spirv = nullptr;
  size_t spirv_size = 0;
  const u8* dxil = nullptr;
  size_t dxil_size = 0;
  const u8* msl = nullptr;
  size_t msl_size = 0;
};

// What the shader declares, in SDL_GPU's binding order (samplers first, then storage buffers; uniform blocks in their own set).
struct ShaderDesc {
  u32 samplers = 0;
  u32 storage_buffers = 0;
  u32 uniform_buffers = 0;
};

// Null when the device accepts none of the embedded formats or the shader fails to build.
SDL_GPUShader* create_shader(GpuContext& ctx, SDL_GPUShaderStage stage, const ShaderCode& code, const ShaderDesc& desc,
                             const char* name);

}  // namespace oc::render
