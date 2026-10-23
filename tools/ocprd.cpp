// ocprd: room .prd effects tool.
//   ocprd [-g <game dir>] info <room|path>          sheets (with every frame slice), emitters (with decoded ops) and sequences
//   ocprd [-g <game dir>] validate                  every room .prd: totals, stream failures, timinfo / TIM mismatches
//   ocprd [-g <game dir>] render <room> <outdir>    one PNG per sprite slice plus the composed frame of every sheet
//   ocprd [-g <game dir>] soak [ticks]              every room emitter + every battle-effect emitter through the effect player
//   ocprd [-g <game dir>] preview <room> <outdir>   emitters of the room at ticks 0/30/60/120 over the layer-0 backdrop (2x PNGs;
//                                                   OCPRD_VERBOSE=1 prints the first objects and quads of every shot)
//   ocprd fx-soak [ticks]                           every registered bespoke overlay against a dummy stage (no game data)
//   ocprd fx-preview <overlay> <outdir>             software-rendered PNGs of one bespoke overlay at ticks 6/30/60/100
// The game dir defaults to $OPENCROSS_GAMEDIR.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "game/effects/bespoke/bespoke.h"
#include "game/effects/bespoke/bespoke_util.h"
#include "game/effects/effect_slots.h"
#include "game/effects/emitter_system.h"
#include "game/effects/sequence_player.h"
#include "game/effects/soft_raster.h"
#include "import/room.h"
#include "import/room_effects.h"
#include "tool_util.h"
#include "vfs/vfs.h"

using namespace oc;

