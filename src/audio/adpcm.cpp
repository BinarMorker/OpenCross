#include "audio/adpcm.h"

#include <algorithm>

namespace oc::audio {

namespace {
constexpr int kFilter0[5] = {0, 60, 115, 98, 122};
constexpr int kFilter1[5] = {0, 0, -52, -55, -60};
constexpr u32 kMaxBlocks = 0x8000;
constexpr int kSamplesPerBlock = 28;
constexpr int kBlockBytes = 16;
}  // namespace

// Invalid shifts (> 12) decode as shift 9, as the hardware does.
void adpcm_decode_block(const u8* block, AdpcmState& state, s16* out) {
  int shift = block[0] & 0xF;
  const int filter = std::min(block[0] >> 4, 4);
  if (shift > 12) shift = 9;
  const int coef0 = kFilter0[filter], coef1 = kFilter1[filter];
  for (int i = 0; i < kSamplesPerBlock; ++i) {
    int nibble = (block[2 + (i >> 1)] >> (4 * (i & 1))) & 0xF;
    if (nibble & 8) nibble -= 16;
    int sample = ((nibble * 4096) >> shift) + ((state.old * coef0 + state.older * coef1 + 32) >> 6);
    sample = std::clamp(sample, -32768, 32767);
    out[i] = s16(sample);
    state.older = state.old;
    state.old = sample;
  }
}

// A loop-start flag (bit 2) sets the repeat address as the voice passes the block, so the last one before the end wins; without one
// the instrument's loop address is used. The end block itself counts, so a loop address on the last block (single-cycle loops) is
// inside the sample. The SPU keeps the ADPCM filter history across the jump, so a looping sample gets its loop body decoded a second
// time from the state at the end of the first pass and that second pass, appended, becomes the loop (a filter-0 body is unchanged, a
// recursive one differs from the first pass by its start-up transient).
AdpcmSample adpcm_decode_sample(ByteSpan data, u32 start, u32 loop_byte) {
  AdpcmSample result;
  AdpcmState state;
  s16 block_pcm[kSamplesPerBlock];
  int loop_block = -1;
  u32 block_count = 0;
  for (u64 off = start; off + kBlockBytes <= data.size() && block_count < kMaxBlocks; off += kBlockBytes, ++block_count) {
    const u8* block = &data[size_t(off)];
    const u8 flags = block[1];
    if (flags & 4) loop_block = int(block_count);
    adpcm_decode_block(block, state, block_pcm);
    result.pcm.insert(result.pcm.end(), block_pcm, block_pcm + kSamplesPerBlock);
    if (flags & 1) {
      ++block_count;
      result.has_end = true;
      result.loops = (flags & 2) != 0;
      break;
    }
  }
  if (loop_block < 0 && loop_byte >= start && (loop_byte - start) / kBlockBytes < block_count) loop_block = int((loop_byte - start) / kBlockBytes);
  result.loop_start = std::max(loop_block, 0) * kSamplesPerBlock;
  if (result.loops && result.has_end && block_count > 0) {
    const u32 first = u32(std::max(loop_block, 0));
    result.loop_start = int(result.pcm.size());
    for (u32 k = first; k < block_count; ++k) {
      adpcm_decode_block(&data[size_t(start) + size_t(k) * kBlockBytes], state, block_pcm);
      result.pcm.insert(result.pcm.end(), block_pcm, block_pcm + kSamplesPerBlock);
    }
  }
  return result;
}

}  // namespace oc::audio
