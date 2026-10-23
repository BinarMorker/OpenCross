#pragma once
// PS1 TIM decoder (classic layout: u32 0x10, u32 flags, optional CLUT block, image block) and a 1024x512 halfword VRAM model.
#include <optional>
#include <span>
#include <vector>

#include "core/types.h"

namespace oc::import {

struct TimBlock {
  u16 x = 0, y = 0;       // VRAM position in halfwords
  u16 w = 0, h = 0;       // width in halfwords
  std::vector<u16> data;  // w*h halfwords
};

struct Tim {
  int bpp = 0;  // 4, 8, 16, 24
  bool has_clut = false;
  TimBlock clut;
  TimBlock image;
};

// Classic TIM. Returns std::nullopt for anything malformed or truncated.
std::optional<Tim> parse_tim(ByteSpan data);

// 15-bit BGR555 -> packed RGBA (bytes R,G,B,A in memory). Colour 0x0000 is transparent; the STP bit is ignored.
u32 bgr555_to_rgba(u16 colour);

// Model textures keep the per-texel STP bit (bit 15) in alpha, because a semi-transparent primitive blends only the texels with STP
// set; the others are drawn opaque and 0x0000 is transparent (PS1 GPU rule; RDE draws such a primitive in two passes, all non-zero
// texels with the ABR blend, then the non-STP texels opaque). Alpha: 0 = transparent (0x0000), kStpAlpha = STP set (incl. 0x8000,
// opaque black + STP), 255 = STP clear. Every value >= 0.5 is opaque for non-semi-transparent drawing.
inline constexpr u8 kStpAlpha = 0xBF;
u32 bgr555_to_rgba_stp(u16 colour);

// Writes a packed RGBA colour as its four bytes R,G,B,A.
inline void store_rgba(u8* dst, u32 rgba) {
  for (size_t k = 0; k < 4; ++k) dst[k] = static_cast<u8>(rgba >> (8 * k));
}

struct Image {
  u32 width = 0, height = 0;
  bool indexed = false;
  std::vector<u8> pixels;    // indexed: width*height indices; otherwise width*height*4 RGBA bytes
  std::vector<u32> palette;  // indexed only: packed RGBA of CLUT row `clut_row` (empty without a CLUT)
};

// Decodes the image block. 4/8bpp stay indexed (palette from CLUT row `clut_row`); 16/24bpp become RGBA.
// keep_stp: palette / 16bpp colours via bgr555_to_rgba_stp (model textures) instead of bgr555_to_rgba.
std::optional<Image> decode_tim(const Tim& tim, u32 clut_row = 0, bool keep_stp = false);

// Expands an indexed image to RGBA using its palette (missing entries are transparent).
Image to_rgba(const Image& img);

class Vram {
 public:
  static constexpr int kWidth = 1024;
  static constexpr int kHeight = 512;

  Vram() : px_(static_cast<size_t>(kWidth) * kHeight, 0) {}

  // Rectangle blit in halfwords, wrapping at the VRAM edges like the GPU.
  void blit(int x, int y, int w, int h, std::span<const u16> data);
  void blit_tim(const Tim& tim, bool with_clut = true);
  // Raw copy at a byte offset (row stride 2048 bytes), used for the .bin CLUT subfile at 0x70000.
  void write_bytes(size_t byte_offset, ByteSpan data);

  u16 at(int x, int y) const { return px_[static_cast<size_t>(y & (kHeight - 1)) * kWidth + static_cast<size_t>(x & (kWidth - 1))]; }
  std::vector<u16> read_clut(int x, int y, int count) const;

  // Texel (x, y) in texel units of the given depth (4: 4 per halfword, 8: 2 per halfword, low part first).
  // 4/8bpp: returns clut[index] when a CLUT is given (0 if out of range), else the raw index. 16bpp: raw halfword.
  u16 texel(int x, int y, int bpp, std::span<const u16> clut = {}) const;

 private:
  std::vector<u16> px_;
};

}  // namespace oc::import