namespace {

const char* kParticleLib = "cdrom/effect/particle.prd";

int usage() {
  std::fprintf(stderr,
               "usage: ocprd [-g GameDir] (info <room> | validate | render <room> <outdir> | soak [ticks] | preview <room> <outdir>)\n");
  return 2;
}

struct Loaded {
  Bytes prd, lib;
  assets::RoomEffects fx;
};

bool load(const vfs::Vfs& game_fs, const std::string& room, Loaded& out) {
  // A name with a '/' is a VFS path of its own (cdrom/title.prd).
  auto prd = game_fs.read(room.find('/') != std::string::npos ? room : tool::room_file(room, ".prd"));
  if (!prd) {
    std::fprintf(stderr, "no .prd for room %s\n", room.c_str());
    return false;
  }
  out.prd = std::move(*prd);
  if (auto lib = game_fs.read(kParticleLib)) out.lib = std::move(*lib);
  import::Vram empty;
  out.fx = import::load_room_effects(out.prd, empty, out.lib);
  return true;
}

void print_arg(const assets::EffectOp& op) {
  struct Printer {
    void operator()(const assets::FxNone&) const {}
    void operator()(const assets::FxValue& arg) const { std::printf(" value=%d", arg.value); }
    void operator()(const assets::FxXyz& arg) const { std::printf(" xyz=(%d,%d,%d)", arg.x, arg.y, arg.z); }
    void operator()(const assets::FxOsc& arg) const {
      for (size_t i = 0; i < arg.count; ++i)
        std::printf(" osc%zu=(A%d,F%d,P%d)", i, arg.axis[i].amp, arg.axis[i].freq, arg.axis[i].phase);
    }
    void operator()(const assets::FxFlags& arg) const { std::printf(" flags=0x%08X blend=%d", arg.flags, arg.blend()); }
    void operator()(const assets::FxSprite& arg) const {
      std::printf(" frame0=%u name=%s type=%d cache=%u", arg.frame0, arg.name.c_str(), arg.type, arg.cache);
    }
    void operator()(const assets::FxNamed& arg) const { std::printf(" name=%s", arg.name.c_str()); }
    void operator()(const assets::FxBlob& arg) const {
      std::printf(" span=%d base=%d v=(%d,%d,%d) base_grow=%d span_grow=%d", arg.span, arg.base, arg.sx, arg.sy, arg.sz,
                  arg.base_grow, arg.span_grow);
    }
    void operator()(const assets::FxExt35& arg) const { std::printf(" ext=0x%08X", arg.ext); }
    void operator()(const assets::FxUvScroll& arg) const {
      std::printf(" off=(%u,%u) win=%dx%d rate=(%d,%d)", arg.off_u, arg.off_v, arg.win_w, arg.win_h, arg.rate_u, arg.rate_v);
    }
    void operator()(const assets::FxMorph& arg) const { std::printf(" mode=%u rate=%d target=%s", arg.mode, arg.rate, arg.name.c_str()); }
    void operator()(const assets::FxBasis& arg) const { std::printf(" sel=(%u,%u,%u,%u) f=0x%X", arg.a, arg.b, arg.c, arg.d, arg.flags); }
    void operator()(const assets::FxDrag& arg) const { std::printf(" drag=%u+rand%%%u", arg.base, arg.rand); }
    void operator()(const assets::FxRandDir& arg) const {
      std::printf(" base=(%u,%u,%u) spread=(%d,%d,%d)", arg.base[0], arg.base[1], arg.base[2], arg.spread[0], arg.spread[1], arg.spread[2]);
    }
    void operator()(const assets::FxPath& arg) const { std::printf(" mode=%d curve=%s", arg.mode, arg.name.c_str()); }
    void operator()(const assets::FxModelBody& arg) const {
      std::printf(" show=%u f200=%u f100=%u clone=%u rate=%u n=%d", arg.show, arg.f200, arg.f100, arg.clone, arg.rate, arg.n);
    }
    void operator()(const assets::FxRaw& arg) const {
      std::printf(" raw=");
      for (u8 byte : arg.data) std::printf("%02X", byte);
    }
  };
  std::visit(Printer{}, op.arg);
}

int cmd_info(const vfs::Vfs& game_fs, const std::string& room) {
  Loaded loaded;
  if (!load(game_fs, room, loaded)) return 1;
  const auto& fx = loaded.fx;
  std::printf("%s.prd: %zu sheets, %zu emitters, %zu sequences, %u TIMs, %u meshes\n", room.c_str(), fx.sheets.size(),
              fx.emitters.size(), fx.sequences.size(), fx.tims, fx.meshes);
  for (const auto& sheet : fx.sheets) {
    std::printf("[sheet %s] tex=(%d,%d) clut=(%d,%d) frames=%zu reloc_pending=%d paired=%d match=%d\n", sheet.name.c_str(),
                sheet.tex_x, sheet.tex_y, sheet.clut_x, sheet.clut_y, sheet.frames.size(), sheet.reloc_pending, sheet.has_tim, sheet.tim_match);
    for (size_t k = 0; k < sheet.frames.size(); ++k)
      for (size_t j = 0; j < sheet.frames[k].size(); ++j) {
        const auto& sprite = sheet.frames[k][j];
        std::printf("   f%zu s%zu: pos=(%d,%d) uv=(%u,%u) size=%ux%u clut=(%d,%d) page=(%d,%d) depth=%dbpp abr=%d orient=%d flags=0x%04X\n",
                    k + 1, j, sprite.x, sprite.y, sprite.u, sprite.v, sprite.w, sprite.h, sprite.clut_x(), sprite.clut_y(), sprite.page_x(),
                    sprite.page_y(), sprite.depth() == 0 ? 4 : 8, sprite.abr(), sprite.orient(), sprite.flags);
      }
  }
  for (const auto& emitter : fx.emitters) {
    std::printf("[emitter %s] start=%d duration=%d period=%u repeat=%d flags=0x%X (initial %s; jitter=%u)\n",
                emitter.name.c_str(), emitter.start, emitter.duration, emitter.period, emitter.repeat, emitter.flags,
                emitter.start_stopped() ? "stopped" : "running", emitter.jitter());
    for (const auto& op : emitter.ops) {
      std::printf("   %04X %-20s len=%u", op.offset, import::effect_op_name(op.op), op.len);
      print_arg(op);
      std::printf("\n");
    }
  }
  for (const auto& sequence : fx.sequences) {
    std::printf("[sequence %s] flags=0x%X\n", sequence.name.c_str(), sequence.flags);
    for (const auto& cmd : sequence.cmds)
      std::printf("   %04X %-14s wait_after=%u %s\n", cmd.offset, import::sequence_op_name(cmd.op), cmd.wait, cmd.name.c_str());
  }
  for (const auto& error : fx.errors) std::printf("ERROR %s\n", error.c_str());
  return fx.errors.empty() ? 0 : 1;
}

int cmd_validate(const vfs::Vfs& game_fs) {
  const auto rooms = tool::room_stems(game_fs, ".prd");
  import::EffectsValidation totals;
  u32 files = 0;
  std::vector<std::string> failures, mismatches;
  for (const auto& room : rooms) {
    Loaded loaded;
    if (!load(game_fs, room, loaded)) {
      failures.push_back(room + ": unreadable");
      continue;
    }
    ++files;
    const auto validation = import::validate_room_effects(loaded.fx);
    totals.emitters += validation.emitters;
    totals.sheets += validation.sheets;
    totals.sequences += validation.sequences;
    totals.frames += validation.frames;
    totals.sprites += validation.sprites;
    totals.sprites_inside += validation.sprites_inside;
    totals.pair_ok += validation.pair_ok;
    totals.pair_mismatch += validation.pair_mismatch;
    totals.unpaired += validation.unpaired;
    for (const auto& error : validation.errors) failures.push_back(room + ": " + error);
    for (const auto& warning : validation.warnings) mismatches.push_back(room + ": " + warning);
  }
  std::printf("room .prd files: %u\n  emitters %u\n  sheets (timinfo) %u\n  sequences %u\n  timinfo frames %u\n", files,
              totals.emitters, totals.sheets, totals.sequences, totals.frames);
  std::printf("  timinfo_unpaired %u\n  timinfo_pair_ok %u\n  timinfo_pair_mismatch %u\n  sprite_total %u\n  sprite_inside_tim %u\n",
              totals.unpaired, totals.pair_ok, totals.pair_mismatch, totals.sprites, totals.sprites_inside);
  std::printf("stream failures: %zu\n", failures.size());
  for (const auto& failure : failures) std::printf("   %s\n", failure.c_str());
  std::printf("timinfo/TIM mismatches: %zu\n", mismatches.size());
  for (const auto& mismatch : mismatches) std::printf("   %s\n", mismatch.c_str());
  return failures.empty() ? 0 : 1;
}

int cmd_render(const vfs::Vfs& game_fs, const std::string& room, const std::string& outdir) {
  Loaded loaded;
  if (!load(game_fs, room, loaded)) return 1;
  std::error_code ec;
  std::filesystem::create_directories(outdir, ec);
  size_t slices = 0;
  for (const auto& sheet : loaded.fx.sheets) {
    for (size_t k = 0; k < sheet.frames.size(); ++k) {
      const auto& frame = sheet.frames[k];
      if (frame.empty()) continue;
      int x0 = frame[0].x, y0 = frame[0].y, x1 = frame[0].x + frame[0].w, y1 = frame[0].y + frame[0].h;
      for (const auto& slice : frame) {
        x0 = std::min<int>(x0, slice.x);
        y0 = std::min<int>(y0, slice.y);
        x1 = std::max<int>(x1, slice.x + slice.w);
        y1 = std::max<int>(y1, slice.y + slice.h);
      }
      const int canvas_w = x1 - x0, canvas_h = y1 - y0;
      Bytes canvas(static_cast<size_t>(canvas_w) * static_cast<size_t>(canvas_h) * 4, 0);
      const std::string base = outdir + "/" + room + "_" + sheet.name + "_f" + std::to_string(k + 1);
      for (size_t j = 0; j < frame.size(); ++j) {
        const auto& slice = frame[j];
        tool::write_png(base + "_s" + std::to_string(j) + ".png", slice.w, slice.h, slice.rgba);
        ++slices;
        for (int yy = 0; yy < slice.h; ++yy)
          for (int xx = 0; xx < slice.w; ++xx) {
            const int sx = slice.mirror_x() ? slice.w - 1 - xx : xx;
            const int sy = slice.mirror_y() ? slice.h - 1 - yy : yy;
            const u8* src = &slice.rgba[(static_cast<size_t>(sy) * slice.w + static_cast<size_t>(sx)) * 4];
            if (!src[3]) continue;
            u8* dst = &canvas[(static_cast<size_t>(slice.y - y0 + yy) * static_cast<size_t>(canvas_w) + static_cast<size_t>(slice.x - x0 + xx)) * 4];
            for (int channel = 0; channel < 4; ++channel) dst[channel] = src[channel];
          }
      }
      tool::write_png(base + ".png", static_cast<u32>(canvas_w), static_cast<u32>(canvas_h), canvas);
    }
  }
  std::printf("wrote %zu slice PNGs (+ composed frames) to %s\n", slices, outdir.c_str());
  return 0;
}

// ---- effect player soak / preview ------------------------------------------------------------------------------------------

struct SoakTotals {
  u32 packs = 0, emitters = 0, sequences = 0, failures = 0;
  u64 spawned = 0, quads = 0, seq_cmds = 0, seq_other = 0, dropped = 0, unresolved = 0, meshes = 0;
  u32 max_live = 0;
};

effects::ActorHooks soak_hooks() {
  effects::ActorHooks hooks;
  hooks.actor_basis = [](int actor, int, int, int, effects::Basis& out, int& scale) {
    out = effects::Basis{};
    out.t = {static_cast<float>(actor) * 8.0f, 0.0f, 0.0f};
    scale = 4096;
    return true;
  };
  hooks.anchor_basis = [](int, effects::Basis& out) {
    out = effects::Basis{};
    return true;
  };
  return hooks;
}

// Instantiates every emitter and sequence of one pack and runs `ticks` ticks.
bool soak_pack(const std::string& label, const assets::RoomEffects& pack, const assets::RoomEffects* lib, int ticks, SoakTotals& tot) {
  try {
    effects::EffectBank bank;
    bank.add(&pack);
    bank.add(lib);
    effects::EmitterSystem sys(bank);
    sys.set_hooks(soak_hooks());
    sys.register_all();
    effects::SequencePlayer seq(bank, sys);
    // Also start the start-stopped emitters (as the room script / a sequence would).
    sys.start("0000");
    for (const auto& sq : pack.sequences) seq.start(sq.name);
    effects::ViewParams view;
    view.trans = {0, 0, 4000};
    std::vector<effects::DrawQuad> quads;
    for (int tick = 0; tick < ticks; ++tick) {
      // Halfway through, exercise the stop / kill / restart entry points.
      if (tick == ticks / 2) {
        sys.stop("0000");
        sys.kill("0000");
        sys.start("0000");
        seq.stop("0000");
        for (const auto& sq : pack.sequences) seq.start(sq.name);
      }
      seq.tick();
      sys.tick();
      if (tick % 20 == 0) {
        quads.clear();
        sys.collect_quads(view, 0.5f, quads);
        effects::sort_draw_order(quads);
        tot.quads += quads.size();
        tot.max_live = std::max<u32>(tot.max_live, static_cast<u32>(sys.live_objects()));
        for (const auto& quad : quads)
          if (quad.slot < 0 || quad.slot > 0xFFF) throw std::runtime_error("slot out of range");
      }
    }
    const auto st = sys.stats();
    tot.spawned += st.spawned;
    tot.dropped += st.dropped;
    tot.unresolved += st.unresolved_sprites;
    tot.meshes += st.mesh_objects;
    tot.emitters += static_cast<u32>(sys.header_count());
    tot.sequences += static_cast<u32>(pack.sequences.size());
    tot.seq_cmds += seq.stats().commands;
    tot.seq_other += seq.stats().other_ops;
    ++tot.packs;
    return true;
  } catch (const std::exception& ex) {
    std::printf("SOAK FAIL %s: %s\n", label.c_str(), ex.what());
    ++tot.failures;
    return false;
  }
}

int cmd_soak(const vfs::Vfs& fs, int ticks) {
  SoakTotals rooms, battle;
  Bytes libbytes;
  if (auto lib = fs.read(kParticleLib)) libbytes = std::move(*lib);
  import::Vram empty;
  const assets::RoomEffects libfx = libbytes.empty() ? assets::RoomEffects{} : import::load_room_effects(libbytes, empty);
  for (const auto& room : tool::room_stems(fs, ".prd")) {
    Loaded loaded;
    if (!load(fs, room, loaded)) {
      ++rooms.failures;
      continue;
    }
    for (const auto& error : loaded.fx.errors) std::printf("PARSE ERROR room %s: %s\n", room.c_str(), error.c_str());
    soak_pack("room " + room, loaded.fx, &libfx, ticks, rooms);
  }
  const auto fnd1 = fs.read("cdrom/cdrom.fnd"), fnd2 = fs.read("cdrom/cdrom2.fnd");
  std::vector<effects::EffectSlot> slots;
  if (fnd1) {
    const Bytes b2 = fnd2 ? *fnd2 : Bytes{};
    slots = effects::battle_effect_slots(*fnd1, b2, [&](const std::string& path) { return fs.exists("cdrom/" + path); });
  }
  u32 slot_files = 0;
  for (const auto& slot : slots) {
    if (slot.prd.size() < 4 || slot.prd.compare(slot.prd.size() - 4, 4, ".prd") != 0) continue;
    auto prd = fs.read("cdrom/" + slot.prd);
    if (!prd || prd->size() < 0x40) continue;
    Bytes ctd;
    if (!slot.ctd.empty())
      if (auto ctd_bytes = fs.read("cdrom/" + slot.ctd)) ctd = std::move(*ctd_bytes);
    const assets::RoomEffects fx = import::load_battle_effects(*prd, ctd, empty, libbytes);
    for (const auto& error : fx.errors) std::printf("PARSE ERROR eid %d %s: %s\n", slot.eid, slot.prd.c_str(), error.c_str());
    ++slot_files;
    soak_pack("eid " + std::to_string(slot.eid) + " " + slot.prd, fx, &libfx, ticks, battle);
  }
  auto print = [&](const char* what, const SoakTotals& totals) {
    std::printf("%s: %u packs, %u emitters, %u sequences | %llu objects spawned, %llu quads sampled, max live %u, "
                "dropped at cap %llu, unresolved sprites %llu, mesh objects %llu | sequence cmds %llu (%llu forwarded) | failures %u\n",
                what, totals.packs, totals.emitters, totals.sequences, static_cast<unsigned long long>(totals.spawned),
                static_cast<unsigned long long>(totals.quads), totals.max_live, static_cast<unsigned long long>(totals.dropped),
                static_cast<unsigned long long>(totals.unresolved), static_cast<unsigned long long>(totals.meshes),
                static_cast<unsigned long long>(totals.seq_cmds), static_cast<unsigned long long>(totals.seq_other), totals.failures);
  };
  std::printf("soak: %d ticks per pack; battle slots with an eins prd: %u (slot = fnd[4322 + 3*eid + 1])\n", ticks, slot_files);
  print("rooms ", rooms);
  print("battle", battle);
  return rooms.failures + battle.failures == 0 ? 0 : 1;
}

int cmd_preview(const vfs::Vfs& fs, const std::string& room, const std::string& outdir) {
  Loaded loaded;
  if (!load(fs, room, loaded)) return 1;
  const std::string base = "cdrom/map/mapbin/" + room;
  auto bin = fs.read(base + ".bin"), ctd = fs.read(base + ".ctd");
  if (!bin || !ctd) {
    std::fprintf(stderr, "no .bin/.ctd for room %s\n", room.c_str());
    return 1;
  }
  std::string err;
  auto scene = import::load_room(room, *bin, *ctd, {}, &err);
  if (!scene) {
    std::fprintf(stderr, "room load failed: %s\n", err.c_str());
    return 1;
  }
  import::Vram empty;
  const assets::RoomEffects libfx = loaded.lib.empty() ? assets::RoomEffects{} : import::load_room_effects(loaded.lib, empty);
  effects::EffectBank bank;
  bank.add(&loaded.fx);
  bank.add(&libfx);
  effects::ViewParams view = effects::ViewParams::from_room(scene->camera);
  std::error_code ec;
  std::filesystem::create_directories(outdir, ec);

  auto make_system = [&](effects::EmitterSystem& sys) {
    sys.register_all();
    // The room script would activate the start-stopped emitters.
    sys.start("0000");
  };
  // Centre the view on the effects: run a dry system to the last frame and take the centroid of its quads.
  {
    effects::EmitterSystem dry(bank);
    make_system(dry);
    for (int tick = 0; tick < 121; ++tick) dry.tick();
    std::vector<effects::DrawQuad> qs;
    dry.collect_quads(view, 1.0f, qs);
    double cx = 0, cy = 0;
    for (const auto& quad : qs) {
      cx += (quad.x[0] + quad.x[3]) * 0.5;
      cy += (quad.y[0] + quad.y[3]) * 0.5;
    }
    if (!qs.empty()) {
      view.scroll_x = static_cast<float>(std::floor(160.0 - cx / static_cast<double>(qs.size())));
      view.scroll_y = static_cast<float>(std::floor(120.0 - cy / static_cast<double>(qs.size())));
    }
    std::printf("%s: %zu emitters, %zu sheets; %zu quads at tick 120, scroll (%.0f, %.0f)\n", room.c_str(), loaded.fx.emitters.size(),
                loaded.fx.sheets.size(), qs.size(), static_cast<double>(view.scroll_x), static_cast<double>(view.scroll_y));
  }
  effects::EmitterSystem sys(bank);
  make_system(sys);
  const int shots[4] = {0, 30, 60, 120};
  int next = 0;
  for (int tick = 0; tick <= 120 && next < 4; ++tick) {
    // Frame `tick` is the state after tick index `tick` (tick + 1 steps).
    sys.tick();
    if (tick != shots[next]) continue;
    effects::SoftCanvas cv(320, 240);
    if (!scene->layers.empty())
      for (const auto& group : scene->layers[0].groups) {
        const float scale = group.group < scene->camera.group_scale.size() && scene->camera.group_scale[group.group] != 0
                                ? static_cast<float>(scene->camera.group_scale[group.group])
                                : 4096.0f;
        const float inv_scale = 4096.0f / scale;
        cv.blit(group.rgba.data(), static_cast<int>(group.width), static_cast<int>(group.height),
                static_cast<int>(std::floor(static_cast<float>(group.x) + view.scroll_x * inv_scale)),
                static_cast<int>(std::floor(static_cast<float>(group.y) + view.scroll_y * inv_scale)));
      }
    std::vector<effects::DrawQuad> qs;
    sys.collect_quads(view, 1.0f, qs);
    effects::sort_draw_order(qs);
    int px = 0;
    for (const auto& quad : qs) px += cv.draw(quad);
    if (std::getenv("OCPRD_VERBOSE")) {
      for (size_t i = 0; i < sys.live_objects() && i < 4; ++i) {
        effects::EmitterSystem::ObjectView ov;
        sys.object_view(i, ov);
        std::printf("   obj %zu (emitter %d): pos (%.1f,%.1f,%.1f) scale (%d,%d,%d) angle (%d,%d,%d) life %d frame %d flags 0x%X\n", i,
                    ov.owner, static_cast<double>(ov.pos[0]), static_cast<double>(ov.pos[1]), static_cast<double>(ov.pos[2]),
                    ov.scale[0], ov.scale[1], ov.scale[2], ov.angle[0], ov.angle[1], ov.angle[2], ov.life, ov.frame, ov.flags);
      }
      for (size_t i = 0; i < qs.size() && i < 8; ++i)
        std::printf("   quad %zu: x %.1f..%.1f y %.1f..%.1f rgb %02X%02X%02X blend %d slot %d\n", i, static_cast<double>(qs[i].x[0]),
                    static_cast<double>(qs[i].x[3]), static_cast<double>(qs[i].y[0]), static_cast<double>(qs[i].y[3]), qs[i].r, qs[i].g,
                    qs[i].b, qs[i].blend, qs[i].slot);
    }
    // 2x nearest for readability
    Bytes big(640u * 480u * 4u);
    for (int y = 0; y < 480; ++y)
      for (int x = 0; x < 640; ++x)
        std::memcpy(&big[(static_cast<size_t>(y) * 640 + static_cast<size_t>(x)) * 4],
                    &cv.rgba()[(static_cast<size_t>(y / 2) * 320 + static_cast<size_t>(x / 2)) * 4], 4);
    char name[96];
    std::snprintf(name, sizeof name, "/%s_t%03d.png", room.c_str(), tick);
    const std::string path = outdir + name;
    if (!tool::write_png(path, 640, 480, big)) {
      std::fprintf(stderr, "cannot write %s\n", path.c_str());
      return 1;
    }
    std::printf("tick %3d: %zu live objects, %zu quads, %d px -> %s\n", tick, sys.live_objects(), qs.size(), px, path.c_str());
    ++next;
  }
  return 0;
}

// ---- bespoke effect overlays (src/game/effects/bespoke): fx-soak / fx-preview -------------------------------------------------------

bool prims_sane(const std::vector<effects::FxPrim>& ps, std::string& why) {
  for (const auto& prim : ps) {
    const int nv = prim.kind == effects::FxPrim::Kind::Tri ? 3 : prim.kind == effects::FxPrim::Kind::Line ? 2 : 4;
    for (int k = 0; k < nv; ++k)
      if (!std::isfinite(prim.v[k].x) || !std::isfinite(prim.v[k].y)) {
        why = "non-finite vertex";
        return false;
      }
    if (prim.slot < 0 || prim.slot > 0xFFF) {
      why = "slot out of range";
      return false;
    }
    if (prim.blend < 0 || prim.blend > effects::kBlendInvert) {
      why = "bad blend";
      return false;
    }
  }
  return true;
}

// Runs one overlay for `ticks` ticks; variant 0 = full dummy stage, 1 = no meshes (actors without models), 2 = garbage private ops.
bool fx_run(const effects::BespokeInfo& info, int ticks, int variant, u64& prims, bool& finished, std::string& why) {
  try {
    effects::DummyStage st;
    if (variant == 1) {
      st.ctx.caster.mesh = nullptr;
      st.ctx.target.mesh = nullptr;
    }
    st.ctx.seed = 1 + static_cast<u32>(variant) * 7919u;
    // exercise every host hook (visibility / pose / tint / spawn / list filters / backdrop swap) so the modules' hook paths run
    st.ctx.hooks.set_visible = [](int, bool) {};
    st.ctx.hooks.actor_tint = [](int, int, int, int) {};
    st.ctx.hooks.actor_pose = [](int, const effects::Vec3&, float) {};
    st.ctx.hooks.spawn = [](std::string_view) {};
    st.ctx.hooks.set_listed = [](int, bool) {};
    st.ctx.hooks.set_stage = [](int) {};
    auto fx = effects::BespokeRegistry::instance().create(info.name, st.ctx);
    if (!fx) {
      why = "create failed";
      return false;
    }
    std::vector<effects::FxPrim> ps;
    finished = false;
    for (int tick = 0; tick < ticks; ++tick) {
      if (variant == 2 && tick % 37 == 5) {
        const int junk[8] = {0, -1, 1 << 20, 3, 0, 0, 0, 0};
        for (int op : {0x100, 0x101, 0x102, 0x180, 0x181, 0x182, 0x183, 0x1FF, 0x7FFF}) fx->command(op, junk, tick % 9 > 8 ? 8 : tick % 9);
      }
      fx->tick();
      if (tick % 10 == 0 || tick < 4) {
        ps.clear();
        fx->draw(ps);
        effects::sort_prims(ps);
        if (!prims_sane(ps, why)) return false;
        prims += ps.size();
      }
      if (fx->done()) finished = true;
    }
    return true;
  } catch (const std::exception& ex) {
    why = ex.what();
    return false;
  }
}

int cmd_fx_soak(int ticks) {
  const auto all = effects::BespokeRegistry::instance().all();
  u32 fail = 0, finished = 0;
  u64 total = 0;
  for (const auto* info : all) {
    u64 prims = 0;
    bool fin = false, ok = true;
    std::string why;
    for (int variant = 0; variant < 3 && ok; ++variant) {
      bool variant_finished = false;
      ok = fx_run(*info, ticks, variant, prims, variant_finished, why);
      if (variant == 0) fin = variant_finished;
    }
    std::printf("%-9s %-14s %-17s %s prims %llu done=%d\n", info->name.c_str(), info->family.c_str(), info->module.c_str(), ok ? "ok  " : "FAIL",
                static_cast<unsigned long long>(prims), fin ? 1 : 0);
    if (!ok) {
      std::printf("   -> %s\n", why.c_str());
      ++fail;
    }
    finished += fin ? 1 : 0;
    total += prims;
  }
  std::printf("fx-soak: %zu overlays, %u failures, %u finished within %d ticks, %llu prims\n", all.size(), fail, finished, ticks,
              static_cast<unsigned long long>(total));
  return fail ? 1 : 0;
}

int cmd_fx_preview(const std::string& overlay, const std::string& outdir) {
  const auto* info = effects::BespokeRegistry::instance().find(overlay);
  if (!info) {
    std::fprintf(stderr, "unknown overlay %s (try fx-soak for the list)\n", overlay.c_str());
    return 1;
  }
  std::error_code ec;
  std::filesystem::create_directories(outdir, ec);
  effects::DummyStage st;
  bool target_visible = true, caster_visible = true;
  st.ctx.hooks.set_visible = [&](int side, bool visible) {
    (side == 1 ? target_visible : caster_visible) = visible;
  };
  auto fx = effects::BespokeRegistry::instance().create(info->name, st.ctx);
  const int shots[4] = {6, 30, 60, 100};
  int next = 0;
  for (int tick = 0; tick <= shots[3] && next < 4; ++tick) {
    fx->tick();
    if (tick != shots[next]) continue;
    effects::SoftCanvas cv(320, 240);
    // Dusk gradient backdrop.
    for (int y = 0; y < 240; ++y)
      for (int x = 0; x < 320; ++x) {
        u8* pixel = &cv.rgba()[(static_cast<size_t>(y) * 320 + static_cast<size_t>(x)) * 4];
        pixel[0] = static_cast<u8>(30 + y / 4);
        pixel[1] = static_cast<u8>(40 + y / 3 + (x / 16) % 2 * 6);
        pixel[2] = static_cast<u8>(90 + y / 3);
      }
    std::vector<effects::FxPrim> ps;
    // actors first (flat figures), then the effect on top, sorted together
    auto add_actor = [&](const effects::FxActor& actor) {
      if (!actor.mesh) return;
      std::vector<effects::Vec3> world(actor.mesh->verts.size());
      for (size_t i = 0; i < world.size(); ++i) world[i] = {actor.pos[0] + actor.mesh->verts[i][0], actor.pos[1] + actor.mesh->verts[i][1], actor.pos[2] + actor.mesh->verts[i][2]};
      effects::util::emit_mesh(st.ctx.view, *actor.mesh, world, actor.mesh->colours, 0, ps, 0);
    };
    if (caster_visible) add_actor(st.ctx.caster);
    if (target_visible) add_actor(st.ctx.target);
    const size_t nactors = ps.size();
    fx->draw(ps);
    for (size_t i = nactors; i < ps.size(); ++i) ps[i].order += 100000;
    effects::sort_prims(ps);
    effects::raster_prims(cv, ps);
    Bytes big(640u * 480u * 4u);
    for (int y = 0; y < 480; ++y)
      for (int x = 0; x < 640; ++x)
        std::memcpy(&big[(static_cast<size_t>(y) * 640 + static_cast<size_t>(x)) * 4], &cv.rgba()[(static_cast<size_t>(y / 2) * 320 + static_cast<size_t>(x / 2)) * 4], 4);
    char name[96];
    std::snprintf(name, sizeof name, "/%s_t%03d.png", info->name.c_str(), tick);
    if (!tool::write_png(outdir + name, 640, 480, big)) {
      std::fprintf(stderr, "cannot write %s%s\n", outdir.c_str(), name);
      return 1;
    }
    std::printf("tick %3d: %zu prims (%zu effect) done=%d -> %s%s\n", tick, ps.size(), ps.size() - nactors, fx->done() ? 1 : 0, outdir.c_str(), name);
    ++next;
  }
  return 0;
}

}  // namespace

