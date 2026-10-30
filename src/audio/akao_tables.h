#pragma once
// Constant tables of the PS1 sound driver and the SPU.
#include "core/types.h"

namespace oc::audio {

// Stereo pan gains: left gain = [idx], right gain = [idx ^ 0xFF]; Q15 (32768 = 1.0).
extern const s16 kAkaoPanGain[256];
// The driver's random bytes (E1 random pitch), read at the 60 Hz frame counter.
extern const u8 kAkaoRandom[256];
// E1 random pitch of a note's pitch register: bit 7 of the random byte clear raises it by (pitch * depth >> 8) * byte >> 7, set lowers
// it by (pitch * depth >> 8) * byte >> 9 (32-bit unsigned arithmetic).
inline u32 akao_random_pitch(u32 pitch, u32 depth, u8 random) {
  const u32 scaled = (pitch * depth >> 8) * random;
  return random & 0x80 ? pitch - (scaled >> 9) : pitch + (scaled >> 7);
}
// SPU Gaussian interpolation filter (hardware constant, 512 entries): out = (g[0xFF-i]*s[-3] + g[0x1FF-i]*s[-2] + g[0x100+i]*s[-1] +
// g[i]*s[0]) >> 15 with i = bits 4..11 of the voice pitch counter.
extern const s16 kSpuGauss[512];
// LFO waveform `type` (0..15): s16 samples; an entry pair 0,0 followed by a negative count means "wrap back by |count|
// entries" (read like the driver: if w[0]==0 && w[1]==0 then w += w[2]).
const s16* akao_lfo(int type);

// Semitone table (4.12 fixed, 2^(i/12)).
inline constexpr u16 kAkaoSemitone[12] = {0x1000, 0x10F3, 0x11F5, 0x1306, 0x1428, 0x155B,
                                          0x16A0, 0x17F9, 0x1966, 0x1AE8, 0x1C82, 0x1E34};

}  // namespace oc::audio
