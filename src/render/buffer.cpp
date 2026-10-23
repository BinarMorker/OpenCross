#include "render/buffer.h"

#include <cstring>

#include "core/log.h"

namespace oc::render {

bool Buffer::create(GpuContext& ctx, const void* data, u32 size, SDL_GPUBufferUsageFlags usage) {
  destroy();
  device_ = ctx.device();
  size_ = size;
  SDL_GPUBufferCreateInfo buffer_info{};
  buffer_info.usage = usage;
  buffer_info.size = size;
  buffer_ = SDL_CreateGPUBuffer(device_, &buffer_info);
  SDL_GPUTransferBufferCreateInfo transfer_info{};
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = size;
  SDL_GPUTransferBuffer* transfer = buffer_ ? SDL_CreateGPUTransferBuffer(device_, &transfer_info) : nullptr;
  void* mapped = transfer ? SDL_MapGPUTransferBuffer(device_, transfer, false) : nullptr;
  if (!mapped) {
    log::error("buffer of {} bytes: {}", size, SDL_GetError());
    if (transfer) SDL_ReleaseGPUTransferBuffer(device_, transfer);
    destroy();
    return false;
  }
  std::memcpy(mapped, data, size);
  SDL_UnmapGPUTransferBuffer(device_, transfer);
  UploadOp op;
  op.source = transfer;
  op.buffer = buffer_;
  op.size = size;
  ctx.upload(op);
  return true;
}

void Buffer::destroy() {
  if (device_ && buffer_) SDL_ReleaseGPUBuffer(device_, buffer_);
  buffer_ = nullptr;
  device_ = nullptr;
  size_ = 0;
}

bool StreamBuffer::create(GpuContext& ctx, u32 size, SDL_GPUBufferUsageFlags usage) {
  destroy();
  ctx_ = &ctx;
  size_ = size;
  SDL_GPUDevice* device = ctx.device();
  SDL_GPUBufferCreateInfo buffer_info{};
  buffer_info.usage = usage;
  buffer_info.size = size;
  SDL_GPUTransferBufferCreateInfo transfer_info{};
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = size;
  for (u32 i = 0; i < kFramesInFlight; ++i) {
    buffers_[i] = SDL_CreateGPUBuffer(device, &buffer_info);
    transfers_[i] = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (!buffers_[i] || !transfers_[i]) {
      log::error("stream buffer of {} bytes: {}", size, SDL_GetError());
      destroy();
      return false;
    }
  }
  return true;
}

void StreamBuffer::destroy() {
  if (ctx_) {
    SDL_GPUDevice* device = ctx_->device();
    for (u32 i = 0; i < kFramesInFlight; ++i) {
      if (transfers_[i]) SDL_ReleaseGPUTransferBuffer(device, transfers_[i]);
      if (buffers_[i]) SDL_ReleaseGPUBuffer(device, buffers_[i]);
      transfers_[i] = nullptr;
      buffers_[i] = nullptr;
    }
  }
  ctx_ = nullptr;
  mapped_ = nullptr;
  size_ = used_ = slot_ = 0;
  serial_ = 0;
}

void* StreamBuffer::begin(const Frame& frame) {
  if (frame.serial != serial_) {
    serial_ = frame.serial;
    slot_ = frame.index;
    used_ = 0;
    mapped_ = SDL_MapGPUTransferBuffer(ctx_->device(), transfers_[slot_], false);
    ctx_->register_stream(*this);
  }
  return mapped_;
}

void StreamBuffer::flush(SDL_GPUCopyPass* pass) {
  SDL_UnmapGPUTransferBuffer(ctx_->device(), transfers_[slot_]);
  if (used_ > 0) {
    const SDL_GPUTransferBufferLocation source{transfers_[slot_], 0};
    const SDL_GPUBufferRegion destination{buffers_[slot_], 0, used_};
    SDL_UploadToGPUBuffer(pass, &source, &destination, false);
  }
  mapped_ = nullptr;
}

}  // namespace oc::render
