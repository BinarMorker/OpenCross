// Battle damage numbers: the per-digit particle timeline of the damv / gurv / gaiv -> dam1 -> dam2 emitter chain, the screen anchor
// projection and the overlap delay list.
#include "game/battle/battle_numbers.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace oc::game::bnum {
namespace {

// The flight emitters of battle/battle.prd > miya (negative = the u16 read as s16).
const Flight kDamv{24, {0, 0, 5776}, {0, 268, -2760}, {16384, 16384, 16384}, {-512, -512, -512}, {128, 128, 128}, {80, 80, 80},
                   {-1024, 0, 0}, {42, 0, 0}};
const Flight kGurv{35, {0, -2208, 0}, {1420, 1640, 0}, {18944, 18944, 18944}, {-424, -424, -424}, {128, 128, 128}, {55, 0, 0},
                   {1056, 384, 400}, {-30, -11, -11}};
const Flight kGaiv{35, {0, 0, 0}, {0, 0, 2012}, {4096, 29184, 4096}, {0, -716, 0}, {128, 128, 128}, {-3, 55, -3},
                   {1056, 384, 0}, {-30, -11, 0}};
// dam1 (spawned when the flight's life ends): LIFE 34, every rate 0, blend 0. dam2 (spawned when dam1's life ends): LIFE 26, SCALE_RATE -45,
// COLOR_RATE -64, ANGLE_RATE (13, 30, 8), blend 5. Both take the predecessor's x / y and colour, scale 0x1000 and their own ANGLE 0;
// dam1 z := 0, dam2 copies it.
constexpr int kDam1Life = 34, kDam2Life = 26, kDam2Scale = -45, kDam2Col = -64;
constexpr std::array<int, 3> kDam2Ang{13, 30, 8};

const Glyph kGlyphs[11] = {{-1, 12, -1}, {0, 9, -1},  {-1, 11, -1}, {0, 11, -1}, {-2, 13, -1}, {0, 10, -1},
                           {-1, 12, -1}, {0, 11, -1}, {0, 11, -1},  {-1, 12, -1}, {-1, 43, -4}};

int sat16(int value) { return std::clamp(value, -32768, 32767); }
int satu16(int value) { return std::clamp(value, 0, 65535); }
}  // namespace

const Flight& flight_for_code(int code) { return code == 1 ? kGurv : code == 3 ? kGaiv : kDamv; }

const Glyph& numf_glyph(int frame) { return kGlyphs[std::clamp(frame, 1, 11) - 1]; }

int digit_ticks(const Flight& flight, int delay) { return delay + flight.life + kDam1Life + kDam2Life; }

// The emitter runs pos += vel << kShift; vel += acc (VEL << 6, ACC << 3) for `life` ticks from the origin and negates the velocity, so the live
// flight comes back towards the origin; the digit / anchor offset is added after that pre-roll. The digit is frozen for `delay` ticks, then the
// three phases run (flight, dam1, dam2), each spawning the next on its last tick instead of moving; the state after dam2 is invisible.
State digit_state(const Flight& flight, float x0, float y0, int delay, int tick) {
  std::int64_t pos[3] = {0, 0, 0}, vel[3], acc[3];
  for (int k = 0; k < 3; ++k) {
    vel[k] = static_cast<std::int64_t>(flight.vel[static_cast<size_t>(k)]) << 6;
    acc[k] = static_cast<std::int64_t>(flight.acc[static_cast<size_t>(k)]) << 3;
  }
  for (int i = 0; i < flight.life; ++i)
    for (int k = 0; k < 3; ++k) {
      pos[k] += vel[k] << kShift;
      vel[k] += acc[k];
    }
  for (auto& component : vel) component = -component;
  pos[0] += static_cast<std::int64_t>(x0 * 65536.0f);
  pos[1] += static_cast<std::int64_t>(y0 * 65536.0f);
  std::array<int, 3> scale = flight.scale, col = flight.col, ang = flight.ang;
  int phase = 0, life = flight.life;
  for (int now = 1; now <= tick; ++now) {
    if (phase == 0 && now <= delay) continue;
    if (life == 1) {
      if (phase == 2) return State{};
      if (phase == 0) pos[2] = 0;
      scale = {4096, 4096, 4096};
      ang = {0, 0, 0};
      life = phase == 0 ? kDam1Life : kDam2Life;
      ++phase;
      continue;
    }
    --life;
    if (phase == 0) {
      for (size_t k = 0; k < 3; ++k) {
        col[k] = satu16(col[k] + flight.col_rate[k]);
        scale[k] = sat16(scale[k] + flight.scale_rate[k]);
        ang[k] = sat16(ang[k] + flight.ang_rate[k]);
      }
      for (int k = 0; k < 3; ++k) {
        pos[k] += vel[k] << kShift;
        vel[k] += acc[k];
      }
    } else if (phase == 2) {
      for (size_t k = 0; k < 3; ++k) {
        col[k] = satu16(col[k] + kDam2Col);
        scale[k] = sat16(scale[k] + kDam2Scale);
        ang[k] = sat16(ang[k] + kDam2Ang[k]);
      }
    }
  }
  State state;
  state.x = static_cast<float>(pos[0]) / 65536.0f;
  state.y = static_cast<float>(pos[1]) / 65536.0f;
  state.z = static_cast<float>(pos[2]) / 65536.0f;
  for (size_t k = 0; k < 3; ++k) {
    state.scale[k] = static_cast<float>(scale[k]);
    state.col[k] = static_cast<float>(col[k]);
    state.ang[k] = static_cast<float>(ang[k]);
  }
  state.lighten = phase != 1;
  state.visible = true;
  return state;
}

std::array<int, 2> anchor_px(float vx, float vy, float vz, float h) {
  const int hi = static_cast<int>(h), x = static_cast<int>(vx), y = static_cast<int>(vy);
  int z = static_cast<int>(vz);
  if (std::abs(z) < (hi >> 2)) z = hi >> 2;
  const int az = std::max(1, std::abs(z));
  int sx = static_cast<int>(static_cast<std::int64_t>(x) * hi / az), sy = static_cast<int>(static_cast<std::int64_t>(y) * hi / az);
  sx = std::clamp(static_cast<int>(static_cast<std::int16_t>(sx)), -0x82, 0x82);
  sy = std::clamp(static_cast<int>(static_cast<std::int16_t>(sy)), -0x5A, 0x28);
  return {sx, sy};
}

int Overlap::add(int px, int py, int digits) {
  const int w = 11 * digits, x = px - w;
  int delay = 0;
  for (const Rect& rect : rects)
    if (x < rect.x + rect.w && rect.x < x + w && py < rect.y + rect.h && rect.y < py + 12) delay += 16;
  rects.push_back({x, py, w, 12, 0x40});
  return delay;
}

void Overlap::tick() {
  for (Rect& rect : rects) --rect.ttl;
  std::erase_if(rects, [](const Rect& rect) { return rect.ttl < 0; });
}

}  // namespace oc::game::bnum
