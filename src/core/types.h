#pragma once
// Fixed-width aliases and small helpers used everywhere.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oc {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

using Bytes = std::vector<u8>;
using ByteSpan = std::span<const u8>;

}  // namespace oc
