#pragma once
// GPU buffers: static (filled once: model vertices / indices) and streamed (rewritten every frame: batch vertices, per-draw data).
#include "render/gpu_context.h"

namespace oc::render {

class Buffer {
 public:
  Buffer() = default;
  ~Buffer() { destroy(); }
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  // Filled once from `data` (SDL_GPU_BUFFERUSAGE_VERTEX / INDEX / GRAPHICS_STORAGE_READ).
  bool create(GpuContext& ctx, const void* data, u32 size, SDL_GPUBufferUsageFlags usage);
  void destroy();

  SDL_GPUBuffer* handle() const { return buffer_; }
  u32 size() const { return size_; }

 private:
  SDL_GPUDevice* device_ = nullptr;
  SDL_GPUBuffer* buffer_ = nullptr;
  u32 size_ = 0;
};

// Per-frame data written through a mapped transfer buffer during the frame and copied into the GPU buffer before the frame's draws
// run (one transfer + GPU buffer per frame in flight). Writes are append-only within a frame: begin() maps the frame's memory on its
// first call of the frame, mark_used() grows the range the copy covers, and GpuContext::end_frame unmaps and records the copy.
class StreamBuffer {
 public:
  StreamBuffer() = default;
  ~StreamBuffer() { destroy(); }
  StreamBuffer(const StreamBuffer&) = delete;
  StreamBuffer& operator=(const StreamBuffer&) = delete;

  bool create(GpuContext& ctx, u32 size, SDL_GPUBufferUsageFlags usage);
  void destroy();

  // The frame's mapped memory (`size` bytes).
  void* begin(const Frame& frame);
  // Everything in [0, bytes) is copied to the GPU buffer at end_frame.
  void mark_used(u32 bytes) { used_ = bytes > used_ ? bytes : used_; }
  // The frame's GPU buffer (after begin()).
  SDL_GPUBuffer* handle() const { return buffers_[slot_]; }
  u32 size() const { return size_; }

 private:
  friend class GpuContext;
  // Unmaps the frame's transfer buffer and records its copy.
  void flush(SDL_GPUCopyPass* pass);

  GpuContext* ctx_ = nullptr;
  SDL_GPUTransferBuffer* transfers_[kFramesInFlight]{};
  SDL_GPUBuffer* buffers_[kFramesInFlight]{};
  void* mapped_ = nullptr;
  u32 size_ = 0;
  u32 used_ = 0;
  u32 slot_ = 0;
  u64 serial_ = 0;
};

}  // namespace oc::render
