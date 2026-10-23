#pragma once
// Battle effect slot table: effect id -> (egfx .ctd, eins .prd, overlay .prg) from the 64-byte path records of cdrom.fnd /
// cdrom2.fnd, absolute record index 4322 + 3 * eid + {0, 1, 2}.
#include <functional>
#include <string>
#include <vector>

#include "core/types.h"

namespace oc::effects {

struct EffectSlot {
  int eid = 0;
  // Archive paths relative to cdrom ("battle/effects/blue/1beam.prd"), "" = none.
  std::string ctd, prd, prg;
  // Filled from cdrom2.fnd.
  bool disc2 = false;
};

// Names of the `index`-th 64-byte record, lowered, with the "c:\work\cdrom\" prefix removed and '/' separators.
std::string fnd_name(ByteSpan fnd, size_t index);

// All slots eid 2..542 that hold real files (excluding the irregular slots 1, 15, 16, 28). `exists(path)` tells whether an
// archive member is present (the dummy placeholders are always accepted). A slot comes from cdrom2.fnd when its cdrom.fnd
// record is a dummy (or for eid 378) and the disc-2 record differs and is real.
std::vector<EffectSlot> battle_effect_slots(ByteSpan fnd1, ByteSpan fnd2, const std::function<bool(const std::string&)>& exists);

}  // namespace oc::effects
