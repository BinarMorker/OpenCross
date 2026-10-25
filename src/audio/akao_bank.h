#pragma once
// AKAO instrument bank (waveNNNN.dat, room yos0 blocks): a 0x40-byte header (id at +4, sample bytes at +0x14, first global
// instrument index at +0x18, count at +0x1C), then 16 bytes per instrument and the SPU ADPCM sample data. Samples are decoded
// once per unique (start, loop) pair.
#include <array>
#include <bitset>
#include <memory>
#include <vector>

#include "audio/adpcm.h"
#include "core/types.h"

namespace oc::audio {

struct Instrument {
  u32 start = 0, loop = 0;  // byte offsets into the bank's sample data
  u32 spu = 0;  // the bank's sound RAM load address: the sample starts at spu + start
  s16 fine_tune = 0, base_note = 0;
  u16 adsr1 = 0, adsr2 = 0;
  u16 bank_id = 0;  // the bank header's id (diagnostics: which file this slot came from)
  bool overwritten = false;  // a later upload reached the sample: decoded again from the RAM image (diagnostics)
  std::shared_ptr<const AdpcmSample> sample;  // null if the start does not point at decodable data
};

struct Bank {
  u16 id = 0;
  u32 first_index = 0;
  u32 spu = 0;  // sound RAM load address (header +0x10)
  std::vector<u8> samples;  // the sample bytes the load writes there
  std::vector<Instrument> inst;  // inst[i] is global instrument first_index + i
};

// `data` starts at the "AKAO" magic of the bank (a whole wave file or one yos0 block). Trailing bytes of the sample area
// may be missing (clamped). Returns null on a malformed header.
std::shared_ptr<const Bank> parse_bank(ByteSpan data);

// The address after the block that ends the sample at `from` in a sound RAM image (end flag, bit 0 of the block's second byte),
// or the end of the image.
u32 sample_end(const std::vector<u8>& ram, u32 from);

// The global instrument table of 256 slots and the 512 KiB sound RAM the banks are uploaded to. A bank load writes its samples at
// its load address and its instruments from its first index, last one wins, and nothing is ever restored: a slot of an earlier
// bank keeps its entry, and when a later upload reaches the RAM its sample was in, it plays what the RAM holds now.
class InstrumentTable {
 public:
  static constexpr u32 kRamBytes = 0x80000;
  // Uploads the bank and writes its instruments at its first index; the earlier slots whose sample the upload reaches are decoded
  // again from the RAM image. Banks no slot refers to any more are dropped from the keep-alive list.
  void overlay(const std::shared_ptr<const Bank>& bank);
  // nullptr if the index is not loaded. Records the slot as used (hits(), diagnostics only).
  const Instrument* get(int index) const {
    if (index < 0 || index >= 256) return nullptr;
    hit_.set(size_t(index));
    return slot_[size_t(index)];
  }
  // Same without recording the use (tools).
  const Instrument* peek(int index) const { return (index >= 0 && index < 256) ? slot_[size_t(index)] : nullptr; }
  // Slots read through get() (tools: which instruments a sound uses).
  const std::bitset<256>& hits() const { return hit_; }
  // The sound RAM image after the last upload (null before the first).
  const std::shared_ptr<const std::vector<u8>>& ram() const { return ram_; }

 private:
  mutable std::bitset<256> hit_;
  std::array<const Instrument*, 256> slot_{};
  std::vector<std::shared_ptr<const Bank>> banks_;  // keeps the instruments alive
  std::shared_ptr<const std::vector<u8>> ram_;  // shared by the copies of the table until the next upload
};

}  // namespace oc::audio