int ocprd_main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  // Bespoke overlays need no game data.
  if (!args.empty() && args[0] == "fx-soak" && args.size() <= 2) return cmd_fx_soak(args.size() == 2 ? std::atoi(args[1].c_str()) : 600);
  if (!args.empty() && args[0] == "fx-preview" && args.size() == 3) return cmd_fx_preview(args[1], args[2]);
  std::string game_dir = std::getenv("OPENCROSS_GAMEDIR") ? std::getenv("OPENCROSS_GAMEDIR") : "";
  if (args.size() >= 2 && args[0] == "-g") {
    game_dir = args[1];
    args.erase(args.begin(), args.begin() + 2);
  }
  if (args.empty() || game_dir.empty()) return usage();
  vfs::Vfs fs;
  if (!fs.open(game_dir)) {
    std::fprintf(stderr, "cannot open game dir %s\n", game_dir.c_str());
    return 1;
  }
  if (args[0] == "info" && args.size() == 2) return cmd_info(fs, args[1]);
  if (args[0] == "validate" && args.size() == 1) return cmd_validate(fs);
  if (args[0] == "render" && args.size() == 3) return cmd_render(fs, args[1], args[2]);
  if (args[0] == "soak" && args.size() <= 2) return cmd_soak(fs, args.size() == 2 ? std::atoi(args[1].c_str()) : 600);
  if (args[0] == "preview" && args.size() == 3) return cmd_preview(fs, args[1], args[2]);
  return usage();
}
