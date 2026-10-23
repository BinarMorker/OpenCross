#include "import/lzss.h"

#include "core/span_reader.h"

namespace oc::import {

bool is_lzss(ByteSpan data) {
  return data.size() >= 12 && data[0] == 's' && data[1] == 's' && data[2] == 'z' && data[3] == 'l';
}

// Bitstream (MSB first within each byte): '1' + 8-bit literal, or '0' + 12-bit ring position + 4-bit count,
// copying count+2 bytes from a 4 KB zero-initialised ring buffer whose write pointer starts at 1. The header's
// decoded length is a hint only.
std::optional<Bytes> lzss_decode(ByteSpan data) {
  if (!is_lzss(data)) return std::nullopt;
  SpanReader header(data);
  header.skip(4);
  const u32 expected = header.u32_();

  Bytes out;
  out.reserve(expected);
  u8 ring[4096] = {};
  u32 ring_pos = 1;

  size_t byte_pos = 12;
  int bit_pos = 0;
  auto bits = [&](int count, u32& value) -> bool {
    value = 0;
    for (int i = 0; i < count; ++i) {
      if (byte_pos >= data.size()) return false;
      value = (value << 1) | ((data[byte_pos] >> (7 - bit_pos)) & 1u);
      if (++bit_pos == 8) {
        bit_pos = 0;
        ++byte_pos;
      }
    }
    return true;
  };

  while (out.size() < expected) {
    u32 flag;
    if (!bits(1, flag)) break;
    if (flag) {
      u32 literal;
      if (!bits(8, literal)) break;
      out.push_back(static_cast<u8>(literal));
      ring[ring_pos++ & 0xFFF] = static_cast<u8>(literal);
    } else {
      u32 ring_offset, count;
      if (!bits(12, ring_offset) || !bits(4, count)) break;
      count += 2;
      for (u32 i = 0; i < count; ++i) {
        const u8 byte = ring[(ring_offset + i) & 0xFFF];
        out.push_back(byte);
        ring[ring_pos++ & 0xFFF] = byte;
      }
    }
  }
  return out;
}

}  // namespace oc::import
