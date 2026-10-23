#pragma once
// PS1 SPU ADPCM: 16-byte blocks (shift/filter byte, flag byte, 14 data bytes = 28 samples). Flag bits: 0 = end of sample,
// 1 = repeat after the end, 2 = loop start.
#include <vector>

#include "core/types.h"

namespace oc::audio {

struct AdpcmState {
  int old = 0, older = 0;
};

// Decodes one 16-byte block into 28 samples, updating the filter history.
void adpcm_decode_block(const u8* block, AdpcmState& state, s16* out28);

// A decoded sample: blocks from the start offset through the first block carrying the end flag (bit0).
struct AdpcmSample {
  std::vector<s16> pcm;
  int loop_start = 0;      // sample index the voice jumps to after the end block (when `loops`)
  bool loops = false;      // end block has the repeat flag (bit1)
  bool has_end = false;    // an end-flag block was reached inside the data
};

// `data` = sample area; `start` = byte offset of the first block; `loop_byte` = the instrument's loop address in the same
// basis (used when no block carries the loop-start flag; ignored if outside the sample). Never reads past `data`.
AdpcmSample adpcm_decode_sample(ByteSpan data, u32 start, u32 loop_byte);

}  // namespace oc::audio
