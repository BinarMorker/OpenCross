#include "import/container.h"

#include "core/span_reader.h"

namespace oc::import {

bool is_cdmake_dummy(ByteSpan data) {
  return data.size() >= 4 && data[0] == 'I' && data[1] == 't' && data[2] == '\'' && data[3] == 's';
}

// Layout: u32 count; u32 off[count]; optionally u32 eof. The EOF pointer is present
// when the word after the table lies at/after the last offset and within the buffer and the first entry starts
// after it; otherwise the table has exactly `count` words and the last entry runs to the end of the buffer.
std::optional<Cpt> parse_cpt(ByteSpan data) {
  if (data.size() < 4) return std::nullopt;
  SpanReader reader(data);
  const u32 count = reader.u32_();
  if (count == 0 || count > 0x10000 || 4 + 4ull * count > data.size()) return std::nullopt;

  std::vector<u32> offsets(count);
  for (u32 i = 0; i < count; ++i) offsets[i] = reader.u32_();
  for (u32 i = 0; i < count; ++i) {
    if (offsets[i] > data.size()) return std::nullopt;
    if (i > 0 && offsets[i] < offsets[i - 1]) return std::nullopt;
  }

  Cpt cpt;
  const size_t table_end = 4 + 4ull * count;
  if (offsets[0] >= table_end + 4 && table_end + 4 <= data.size()) {
    const u32 eof = reader.at<u32>(table_end);
    if (eof >= offsets.back() && eof <= data.size()) {
      cpt.has_eof_pointer = true;
      offsets.push_back(eof);
    }
  }
  cpt.entries.reserve(count);
  for (u32 i = 0; i < count; ++i) {
    CptEntry entry;
    entry.offset = offsets[i];
    const u32 end = (i + 1 < offsets.size()) ? offsets[i + 1] : static_cast<u32>(data.size());
    entry.size = end - entry.offset;
    entry.data = data.subspan(entry.offset, entry.size);
    cpt.entries.push_back(entry);
  }
  return cpt;
}

bool is_drp(ByteSpan data) {
  return data.size() >= 12 && data[0] == 'd' && data[1] == 'r' && data[2] == 'p' && data[3] == 0;
}

// Header word 8 stores 64 * count. Subfile header: u32 zero, char name[4], u8 type, u24 = 16 * payload length.
std::optional<Drp> parse_drp(ByteSpan data) {
  if (!is_drp(data)) return std::nullopt;
  SpanReader reader(data, 8);
  const u32 count = reader.u32_() / 64;
  if (12 + 4ull * count > data.size()) return std::nullopt;

  Drp drp;
  drp.entries.reserve(count);
  for (u32 i = 0; i < count; ++i) {
    const u32 offset = reader.at<u32>(12 + 4ull * i);
    if (offset + 12ull > data.size()) return std::nullopt;
    DrpEntry entry;
    entry.offset = offset;
    entry.name.assign(reinterpret_cast<const char*>(data.data() + offset + 4), 4);
    while (!entry.name.empty() && entry.name.back() == '\0') entry.name.pop_back();
    entry.type = data[offset + 8];
    const u32 length = (data[offset + 9] | (data[offset + 10] << 8) | (data[offset + 11] << 16)) / 16u;
    const size_t start = offset + 12ull;
    const size_t available = data.size() - start;
    entry.data = data.subspan(start, std::min<size_t>(length, available));
    drp.entries.push_back(std::move(entry));
  }
  return drp;
}

}  // namespace oc::import
