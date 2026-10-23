#include "game/effects/effect_slots.h"

#include <cctype>
#include <utility>

namespace oc::effects {

std::string fnd_name(ByteSpan fnd, size_t index) {
  if ((index + 1) * 64 > fnd.size()) return {};
  std::string name;
  for (size_t i = 0; i < 64 && fnd[index * 64 + i]; ++i) name.push_back(static_cast<char>(std::tolower(fnd[index * 64 + i])));
  for (char& ch : name)
    if (ch == '\\') ch = '/';
  const std::string prefix = "c:/work/cdrom/";
  if (name.compare(0, prefix.size(), prefix) == 0) name.erase(0, prefix.size());
  return name;
}

namespace {
bool is_dummy_path(const std::string& path) { return path.rfind("dummy/", 0) == 0; }

bool is_dummy(const std::string& ctd, const std::string& prd, const std::string& prg) {
  auto unused = [](const std::string& path) { return path.empty() || is_dummy_path(path); };
  return unused(ctd) && unused(prd) && unused(prg);
}
}  // namespace

std::vector<EffectSlot> battle_effect_slots(ByteSpan fnd1, ByteSpan fnd2, const std::function<bool(const std::string&)>& exists) {
  std::vector<EffectSlot> out;
  const size_t records1 = fnd1.size() / 64, records2 = fnd2.size() / 64;
  for (int eid = 2; eid <= 542; ++eid) {
    if (eid == 15 || eid == 16 || eid == 28) continue;
    const size_t base = 4322 + 3 * static_cast<size_t>(eid);
    if (base + 2 >= records1) continue;
    EffectSlot slot;
    slot.eid = eid;
    std::string disc1[3], disc2[3];
    for (size_t k = 0; k < 3; ++k) disc1[k] = fnd_name(fnd1, base + k);
    if (base + 2 < records2)
      for (size_t k = 0; k < 3; ++k) disc2[k] = fnd_name(fnd2, base + k);
    const bool disc1_dummy = is_dummy(disc1[0], disc1[1], disc1[2]);
    const bool same = disc1[0] == disc2[0] && disc1[1] == disc2[1] && disc1[2] == disc2[2];
    const std::string* use = disc1;
    if (base + 2 < records2 && !same && !is_dummy(disc2[0], disc2[1], disc2[2]) && (disc1_dummy || eid == 378)) {
      use = disc2;
      slot.disc2 = true;
    }
    if (is_dummy(use[0], use[1], use[2])) continue;
    bool all_present = true;
    for (size_t k = 0; k < 3; ++k)
      if (!use[k].empty() && !is_dummy_path(use[k]) && !exists(use[k])) all_present = false;
    if (!all_present) continue;
    slot.ctd = is_dummy_path(use[0]) ? "" : use[0];
    slot.prd = is_dummy_path(use[1]) ? "" : use[1];
    slot.prg = is_dummy_path(use[2]) ? "" : use[2];
    out.push_back(std::move(slot));
  }
  return out;
}

}  // namespace oc::effects
