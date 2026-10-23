// RoomLighting: table lookup by flag nibble, per-tick easing, entity RGB product, water slots.
#include <cstdio>

#include "game/room_lighting.h"

using namespace oc;

namespace {
int failures = 0;
#define CHECK(x)                                              \
  do {                                                        \
    if (!(x)) {                                               \
      std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); \
      ++failures;                                             \
    }                                                         \
  } while (0)
}  // namespace

int main() {
  game::RoomLighting rl;
  game::ActorLight light;
  // No table = neutral.
  CHECK(rl.target(3) == (std::array<int, 3>{128, 128, 128}));
  // A table of 128, 100, 90, 160, 110, ...
  u8 table[64] ={0x80, 0x80, 0x80, 0, 0x64, 0x64, 0x64, 0, 0x5A, 0x5A, 0x5A, 0, 0xA0, 0xA0, 0xA0, 0, 0x6E, 0x6E, 0x6E, 0};
  rl.set_table(table);
  // Only the low nibble selects.
  CHECK(rl.target(0x20001) == (std::array<int, 3>{100, 100, 100}));
  CHECK(rl.target(3)[1] == 160);
  rl.snap(light, 2);
  CHECK(light.cur[0] == 90);
  // 90 -> 98 -> ... -> 160 in steps of 8, clamped.
  rl.tick(light, 3);
  CHECK(light.cur[0] == 98);
  for (int i = 0; i < 20; ++i) rl.tick(light, 3);
  CHECK(light.cur[2] == 160);
  // A neutral actor eases to 128.
  rl.tick(light, 3, true);
  CHECK(light.cur[0] == 152);
  auto tint = game::RoomLighting::tint({{160, 128, 64}}, std::array<int, 3>{128, 64, 128});
  CHECK(tint[0] > 1.24f && tint[0] < 1.26f && tint[1] > 0.49f && tint[1] < 0.51f && tint[2] > 0.49f && tint[2] < 0.51f);
  CHECK(game::RoomLighting::water_slot(0x20000) == 0 && game::RoomLighting::water_slot(0x40000) == 1 &&
        game::RoomLighting::water_slot(0x60000) == 2 && game::RoomLighting::water_slot(0x1000) == -1);
  rl.clear();
  CHECK(!rl.has_table());
  std::printf(failures ? "lighting: FAILED\n" : "lighting: ok\n");
  return failures ? 1 : 0;
}
