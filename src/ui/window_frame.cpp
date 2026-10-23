// Settings window-skin preview geometry (canvas 1280 x 960); the per-skin uv numbers are shared with the key-item list panel
// (keyitem_popup.cpp ListSkin) because both are filled from the same piece records.
#include "ui/window_frame.h"

#include <algorithm>

namespace oc::ui {
namespace {

// Bottom edge dv / v0, B0 v0, R24 v0; left edge; right edge middle pieces.
struct SkinUv {
  float bdv, bv, b0v, r24v;
  float lu, ldu, ldv;
  float cu, cdu, cv, cdv;
};
constexpr SkinUv kUv[16] = {
    {.165f, .1665f, .408f, .408f, .126f, .125f, .161f, .126f, .1225f, .17067f, .160f},
    {.165f, .1665f, .4077f, .4077f, .126f, .125f, .161f, .1275f, .125f, .16667f, .160f},
    {.170f, .1580f, .4180f, .4210f, .126f, .125f, .161f, .126f, .125f, .17067f, .160f},
    {.165f, .1665f, .408f, .408f, .126f, .125f, .161f, .126f, .125f, .17067f, .160f},
    {.161f, .1665f, .408f, .408f, .12615f, .125f, .165f, .126f, .1225f, .1699f, .1568f},
    {.165f, .1665f, .4075f, .4075f, .126f, .125f, .161f, .14f, .125f, .17067f, .160f},
    {.165f, .1665f, .408f, .4195f, .1265f, .125f, .161f, .126f, .125f, .17067f, .160f},
    {.165f, .1665f, .4077f, .4077f, .126f, .125f, .161f, .126f, .1275f, .169f, .1608f},
    {.166f, .1665f, .408f, .408f, .125f, .125f, .161f, .1253f, .124f, .17067f, .160f},
    {.166f, .1654f, .408f, .408f, .126f, .124f, .161f, .126f, .125f, .17067f, .160f},
    {.163f, .1665f, .408f, .408f, .126f, .125f, .161f, .126f, .125f, .17067f, .160f},
    {.163f, .1665f, .4077f, .4050f, .126f, .125f, .161f, .125f, .124f, .17067f, .160f},
    {.164f, .1665f, .408f, .408f, .126f, .125f, .161f, .126f, .125f, .17067f, .160f},
    {.165f, .1665f, .408f, .408f, .126f, .125f, .161f, .126f, .125f, .17067f, .160f},
    {.165f, .1665f, .408f, .408f, .126f, .125f, .161f, .126f, .125f, .17067f, .160f},
    {.166f, .1665f, .408f, .408f, .126f, .125f, .161f, .126f, .125f, .17067f, .160f}};

constexpr float kRest[16] = {434, 429, 430, 429.5f, 431, 436, 432, 429, 429, 429, 432, 430, 430, 429.5f, 429, 430};
constexpr float kPitch = 64.5f, kTile = 65.0f, kEdge = 70.0f, kCorner = 175.0f, kTop = 640.0f, kLastRow = kTop + 3 * kPitch;

}  // namespace

float skin_window_x(int skin) { return kRest[std::clamp(skin, 0, 15)]; }

// Piece order: the 7 x 4 fill grid (column-major, 65-unit tiles at a 64.5 pitch), the two left corners with the top / bottom edge
// pieces interleaved (1..4, skin 0: 1..5), the left edge strip, then the right corners (edge slot 5) with the right edge strip (slot 6).
// The per-skin offsets below are the sheet-specific nudges of RDE's piece records; the leaf is the 70 x 70 page-turn quad.
SkinWindow build_skin_window(int skin, int leaf_phase) {
  const int skin_idx = std::clamp(skin, 0, 15);
  const SkinUv& uv = kUv[skin_idx];
  const float rest_x = kRest[skin_idx];
  SkinWindow window;
  auto quad = [&](float x, float y, float sw, float sh, float u0, float v0, float du, float dv) { window.quads.push_back({x, y, sw, sh, u0, v0, du, dv}); };

  for (int col = 0; col < 7; ++col)
    for (int row = 0; row < 4; ++row)
      quad(rest_x + kPitch * static_cast<float>(col), kTop + kPitch * static_cast<float>(row), kTile, kTile, (col & 1) ? 0.125f : 0.0f,
           (row & 1) ? 0.49925f : 0.335f, 0.1233f, 0.165f);
  window.left = rest_x;
  window.top = kTop;
  window.right = rest_x + kPitch * 6 + kTile;
  window.bottom = kLastRow + kTile;

  const bool narrow = skin_idx == 8 || skin_idx == 9 || skin_idx == 11;
  const float cdu = narrow ? .3120f : .3125f, cdv = narrow ? .4130f : .4140f, rdu = narrow ? .3103f : .3125f;
  const float bcdv = skin_idx == 10 ? .4132f : cdv;
  const float T0x = rest_x - 65.0f * 1.28f * (skin_idx == 0 ? 1.013f : 1.0f);
  const float T0y = kTop - 65.0f * 1.35f + (skin_idx == 0 ? -1.0f : 0.0f);
  float B0y = kLastRow + kEdge * 0.5f;
  switch (skin_idx) {
    case 0: B0y -= 65.0f * 0.745f; break;
    case 1:
    case 2: B0y -= 65.0f * 0.95f; break;
    case 4: B0y -= 65.0f; break;
    case 5: B0y -= 65.0f * 0.7f + 20.0f; break;
    case 6: B0y -= 65.0f * 0.7f + 15.0f; break;
    default: B0y -= 65.0f * 0.7f; break;
  }
  float bottom_edge_y = kLastRow + kEdge / 2.05f;
  switch (skin_idx) {
    case 0: bottom_edge_y += 65.0f * 0.4f; break;
    case 1: bottom_edge_y += 65.0f * 0.2f; break;
    case 2: bottom_edge_y += 65.0f * 0.12f; break;
    case 4: bottom_edge_y += 65.0f * 0.14f; break;
    case 5: bottom_edge_y += 65.0f * 0.45f - 20.0f; break;
    case 6: bottom_edge_y += 65.0f * 0.45f - 15.0f; break;
    case 8: bottom_edge_y += 65.0f * 0.443f - 2.5f; break;
    case 9: bottom_edge_y += 65.0f * 0.439f - 0.1f; break;
    case 11: bottom_edge_y += 65.0f * 0.444f; break;
    default: bottom_edge_y += 65.0f * 0.45f; break;
  }
  const float edge_x0 = T0x + kEdge * 1.5f * 0.98f - 65.0f / 1.03f + 65.0f;
  const float top_edge_y = T0y + kEdge * 1.475f - 65.0f * (skin_idx == 9 ? 1.054f : 1.047f);

  quad(T0x, T0y, kCorner, kCorner, .3125f, 0, cdu, cdv);
  quad(T0x, B0y, kCorner, kCorner, .3125f, uv.b0v, cdu, bcdv);
  for (int i = 1; i <= (skin_idx == 0 ? 5 : 4); ++i) {
    quad(edge_x0 + kEdge * static_cast<float>(i), top_edge_y, kEdge, kEdge, 0, 0, 0.123f, 0.16667f);
    quad(edge_x0 + kEdge * static_cast<float>(i), bottom_edge_y, kEdge, kEdge, 0, uv.bv, 0.123f, uv.bdv);
  }
  const float left_x = rest_x - 65.0f * 0.73f * (skin_idx == 0 ? 1.03f : 1.0f);
  for (int j = 0; j < 18; ++j) {
    const float y = static_cast<float>(j) * kEdge * 0.999f + T0y + 65.0f * 2.68f;
    if (kEdge * 0.2f + y - 15.0f > B0y) break;
    quad(left_x, y, kEdge, kEdge, uv.lu, 0, uv.ldu, uv.ldv);
  }
  float R0x = edge_x0 + kEdge * 5;
  if (skin_idx == 0) R0x *= 1.008f;
  if (skin_idx == 4) R0x *= 0.997f;
  float right_edge_x = edge_x0 + kEdge * 6;
  switch (skin_idx) {
    case 0: right_edge_x = R0x + 56.5f; break;
    case 4: right_edge_x -= 4.2f; break;
    case 5: right_edge_x -= 5.5f; break;
    case 9:
    case 11: right_edge_x += 1.0f; break;
    case 10: right_edge_x += 0.5f; break;
    default: break;
  }
  const float r0u = skin_idx == 0 || skin_idx == 5 ? .65f : skin_idx == 6 ? .64f : .625f, r24u = skin_idx == 0 || skin_idx == 5 ? .65f : .625f;
  const float R24y = B0y + (skin_idx == 6 ? 4.5f : 0.0f);
  quad(R0x, T0y, kCorner, kCorner, r0u, 0, rdu, cdv);
  for (int i = 1; i < 24; ++i) {
    const float y = T0y + 65.0f * 1.615f + kEdge * static_cast<float>(i) + (skin_idx == 0 ? -1.0f : 0.0f);
    if (y > R24y) break;
    quad(right_edge_x, y, kEdge, kEdge, uv.cu, uv.cv, uv.cdu, uv.cdv);
  }
  quad(R0x, R24y, kCorner, kCorner, r24u, uv.r24v, rdu, bcdv);

  window.leaf = {rest_x + 380.0f, 825.0f, kEdge, kEdge, 0.5f + 0.125f * static_cast<float>(std::clamp(leaf_phase, 0, 3)), 0.83333f, 0.124f, 0.16667f};
  return window;
}

}  // namespace oc::ui
