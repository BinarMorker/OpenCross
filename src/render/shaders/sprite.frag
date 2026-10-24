#version 450
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 3, binding = 0) uniform Filter { int filter_mode; int keep_light; } pf;
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 out_color;
void main() {
  vec4 t;
  vec2 sz = vec2(textureSize(tex, 0));
  if (pf.filter_mode == 1) {
    t = textureLod(tex, (floor(uv * sz) + 0.5) / sz, 0.0);
  } else if (pf.filter_mode == 2) {
    vec2 ddx = dFdx(uv), ddy = dFdy(uv);
    t = vec4(0.0);
    for (int j = 0; j < 4; ++j)
      for (int i = 0; i < 4; ++i) {
        vec2 p = uv + ((float(i) + 0.5) * 0.25 - 0.5) * ddx + ((float(j) + 0.5) * 0.25 - 0.5) * ddy;
        t += textureLod(tex, (floor(p * sz) + 0.5) / sz, 0.0);
      }
    t *= 1.0 / 16.0;
  } else if (pf.filter_mode == 3) {
    vec2 p = uv * sz - 0.5;
    vec2 f = fract(p);
    vec2 b = floor(p) + 0.5;
    vec4 t00 = textureLod(tex, b / sz, 0.0);
    vec4 t10 = textureLod(tex, (b + vec2(1.0, 0.0)) / sz, 0.0);
    vec4 t01 = textureLod(tex, (b + vec2(0.0, 1.0)) / sz, 0.0);
    vec4 t11 = textureLod(tex, (b + vec2(1.0, 1.0)) / sz, 0.0);
    t = mix(mix(t00, t10, f.x), mix(t01, t11, f.x), f.y);
  } else {
    t = texture(tex, uv);
  }
  vec4 c = t * color;
  // Keeps fully transparent texels out of the depth buffer; a premultiplied composite keeps added light (alpha 0, colour > 0).
  if (c.a <= 0.0 && (pf.keep_light == 0 || max(c.r, max(c.g, c.b)) <= 0.0)) discard;
  out_color = c;
}
