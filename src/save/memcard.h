#pragma once
// PS1 raw memory-card image (128 KB, 16 blocks of 8 KB, 128-byte frames).
#include <string>
#include <string_view>

#include "core/types.h"

namespace oc::save {

// state: 0xA0 free, 0x51 first block, 0x52 middle, 0x53 last, 0xA1..0xA3 deleted. size is the file size in bytes (0x2000 per block).
// next is the 0-based index of the next directory frame, 0xFFFF = none. name is up to 20 characters.
struct DirFrame {
  u32 state = 0xA0;
  u32 size = 0;
  u16 next = 0xFFFF;
  std::string name;
  bool xor_ok = true;

  bool used() const { return (state & 0xF0) == 0x50; }
  bool first() const { return state == 0x51; }
  bool free() const { return state == 0xA0; }
};

class MemCard {
 public:
  static constexpr size_t kSize = 0x20000, kBlock = 0x2000, kFrame = 0x80, kBlocks = 15;

  // Throws std::runtime_error if the size is not 0x20000.
  explicit MemCard(Bytes image);
  // A formatted empty card.
  static MemCard blank();

  const Bytes& bytes() const { return data_; }
  bool header_ok() const;

  // Directory frame / block numbers are 1..15 (block n lives at 0x2000 * n).
  DirFrame dir(int block_number) const;
  // Recomputes the frame XOR.
  void set_dir(int block_number, u32 state, u32 size, u16 next, std::string_view name);
  // Resets the frame and zeroes the block.
  void free_dir(int block_number);

  ByteSpan block(int block_number) const;
  // data.size() must be 0x2000.
  void write_block(int block_number, ByteSpan data);

  // First-block entry with that name, 0 if absent.
  int find(std::string_view name) const;
  // First free block, 0 if the card is full.
  int allocate() const;

  // Recomputes the header frame and all directory frame XOR bytes.
  void fix_checksums();

 private:
  Bytes data_;
};

// XOR of the first 127 bytes of a frame.
u8 frame_xor(const u8* frame127);

}  // namespace oc::save
