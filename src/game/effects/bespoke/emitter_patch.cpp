// Overlays that patch the pack's emitters at init. The emitter definitions stay const: the EmitterSystem
// keeps the overlay's record writes per header (patch_op) and runs its replaced object callbacks (set_object_hook).
//   icerance  Ice Lance   emitter '1' keeps its spawn position
//   27earth   Earthquake  every own emitter turned about y for a monster caster
#include "game/effects/bespoke/overlay_base.h"
#include "game/effects/fx_math.h"

namespace oc::effects {
namespace {

// The lance (emitter '1', a mesh sprite) is integrated by the engine and pulled back by the same velocity in its draw callback, so it stays
// where it spawned while its velocity still orients it (face-velocity flags).
class IceLance : public OverlayFx {
 protected:
  void init() override {
    if (!em()) return;
    em()->set_object_hook("1", [](EmitterSystem::ObjectRef& object) {
      for (int i = 0; i < 3; ++i)
        object.pos[i] = static_cast<s32>(static_cast<u32>(object.pos[i]) - (static_cast<u32>(object.vel[i]) << object.shift));
    }, true);
  }
};

// Every emitter of the pack: op 0x39 selector byte a = 0 (world basis), op 08 position turned about y by A (x' = (z sinA + x cosA) >> 12,
// z' = (z cosA - x sinA) >> 12), op 0x19 y angle += A when op 06 has flag 0x4000000; 'tget' op 0x0A = (1, 0, 0), or (-1, 0, 0) when A != 0.
// A = 0x800 for a monster caster, else 0. Not done: the camera records' reference byte (the pack's type-0x0C entries; BattleCamera owns them).
class Earth27 : public OverlayFx {
 protected:
  void init() override {
    if (!em()) return;
    const FxModel* caster_model = caster(0);
    const int turn = caster_model && !caster_model->pc ? 0x800 : 0;
    const int sin_turn = rsin(turn), cos_turn = rcos(turn);
    for (size_t i = 0; i < em()->header_count(); ++i) {
      if (!own_header(i)) continue;
      std::array<int, 4> words{};
      if (em()->op_value(i, 0x39, words)) {
        words[0] = 0;
        em()->patch_op(i, 0x39, words);
      }
      if (em()->op_value(i, 0x08, words)) {
        const int x = words[0], z = words[2];
        em()->patch_op(i, 0x08, {static_cast<s16>((z * sin_turn + x * cos_turn) >> 12), words[1], static_cast<s16>((z * cos_turn - x * sin_turn) >> 12), 0});
      }
      bool turns_with_caster = false;
      for (const auto& op : em()->header_def(i).ops)
        if (op.op == 0x06)
          if (const auto* flags = std::get_if<assets::FxFlags>(&op.arg)) turns_with_caster = (flags->flags & 0x4000000) != 0;
      if (turns_with_caster && em()->op_value(i, 0x19, words)) {
        words[1] = static_cast<s16>(words[1] + turn);
        em()->patch_op(i, 0x19, words);
      }
    }
    const int tget = em()->find("tget");
    if (tget >= 0) em()->patch_op(static_cast<size_t>(tget), 0x0A, {turn == 0 ? 1 : -1, 0, 0, 0});
  }
};

}  // namespace

void register_emitter_patches(BespokeRegistry& r) {
  r.add({"icerance", "emitter-patch", "IceLance", [] { return std::make_unique<IceLance>(); }});
  r.add({"27earth", "emitter-patch", "Earth27", [] { return std::make_unique<Earth27>(); }});
}

}  // namespace oc::effects
