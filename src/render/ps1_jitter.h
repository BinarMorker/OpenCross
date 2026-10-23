#pragma once
// [Video] Ps1Jitter: the PS1 GTE's integer screen coordinates and the GPU's affine texture mapping, in the 3D pipelines (mesh / field room
// models, battle stage + effect polygons). The host sets it once per 3D picture (app/app_draw.cpp); the pipelines read it when they draw.
//   on      snap each projected vertex to a whole PS1 pixel and interpolate UVs screen-linearly
//   px, py  PS1 pixels per NDC unit of the target being drawn (half the picture's width / height in PS1 pixels: 160 x 112 for the
//           320 x 224 target; wider with widescreen; letterbox-aware for the window-resolution targets)
namespace oc::render {

struct Ps1Jitter {
  bool on = false;
  float px = 160.0f, py = 112.0f;
};
inline Ps1Jitter g_ps1_jitter;
inline const Ps1Jitter& ps1_jitter() { return g_ps1_jitter; }

}  // namespace oc::render
