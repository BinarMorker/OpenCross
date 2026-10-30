#pragma once
// The RDE auto-save: slot 0 of card 1, written once per field tick when the party's location changes while saving is
// allowed, except through the excluded entrances of the four world-map rooms 0xFA..0xFD. OpenCross also keeps one on its own
// save page (targets()).
#include "core/types.h"
#include "save/cc_save.h"
#include "save/oc_saves.h"

namespace oc::game {

class Autosave {
 public:
  struct Field {
    // The map id the party stands in.
    u16 location = 0;
    // The main menu may save (script variable 0x3C == 1).
    bool allowed = false;
    // Map id; rooms 0xFA..0xFD have entrance exclusions.
    int room = -1;
    // The entrance record the room was entered through.
    int entrance = 0;
  };

  // A save was loaded (Continue / Save > Load): the next tick only records the location.
  void note_load() { suppress_ = true; }
  // Per field tick. True = a write is due now.
  bool update(const Field& field);
  // The no-auto-save entrances of the four rooms 0xFA..0xFD.
  static bool entrance_excluded(int room, int entrance);

  // Writes `state` as card 1 slot 0 (leader-name field = "auto save" in katakana); false = no card 1 or the card is full.
  // `state` is not modified.
  static bool write(save::SaveDat& dat, const save::SaveSlot& state);
  // The same as the OpenCross page's auto-save (file 0). Throws on I/O errors.
  static void write(save::OcSaves& page, const save::SaveSlot& state);

  // Where an auto-save goes: card 1 when either memory card holds a save, the OpenCross page when it holds one; with no save
  // anywhere, the OpenCross page.
  struct Targets {
    bool card = false;
    bool page = false;
  };
  static Targets targets(const save::SaveDat* dat, const save::OcSaves* page);

 private:
  bool suppress_ = false;
  u16 last_location_ = 0;
  bool last_allowed_ = false;
};

}  // namespace oc::game
