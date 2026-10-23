#include "import/tim.h"

#include <algorithm>

#include "core/span_reader.h"

namespace oc::import {

namespace {

bool read_block(SpanReader& reader, TimBlock& block, u32& block_len) {
  block_len = reader.u32_();
  block.x = reader.u16_();
  block.y = reader.u16_();
  block.w = reader.u16_();
  block.h = reader.u16_();
  const size_t halfwords = static_cast<size_t>(block.w) * block.h;
  if (block_len < 12 || halfwords * 2 > reader.remaining()) return false;
  block.data.resize(halfwords);
  for (size_t i = 0; i < halfwords; ++i) block.data[i] = reader.u16_();
  return true;
}

}  // namespace

std::optional<Tim> parse_tim(ByteSpan data) {
  try {
    SpanReader reader(data);
    if (data.size() < 20 || reader.u32_() != 0x10) return std::nullopt;
    const u32 flags = reader.u32_();
    if (flags & ~0xFu) return std::nullopt;
    static constexpr int kBpp[4] = {4, 8, 16, 24};
    Tim tim;
    tim.bpp = kBpp[flags & 3];
    tim.has_clut = (flags & 8) != 0;
    if (tim.has_clut) {
      const size_t start = reader.pos();
      u32 block_len = 0;
      if (!read_block(reader, tim.clut, block_len)) return std::nullopt;
      reader.seek(start + block_len);
    }
    u32 block_len = 0;
    if (!read_block(reader, tim.image, block_len)) return std::nullopt;
    return tim;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

u32 bgr555_to_rgba(u16 colour) {
  u32 r = (colour & 31u) << 3, g = ((colour >> 5) & 31u) << 3, b = ((colour >> 10) & 31u) << 3;
  r |= r >> 5;
  g |= g >> 5;
  b |= b >> 5;
  return r | (g << 8) | (b << 16) | (colour == 0 ? 0u : 0xFF000000u);
}

u32 bgr555_to_rgba_stp(u16 colour) {
  const u32 rgba = bgr555_to_rgba(colour);
  return (colour & 0x8000u) ? (rgba & 0x00FFFFFFu) | (static_cast<u32>(kStpAlpha) << 24) : rgba;
}

std::optional<Image> decode_tim(const Tim& tim, u32 clut_row, bool keep_stp) {
  const auto convert = keep_stp ? bgr555_to_rgba_stp : bgr555_to_rgba;
  const TimBlock& image = tim.image;
  Image out;
  out.height = image.h;
  switch (tim.bpp) {
    case 4:
    case 8: {
      const u32 texels_per_halfword = tim.bpp == 4 ? 4u : 2u;
      out.width = image.w * texels_per_halfword;
      out.indexed = true;
      out.pixels.resize(static_cast<size_t>(out.width) * out.height);
      for (u32 y = 0; y < out.height; ++y)
        for (u32 x = 0; x < out.width; ++x) {
          const u16 halfword = image.data[static_cast<size_t>(y) * image.w + x / texels_per_halfword];
          out.pixels[static_cast<size_t>(y) * out.width + x] =
              tim.bpp == 4 ? static_cast<u8>((halfword >> (4 * (x & 3))) & 0xF) : static_cast<u8>((halfword >> (8 * (x & 1))) & 0xFF);
        }
      if (tim.has_clut) {
        const size_t row_entries = tim.bpp == 4 ? 16 : 256;
        const size_t first = static_cast<size_t>(clut_row) * row_entries;
        if (first < tim.clut.data.size()) {
          const size_t count = std::min(row_entries, tim.clut.data.size() - first);
          for (size_t i = 0; i < count; ++i) out.palette.push_back(convert(tim.clut.data[first + i]));
        }
      }
      return out;
    }
    case 16: {
      out.width = image.w;
      out.pixels.resize(static_cast<size_t>(out.width) * out.height * 4);
      for (size_t i = 0; i < image.data.size(); ++i) store_rgba(&out.pixels[i * 4], convert(image.data[i]));
      return out;
    }
    case 24: {
      out.width = image.w * 2 / 3;
      out.pixels.resize(static_cast<size_t>(out.width) * out.height * 4);
      for (u32 y = 0; y < out.height; ++y) {
        const size_t row_byte_offset = static_cast<size_t>(y) * image.w * 2;
        auto byte_at = [&](size_t offset) {
          const u16 halfword = image.data[(row_byte_offset + offset) / 2];
          return static_cast<u8>(((row_byte_offset + offset) & 1) ? halfword >> 8 : halfword & 0xFF);
        };
        for (u32 x = 0; x < out.width; ++x) {
          u8* pixel = &out.pixels[(static_cast<size_t>(y) * out.width + x) * 4];
          pixel[0] = byte_at(x * 3ull);
          pixel[1] = byte_at(x * 3ull + 1);
          pixel[2] = byte_at(x * 3ull + 2);
          pixel[3] = 255;
        }
      }
      return out;
    }
    default:
      return std::nullopt;
  }
}

Image to_rgba(const Image& img) {
  if (!img.indexed) return img;
  Image out;
  out.width = img.width;
  out.height = img.height;
  out.pixels.assign(img.pixels.size() * 4, 0);
  for (size_t i = 0; i < img.pixels.size(); ++i) {
    const u8 index = img.pixels[i];
    store_rgba(&out.pixels[i * 4], index < img.palette.size() ? img.palette[index] : 0u);
  }
  return out;
}

void Vram::blit(int x, int y, int w, int h, std::span<const u16> data) {
  if (w < 0 || h < 0 || data.size() < static_cast<size_t>(w) * static_cast<size_t>(h)) return;
  for (int row = 0; row < h; ++row)
    for (int column = 0; column < w; ++column)
      px_[static_cast<size_t>((y + row) & (kHeight - 1)) * kWidth + static_cast<size_t>((x + column) & (kWidth - 1))] =
          data[static_cast<size_t>(row) * static_cast<size_t>(w) + static_cast<size_t>(column)];
}

void Vram::blit_tim(const Tim& tim, bool with_clut) {
  if (tim.has_clut && with_clut) blit(tim.clut.x, tim.clut.y, tim.clut.w, tim.clut.h, tim.clut.data);
  blit(tim.image.x, tim.image.y, tim.image.w, tim.image.h, tim.image.data);
}

void Vram::write_bytes(size_t byte_offset, ByteSpan data) {
  for (size_t i = 0; i + 1 < data.size(); i += 2) {
    const size_t halfword_index = (byte_offset + i) / 2;
    if (halfword_index >= px_.size()) return;
    px_[halfword_index] = static_cast<u16>(data[i] | (data[i + 1] << 8));
  }
}

std::vector<u16> Vram::read_clut(int x, int y, int count) const {
  std::vector<u16> out(static_cast<size_t>(std::max(count, 0)));
  for (int i = 0; i < count; ++i) out[static_cast<size_t>(i)] = at(x + i, y);
  return out;
}

u16 Vram::texel(int x, int y, int bpp, std::span<const u16> clut) const {
  u16 index = 0;
  if (bpp == 4) {
    index = static_cast<u16>((at(x >> 2, y) >> (4 * (x & 3))) & 0xF);
  } else if (bpp == 8) {
    index = static_cast<u16>((at(x >> 1, y) >> (8 * (x & 1))) & 0xFF);
  } else {
    return at(x, y);
  }
  if (clut.empty()) return index;
  return index < clut.size() ? clut[index] : u16{0};
}

}  // namespace oc::import
