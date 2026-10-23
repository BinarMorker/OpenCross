#version 450
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 3, binding = 0) uniform Push { vec2 size; float ramp; float pad; vec4 tint; } pc;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;
void main() {
  vec3 c = texture(tex, uv).rgb;
  if (pc.ramp > 0.5) {  // pause screen filter: a monochrome purple ramp of the luminance
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    out_color = vec4(l * pc.tint.rgb, 1.0);
    return;
  }
  float lines = min(240.0, pc.size.y * 0.5);
  float scan = 0.5 + 0.5 * cos(uv.y * lines * 6.2831853);
  c *= mix(1.0, 0.72 + 0.28 * scan, 0.85);
  if (pc.size.x >= 1200.0) {
    int col = int(gl_FragCoord.x) % 3;
    vec3 m = vec3(col == 0 ? 1.0 : 0.88, col == 1 ? 1.0 : 0.88, col == 2 ? 1.0 : 0.88);
    c *= m;
  }
  vec2 q = uv * (1.0 - uv);
  c *= mix(0.78, 1.0, clamp(pow(16.0 * q.x * q.y, 0.25), 0.0, 1.0));
  out_color = vec4(c * 1.12, 1.0);
}
