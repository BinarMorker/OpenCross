#include "ui/menu/status_model.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "import/container.h"
#include "import/model.h"

namespace oc::ui {

namespace {
// Folder under menu/player/ of every character id 0..43 (same table as FieldWorld::character_model).
constexpr const char* kFolders[44] = {
    "selju",  "kid",     "guill",  "ishito",   "slash",   "jakotu", "riddle", "carshu", "zoah",    "malchera", "colcha",
    "lutianna", "poshul", "lazzuly", "zappa",    "ocha",    "radius", "farga",  "obachan", "gren1",  "lena",     "miki",
    "harley", "jyaness", "dragon", "hosi",      "spri",    "dan",    "kabuo",  "rablue", "jilbelt", "hone",     "kinoko",
    "ilenes", "mell",    "liia",   "banc",      "sunef",   "steena", "doc",    "gyada",  "pierre",  "olha",     "tumaru"};
// Weapon pack of battle model m (file table 3195 + 3 m: entry m * 3 + 4 of the model block).
constexpr const char* kWeaponPacks[44] = {
    "kai",  "knife", "rod",   "gun",   "black", "sword", "rod",  "axe",  "black", "black", "black", "nage", "black", "minirod", "axe",
    "cook", "tue",   "sword", "cook",  "sword", "cook",  "black", "nage", "ninjin", "black", "gun", "tue",  "black", "sword", "black",
    "black", "black", "axe",  "harp",  "boom",  "axe",   "boom", "nage", "sword", "nage",  "black", "sword", "black", "black"};
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

StatusModelView::Entry* StatusModelView::entry(int id, bool hd_art) {
  if (id < 0 || id >= 44) return nullptr;
  const int key = id * 2 + (hd_art ? 1 : 0);
  if (auto it = cache_.find(key); it != cache_.end()) return it->second.get();
  auto created = std::make_unique<Entry>();
  const std::string folder = kFolders[id];
  auto ome = vfs_.read("cdrom/menu/player/" + folder + ".ome");
  // The 4x HD texture is used only in HD art mode: original art keeps the disc texture.
  std::optional<Bytes> hd;
  if (hd_art) hd = vfs_.read("hd/menu/player/" + folder + ".tim");
  bool ok = false;
  if (ome) {
    if (auto cpt = import::parse_cpt(*ome); cpt && cpt->entries.size() >= 6) {
      std::string err;
      // The menu Mdl has a short header: the disc file has 5 sections (S1 at 0x1C), the HD art mode's variant 4
      // sections (S1 at 0x18, no S5; its "EOF" word equals S4, the anim pack that follows it, so the word cannot be trusted). Rebuild the
      // standard 6-section header (S1 at 0x20, missing sections 0, EOF = the real size).
      Bytes mdl(cpt->entries[3].data.begin(), cpt->entries[3].data.end());
      if (mdl.size() > 0x20 && (mdl[0] == 4 || mdl[0] == 5)) {
        const size_t nsec = mdl[0], old_hdr = 4 * (nsec + 2), shift = 0x20 - old_hdr;
        Bytes fixed(mdl.size() + shift, 0);
        auto rd = [&](size_t offset) {
          return static_cast<u32>(mdl[offset] | mdl[offset + 1] << 8 | mdl[offset + 2] << 16 | static_cast<u32>(mdl[offset + 3]) << 24);
        };
        auto wr = [&](size_t offset, u32 value) {
          for (int k = 0; k < 4; ++k) fixed[offset + static_cast<size_t>(k)] = static_cast<u8>(value >> (8 * k));
        };
        wr(0, 6);
        for (size_t k = 0; k < nsec; ++k) wr(4 + 4 * k, rd(4 + 4 * k) + static_cast<u32>(shift));
        wr(28, static_cast<u32>(fixed.size()));
        std::copy(mdl.begin() + static_cast<std::ptrdiff_t>(old_hdr), mdl.end(), fixed.begin() + 0x20);
        mdl = std::move(fixed);
      }
      // Entry 3 = Mdl, entry 5 = Atim cpt (main texture + overlay); entry 4 is the PS1 clut/tim blob. The importer wants the Mdl bytes only.
      auto asset = import::load_model(mdl, cpt->entries[5].data, ByteSpan{}, &err, hd ? ByteSpan(*hd) : ByteSpan{});
      if (asset) {
        float lo = 1e9f, hi = -1e9f;
        for (const assets::Mesh& mesh : asset->meshes)
          for (const assets::SkinVertex& vertex : mesh.pool) lo = std::min(lo, vertex.bind_pos[1]), hi = std::max(hi, vertex.bind_pos[1]);
        created->y_min = lo, created->y_max = hi;
        std::string used;
        for (size_t k = 0; k < asset->attach.points.size(); ++k)
          if (asset->attach.points[k].used) used += " " + std::to_string(k) + "@j" + std::to_string(asset->attach.points[k].joint);
        log::info("status model {}: {} joints, {} clips, y {:.0f}..{:.0f}, attach{}", folder, asset->skeleton.joints.size(), asset->clips.size(), lo, hi, used);
        // Original art must show the disc texture (no 4x HD image).
        log::info("status model {}: texture {} x {}, HD {}", folder, asset->textures.empty() ? 0u : asset->textures[0].width,
                  asset->textures.empty() ? 0u : asset->textures[0].height,
                  asset->has_hd() ? std::to_string(asset->hd_textures[0].width) + " x " + std::to_string(asset->hd_textures[0].height) : std::string("none"));
        created->body = game::Actor::make_model(meshes_, std::move(*asset));
        ok = created->body != nullptr && hi > lo;
      } else {
        log::warn("status model {}: {}", folder, err);
      }
    }
  }
  if (ok) {
    created->pose.set_model(&created->body->asset);
    // Attach 07 / 08 = hands, 0x12 = the weapon grip (Serge); use 0x12 when the model has it, else the right hand.
    const auto& pts = created->body->asset.attach.points;
    created->attach = pts[0x12].used ? 0x12 : pts[7].used ? 7 : 15;
  } else {
    created->body.reset();
  }
  Entry* result = created.get();
  cache_[key] = std::move(created);
  return result;
}

void StatusModelView::set_weapon(Entry& model, int char_id, int part) {
  if (model.weapon_part == part) return;
  model.weapon_part = part;
  model.weapon.reset();
  if (part < 0 || char_id < 0 || char_id >= 44) return;
  const std::string pack = kWeaponPacks[char_id];
  const std::string key = pack + "#" + std::to_string(part);
  if (auto it = weapons_.find(key); it != weapons_.end()) {
    model.weapon = it->second;
    return;
  }
  std::shared_ptr<game::ModelHandle> handle;
  if (auto raw = vfs_.read("cdrom/battle/wepon/" + pack + ".bin")) {
    std::string err;
    if (auto wp = import::load_weapon_pack(*raw, &err); wp && part < static_cast<int>(wp->parts.size())) {
      float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
      for (const assets::SkinVertex& vertex : wp->parts[static_cast<size_t>(part)].mesh.pool)
        for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], vertex.bind_pos[static_cast<size_t>(k)]), hi[k] = std::max(hi[k], vertex.bind_pos[static_cast<size_t>(k)]);
      log::info("status weapon {}: x {:.0f}..{:.0f} y {:.0f}..{:.0f} z {:.0f}..{:.0f} ({} parts)", key, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], wp->parts.size());
      assets::ModelAsset weapon_asset;
      weapon_asset.kind = "weapon";
      assets::Joint joint;
      weapon_asset.skeleton.joints.push_back(joint);
      weapon_asset.textures.push_back(wp->parts[static_cast<size_t>(part)].texture);
      weapon_asset.main_texture_count = 1;
      weapon_asset.meshes.push_back(wp->parts[static_cast<size_t>(part)].mesh);
      handle = game::Actor::make_model(meshes_, std::move(weapon_asset));
    } else {
      log::warn("status weapon {}: {}", key, err.empty() ? "no such part" : err);
    }
  }
  weapons_[key] = handle;
  model.weapon = handle;
}

