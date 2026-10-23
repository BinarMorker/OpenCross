#version 450
// AFFINE (stage_affine.*.inc, built with -DAFFINE): `noperspective` on every non-flat varying = the PS1 GPU's screen-linear
// interpolation of texture coordinates and colours (the light position cam_pos stays perspective-correct).
#ifdef AFFINE
#define VARY noperspective
#else
#define VARY
#endif
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uvraw;
layout(location = 4) in vec2 in_uvscale;
layout(location = 5) in vec3 in_centre;
layout(set = 1, binding = 0) uniform Push { mat4 clip; vec4 fogc; vec4 color; vec4 params; vec4 uvw; vec4 jit; vec4 pinv; vec4 lpos[3]; vec4 lcol[3]; } pc;
layout(location = 8) out vec3 cam_pos;
VARY layout(location = 0) out vec2 uv;
VARY layout(location = 1) out vec3 color;
VARY layout(location = 2) out float cam_z;
VARY layout(location = 3) out vec2 uv_page;
VARY layout(location = 4) out vec2 uv_tex;
VARY layout(location = 5) out vec2 uv_scale;
layout(location = 6) flat out float ot_z;
VARY layout(location = 7) out float fog_p;
void main() {
  vec4 pf = vec4(in_pos.x, -in_pos.y, in_pos.z, 1.0);  // asset positions are y up; the instance transform takes PS1 file space
  gl_Position = pc.clip * pf;
  // [Enhancements] SpellLights (jit.w = 1): the camera-space position, rebuilt from the clip position with the projection's constants (pinv = P00, P02, P11, P12).
  cam_pos = pc.jit.w > 0.5 ? vec3((gl_Position.x - pc.pinv.y * gl_Position.w) / pc.pinv.x, (gl_Position.y - pc.pinv.w * gl_Position.w) / pc.pinv.z, gl_Position.w) : vec3(0.0);
  if (pc.jit.z > 0.5 && gl_Position.w > 0.0)  // [Video] Ps1Jitter: GTE integer screen coordinates (jit.xy = PS1 pixels per NDC unit)
    gl_Position.xy = floor(gl_Position.xy / gl_Position.w * pc.jit.xy + 0.5) / pc.jit.xy * gl_Position.w;
  cam_z = gl_Position.w;
  ot_z = (pc.clip * vec4(in_centre.x, -in_centre.y, in_centre.z, 1.0)).w;  // the packet's average camera z: its ordering-table depth
  // GTE depth cue: constant over the packet (its OT depth) in original art mode; HD art mode (params.y >= 8) evaluates it per vertex so it
  // varies smoothly across the big ground cells and meets at shared edges.
  float pym = pc.params.y >= 15.5 ? pc.params.y - 16.0 : pc.params.y;  // +16: an EdgeBlend strip (fragment shader)
  float fog_z = pym >= 7.5 ? cam_z : ot_z;
  fog_p = pc.fogc.w == 0.0 ? 0.0 : clamp(pc.fogc.w + pc.params.w / max(fog_z, 1.0), 0.0, 1.0);
  uv = vec2(in_uv.x, 1.0 - in_uv.y);  // asset UVs are OBJ-style (v up); texture rows are stored top-down
  uv_page = uv - in_uvraw * in_uvscale;  // atlas position of the texture page origin
  uv_tex = in_uvraw + pc.uvw.xy;         // packet texel + scroll phase, wrapped per fragment
  uv_scale = in_uvscale;
  color = in_color;
  // SDL_GPU clip space has +y up on every backend; everything above works in the PS1's y-down frame.
  gl_Position.y = -gl_Position.y;
}
