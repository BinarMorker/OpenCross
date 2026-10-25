#include "game/autosave.h"

#include <algorithm>

namespace oc::game {

bool Autosave::entrance_excluded(int room, int entrance) {
  switch (room) {
    case 0xFA: return entrance >= 0x13 && entrance <= 0x16;
    case 0xFB: return entrance >= 0x13 && entrance <= 0x17;
    case 0xFC: return (entrance >= 0x10 && entrance <= 0x15) || entrance == 0x18 || entrance == 0x19 || entrance == 0x1A || entrance == 0x1D;
    case 0xFD: return (entrance >= 0x10 && entrance <= 0x15) || (entrance >= 0x17 && entrance <= 0x1A) || entrance == 0x1D;
    default: return false;
  }
}

bool Autosave::update(const Field& field) {
  const auto done = [&](bool allowed) {
    last_location_ = field.location;
    last_allowed_ = allowed;
  };
  if (suppress_) {
    suppress_ = false;
    done(true);
    return false;
  }
  if (!field.allowed || (last_allowed_ && field.location == last_location_)) {
    done(field.allowed);
    return false;
  }
  const bool excluded = entrance_excluded(field.room, field.entrance);
  done(field.allowed);
  return !excluded;
}

// The slot list shows the header caption ("auto save" in katakana, 12 bytes Shift-JIS) instead of the leader's name; the RDE
// extension record of slot 0 (language, battle counter, custom names) follows the block.
namespace {
save::SaveSlot captioned(const save::SaveSlot& state) {
  save::SaveSlot slot = state;
  static const u8 kCaption[12] = {0x83, 0x49, 0x81, 0x5B, 0x83, 0x67, 0x83, 0x5A, 0x81, 0x5B, 0x83, 0x75};
  std::copy(std::begin(kCaption), std::end(kCaption), slot.head.begin() + save::head::kLeaderName);
  return slot;
}
}  // namespace

bool Autosave::write(save::SaveDat& dat, const save::SaveSlot& state) {
  save::MemCard* card = dat.card(1);
  if (!card) return false;
  if (!save::write_slot(*card, 0, captioned(state))) return false;
  dat.write_ext(1, 0);
  return true;
}

void Autosave::write(save::OcSaves& page, const save::SaveSlot& state) { page.write(0, captioned(state)); }

Autosave::Targets Autosave::targets(const save::SaveDat* dat, const save::OcSaves* page) {
  Targets out;
  for (int card_no = 1; card_no <= 2 && dat; ++card_no)
    if (const save::MemCard* card = dat->card(card_no); card && !save::list_slots(*card).empty()) out.card = true;
  out.page = page && !page->empty();
  if (!out.card && !out.page) out.page = page != nullptr;
  return out;
}

}  // namespace oc::game