void StatusModelView::release() {
  cache_.clear();
  weapons_.clear();
}

void StatusModelView::draw(const render::Frame& frame, float vw, float vh, const MenuModelSlot& slot) {
  Entry* shown = entry(slot.char_id, slot.hd);
  if (!shown || !shown->body) return;
  // Pose clip (menutest: pose 0 stand, 3 hurt) at 15 keyframes per second = 0.25 per 60 Hz tick.
  const int want = slot.low_hp ? 3 : slot.pose;
  if (want != shown->pose_id) {
    shown->pose.play(want, 0);
    shown->pose_id = want;
  }
  shown->accum += slot.dt * 60.0f;
  for (; shown->accum >= 1.0f; shown->accum -= 1.0f) shown->pose.tick();
  shown->pose.palette(0.0f, palette_);
  set_weapon(*shown, slot.char_id, slot.weapon_part);
  if (!shown->logged) {
    shown->logged = true;
    for (int k : {7, 8, 15, 16, 18, 28, 29, 30, 31}) {
      float x, y, z;
      if (shown->pose.attach_now(k, 0.0f, x, y, z)) log::info("attach {} world ({:.0f}, {:.0f}, {:.0f}) joint {}", k, x, y, z, shown->body->asset.attach.points[static_cast<size_t>(k)].joint);
    }
    const auto& joint_matrix = palette_[shown->body->asset.attach.points[static_cast<size_t>(shown->attach)].joint];
    log::info("joint matrix of attach {}: [{:.2f} {:.2f} {:.2f}] [{:.2f} {:.2f} {:.2f}] [{:.2f} {:.2f} {:.2f}]", shown->attach, joint_matrix.at(0, 0), joint_matrix.at(0, 1), joint_matrix.at(0, 2), joint_matrix.at(1, 0), joint_matrix.at(1, 1), joint_matrix.at(1, 2), joint_matrix.at(2, 0), joint_matrix.at(2, 1), joint_matrix.at(2, 2));
  }

  // Camera on the +Z axis looking at the origin, y down (model space). The projection centre is the figure's foot point,
  // the focal length is chosen so that (y_max - y_min) model units cover slot.height pixels at the camera distance.
  const float units = shown->y_max - shown->y_min;
  const float distance = units * 3.0f;
  // Pixels per unit at z = 1, = 3 * height.
  const float focal = slot.height * distance / units;
  render::Mat4 proj;
  proj.at(0, 0) = 2.0f * focal / vw;
  proj.at(0, 2) = 2.0f * slot.cx / vw - 1.0f;
  proj.at(1, 1) = 2.0f * focal / vh;
  proj.at(1, 2) = 2.0f * slot.feet_y / vh - 1.0f;
  proj.at(2, 2) = 0.5f;
  proj.at(3, 2) = 1.0f;
  proj.at(3, 3) = 0.0f;
  const render::Mat4 view = render::mat4_translation(0, 0, distance);
  const render::Mat4 model = render::mat4_rotation_y(slot.yaw_turns * 2.0f * kPi) * render::mat4_translation(0, -shown->y_max, 0);

  render::MeshPipeline::DrawParams dp;
  dp.view_proj = proj * view;
  dp.view = view;
  dp.model = model;
  dp.palette = palette_;
  dp.linear = slot.hd;
  dp.hd = slot.hd;
  // The menu is drawn over the field / world map in the same render pass, and those leave their depth values behind (room layers, actors):
  // the model would be depth-tested against them and show holes. It is the topmost 3D element of the menu: start from a clean depth buffer.
  frame.clear_depth();
  meshes_.begin(frame, frame.full());
  meshes_.draw(*shown->body->gpu, dp);
  if (shown->weapon && shown->weapon->gpu && shown->attach >= 0) {
    const assets::AttachPoint& ap = shown->body->asset.attach.points[static_cast<size_t>(shown->attach)];
    if (ap.joint < palette_.size()) {
      std::vector<render::Mat4> wp(1, palette_[ap.joint] * render::mat4_translation(ap.pos[0], ap.pos[1], ap.pos[2]));
      render::MeshPipeline::DrawParams wd = dp;
      wd.palette = wp;
      meshes_.draw(*shown->weapon->gpu, wd);
    }
  }
  meshes_.end();
}

}  // namespace oc::ui
