#include "game/battle/texture_dump.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iterator>
#include <set>

#include "assets/battle_stage.h"
#include "assets/model.h"
#include "assets/room_scene.h"
#include "core/log.h"
#include "game/battle/battle_fx.h"
#include "game/effects/gpu_effects.h"
#include "game/title_backdrop.h"
#include "import/battle_stage.h"
#include "import/model.h"
#include "import/room.h"
#include "render/stage_tiles.h"
#include "render/texture_registry.h"
#include "vfs/vfs.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace oc::game {

namespace {

namespace tr = render::texture_registry;

std::string lower(std::string text) {
  for (char& ch : text) ch = ch == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return text;
}

bool rgba_ok(const import::Image& img) { return !img.indexed && img.width && img.height && img.pixels.size() >= size_t(img.width) * img.height * 4; }

// `occ dump-textures --raw`: the raw (mode, CLUT) atlases of baked stages too, under raw/.
bool g_raw = false;

// Baked assets (battle stages, the title, room meshes): one PNG per object (assets::StageObjectTexture). Un-baked ones (effect meshes): the atlases.
void dump_stage_asset(const assets::StageAsset& st, const std::string& group) {
  if (!st.object_textures.empty()) {
    render::dump_tiles(st, group, true);
    if (!g_raw) return;
    for (size_t i = 0; i < st.textures.size(); ++i) {
      const assets::StageTexture& texture = st.textures[i];
      if (rgba_ok(texture.image)) tr::dump_forced(render::raw_atlas_key(group, static_cast<int>(i), texture.mode, texture.clut_x, texture.clut_y), texture.image.width, texture.image.height, texture.image.pixels.data());
    }
    return;
  }
  for (size_t i = 0; i < st.textures.size(); ++i) {
    const assets::StageTexture& texture = st.textures[i];
    const import::Image img = texture.image.indexed ? import::to_rgba(texture.image) : texture.image;
    if (!rgba_ok(img)) continue;
    tr::dump_forced(render::stage_group_key(group, static_cast<int>(i), texture.mode, texture.clut_x, texture.clut_y, img.width, img.height, img.pixels.data()), img.width,
                    img.height, img.pixels.data());
  }
}

void dump_sheets(const assets::RoomEffects& fx) {
  for (const assets::SpriteSheet& sh : fx.sheets) {
    u32 w = 0, h = 0;
    std::vector<u8> img;
    if (!effects::sheet_atlas_image(sh, w, h, img)) continue;
    tr::dump_forced(effects::effect_sheet_key(sh, w, h, img.data()), w, h, img.data());
  }
}

void dump_model_asset(const assets::ModelAsset& asset, const std::string& source) {
  for (size_t i = 0; i < asset.textures.size(); ++i) {
    const import::Image& img = asset.textures[i];
    if (!rgba_ok(img)) continue;
    tr::dump_forced(render::model_texture_key(source, i, asset.main_texture_count, img.width, img.height, img.pixels.data()), img.width, img.height,
                    img.pixels.data());
  }
}

Bytes read_or_empty(const vfs::Vfs& archive, const std::string& path) {
  auto data = archive.read(path);
  return data ? std::move(*data) : Bytes{};
}

// Phase bookkeeping: files written in a phase = the registry's unique-file count difference.
struct Phase {
  TextureDumpProgress& progress;
  u32 start;
  Phase(TextureDumpProgress& progress_ref, std::string name, u32 total) : progress(progress_ref), start(tr::stats().dumped) {
    progress.done = 0;
    progress.total = total;
    progress.set_phase(std::move(name));
  }
  ~Phase() {
    const u32 written = tr::stats().dumped - start;
    progress.files += written;
    progress.end_phase(written);
  }
};

u32 phase_stages(const vfs::Vfs& archive, TextureDumpProgress& progress) {
  std::vector<std::string> names;
  std::set<std::string> seen;
  for (const std::string& path : archive.list("cdrom")) {
    const std::string lowered = lower(path);
    if (lowered.starts_with("battle/bg/bg_") && lowered.ends_with(".prd") && seen.insert(lowered).second) names.push_back(path);
  }
  Phase ph(progress, "battle stages", static_cast<u32>(names.size()));
  u32 failed = 0;
  for (const std::string& path : names) {
    if (progress.cancel) break;
    const std::string lowered = lower(path);
    // Stem of the file name, e.g. bg_NN.
    const std::string stem = lowered.substr(10, lowered.size() - 14);
    const auto prd = archive.read("cdrom/" + path);
    const Bytes ctd = read_or_empty(archive, "cdrom/" + path.substr(0, path.size() - 4) + ".ctd");
    if (prd) {
      if (auto st = import::load_stage(*prd, ctd)) {
        render::bake_object_textures(*st);
        dump_stage_asset(*st, "battle_stage/" + stem);
      } else {
        ++failed;
      }
    }
    ++progress.done;
  }
  return failed;
}

u32 phase_title(const vfs::Vfs& archive, TextureDumpProgress& progress) {
  Phase ph(progress, "title backdrop", 1);
  auto st = load_title_stage(archive);
  if (!st) return 1;
  dump_stage_asset(*st, "title");
  progress.done = 1;
  return 0;
}

u32 phase_effects(const vfs::Vfs& archive, TextureDumpProgress& progress) {
  BattleFxLibrary lib;
  if (!lib.init(archive, /*bind_globals=*/false)) return 1;
  Phase ph(progress, "effect packs", static_cast<u32>(lib.slots().size()) + 1);
  dump_sheets(lib.particle_lib());
  if (auto res = lib.resident()) {
    for (const auto& pk : res->packs) dump_sheets(*pk);
    if (res->meshes) dump_stage_asset(*res->meshes, "effect_mesh");
  }
  ++progress.done;
  for (const auto& slot : lib.slots()) {
    if (progress.cancel) break;
    if (const auto pack = lib.get(slot.eid)) {
      dump_sheets(pack->fx);
      if (pack->meshes) dump_stage_asset(*pack->meshes, "effect_mesh");
    }
    ++progress.done;
  }
  return 0;
}

std::vector<std::string> list_rooms(const vfs::Vfs& archive) {
  std::vector<std::string> bins, ctds;
  for (const std::string& path : archive.list("cdrom")) {
    const std::string lowered = lower(path);
    const size_t at = lowered.find("map/mapbin/");
    if (at == std::string::npos || lowered.size() < at + 16) continue;
    const std::string stem = lowered.substr(at + 11, lowered.size() - at - 15);
    if (stem.find('/') != std::string::npos) continue;
    if (lowered.ends_with(".bin")) bins.push_back(stem);
    if (lowered.ends_with(".ctd")) ctds.push_back(stem);
  }
  std::sort(bins.begin(), bins.end());
  std::sort(ctds.begin(), ctds.end());
  std::vector<std::string> out;
  std::set_intersection(bins.begin(), bins.end(), ctds.begin(), ctds.end(), std::back_inserter(out));
  return out;
}

// One room: its effect sheets (room VRAM), the .prd mesh effects' atlases and every room model's textures.
bool dump_room(const vfs::Vfs& archive, const std::string& name, unsigned what) {
  const std::string base = "cdrom/map/mapbin/" + name;
  const auto bin = archive.read(base + ".bin");
  const auto ctd = archive.read(base + ".ctd");
  if (!bin || !ctd) return false;
  const auto prd = archive.read(base + ".prd");
  std::string err;
  auto room = import::load_room(name, *bin, *ctd, prd ? ByteSpan(*prd) : ByteSpan{}, &err);
  if (!room) return false;
  if (what & kDumpEffects) dump_sheets(room->effects);
  if (!(what & kDumpRooms)) return true;
  if (prd) {
    auto st = import::load_stage(*prd, {}, &err);
    if (st && !st->parts.empty()) {
      st->tex_group = "room_mesh";
      render::bake_object_textures(*st);
      dump_stage_asset(*st, "room_mesh");
    }
  }
  for (const auto& room_model : room->models) {
    auto asset = room_model.page_textures.empty() ? import::load_room_model(room_model.mdl, room_model.texture, &err) : import::load_room_model(room_model.mdl, room_model.page_textures, &err);
    if (asset) dump_model_asset(*asset, "@room_model");
  }
  return true;
}

u32 phase_rooms(vfs::Vfs& archive, TextureDumpProgress& progress, unsigned what) {
  const std::vector<std::string> rooms = list_rooms(archive);
  // Two passes per room: classic (disc) files, then the HD file set (art differs: .ctd pages, models).
  Phase ph(progress, "rooms", static_cast<u32>(rooms.size()) * 2);
  u32 failed = 0;
  for (const bool hd : {false, true}) {
    archive.set_hd_mode(hd);
    for (const std::string& room_name : rooms) {
      if (progress.cancel) {
        archive.set_hd_mode(false);
        return failed;
      }
      try {
        if (!dump_room(archive, room_name, what)) ++failed;
      } catch (...) {
        ++failed;
      }
      ++progress.done;
    }
  }
  archive.set_hd_mode(false);
  return failed;
}

u32 phase_models(vfs::Vfs& archive, TextureDumpProgress& progress) {
  // HD models (cdrom2 override first); the path-named keys hold the art the game shows by default.
  archive.set_hd_mode(true);
  std::vector<std::string> stems;
  std::set<std::string> seen;
  for (const std::string& path : archive.list("cdrom")) {
    const std::string lowered = lower(path);
    if (!lowered.ends_with(".obj") || !(lowered.starts_with("battle/player/") || lowered.starts_with("battle/monster/") || lowered.starts_with("fieldobj/player/"))) continue;
    if (seen.insert(lowered).second) stems.push_back(lowered.substr(0, lowered.size() - 4));
  }
  Phase ph(progress, "models", static_cast<u32>(stems.size()));
  u32 failed = 0;
  for (const std::string& stem : stems) {
    if (progress.cancel) break;
    try {
      const bool field = stem.starts_with("fieldobj/");
      const std::string base = "cdrom/" + stem;
      const Bytes obj = read_or_empty(archive, base + ".obj");
      Bytes tex, ext;
      const std::string name = stem.substr(stem.find_last_of('/') + 1);
      if (field) {
        tex = read_or_empty(archive, base + ".tim");
        ext = read_or_empty(archive, "cdrom/fieldobj/ex/" + name + ".bin");
      } else {
        tex = read_or_empty(archive, base + ".ltd");
        if (tex.empty()) tex = read_or_empty(archive, base + ".ltc");
        ext = read_or_empty(archive, "cdrom/" + stem.substr(0, stem.find_last_of('/')) + "/at0.prd");
      }
      std::string err;
      auto asset = field ? import::load_field_model(obj, tex, ext, &err) : import::load_model(obj, tex, ext, &err);
      if (asset) dump_model_asset(*asset, stem);
      else ++failed;
    } catch (...) {
      ++failed;
    }
    ++progress.done;
  }
  archive.set_hd_mode(false);
  return failed;
}

}  // namespace

