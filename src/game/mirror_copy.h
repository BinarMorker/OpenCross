#pragma once
// The reflected copy of an actor that script op FE 03 switches on: the pure rules, header-only so the unit tests reach them.
#include "core/types.h"

namespace oc::game::mirror_copy {

// The copy is drawn only while the entity flag word has bit 11 set (FE 03) and bit 26 clear (FE 45 would set it).
constexpr bool drawn(u32 flags270) { return (flags270 & 0x4000800u) == 0x800u; }

// Axis code 4 / 8 / 0x10 -> world axis 0 / 1 / 2 (x / y / z); -1 = no copy.
constexpr int axis_index(int code) { return code == 4 ? 0 : code == 8 ? 1 : code == 0x10 ? 2 : -1; }

// The copy's coordinate on the mirrored axis: `plane2` = twice the mirror plane's coordinate, so the result is the reflection.
constexpr int reflect(int plane2, int pos) { return plane2 - pos; }

// The copy's ordering-table slot = the actor's slot + the same push the submerged faces take, i.e. behind the background layers
// whose z is below the push: it is filed with the back layers' pass.
constexpr float slot(float actor_slot, float push) { return actor_slot + push; }

}  // namespace oc::game::mirror_copy
