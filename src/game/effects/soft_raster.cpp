#include "game/effects/soft_raster.h"

#include <algorithm>
#include <cmath>

namespace oc::effects {

void SoftCanvas::blit(const u8* img, int iw, int ih, int x, int y) {
  for (int j = 0; j < ih; ++j) {
    const int dy = y + j;
    if (dy < 0 || dy >= h_) continue;
    for (int i = 0; i < iw; ++i) {
      const int dx = x + i;
      if (dx < 0 || dx >= w_) continue;
      const u8* src = img + (static_cast<size_t>(j) * static_cast<size_t>(iw) + static_cast<size_t>(i)) * 4;
      if (!src[3]) continue;
      u8* dst = &rgba_[(static_cast<size_t>(dy) * static_cast<size_t>(w_) + static_cast<size_t>(dx)) * 4];
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = 255;
    }
  }
}

// Scanline rasteriser over the quad's bounding box: every pixel centre is tested against the two triangles over the texture
// corners, (0,1,2) and (1,3,2), with barycentric coordinates; the first triangle that contains it gives the texture coordinate
// (corner uv: 0=(0,0) 1=(1,0) 2=(0,1) 3=(1,1)), which samples the nearest texel. Alpha-0 texels are skipped; the others are
// modulated by the quad colour and blended per channel with the quad's blend mode.
int SoftCanvas::draw(const DrawQuad& quad) {
  if (!quad.sprite || quad.sprite->rgba.empty() || quad.sprite->w == 0 || quad.sprite->h == 0) return 0;
  const int sw = quad.sprite->w, sh = quad.sprite->h;
  float minx = quad.x[0], maxx = quad.x[0], miny = quad.y[0], maxy = quad.y[0];
  for (int k = 1; k < 4; ++k) {
    minx = std::min(minx, quad.x[k]);
    maxx = std::max(maxx, quad.x[k]);
    miny = std::min(miny, quad.y[k]);
    maxy = std::max(maxy, quad.y[k]);
  }
  const int x0 = std::max(0, static_cast<int>(std::floor(minx))), x1 = std::min(w_ - 1, static_cast<int>(std::ceil(maxx)));
  const int y0 = std::max(0, static_cast<int>(std::floor(miny))), y1 = std::min(h_ - 1, static_cast<int>(std::ceil(maxy)));
  if (x0 > x1 || y0 > y1) return 0;
  static constexpr float cu[4] = {0, 1, 0, 1}, cv[4] = {0, 0, 1, 1};
  static constexpr int corners[2][3] = {{0, 1, 2}, {1, 3, 2}};
  int written = 0;
  for (int py = y0; py <= y1; ++py)
    for (int px = x0; px <= x1; ++px) {
      const float fx = static_cast<float>(px) + 0.5f, fy = static_cast<float>(py) + 0.5f;
      float u = 0, v = 0;
      bool inside = false;
      for (const auto& corner_ids : corners) {
        const float ax = quad.x[corner_ids[0]], ay = quad.y[corner_ids[0]], bx = quad.x[corner_ids[1]], by = quad.y[corner_ids[1]],
                    cx = quad.x[corner_ids[2]], cy = quad.y[corner_ids[2]];
        const float denom = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::fabs(denom) < 1e-9f) continue;
        const float bary0 = ((by - cy) * (fx - cx) + (cx - bx) * (fy - cy)) / denom;
        const float bary1 = ((cy - ay) * (fx - cx) + (ax - cx) * (fy - cy)) / denom;
        const float bary2 = 1.0f - bary0 - bary1;
        if (bary0 < -1e-4f || bary1 < -1e-4f || bary2 < -1e-4f) continue;
        u = bary0 * cu[corner_ids[0]] + bary1 * cu[corner_ids[1]] + bary2 * cu[corner_ids[2]];
        v = bary0 * cv[corner_ids[0]] + bary1 * cv[corner_ids[1]] + bary2 * cv[corner_ids[2]];
        inside = true;
        break;
      }
      if (!inside) continue;
      const int tx = std::clamp(static_cast<int>(u * static_cast<float>(sw)), 0, sw - 1);
      const int ty = std::clamp(static_cast<int>(v * static_cast<float>(sh)), 0, sh - 1);
      const u8* texel = &quad.sprite->rgba[(static_cast<size_t>(ty) * static_cast<size_t>(sw) + static_cast<size_t>(tx)) * 4];
      if (!texel[3]) continue;
      u8* dst = &rgba_[(static_cast<size_t>(py) * static_cast<size_t>(w_) + static_cast<size_t>(px)) * 4];
      const int colour[3] = {quad.r, quad.g, quad.b};
      for (int ch = 0; ch < 3; ++ch) dst[ch] = static_cast<u8>(blend_channel(quad.blend, dst[ch], modulate(texel[ch], colour[ch])));
      dst[3] = 255;
      ++written;
    }
  return written;
}

}  // namespace oc::effects
