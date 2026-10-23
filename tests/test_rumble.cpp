// Controller rumble: the byte -> motor strength mapping, the per-frame contribution model with its test pulse, and the room-effect sequence
// ops 28 (small motor for n ticks) / 29 (big motor ramp) driving the motors through the SequencePlayer.
#include <cstdio>
#include <string>
#include <vector>

#include "game/effects/emitter_system.h"
#include "game/effects/sequence_player.h"
#include "platform/rumble.h"

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

assets::SeqCmd cmd(u16 op, u16 len, u16 wait, std::vector<u8> data) {
  assets::SeqCmd command;
  command.op = op;
  command.len = len;
  command.wait = wait;
  command.data = std::move(data);
  command.data.resize(len - 4u, 0);
  return command;
}

// Operands of the rumble ops 28 / 29: the tick count at +4 and the motor level at +8 of the command record.
std::vector<u8> rumble_operands(u16 ticks, s16 level) {
  std::vector<u8> data(8, 0);
  data[0] = ticks & 0xFF, data[1] = ticks >> 8;
  data[4] = static_cast<u8>(level & 0xFF), data[5] = static_cast<u8>((level >> 8) & 0xFF);
  return data;
}

// Byte -> motor strength: the small flag drives the high motor fully; a big byte maps to byte / 268.4 + 0.05 (255 saturates at 1.0, 128 gives
// 0.527) and never drops below the 0.05 floor once it is on.
void test_map() {
  using platform::rumble_map;
  CHECK(rumble_map(0, 0) == platform::RumbleOut{});
  CHECK(rumble_map(1, 0).high == 0xFFFF && rumble_map(1, 0).low == 0);
  CHECK(rumble_map(0, 255).low == 0xFFFF);
  const int mid = rumble_map(0, 128).low;
  CHECK(mid > 0x7000 && mid < 0x9000);
  CHECK(rumble_map(0, 1).low > 0x0C00);
}

// Per-frame contributions: the latest value of one source wins, sources add (clamped to 255) and OR their small flags, nothing carries over to
// the next frame, the option switch gates the output, and the test pulse starts at 0xE0 and decays by 10 per 1/60 s.
void test_model() {
  platform::Rumble& rumble = platform::Rumble::instance();
  int source_a = 0, source_b = 0;
  rumble.contribute(&source_a, 1, 100);
  rumble.contribute(&source_a, 0, 100);
  rumble.contribute(&source_b, 1, 200);
  platform::RumbleOut out = rumble.flush(true, 1.0 / 60);
  CHECK(rumble.small() == 1 && rumble.big() == 255);
  CHECK(out == platform::rumble_map(1, 255));
  out = rumble.flush(true, 1.0 / 60);
  CHECK(rumble.small() == 0 && rumble.big() == 0 && out == platform::RumbleOut{});
  rumble.contribute(&source_a, 1, 50);
  out = rumble.flush(false, 1.0 / 60);
  CHECK(out == platform::RumbleOut{});
  rumble.pulse();
  rumble.flush(true, 1.0 / 60);
  CHECK(rumble.big() == 0xE0);
  rumble.flush(true, 1.0 / 60);
  CHECK(rumble.big() == 0xD6);
  for (int i = 0; i < 30; ++i) rumble.flush(true, 1.0 / 60);
  CHECK(rumble.big() == 0);
}

// A sequence of 29 (ramp to 128 over 4 ticks), 28 (small motor for 3 ticks) and 29 (back to 0 over 2 ticks): the task adds its level before
// the tick's ramp step (0, 32, 64, 96, 128), the small motor runs for exactly its 3 ticks starting after its command ran, and the big motor is
// back at 0 when the task has ended.
void test_sequence() {
  assets::RoomEffects fx;
  assets::Sequence seq;
  seq.name = "main";
  seq.cmds = {cmd(0x29, 12, 4, rumble_operands(4, 128)), cmd(0x28, 8, 6, rumble_operands(3, 0)), cmd(0x29, 12, 2, rumble_operands(2, 0)),
              cmd(0x21, 8, 0, {})};
  fx.sequences.push_back(seq);
  effects::EffectBank bank;
  bank.add(&fx);
  effects::EmitterSystem sys(bank);
  sys.register_all();
  effects::SequencePlayer player(bank, sys);
  platform::Rumble& rumble = platform::Rumble::instance();
  rumble.flush(true, 0);
  CHECK(player.start("main"));
  std::vector<int> big, small;
  for (int tick = 0; tick < 16; ++tick) {
    player.tick();
    rumble.flush(true, 0);
    big.push_back(rumble.big());
    small.push_back(rumble.small());
  }
  CHECK(big[0] == 0 && big[1] == 32 && big[2] == 64 && big[3] == 96);
  CHECK(big[4] == 128);
  CHECK(small[4] == 0 || small[4] == 1);
  int smalls = 0;
  for (int level : small) smalls += level;
  CHECK(smalls == 3);
  CHECK(big[15] == 0);
  CHECK(player.active() == 0);
}
}  // namespace

int main() {
  test_map();
  test_model();
  test_sequence();
  std::printf(failures ? "test_rumble: %d FAILED\n" : "test_rumble: all passed\n", failures);
  return failures ? 1 : 0;
}
