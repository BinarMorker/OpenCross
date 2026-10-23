#include "save/memcard.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace oc::save {

u8 frame_xor(const u8* frame) {
  u8 checksum = 0;
  for (int i = 0; i < 127; ++i) checksum ^= frame[i];
  return checksum;
}

namespace {
void check_block_number(int block_number) {
  if (block_number < 1 || block_number > 15) throw std::out_of_range("memcard block number must be 1..15");
}
u32 rd32(const u8* bytes) { return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (u32(bytes[3]) << 24); }
void wr32(u8* bytes, u32 value) {
  for (int i = 0; i < 4; ++i) bytes[i] = static_cast<u8>(value >> (8 * i));
}
}  // namespace

MemCard::MemCard(Bytes image) : data_(std::move(image)) {
  if (data_.size() != kSize) throw std::runtime_error("memory card image must be 0x20000 bytes");
}

MemCard MemCard::blank() {
  MemCard card(Bytes(kSize, 0));
  card.data_[0] = 'M';
  card.data_[1] = 'C';
  for (int block = 1; block <= 15; ++block) card.free_dir(block);
  // Broken-sector list: no broken sectors.
  for (int frame = 16; frame <= 35; ++frame) {
    u8* bytes = &card.data_[frame * kFrame];
    wr32(bytes, 0xFFFFFFFF);
    bytes[8] = bytes[9] = 0xFF;
  }
  card.fix_checksums();
  return card;
}

bool MemCard::header_ok() const { return data_[0] == 'M' && data_[1] == 'C' && frame_xor(data_.data()) == data_[127]; }

DirFrame MemCard::dir(int block_number) const {
  check_block_number(block_number);
  const u8* bytes = &data_[block_number * kFrame];
  DirFrame frame;
  frame.state = rd32(bytes);
  frame.size = rd32(bytes + 4);
  frame.next = static_cast<u16>(bytes[8] | (bytes[9] << 8));
  frame.name.assign(reinterpret_cast<const char*>(bytes + 10), strnlen(reinterpret_cast<const char*>(bytes + 10), 20));
  frame.xor_ok = frame_xor(bytes) == bytes[127];
  return frame;
}

void MemCard::set_dir(int block_number, u32 state, u32 size, u16 next, std::string_view name) {
  check_block_number(block_number);
  u8* bytes = &data_[block_number * kFrame];
  std::memset(bytes, 0, kFrame);
  wr32(bytes, state);
  wr32(bytes + 4, size);
  bytes[8] = static_cast<u8>(next);
  bytes[9] = static_cast<u8>(next >> 8);
  std::memcpy(bytes + 10, name.data(), std::min<size_t>(name.size(), 20));
  bytes[127] = frame_xor(bytes);
}

void MemCard::free_dir(int block_number) {
  set_dir(block_number, 0xA0, 0, 0xFFFF, {});
  std::fill_n(data_.begin() + block_number * kBlock, kBlock, u8{0});
}

ByteSpan MemCard::block(int block_number) const {
  check_block_number(block_number);
  return ByteSpan(data_).subspan(block_number * kBlock, kBlock);
}

void MemCard::write_block(int block_number, ByteSpan data) {
  check_block_number(block_number);
  if (data.size() != kBlock) throw std::runtime_error("memcard block must be 0x2000 bytes");
  std::memcpy(&data_[block_number * kBlock], data.data(), kBlock);
}

int MemCard::find(std::string_view name) const {
  for (int block = 1; block <= 15; ++block) {
    const DirFrame frame = dir(block);
    if (frame.first() && frame.name == name) return block;
  }
  return 0;
}

int MemCard::allocate() const {
  for (int block = 1; block <= 15; ++block)
    if (dir(block).free()) return block;
  return 0;
}

void MemCard::fix_checksums() {
  data_[127] = frame_xor(data_.data());
  for (int block = 1; block <= 15; ++block) data_[block * kFrame + 127] = frame_xor(&data_[block * kFrame]);
}

}  // namespace oc::save
