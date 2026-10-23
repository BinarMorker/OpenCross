#include "audio/spu_reverb.h"

#include <algorithm>

namespace oc::audio {
namespace {

struct Preset {
  // mBASE: work area start in 8-byte units.
  u16 base;
  u16 regs[32];
};
// libspu reverb presets: mBASE, then the 32 registers in hardware order.
constexpr Preset kPresets[10] = {
    {0xFFFE, {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000}},
    {0xFB28, {0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80, 0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227, 0x0374, 0x01EF, 0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x01B4, 0x0136, 0x00B8, 0x005C, 0x8000, 0x8000}},
    {0xFC18, {0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00, 0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF, 0x0372, 0x0266, 0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7, 0x018F, 0x00B5, 0x00B4, 0x0080, 0x004C, 0x0026, 0x8000, 0x8000}},
    {0xF6F8, {0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0, 0x5280, 0x4EC0, 0x0904, 0x076B, 0x0824, 0x065F, 0x07A2, 0x0616, 0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305, 0x0462, 0x02B7, 0x042F, 0x0265, 0x0264, 0x01B2, 0x0100, 0x0080, 0x8000, 0x8000}},
    {0xF204, {0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680, 0x5680, 0x52C0, 0x0DFB, 0x0B58, 0x0D09, 0x0A3C, 0x0BD9, 0x0973, 0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0, 0x06EF, 0x03D2, 0x05EA, 0x031D, 0x031C, 0x0238, 0x0154, 0x00AA, 0x8000, 0x8000}},
    {0xEA44, {0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000, 0x6000, 0x5C00, 0x15BA, 0x11BB, 0x14C2, 0x10BD, 0x11BC, 0x0DC1, 0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1, 0x0A00, 0x06CD, 0x09C2, 0x05C1, 0x05C0, 0x041A, 0x0274, 0x013A, 0x8000, 0x8000}},
    {0xE128, {0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000, 0x6000, 0x5400, 0x1ED6, 0x1A31, 0x1D14, 0x183B, 0x1BC2, 0x16B2, 0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D, 0x11F6, 0x0C5D, 0x1056, 0x0AE1, 0x0AE0, 0x07A2, 0x0464, 0x0232, 0x8000, 0x8000}},
    {0xCFF8, {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100, 0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000, 0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000}},
    {0xCFF8, {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000, 0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000}},
    {0xF880, {0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500, 0x5F80, 0x54C0, 0x0371, 0x02AF, 0x02E5, 0x01DF, 0x02B0, 0x01D7, 0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1, 0x011F, 0x0059, 0x01A0, 0x00E3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000}},
};

// 39-tap half-band resampling filter of the reverb unit; the odd taps are zero except the centre (0x4000), so only the even taps
// are listed.
constexpr int kFir[20] = {-0x0001, 0x0002,  -0x000A, 0x0023,  -0x0067, 0x010A,  -0x0268, 0x0534,  -0x0B90, 0x2806,
                          0x2806,  -0x0B90, 0x0534,  -0x0268, 0x010A,  -0x0067, 0x0023,  -0x000A, 0x0002,  -0x0001};

int clamp16(int value) { return std::clamp(value, -32768, 32767); }
int sign_extend(u16 value) { return s16(value); }

// Register indices (hardware order).
enum : int {
  dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2,
  dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN
};

}  // namespace

// The work area is the halfwords from mBASE to the end of the 512 KB SPU RAM.
void SpuReverb::set_mode(int mode) {
  if (mode < 0 || mode > 9 || mode == mode_) return;
  mode_ = mode;
  const Preset& preset = kPresets[mode];
  std::copy(std::begin(preset.regs), std::end(preset.regs), regs_.begin());
  ram_.assign(size_t(0x40000u - (u32(preset.base) << 2)), 0);
  clear();
}

void SpuReverb::clear() {
  std::fill(ram_.begin(), ram_.end(), s16(0));
  for (auto& history : down_) history.fill(0);
  for (auto& history : up_) history.fill(0);
  cur_ = 0;
  pos_ = 0;
}

s16 SpuReverb::read(u32 addr8, int off) const {
  if (ram_.empty()) return 0;
  const s64 size = s64(ram_.size());
  s64 index = (s64(cur_) + s64(addr8) * 4 + off) % size;
  if (index < 0) index += size;
  return ram_[size_t(index)];
}

void SpuReverb::write(u32 addr8, s16 value) {
  if (ram_.empty()) return;
  ram_[size_t((u64(cur_) + u64(addr8) * 4) % u64(ram_.size()))] = value;
}

// Every odd 44.1 kHz sample runs one 22.05 kHz reverb step: the input history is filtered down, the same-side and cross
// reflections (IIR: [mLSAME] = (Lin + [dLSAME]*vWALL - [mLSAME-2])*vIIR + [mLSAME-2], ...), the four early-echo combs and the two
// all-pass stages are computed per side, the result enters the output history and the FIR over that history is the output; the
// even samples take the centre tap of the output history.
void SpuReverb::process(int in_l, int in_r, int& out_l, int& out_r) {
  out_l = out_r = 0;
  if (mode_ <= 0 || ram_.empty()) return;
  const u32 phase = pos_ & 0x3F;
  down_[0][phase] = down_[0][phase | 0x40] = s16(clamp16(in_l));
  down_[1][phase] = down_[1][phase | 0x40] = s16(clamp16(in_r));
  int out[2];
  if (phase & 1) {
    int downsampled[2];
    for (int side = 0; side < 2; ++side) {
      const s16* history = &down_[size_t(side)][(phase - 38) & 0x3F];
      int acc = 0x4000 * history[19];
      for (int k = 0; k < 20; ++k) acc += kFir[k] * history[2 * k];
      downsampled[side] = clamp16(acc >> 15);
    }
    const int alpha = sign_extend(regs_[vIIR]);
    auto iiasm = [&](int value) { return alpha == -32768 ? (value == -32768 ? 0 : value * -65536) : value * (32768 - alpha); };
    auto neg = [](int value) { return value == -32768 ? 0x7FFF : -value; };
    const u32 dest_a[2] = {regs_[mLSAME], regs_[mRSAME]}, dest_b[2] = {regs_[mLDIFF], regs_[mRDIFF]};
    const u32 src_a[2] = {regs_[dLSAME], regs_[dRSAME]}, src_b[2] = {regs_[dLDIFF], regs_[dRDIFF]};
    const u32 comb[4][2] = {{regs_[mLCOMB1], regs_[mRCOMB1]}, {regs_[mLCOMB2], regs_[mRCOMB2]}, {regs_[mLCOMB3], regs_[mRCOMB3]}, {regs_[mLCOMB4], regs_[mRCOMB4]}};
    const u32 apf1[2] = {regs_[mLAPF1], regs_[mRAPF1]}, apf2[2] = {regs_[mLAPF2], regs_[mRAPF2]};
    const int in_coef[2] = {sign_extend(regs_[vLIN]), sign_extend(regs_[vRIN])};
    const int wall = sign_extend(regs_[vWALL]), fb_alpha = sign_extend(regs_[vAPF1]), fb_x = sign_extend(regs_[vAPF2]);
    for (int side = 0; side < 2; ++side) {
      const int in = (downsampled[side] * in_coef[side]) >> 14;
      const int ia = clamp16((((read(src_a[side]) * wall) >> 14) + in) >> 1);
      const int ib = clamp16((((read(src_b[side ^ 1]) * wall) >> 14) + in) >> 1);
      const int iir_a = clamp16((((ia * alpha) >> 14) + (iiasm(read(dest_a[side], -1)) >> 14)) >> 1);
      const int iir_b = clamp16((((ib * alpha) >> 14) + (iiasm(read(dest_b[side], -1)) >> 14)) >> 1);
      write(dest_a[side], s16(iir_a));
      write(dest_b[side], s16(iir_b));
      const int acc = ((read(comb[0][side]) * sign_extend(regs_[vCOMB1])) >> 14) + ((read(comb[1][side]) * sign_extend(regs_[vCOMB2])) >> 14) +
                      ((read(comb[2][side]) * sign_extend(regs_[vCOMB3])) >> 14) + ((read(comb[3][side]) * sign_extend(regs_[vCOMB4])) >> 14);
      const int fa = read(apf1[side] - regs_[dAPF1]);
      const int fb = read(apf2[side] - regs_[dAPF2]);
      const int mda = clamp16((acc + ((fa * neg(fb_alpha)) >> 14)) >> 1);
      const int mdb = clamp16(fa + ((((mda * fb_alpha) >> 14) + ((fb * neg(fb_x)) >> 14)) >> 1));
      const s16 wet = s16(clamp16(fb + ((mdb * fb_x) >> 15)));
      up_[size_t(side)][phase >> 1] = up_[size_t(side)][(phase >> 1) | 0x20] = wet;
      write(apf1[side], s16(mda));
      write(apf2[side], s16(mdb));
    }
    cur_ = (cur_ + 1) % u32(ram_.size());
    for (int side = 0; side < 2; ++side) {
      const s16* history = &up_[size_t(side)][((phase >> 1) - 19) & 0x1F];
      int acc = 0;
      for (int k = 0; k < 20; ++k) acc += kFir[k] * history[k];
      out[side] = clamp16(acc >> 14);
    }
  } else {
    const size_t centre = (((phase >> 1) - 19) & 0x1F) + 9;
    out[0] = up_[0][centre];
    out[1] = up_[1][centre];
  }
  ++pos_;
  out_l = (out[0] * vlout_) >> 15;
  out_r = (out[1] * vrout_) >> 15;
}

}  // namespace oc::audio
