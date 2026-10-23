#pragma once
// ModelCache: the battle's loaded unit models, weapon models and per-model script blocks, keyed by model folder stem, plus the
// file lookups that name them (character / monster model folders, weapon packs).
#include <map>
#include <memory>
#include <string>

#include "assets/room_effects.h"
#include "core/types.h"
#include "game/actor.h"
#include "game/battle/battle_camera.h"
#include "game/battle/battle_seq.h"

namespace oc::battle {
struct Unit;
}
namespace oc::data {
struct GameTables;
}
namespace oc::render {
class MeshPipeline;
}
namespace oc::vfs {
class Vfs;
}

namespace oc::game {

// Scripts and resources shared by every unit that uses the same model folder: the model's S5 state-script block, the external 'atk'
// table and the camera entries (type 0x0C / 0x10: the atk0..3 sequences and their shots) of its at0.prd, the raw at0.prd (sound
// registration happens per battle, with the weapon class) and the model's own S6 DRP (44 monsters), whose emitter headers stay
// registered for the whole battle.
struct ModelScripts {
  bseq::Table s5, atk;
  bool has_s5 = false, has_atk = false;
  CamBank cam;
  Bytes at0;
  std::shared_ptr<const assets::RoomEffects> s6fx;
};

struct ModelCache {
  std::map<std::string, std::shared_ptr<ModelHandle>> models;
  // "pack#part".
  std::map<std::string, std::shared_ptr<ModelHandle>> weapons;
  std::map<std::string, std::shared_ptr<ModelScripts>> scripts;
  // Used when a model has no 'atk' table.
  bseq::Table fallback_atk;

  // Drops every loaded model, weapon and script block (the fallback table stays).
  void clear();

  static std::string player_model(const vfs::Vfs& vfs, int char_id);
  static std::string monster_model(const data::GameTables* tables, int monster_id);
  // The unit's model folder: the character's, or the creature's after a Dopplegang form change.
  static std::string unit_model_stem(const vfs::Vfs& vfs, const data::GameTables* tables, const battle::Unit& unit);
  // The hit-sound / hit-emitter group digit of a unit: the equipped weapon's material tier & 0xF (the same byte as the weapon model part;
  // NOT the crit class), -1 for monsters (their "_s00" entries are not renamed).
  static int weapon_digit(const data::GameTables* tables, const battle::Unit& unit);

  std::shared_ptr<ModelHandle> load_model_for(const vfs::Vfs& vfs, render::MeshPipeline& meshes, const std::string& dir_stem);
  std::shared_ptr<ModelHandle> load_weapon(const vfs::Vfs& vfs, render::MeshPipeline& meshes, int char_id, int part);
  std::shared_ptr<ModelHandle> load_enemy_weapon(const vfs::Vfs& vfs, render::MeshPipeline& meshes, int model, int part);
  std::shared_ptr<ModelScripts> load_scripts_for(const vfs::Vfs& vfs, const CamBank* bank, const std::string& dir_stem);
};

}  // namespace oc::game
