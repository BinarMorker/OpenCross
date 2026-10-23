#pragma once
// SPU reverb unit: the hardware reverb, run on the sum of the voices whose reverb bit is set. It works at 22050 Hz in a ring
// buffer (the "work area": SPU RAM from mBASE to the end), with the hardware's 39-tap half-band FIR to go down from and back up to
// 44100 Hz. Registers in hardware order: dAPF1 dAPF2 vIIR vCOMB1..4 vWALL vAPF1 vAPF2 mLSAME mRSAME mLCOMB1 mRCOMB1 mLCOMB2
// mRCOMB2 dLSAME dRSAME mLDIFF mRDIFF mLCOMB3 mRCOMB3 mLCOMB4 mRCOMB4 dLDIFF dRDIFF mLAPF1 mRAPF1 mLAPF2 mRAPF2 vLIN vRIN;
// addresses in 8-byte units. Output volume vLOUT / vROUT = the music's reverb depth (FE 02 / FE 03).
#include <array>
#include <vector>

#include "core/types.h"

namespace oc::audio {

class SpuReverb {
 public:
  // libspu reverb mode type 0..9 (0 off, 1 room, 2..4 studio small/medium/large, 5 hall, 6 space echo, 7 echo, 8 delay, 9 pipe).
  // The game uses the music header's u16 at +8, 4 at boot.
  void set_mode(int mode);
  int mode() const { return mode_; }
  // s16 (0x7FFF = 1.0)
  void set_depth(int left, int right) { vlout_ = left; vrout_ = right; }
  // One 44100 Hz stereo sample in (s16 scale), the reverb's contribution out (s16 scale).
  void process(int in_l, int in_r, int& out_l, int& out_r);
  void clear();

 private:
  // Work-area addressing: a register value (8-byte units = 4 halfwords) relative to the current address, wrapping in the work area.
  s16 read(u32 addr8, int off = 0) const;
  void write(u32 addr8, s16 value);

  int mode_ = -1;
  std::array<u16, 32> regs_{};
  int vlout_ = 0, vrout_ = 0;
  std::vector<s16> ram_;  // the work area (halfwords)
  u32 cur_ = 0;           // current address relative to the work area start (halfwords)
  std::array<std::array<s16, 128>, 2> down_{};  // 44.1 kHz input history, duplicated so (index | 0x40) never wraps
  std::array<std::array<s16, 64>, 2> up_{};     // 22.05 kHz output history, duplicated likewise
  u32 pos_ = 0;                                  // 44.1 kHz sample counter (odd samples run the reverb step)
};

}  // namespace oc::audio