std::string TextureDumpProgress::phase() const {
  std::lock_guard lock(mutex_);
  return phase_;
}
std::vector<std::pair<std::string, u32>> TextureDumpProgress::report() const {
  std::lock_guard lock(mutex_);
  return report_;
}
std::string TextureDumpProgress::summary() const {
  std::lock_guard lock(mutex_);
  return summary_;
}
void TextureDumpProgress::set_phase(std::string name) {
  std::lock_guard lock(mutex_);
  phase_ = std::move(name);
}
void TextureDumpProgress::end_phase(u32 files_in_phase) {
  std::lock_guard lock(mutex_);
  report_.emplace_back(phase_, files_in_phase);
}

int run_texture_dump(const std::filesystem::path& game_dir, unsigned what, TextureDumpProgress& progress) {
  const auto start_time = std::chrono::steady_clock::now();
  progress.running = true;
  progress.finished = false;
  progress.files = 0;
  {
    std::lock_guard lock(progress.mutex_);
    progress.report_.clear();
  }
  vfs::Vfs archive;
  u32 failed = 0;
  int rc = 0;
  if (!archive.open(game_dir)) {
    rc = 1;
  } else {
    if (what & kDumpStages) failed += phase_stages(archive, progress);
    if ((what & kDumpTitle) && !progress.cancel) failed += phase_title(archive, progress);
    if ((what & kDumpEffects) && !progress.cancel) failed += phase_effects(archive, progress);
    g_raw = (what & kDumpRaw) != 0;
    if ((what & (kDumpRooms | kDumpEffects)) && !progress.cancel) failed += phase_rooms(archive, progress, what);
    if ((what & kDumpModels) && !progress.cancel) failed += phase_models(archive, progress);
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
  progress.seconds = secs;
  std::string summary = std::string(rc ? "texture dump: game files not found" : progress.cancel ? "texture dump cancelled" : "texture dump done") + ": " +
                        std::to_string(progress.files.load()) + " file(s)";
  for (const auto& [name, count] : progress.report()) summary += ", " + name + " " + std::to_string(count);
  summary += ", " + std::to_string(failed) + " item(s) skipped, " + std::to_string(static_cast<int>(secs)) + " s";
  {
    std::lock_guard lock(progress.mutex_);
    progress.summary_ = summary;
  }
  log::info("{}", summary);
  progress.running = false;
  progress.finished = true;
  return rc;
}

bool TextureDumpJob::start(const std::filesystem::path& game_dir, unsigned what) {
  if (progress_.running) return false;
  join();
  progress_.cancel = false;
  // Set before the thread starts so a second click cannot race it.
  progress_.running = true;
  progress_.finished = false;
  progress_.done = 0;
  progress_.total = 0;
  thread_ = std::thread([this, game_dir, what] {
#ifdef _WIN32
    // Keep the game's frames smooth.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    run_texture_dump(game_dir, what, progress_);
  });
  return true;
}

void TextureDumpJob::join() {
  if (thread_.joinable()) thread_.join();
}

}  // namespace oc::game
