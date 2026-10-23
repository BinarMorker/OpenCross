// ocbattle fx-reg: the effect-registry model of a battle start.
//
//   ocbattle fx-reg SCENE [--bg N]   the (type, name) entries ours has at battle start: battle/battle.prd (nested drps included),
//                                    the stage bg_NN.prd and each present unit's model S6 DRP (party = Serge only).
//                                    One line per entry: type<TAB>name<TAB>count<TAB>sources (comma list: battle, particle, bg, u<unit>=<model dir>)
//   ocbattle fx-reg --index          every (type, name) of every drp the game has (battle.prd, particle, bg_00..99, at0.prd of every model,
//                                    effect slots): type<TAB>name<TAB>file list
//   ocbattle fx-reg --index --all    the same over every cdrom member that parses as a drp
//   ocbattle fx-reg --file PATH      one drp (path under cdrom/): its entries in order (type, name, size), nested drps indented
//   ocbattle fx-reg --refs NAME      every drp file / entry path whose decoded entries (LZSS inflated) contain the four bytes NAME,
//                                    then the scenes whose enemy models own that at0.prd
//   ocbattle fx-reg --s6             every battle model with an S6 DRP: stem, its type-10 header names, the enemy ids using it, the scenes
//   ocbattle fx-reg --grep NAME      the four bytes NAME anywhere in any cdrom member (raw, LZSS inflated, or inside a nested entry)
// Every entry of a loaded drp is registered, nested type-1 drps included; LZSS entries are registered as one node.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/types.h"
#include "data/tables.h"
#include "game/battle/battle_fx.h"
#include "game/battle/model_table.h"
#include "import/container.h"
#include "import/lzss.h"
#include "vfs/vfs.h"

using namespace oc;

namespace {

using Key = std::pair<int, std::string>;

void walk(ByteSpan bytes, std::map<Key, int>& out) {
  const auto drp = import::parse_drp(bytes);
  if (!drp) return;
  for (const auto& entry : drp->entries) {
    ++out[{static_cast<int>(entry.type), entry.name}];
    if (entry.type == 1 && import::is_drp(entry.data)) walk(entry.data, out);
  }
}

// Every entry (nested, LZSS inflated) holding the bytes of `needle`; prints "<file>: path".
void refs(ByteSpan bytes, const std::string& needle, const std::string& file, const std::string& path, int& hits, int depth = 0) {
  const auto drp = import::parse_drp(bytes);
  if (!drp || depth > 6) return;
  for (const auto& entry : drp->entries) {
    const std::string entry_path = path + "/" + entry.name;
    Bytes dec;
    ByteSpan body = entry.data;
    if (import::is_lzss(entry.data)) {
      if (auto inflated = import::lzss_decode(entry.data)) {
        dec = std::move(*inflated);
        body = dec;
      }
    }
    if (body.size() >= 4 && std::search(body.begin(), body.end(), needle.begin(), needle.end()) != body.end()) {
      std::printf("%s: %s (type %d)\n", file.c_str(), entry_path.c_str(), static_cast<int>(entry.type));
      ++hits;
    }
    if (import::is_drp(body)) refs(body, needle, file, entry_path, hits, depth + 1);
  }
}

std::string dir_of(const std::string& stem) { return stem.substr(0, stem.find_last_of('/')); }

}  // namespace

