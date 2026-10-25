#version 450
layout(set = 2, binding = 0) uniform sampler2D tex;
// color.rgb: flat colour, color.w: back-face test (vertex shader). misc.x: 1 = flat colour, misc.y: depth scale, misc.z: 1 = water on,
// misc.w: water level (+y down; model y, or world y in ot mode), tint.rgb: colour multiplier, water.rgb: colour below the level (replaces
// tint), fog.rgb: far colour / 128, fog_rng.xy: fog near / far (GTE depth cue; far <= near = off), fog_rng.z: STP texel filter (0 all,
// 1 texels without STP only, 2 STP texels only; flat faces count as STP), fog_rng.w: 1 = white texels (FE 45 flash CLUT). ot.x: 1 = field ordering-table faces (2 = with the smooth water line), ot.y: nearest-corner sort,
// ot.z: layer bias (slots), ot.w: submerged-face bias (slots); ot2.x: face filter (0 all, 1 dry only, 2 submerged only), ot2.y: 1 = depth
// only (alpha-0 output into a colour-keeping blend), ot2.z: 1 / kDepthRange (slot -> depth-buffer value), ot2.w: output alpha (0.5 = ABR 0).
layout(set = 3, binding = 0) uniform Push { vec4 color; vec4 misc; vec4 tint; vec4 water; vec4 fog; vec4 fog_rng; vec4 ot; vec4 ot2; vec4 slot; } pc;
layout(location = 0) in vec3 uv;
layout(location = 1) in float cam_z;
layout(location = 2) in float local_y;
layout(location = 3) flat in float face_slot;
layout(location = 4) flat in float face_wet;
layout(location = 5) in vec4 v_light;
layout(location = 6) in float world_y;
layout(location = 7) flat in float face_all;
layout(location = 8) flat in vec2 slab_v;
layout(location = 9) in vec3 v_extra;
layout(location = 10) flat in vec3 v_amb;
layout(location = 11) in vec3 v_corner;
layout(location = 0) out vec4 out_color;
void main() {
  bool ot = pc.ot.x > 0.5;
  bool wet = ot ? face_wet > 0.5 : (pc.misc.z > 0.5 && local_y > pc.misc.w);
  if (ot && pc.ot.x > 1.5) {  // smooth water line: the exact plane inside a straddling face
    wet = face_all > 0.5 || (face_wet > 0.5 && world_y > pc.misc.w);
    float fs = face_slot + pc.ot.z + (wet ? pc.ot.w : 0.0);
    if (fs <= slab_v.x || fs > slab_v.y) discard;
  }
  if (ot && pc.ot2.x > 0.5 && (pc.ot2.x > 1.5) != wet) discard;
  vec4 c = pc.misc.x > 0.5 ? vec4(pc.color.rgb, 1.0) : texture(tex, uv.xy / uv.z);
  if (c.a < 0.5) discard;
  // Texel alpha: 1 = STP clear, 0.75 (import kStpAlpha 0xBF) = STP set; the midpoint separates them under bilinear filtering too.
  bool stp = pc.misc.x > 0.5 || c.a < 0.875;
  if (pc.fog_rng.z > 0.5 && (pc.fog_rng.z > 1.5) != stp) discard;
  if (pc.fog_rng.w > 0.5) c.rgb = vec3(1.0);  // FE 45 white CLUT: every non-transparent texel 0x7FFF, STP kept
  // Submerged faces take the water colour; a textured face's texel is modulated by its corner's packet colour (flat / solid faces: neutral).
  vec3 light = (wet ? pc.water.rgb * pc.tint.a : pc.tint.rgb) * (pc.misc.x > 0.5 ? vec3(1.0) : v_corner);
  if (v_light.w > 0.5) light = v_light.rgb;  // PS1 lit path: the GTE NCCT corner colour, no depth cue
  else if (pc.fog_rng.y > pc.fog_rng.x) {  // libgte SetFogNearFar(a, b): p = b (z - a) / ((b - a) z), DPCS: rgb += p (far - rgb)
    float p = clamp(pc.fog_rng.y * (cam_z - pc.fog_rng.x) / ((pc.fog_rng.y - pc.fog_rng.x) * max(cam_z, 1.0)), 0.0, 1.0);
    light = mix(light, pc.fog.rgb * pc.tint.a, p);
  }
  if (v_extra != vec3(0.0)) light = max(light + v_extra, light * 0.65);  // [Enhancements] Lighting (never < 65 % of the PS1 colour)
  light *= v_amb;  // [Enhancements] Lighting: the scene ambient (1 = neutral)
  out_color = vec4(clamp(c.rgb * light, 0.0, 1.0), pc.ot2.y > 0.5 ? 0.0 : pc.ot2.w);
  if (ot) {
    // One slot per face (+ layer bias, + the submerged push); the fraction orders fragments of faces sharing a slot without leaving it.
    float slot = face_slot + pc.ot.z + (wet ? pc.ot.w : 0.0);
    float fine = clamp(cam_z / 16.0 - face_slot, 0.0, 1.0);
    gl_FragDepth = clamp((slot + 0.001 + 0.998 * fine) * pc.ot2.z, 0.0, 1.0);
  } else {
    gl_FragDepth = clamp(cam_z * pc.misc.y, 0.0, 1.0);
  }
}
