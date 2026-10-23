#pragma once
// Mods: folders of loose replacement files in <GameDir>\mods\<mod name>\..., resolved once against the shipped archives and
// installed into the VFS as an Overlay for the current art mode.
//
// Resolution of one mod file (relative path inside the mod folder, compared component-wise, case-insensitive):
//   1. file name "<archive>.dat" of a shipped data\<archive>.dat that is a ZIP -> whole-archive replacement;
//   2. otherwise every shipped member with the same file name is a candidate (cdrom2 members count as cdrom/, the path the game
//      reads); the candidates sharing the longest trailing run of path components with the mod path win, so leading wrapper
//      folders are ignored and a bare file name falls back to a unique name match; one winner = matched, several = ambiguous
//      (not loaded), none = unmatched (not loaded).
// Priority: the mod list order; the first enabled mod that loads on the current art mode wins a target.
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace oc::vfs {

class Vfs;

// "Load on" setting of a mod: in which art modes its files are active.
enum class ModLoadOn : std::uint8_t { Modded, Sd, Hd, All };
// The art mode the overlay is built for (Sd = original / classic art, Modded = HD art + mods).
enum class ModArt : std::uint8_t { Sd, Hd, Modded };

// The "load on" names "modded", "sd", "hd", "all"; an unknown name parses as Modded.
const char* load_on_name(ModLoadOn load_on);
ModLoadOn parse_load_on(std::string_view name);
bool loads_on(ModLoadOn load_on, ModArt art);

// First components of a texture replacement path (render/texture_registry.h): a mod file <mod>\[wrapper\]textures\<category>\... is a
// texture replacement, not an archive member.
inline constexpr const char* kTextureCategories[] = {"battle_stage", "title", "effects", "effect_mesh", "room_mesh", "room_model", "model", "tiles"};

// One mod file: `target` = the logical VFS path ("hd/face/2021/00_seljyu.png") or, for archives, the archive name ("movie"); `file` =
// the file on disk; `rel` = its path inside the mod folder (UTF-8, '/'), for display / logs.
struct ModFile {
  std::string target;
  std::filesystem::path file;
  std::string rel;
};

// One mod folder. `name` is the folder name (UTF-8): the name shown in the Mods tab and the [Mods] ini key. `files` are the matched
// member replacements, `archives` the whole-archive replacements, `textures` the texture replacements (target = canonical texture
// path), `voices` the voice lines (<mod>\voices\**\*.mp3, or any .mp3 when the folder itself is called "voices"; played by
// audio::VoicePlayer, not VFS targets). `unmatched` / `ambiguous` list the rel paths that match no shipped member / whose best match is
// not unique.
struct Mod {
  std::string name;
  std::filesystem::path root;
  bool enabled = true;
  ModLoadOn load_on = ModLoadOn::Modded;
  std::vector<ModFile> files;
  std::vector<ModFile> archives;
  std::vector<ModFile> textures;
  std::vector<ModFile> voices;
  std::vector<std::string> unmatched;
  std::vector<std::string> ambiguous;
};

// Persisted per-mod settings, in priority order.
struct ModPref {
  std::string name;
  bool enabled = true;
  ModLoadOn load_on = ModLoadOn::Modded;
};

class ModManager {
 public:
  // Scans <mods_dir>\<name>\** and resolves every file against vfs.list_base(). Order and settings come from `prefs` (by name);
  // folders not in `prefs` are appended in name order with the defaults; prefs whose folder is gone are dropped. Logs each mod's
  // matched / unmatched / ambiguous counts.
  void scan(const Vfs& vfs, const std::filesystem::path& mods_dir, const std::vector<ModPref>& prefs);
  std::vector<ModPref> prefs() const;

  std::vector<Mod>& mods() { return mods_; }
  const std::vector<Mod>& mods() const { return mods_; }
  const std::filesystem::path& dir() const { return dir_; }

  // Builds the overlay of the enabled mods that load on `art` (priority order, first wins) and installs it into `vfs`.
  void apply(Vfs& vfs, ModArt art) const;

  // Targets of mod `index` that another enabled mod also provides in a shared art mode, with the winning mod (may be `index` itself).
  struct Conflict {
    std::string target;
    size_t winner = 0;
  };
  std::vector<Conflict> conflicts(size_t index) const;

 private:
  std::filesystem::path dir_;
  std::vector<Mod> mods_;
};

}  // namespace oc::vfs