int cmd_fx_reg(const data::GameTables& tables, const vfs::Vfs& game_fs, const std::vector<std::string>& args) {
  if (args.empty()) return 2;
  using game::kBattleModels;
  std::vector<std::string> model_dirs;
  std::set<std::string> seen;
  for (const std::string_view stem : kBattleModels) {
    const std::string st(stem);
    if (st.empty()) continue;
    if (seen.insert(dir_of(st)).second) model_dirs.push_back(dir_of(st));
  }
  auto enemy_dir = [&](int enemy) -> std::string {
    if (enemy < 0 || enemy >= static_cast<int>(tables.enemies.size())) return {};
    const size_t model = tables.enemies[static_cast<size_t>(enemy)].model();
    if (model >= kBattleModels.size() || kBattleModels[model].empty()) return {};
    return dir_of(std::string(kBattleModels[model]));
  };
  if (args[0] == "--s6") {
    std::set<std::string> done;
    for (const std::string_view sv : kBattleModels) {
      const std::string stem(sv);
      if (stem.empty() || !done.insert(stem).second) continue;
      const auto obj = game_fs.read("cdrom/" + stem + ".obj");
      if (!obj || obj->size() < 32) continue;
      u32 s6 = 0;
      std::memcpy(&s6, obj->data() + 4 + 5 * 4, 4);
      if (!s6 || s6 + 12 >= obj->size()) continue;
      std::map<Key, int> entries;
      walk(ByteSpan(obj->data() + s6, obj->size() - s6), entries);
      std::string names;
      for (const auto& [key, count] : entries)
        if (key.first == 10) names += (names.empty() ? "" : ",") + key.second + (count > 1 ? "x" + std::to_string(count) : "");
      // The other entry types of the S6 (sequences 0x10, cameras 0x0C ...): type:name.
      std::string other;
      for (const auto& [key, count] : entries)
        if (key.first != 10) other += (other.empty() ? "" : ",") + std::to_string(key.first) + ":" + key.second + (count > 1 ? "x" + std::to_string(count) : "");
      std::string en, scs;
      for (size_t enemy = 0; enemy < tables.enemies.size(); ++enemy) {
        const size_t mi = tables.enemies[enemy].model();
        if (mi < kBattleModels.size() && std::string(kBattleModels[mi]) == stem) en += (en.empty() ? "" : ",") + std::to_string(enemy);
      }
      for (size_t scene_index = 0; scene_index < tables.scenes.size(); ++scene_index)
        for (int k = 0; k < 3; ++k) {
          const auto& sc = tables.scenes[scene_index];
          if (!sc.enemy_count(k) || sc.enemy_id(k) == 0xFF || sc.enemy_id(k) >= tables.enemies.size()) continue;
          const size_t mi = tables.enemies[sc.enemy_id(k)].model();
          if (mi < kBattleModels.size() && std::string(kBattleModels[mi]) == stem) {
            scs += (scs.empty() ? "" : ",") + std::to_string(scene_index);
            break;
          }
        }
      std::printf("%s\t%s\t%s\t%s\t%s\n", stem.c_str(), names.c_str(), en.c_str(), scs.c_str(), other.c_str());
    }
    return 0;
  }
  if (args[0] == "--index" || args[0] == "--refs") {
    // Label, vfs path.
    std::vector<std::pair<std::string, std::string>> files;
    files.push_back({"battle.prd", "cdrom/battle/battle.prd"});
    files.push_back({"particle.prd", "cdrom/effect/particle.prd"});
    for (int i = 0; i < 100; ++i) {
      char buf[64];
      std::snprintf(buf, sizeof buf, "bg_%02d.prd", i);
      files.push_back({buf, std::string("cdrom/battle/bg/") + buf});
    }
    files.push_back({"kamei.prd", "cdrom/battle/bg/kamei.prd"});
    for (const auto& dir : model_dirs) files.push_back({dir + "/at0.prd", "cdrom/" + dir + "/at0.prd"});
    game::BattleFxLibrary lib;
    if (lib.init(game_fs))
      for (const auto& slot : lib.slots())
        if (slot.prd.size() > 4) files.push_back({"fx:" + slot.prd, "cdrom/" + slot.prd});
    if (args[0] == "--index" && args.size() > 1 && args[1] == "--all") {
      files.clear();
      for (const std::string& member : game_fs.list("cdrom")) files.push_back({member, "cdrom/" + member});
    }
    if (args[0] == "--index") {
      std::map<Key, std::set<std::string>> idx;
      for (const auto& [label, path] : files) {
        const auto file = game_fs.read(path);
        if (!file || file->size() < 16) continue;
        std::map<Key, int> entries;
        walk(*file, entries);
        for (const auto& [key, count] : entries) idx[key].insert(label);
      }
      for (const auto& [key, labels] : idx) {
        std::string label_list;
        for (const auto& label : labels) label_list += (label_list.empty() ? "" : ",") + label;
        std::printf("%d\t%s\t%s\n", key.first, key.second.c_str(), label_list.c_str());
      }
      return 0;
    }
    if (args.size() < 2 || args[1].size() != 4) return 2;
    std::set<std::string> owners;
    for (const auto& [label, path] : files) {
      const auto file = game_fs.read(path);
      if (!file || file->size() < 16) continue;
      int hits = 0;
      refs(*file, args[1], label, "", hits);
      if (hits && label.size() > 8 && label.compare(label.size() - 8, 8, "/at0.prd") == 0) owners.insert(label.substr(0, label.size() - 8));
    }
    for (int scene_index = 0; scene_index < static_cast<int>(tables.scenes.size()); ++scene_index) {
      std::string line;
      const auto& sc = tables.scenes[static_cast<size_t>(scene_index)];
      for (int k = 0; k < 3; ++k) {
        if (!sc.enemy_count(k) || sc.enemy_id(k) == 0xFF) continue;
        const std::string dir = enemy_dir(sc.enemy_id(k));
        if (!dir.empty() && owners.count(dir)) line += " [" + std::to_string(sc.enemy_id(k)) + "]" + dir;
      }
      if (!line.empty()) std::printf("scene %d:%s\n", scene_index, line.c_str());
    }
    return 0;
  }
  // --grep: the four bytes NAME anywhere in any cdrom member (raw, LZSS inflated, or inside a nested entry).
  if (args[0] == "--grep" && args.size() > 1) {
    for (const std::string& member : game_fs.list("cdrom")) {
      const auto file = game_fs.read("cdrom/" + member);
      if (!file || file->size() < 4) continue;
      Bytes dec;
      ByteSpan body = *file;
      if (import::is_lzss(*file)) {
        if (auto inflated = import::lzss_decode(*file)) {
          dec = std::move(*inflated);
          body = dec;
        }
      }
      const std::string& nm = args[1];
      if (std::search(body.begin(), body.end(), nm.begin(), nm.end()) != body.end()) {
        std::printf("%s\n", member.c_str());
        continue;
      }
      int hits = 0;
      refs(body, nm, member, "", hits);
    }
    return 0;
  }
  if (args[0] == "--file" && args.size() > 1) {
    std::function<void(ByteSpan, int)> dump = [&](ByteSpan bytes, int depth) {
      const auto drp = import::parse_drp(bytes);
      if (!drp) return;
      for (const auto& entry : drp->entries) {
        std::printf("%*s%d %s %zu\n", depth * 2, "", static_cast<int>(entry.type), entry.name.c_str(), entry.data.size());
        if (entry.type == 1 && import::is_drp(entry.data)) dump(entry.data, depth + 1);
      }
    };
    const auto file = game_fs.read("cdrom/" + args[1]);
    if (!file) return 1;
    dump(*file, 0);
    return 0;
  }
  const int scene = std::atoi(args[0].c_str());
  int bg = 0;
  for (size_t i = 1; i + 1 < args.size(); ++i)
    if (args[i] == "--bg") bg = std::atoi(args[i + 1].c_str());
  if (scene < 0 || scene >= static_cast<int>(tables.scenes.size())) return 1;
  std::map<Key, std::pair<int, std::string>> reg;
  auto add = [&](const std::string& src, const std::string& path) {
    const auto file = game_fs.read(path);
    if (!file || file->size() < 16) return;
    std::map<Key, int> entries;
    walk(*file, entries);
    for (const auto& [key, count] : entries) {
      reg[key].first += count;
      reg[key].second += (reg[key].second.empty() ? "" : ",") + src;
    }
  };
  add("battle", "cdrom/battle/battle.prd");
  char bg_path[64];
  std::snprintf(bg_path, sizeof bg_path, "cdrom/battle/bg/bg_%02d.prd", bg);
  add("bg", bg_path);
  // No at0.prd / particle.prd is registered at battle start; each model's own S6 DRP (obj header +4+5*4) joins at its first load.
  std::set<std::string> models;
  auto add_s6 = [&](const std::string& src, const std::string& stem) {
    // A second unit of the same model is skipped.
    if (stem.empty() || !models.insert(stem).second) return;
    const auto obj = game_fs.read("cdrom/" + stem + ".obj");
    if (!obj || obj->size() < 32) return;
    u32 s6 = 0;
    std::memcpy(&s6, obj->data() + 4 + 5 * 4, 4);
    if (!s6 || s6 + 12 >= obj->size()) return;
    std::map<Key, int> entries;
    walk(ByteSpan(obj->data() + s6, obj->size() - s6), entries);
    for (const auto& [key, count] : entries) {
      reg[key].first += count;
      reg[key].second += (reg[key].second.empty() ? "" : ",") + src;
    }
  };
  for (const char* pc : {"battle/player/selju2/selju", "battle/player/selju/selju"})
    if (game_fs.exists(std::string("cdrom/") + pc + ".obj")) {
      add_s6(std::string("s6:u0=") + pc, pc);
      break;
    }
  const auto& sc = tables.scenes[static_cast<size_t>(scene)];
  int unit = 3;
  for (int k = 0; k < 3; ++k) {
    if (!sc.enemy_count(k) || sc.enemy_id(k) == 0xFF || sc.enemy_id(k) >= tables.enemies.size()) continue;
    const size_t model = tables.enemies[sc.enemy_id(k)].model();
    if (model >= kBattleModels.size()) continue;
    const std::string stem(kBattleModels[model]);
    for (int copy = 0; copy < sc.enemy_count(k); ++copy) add_s6("s6:u" + std::to_string(unit++) + "=" + stem, stem);
  }
  for (const auto& [key, entry] : reg) std::printf("%d\t%s\t%d\t%s\n", key.first, key.second.c_str(), entry.first, entry.second.c_str());
  return 0;
}
