#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in float in_z;
layout(set = 1, binding = 0) uniform Push { vec2 scale; vec2 offset; } pc;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;
void main() {
  gl_Position = vec4(in_pos * pc.scale + pc.offset, in_z, 1.0);
  uv = in_uv;
  color = in_color;
  // SDL_GPU clip space has +y up on every backend; everything above works in the PS1's y-down frame.
  gl_Position.y = -gl_Position.y;
}
