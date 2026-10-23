#version 450
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 out_color;
void main() {
  vec4 t = texture(tex, uv);
  float a = t.a * color.a;
  if (a <= 0.0) discard;
  out_color = vec4(t.rgb * color.rgb * 2.0, a);
}
