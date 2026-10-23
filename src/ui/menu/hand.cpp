#include "ui/menu/hand.h"

#include <algorithm>
#include <cmath>

namespace oc::ui {

int hand_angle_toward(float dx, float dy, float sx, float sy) {
  // The tip is at -(c, s) * 76 PS1 px.
  const float theta = std::atan2(-dy / sy, -dx / sx);
  int angle = static_cast<int>(std::lround(theta * 2048.0f / 3.14159265f)) - 1024;
  while (angle < -2048) angle += 4096;
  while (angle > 2047) angle -= 4096;
  return angle;
}

HandImage render_hand(const u8* src, u32 sw_, u32 sh_, int angle, float sx, float sy) {
  HandImage image;
  // kS: PS1 px per image px; kHalf: half extent of the square (PS1 px).
  const float kS = 0.242f, kHalf = 105.0f;
  image.half_w = kHalf * sx, image.half_h = kHalf * sy;
  image.w = static_cast<u32>(2 * image.half_w), image.h = static_cast<u32>(2 * image.half_h);
  image.rgba.assign(static_cast<size_t>(image.w) * image.h * 4, 0);
  const float th = (static_cast<float>(angle) + 1024.0f) * 3.14159265f / 2048.0f, cos_th = std::cos(th), sn = std::sin(th);
  const float sw = static_cast<float>(sw_), sh = static_cast<float>(sh_);
  auto px = [&](int x, int y, float* texel) {
    x = std::clamp(x, 0, static_cast<int>(sw_) - 1), y = std::clamp(y, 0, static_cast<int>(sh_) - 1);
    const u8* src_px = &src[(static_cast<size_t>(y) * sw_ + static_cast<size_t>(x)) * 4];
    for (int k = 0; k < 4; ++k) texel[k] = src_px[k];
  };
  for (u32 y = 0; y < image.h; ++y)
    for (u32 x = 0; x < image.w; ++x) {
      const float dx = (static_cast<float>(x) + 0.5f - image.half_w) / sx, dy = (static_cast<float>(y) + 0.5f - image.half_h) / sy;
      // along / across: position along / across the needle, PS1 px.
      const float along = dx * cos_th + dy * sn, across = -dx * sn + dy * cos_th;
      const float u = (along / kS + 314.0f) - 0.5f, v = across / kS + sh * 0.5f - 0.5f;
      if (u < -1 || v < -1 || u > sw || v > sh) continue;
      const int x0 = static_cast<int>(std::floor(u)), y0 = static_cast<int>(std::floor(v));
      const float fx = u - static_cast<float>(x0), fy = v - static_cast<float>(y0);
      float t00[4], t10[4], t01[4], t11[4];
      px(x0, y0, t00), px(x0 + 1, y0, t10), px(x0, y0 + 1, t01), px(x0 + 1, y0 + 1, t11);
      float acc[4] = {0, 0, 0, 0}, wa = 0;
      const float wgt[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
      const float* tt[4] = {t00, t10, t01, t11};
      for (int k = 0; k < 4; ++k) {
        const float al = tt[k][3] * wgt[k];
        wa += al;
        for (int ch = 0; ch < 3; ++ch) acc[ch] += tt[k][ch] * al;
      }
      u8* out_px = &image.rgba[(static_cast<size_t>(y) * image.w + x) * 4];
      if (wa > 0.01f) {
        for (int ch = 0; ch < 3; ++ch) out_px[ch] = static_cast<u8>(std::clamp(acc[ch] / wa, 0.0f, 255.0f));
        out_px[3] = static_cast<u8>(std::clamp(wa, 0.0f, 255.0f));
      }
    }
  return image;
}

}  // namespace oc::ui
