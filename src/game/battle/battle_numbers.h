#pragma once
// Battle damage / heal / miss numbers. Pure logic: the anchor rule, the overlap delay and the per-digit particle timeline of the `damv` /
// `gurv` / `gaiv` -> `dam1` -> `dam2` emitters of battle/battle.prd (miya). Positions are the particles' VIEW-SPACE world units (no camera;
// the depth is z + H << 5 with the battle position shift 5), so a number stays where it spawned on the screen whatever the camera does.
#include <array>
#include <vector>

namespace oc::game::bnum {

// Battle position shift: pos += vel << 5, quad corners << 5, depth base H << 5.
constexpr int kShift = 5;

// The flight emitter of one number kind (op values as stored in miya/<name>; ops 0x0A VEL (<< 6), 0x0C ACC (<< 3), 0x0F LIFE,
// 0x12 / 0x15 SCALE / rate, 0x19 / 0x1B ANGLE / rate, 0x20 / 0x22 COLOR / rate, 0x38 INTEGRATE).
struct Flight {
  int life;
  std::array<int, 3> vel, acc, scale, scale_rate, col, col_rate, ang, ang_rate;
};
// Result code 1 -> "gurv", 3 -> "gaiv", else (0 hit, 2 miss) "damv".
const Flight& flight_for_code(int code);

// One particle state for drawing: view-space position (world units, y down, z without the H << 5 base), scale (4.12), colour
// (12-bit, 2048 = neutral) and angles (4096 = turn). `lighten` = blend 5 (the frame drawn B - F then B + F = max(B, F)); false = opaque
// (dam1, blend 0). `visible` false once the last phase has ended.
struct State {
  float x = 0, y = 0, z = 0;
  std::array<float, 3> scale{4096, 4096, 4096};
  std::array<float, 3> col{2048, 2048, 2048};
  std::array<float, 3> ang{0, 0, 0};
  bool lighten = false;
  bool visible = false;
};

// State of one digit `tick` ticks after the CallDamage spawn (tick 0 = the spawn frame). x0 / y0 = the spawn offset (digit layout + the
// screen anchor, world units); delay = the digit's countdown (the particle is frozen while it is >= 1).
State digit_state(const Flight& flight, float x0, float y0, int delay, int tick);
// Ticks until the last phase (dam2) of a digit with this delay has ended.
int digit_ticks(const Flight& flight, int delay);

// Screen anchor: the view-space point (PS1 axes, y down) projected with H, |z| raised to H / 4, x clamped to +-130, y to -90..40
// (pixels from the projection centre); integer GTE-style division.
std::array<int, 2> anchor_px(float vx, float vy, float vz, float h);

// The overlap list: one 11 n x 12 rect per offset-layout number, alive 0x40 ticks; every live rect the new one overlaps adds 16 ticks
// of delay. A rect is unlinked once its ttl is negative.
struct Overlap {
  struct Rect {
    int x, y, w, h, ttl;
  };
  std::vector<Rect> rects;
  // Registers the rect and returns the delay.
  int add(int px, int py, int digits);
  void tick();
};

// numf sprite frames (battle.prd tana/numf, timinfo frames 1..11): quad x offset, width, y offset of the whole frame (texels).
struct Glyph {
  int x, w, y;
};
// frame 1..10 = digits 0..9, 11 = "miss!"
const Glyph& numf_glyph(int frame);

}  // namespace oc::game::bnum
