#pragma once
// Chrono Cross LZSS ("sszl") decoder: 4-byte magic, u32 decoded length, u32 reserved, then an MSB-first bitstream.
#include <optional>

#include "core/types.h"

namespace oc::import {

// True if the buffer starts with the "sszl" magic.
bool is_lzss(ByteSpan data);

// Decodes a whole "sszl" file (12-byte header + bitstream). Returns std::nullopt on a malformed stream.
std::optional<Bytes> lzss_decode(ByteSpan data);

}  // namespace oc::import
