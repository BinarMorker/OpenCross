#pragma once
// Continue: the memory-card choice (Slot 1 / Slot 2), then that card's save slot list (15 slots per card, CHRONOnn, slot 00 = the
// RDE auto-save). Confirm on a card opens its list ("No save data" for an empty card); confirm on a file picks it; cancel on the list
// goes back to the card choice, cancel on the card choice leaves.
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "save/cc_save.h"
#include "ui/title/canvas.h"
#include "ui/title/nav.h"

namespace oc::ui {

struct SlotEntry {
  // card: the memory card (1 or 2); slot: the CHRONOnn number; block: the memory-card block.
  int card = 1;
  int slot = 0;
  int block = 0;
  save::SaveSlot data;
};

class SlotList {
 public:
  // All decodable slots of `card` (empty when the card is absent), in the card's directory order (= box order).
  static std::vector<SlotEntry> collect(const save::SaveDat& dat, int card = 1);
  // True when either card holds a slot.
  static bool any(const save::SaveDat* dat);

  // Starts on the card choice with the files of both cards of `dat` (none when null).
  // `plus`: the Continue+ list of the New Game+ title: slots whose 2 summary flag bits are set (a
  // cleared save or a New Game+ game) show the menutest message instead of their details and are refused (sfx 0x18).
  void begin(const save::SaveDat* dat, import::Lang lang, ArtMode mode, bool plus = false);
  bool plus() const { return plus_; }
  void set_mode(ArtMode mode) { mode_ = mode; }
  // Character id -> display name (default sysmes name); unset = "#id".
  void set_name_lookup(std::function<std::u32string(int)> fn) { name_of_ = std::move(fn); }
  void set_sfx(std::function<void(int)> fn) { sfx_ = std::move(fn); }

  void update(const Nav& nav, float dt);
  bool picked() const { return picked_; }
  bool cancelled() const { return cancelled_; }
  // The file list of the chosen card is open (false on the card choice and its "No save data" message).
  bool listing() const { return phase_ == Phase::Files; }
  const SlotEntry* selected() const {
    return phase_ != Phase::Files || entries_.empty() ? nullptr : &entries_[static_cast<size_t>(sel_)];
  }
  int selected_index() const { return sel_; }
  size_t size() const { return entries_.size(); }
  void draw(DrawList& dl, UiKit& kit) const;

 private:
  void play(int id) {
    if (sfx_) sfx_(id);
  }
  struct NamePic {
    render::TexRef set = {};
    float w = 0, h = 0;
  };
  mutable std::map<std::string, NamePic> pics_;
  // Image cache vs the mod overlay: when Vfs::generation() moves (Mods tab / art mode Modded), every cached picture is decoded again from
  // the re-pointed files; a same-sized one is refreshed in place (UiKit::update_rgba), others re-uploaded.
  mutable std::map<std::string, NamePic> stale_;
  mutable std::uint32_t vfs_gen_ = 0;
  mutable std::map<int, std::string> face_files_;
  // Which face folder face_files_ lists: 0 none, 1 = face/2021 (HD), 2 = face/1999 (original art).
  mutable int faces_listed_ = 0;
  mutable std::unique_ptr<import::LangTables> tables_;
  // Card: Slot 1 / Slot 2 choice; NoData: "No save data in Slot N." (confirm or 2 s back to the card choice); Files: the list.
  enum class Phase { Card, NoData, Files };
  Phase phase_ = Phase::Card;
  // card_: 0 = Slot 1, 1 = Slot 2; entries_: the files of that card once chosen.
  int card_ = 0;
  float phase_time_ = 0, clock_ = 0;
  std::array<std::vector<SlotEntry>, 2> cards_;
  std::vector<SlotEntry> entries_;
  import::Lang lang_ = import::Lang::En;
  ArtMode mode_ = ArtMode::Hd;
  int sel_ = 0, top_ = 0;
  bool picked_ = false, cancelled_ = false;
  bool plus_ = false;
  std::function<std::u32string(int)> name_of_;
  std::function<void(int)> sfx_;
};

// Play time as "H:MM:SS".
std::u32string format_playtime(u32 frames);

}  // namespace oc::ui
