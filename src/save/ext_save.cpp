#include "save/ext_save.h"

#include <algorithm>
#include <cstring>

namespace oc::save {

namespace {
u32 rd32(const Bytes& bytes, size_t offset) { return u32(bytes[offset]) | u32(bytes[offset + 1]) << 8 | u32(bytes[offset + 2]) << 16 | u32(bytes[offset + 3]) << 24; }
void wr32(Bytes& bytes, size_t offset, u32 value) {
  for (int i = 0; i < 4; ++i) bytes[offset + static_cast<size_t>(i)] = static_cast<u8>(value >> (8 * i));
}
size_t rec_off(int i) { return 8 + static_cast<size_t>(i) * kExtRecord; }
size_t pair_off(int i) { return kExtLegacySize + 2 * static_cast<size_t>(i); }

void put_utf8(std::string& out, char32_t cp) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000)) cp = U'�';
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}
}  // namespace

std::string utf32_to_utf8(const std::u32string& text) {
  std::string out;
  for (char32_t cp : text) put_utf8(out, cp);
  return out;
}

std::u32string utf8_to_utf32(const std::string& text) {
  std::u32string out;
  for (size_t i = 0; i < text.size();) {
    const u8 lead = static_cast<u8>(text[i]);
    const int length = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 0;
    if (length == 0 || i + static_cast<size_t>(length) > text.size()) {
      out += U'�';
      ++i;
      continue;
    }
    char32_t cp = length == 1 ? lead : lead & (0x3F >> (length - 1));
    bool ok = true;
    for (int k = 1; k < length; ++k) {
      const u8 cont = static_cast<u8>(text[i + static_cast<size_t>(k)]);
      if ((cont & 0xC0) != 0x80) ok = false;
      cp = cp << 6 | (cont & 0x3F);
    }
    out += ok ? cp : U'�';
    i += ok ? static_cast<size_t>(length) : 1;
  }
  return out;
}

ExtFile::ExtFile() : bytes_(kExtSize, 0) {
  std::memcpy(bytes_.data(), "EXT", 4);
  wr32(bytes_, 4, kExtVersion);
}

ExtFile ExtFile::parse(ByteSpan bytes) {
  ExtFile file;
  if (bytes.size() < 8 || std::memcmp(bytes.data(), "EXT", 4) != 0) return file;
  const u32 ver = u32(bytes[4]) | u32(bytes[5]) << 8 | u32(bytes[6]) << 16 | u32(bytes[7]) << 24;
  if (bytes.size() == kExtSize && ver == kExtVersion) {
    std::copy(bytes.begin(), bytes.end(), file.bytes_.begin());
  } else if (bytes.size() == kExtLegacySize && ver == 0x103) {
    std::copy(bytes.begin(), bytes.end(), file.bytes_.begin());
    wr32(file.bytes_, 4, kExtVersion);
  }
  return file;
}

ExtSlot ExtFile::slot(int index) const {
  ExtSlot result;
  if (index < 0 || index >= kExtSlots) return result;
  const size_t offset = rec_off(index);
  result.language = rd32(bytes_, offset);
  result.battles = rd32(bytes_, offset + 4);
  for (int k = 0; k < kExtNames; ++k) {
    const char* name_bytes = reinterpret_cast<const char*>(&bytes_[offset + 8 + static_cast<size_t>(k) * kExtNameLen]);
    size_t length = 0;
    while (length < kExtNameLen && name_bytes[length]) ++length;
    if (length) result.names[k] = utf8_to_utf32(std::string(name_bytes, length));
  }
  result.form = bytes_[pair_off(index)];
  result.valid = bytes_[pair_off(index) + 1];
  return result;
}

void ExtFile::set_slot(int index, const ExtSlot& record) {
  if (index < 0 || index >= kExtSlots) return;
  const size_t offset = rec_off(index);
  wr32(bytes_, offset, record.language);
  wr32(bytes_, offset + 4, record.battles);
  // The game clears the 0x580 name area first.
  std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset + 8), kExtNames * kExtNameLen, u8(0));
  for (const auto& [id, name] : record.names) {
    if (id < 0 || id >= kExtNames) continue;
    std::string encoded;
    for (char32_t cp : name) {
      std::string one;
      put_utf8(one, cp);
      // At most 31 bytes + NUL.
      if (encoded.size() + one.size() > kExtNameLen - 1) break;
      encoded += one;
    }
    std::memcpy(&bytes_[offset + 8 + static_cast<size_t>(id) * kExtNameLen], encoded.data(), encoded.size());
  }
  bytes_[pair_off(index)] = record.form;
  bytes_[pair_off(index) + 1] = record.valid;
}

}  // namespace oc::save
