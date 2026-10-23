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
  s16 fine_tune = 0, base_note = 0;
  u16 adsr1 = 0, adsr2 = 0;
  u16 bank_id = 0;  // the bank header's id (diagnostics: which file this slot came from)
  std::shared_ptr<const AdpcmSample> sample;  // null if the start does not point at decodable data
};

struct Bank {
  u16 id = 0;
  u32 first_index = 0;
  std::vector<Instrument> inst;  // inst[i] is global instrument first_index + i
};

// `data` starts at the "AKAO" magic of the bank (a whole wave file or one yos0 block). Trailing bytes of the sample area
// may be missing (clamped). Returns null on a malformed header.
std::shared_ptr<const Bank> parse_bank(ByteSpan data);

// The global instrument table of 256 slots: banks overlay index ranges, last one wins; nothing is ever restored.
class InstrumentTable {
 public:
  // Writes the bank's instruments at its first index. Banks no slot refers to any more are dropped from the keep-alive list.
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

 private:
  mutable std::bitset<256> hit_;
  std::array<const Instrument*, 256> slot_{};
  std::vector<std::shared_ptr<const Bank>> banks_;  // keeps the instruments alive
};

}  // namespace oc::audio
