#pragma once
// Unchecked little-endian reads for the audio file parsers; the caller validates the offsets.
#include "core/types.h"

namespace oc::audio {

inline u16 read_u16(const u8* bytes) { return u16(bytes[0] | bytes[1] << 8); }
inline u32 read_u32(const u8* bytes) { return u32(bytes[0]) | u32(bytes[1]) << 8 | u32(bytes[2]) << 16 | u32(bytes[3]) << 24; }
inline u16 read_u16(ByteSpan data, size_t off) { return read_u16(&data[off]); }
inline u32 read_u32(ByteSpan data, size_t off) { return read_u32(&data[off]); }

}  // namespace oc::audio
