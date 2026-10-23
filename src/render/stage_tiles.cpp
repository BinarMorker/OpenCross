#include "render/stage_tiles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>

#include "render/texture_registry.h"

namespace oc::render {

namespace {

// Inclusive texel bounds.
struct IRect {
  int x0, y0, x1, y1;
};
bool overlaps(const IRect& lhs, const IRect& rhs) { return lhs.x0 <= rhs.x1 && rhs.x0 <= lhs.x1 && lhs.y0 <= rhs.y1 && rhs.y0 <= lhs.y1; }

// Merges rects that overlap (repeat until stable; adjacent rects stay separate tiles); the result is pairwise disjoint, in a deterministic order.
std::vector<IRect> merge_rects(std::vector<IRect> rects) {
  std::sort(rects.begin(), rects.end(), [](const IRect& lhs, const IRect& rhs) { return std::tie(lhs.y0, lhs.x0, lhs.y1, lhs.x1) < std::tie(rhs.y0, rhs.x0, rhs.y1, rhs.x1); });
  rects.erase(std::unique(rects.begin(), rects.end(), [](const IRect& lhs, const IRect& rhs) { return lhs.x0 == rhs.x0 && lhs.y0 == rhs.y0 && lhs.x1 == rhs.x1 && lhs.y1 == rhs.y1; }), rects.end());
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i = 0; i < rects.size(); ++i)
      for (size_t j = i + 1; j < rects.size();) {
        if (overlaps(rects[i], rects[j])) {
          rects[i] = {std::min(rects[i].x0, rects[j].x0), std::min(rects[i].y0, rects[j].y0), std::max(rects[i].x1, rects[j].x1), std::max(rects[i].y1, rects[j].y1)};
          rects.erase(rects.begin() + static_cast<std::ptrdiff_t>(j));
          changed = true;
        } else {
          ++j;
        }
      }
  }
  std::sort(rects.begin(), rects.end(), [](const IRect& lhs, const IRect& rhs) { return std::tie(lhs.y0, lhs.x0) < std::tie(rhs.y0, rhs.x0); });
  return rects;
}

bool rgba_ok(const import::Image& im) { return !im.indexed && im.width && im.height && im.pixels.size() >= size_t(im.width) * im.height * 4; }

void copy_px(std::vector<u8>& dst, u32 dw, int dx, int dy, const u8* src) { std::memcpy(&dst[(static_cast<size_t>(dy) * dw + static_cast<size_t>(dx)) * 4], src, 4); }

}  // namespace

ShelfPacking shelf_pack(const std::vector<std::array<int, 2>>& sizes, int pad) {
  ShelfPacking out;
  out.pos.assign(sizes.size(), {0, 0});
  if (sizes.empty()) return out;
  std::vector<size_t> order(sizes.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](size_t index_a, size_t index_b) { return std::tie(sizes[index_b][1], sizes[index_b][0]) < std::tie(sizes[index_a][1], sizes[index_a][0]); });
  long long area = 0;
  int max_w = 1;
  for (const auto& size : sizes) {
    area += static_cast<long long>(size[0] + 2 * pad) * (size[1] + 2 * pad);
    max_w = std::max(max_w, size[0] + 2 * pad);
  }
  int image_w = 8;
  const int want = std::max(max_w, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(area) * 1.15))));
  while (image_w < want) image_w *= 2;
  int x = 0, y = 0, shelf_h = 0;
  for (size_t i : order) {
    const int sw = sizes[i][0] + 2 * pad, sh = sizes[i][1] + 2 * pad;
    if (x + sw > image_w) {
      y += shelf_h;
      x = 0;
      shelf_h = 0;
    }
    out.pos[i] = {x, y};
    x += sw;
    shelf_h = std::max(shelf_h, sh);
  }
  out.w = image_w;
  out.h = (y + shelf_h + 3) & ~3;
  return out;
}

void bake_object_textures(assets::StageAsset& stage) {
  stage.tiles.clear();
  stage.tile_map = {};
  stage.object_textures.clear();
  constexpr int kPad = 1;
  // scroll_meshes: meshes drawn by an op-36 UV-scroll emitter. scroll_windows: mesh -> its op-36 texture window (off_u, off_v, win_w,
  // win_h; win 0 = the 256 page).
  std::set<std::string> scroll_meshes;
  std::map<std::string, std::array<int, 4>> scroll_windows;
  for (const assets::StageEmitter& em : stage.emitters)
    if ((em.ext & 1) && !em.mesh.empty()) {
      scroll_meshes.insert(em.mesh);
      scroll_windows[em.mesh] = em.uv_window;
    }
  // The window rect of a scrolling mesh in atlas texels, from the page origin (ox, oy): the GPU wraps every texel of such a mesh inside it
  // (stage_pipeline.cpp: w = off + mod(u - off, win)), so it is the only art the mesh can ever show (not the whole page).
  auto window_rect = [&](const std::string& mesh, int ox, int oy, const import::Image& im) {
    const std::array<int, 4> window = scroll_windows.count(mesh) ? scroll_windows[mesh] : std::array<int, 4>{0, 0, 0, 0};
    const int ww = window[2] > 0 ? window[2] : 256, wh = window[3] > 0 ? window[3] : 256;
    const int wx = ox + (window[2] > 0 ? window[0] : 0), wy = oy + (window[3] > 0 ? window[1] : 0);
    return IRect{std::max(wx, 0), std::max(wy, 0), std::min(wx + ww - 1, static_cast<int>(im.width) - 1), std::min(wy + wh - 1, static_cast<int>(im.height) - 1)};
  };
  // The GPU's wrap of a packet texel into the window (win 0: the 256 page).
  auto wrap_into = [&](const std::string& mesh, int raw, int axis) {
    const std::array<int, 4> window = scroll_windows.count(mesh) ? scroll_windows[mesh] : std::array<int, 4>{0, 0, 0, 0};
    const int off = window[axis], win = window[2 + axis];
    return win > 0 ? off + (((raw - off) % win) + win) % win : raw & 255;
  };
  // Atlas texel of a textured vertex.
  auto texel = [&](const assets::StageVertex& vertex, int tex, int& x, int& y) {
    const import::Image& im = stage.textures[static_cast<size_t>(tex)].image;
    x = std::clamp(static_cast<int>(std::lround(vertex.uv[0] * static_cast<float>(im.width) - 0.5f)), 0, static_cast<int>(im.width) - 1);
    y = std::clamp(static_cast<int>(std::lround((1.0f - vertex.uv[1]) * static_cast<float>(im.height) - 0.5f)), 0, static_cast<int>(im.height) - 1);
  };
  auto usable = [&](const assets::StageSubMesh& sm) {
    return sm.texture >= 0 && static_cast<size_t>(sm.texture) < stage.textures.size() && rgba_ok(stage.textures[static_cast<size_t>(sm.texture)].image);
  };
  // 1. every texel rect the faces sample, stage-wide per atlas (scrolling parts: the whole page of each corner)
  std::vector<std::vector<IRect>> want(stage.textures.size());
  // A mesh no stage emitter names is never spawned (StageScene maps emitter mesh names to parts), so its tiles are not "used by the level":
  // skipped here and below (its submeshes keep object_texture = -1 = the raw atlas, should anything ever draw it).
  auto unused_part = [](const assets::StagePart& part) { return !part.ground && part.emitters.empty(); };
  for (auto& part : stage.parts) {
    for (auto& sm : part.submeshes) sm.object_texture = -1;
    if (unused_part(part)) continue;
    const bool scrolls = scroll_meshes.count(part.name) != 0;
    for (const assets::StageSubMesh& sm : part.submeshes) {
      if (!usable(sm)) continue;
      const import::Image& im = stage.textures[static_cast<size_t>(sm.texture)].image;
      for (u32 k = 0; k + 2 < sm.index_count; k += 3) {
        IRect rect{1 << 20, 1 << 20, -(1 << 20), -(1 << 20)};
        bool ok = true;
        std::vector<IRect> pages;
        for (int corner = 0; corner < 3 && ok; ++corner) {
          const assets::StageVertex& vertex = part.vertices[part.indices[sm.first_index + k + static_cast<u32>(corner)]];
          if (vertex.uv_scale[0] <= 0.0f) {
            ok = false;
            break;
          }
          int x, y;
          texel(vertex, sm.texture, x, y);
          rect = {std::min(rect.x0, x), std::min(rect.y0, y), std::max(rect.x1, x), std::max(rect.y1, y)};
          // Page origin = texel - packet texel; the mesh samples only its texture window of that page.
          if (scrolls) {
            const int ox = x - static_cast<int>(std::lround(vertex.uv_raw[0])), oy = y - static_cast<int>(std::lround(vertex.uv_raw[1]));
            pages.push_back(window_rect(part.name, ox, oy, im));
          }
        }
        if (!ok) continue;
        // A scrolling face's own corners may lie outside the window: the window is its rect.
        if (!scrolls) want[static_cast<size_t>(sm.texture)].push_back(rect);
        for (const IRect& page : pages)
          if (page.x1 >= page.x0 && page.y1 >= page.y0) want[static_cast<size_t>(sm.texture)].push_back(page);
      }
    }
  }
  // 2. merge, cut the tiles, de-duplicate by content
  struct TRect {
    int atlas;
    IRect bounds;
    int tile;
  };
  std::vector<TRect> rects;
  std::unordered_map<std::string, int> by_hash;
  for (size_t atlas_index = 0; atlas_index < want.size(); ++atlas_index) {
    if (want[atlas_index].empty()) continue;
    const import::Image& src = stage.textures[atlas_index].image;
    for (const IRect& rect : merge_rects(std::move(want[atlas_index]))) {
      assets::StageTile tile;
      tile.w = rect.x1 - rect.x0 + 1;
      tile.h = rect.y1 - rect.y0 + 1;
      tile.image.width = static_cast<u32>(tile.w);
      tile.image.height = static_cast<u32>(tile.h);
      tile.image.pixels.resize(static_cast<size_t>(tile.w) * static_cast<size_t>(tile.h) * 4);
      for (int y = 0; y < tile.h; ++y)
        std::memcpy(&tile.image.pixels[static_cast<size_t>(y) * static_cast<size_t>(tile.w) * 4], &src.pixels[(static_cast<size_t>(rect.y0 + y) * src.width + static_cast<size_t>(rect.x0)) * 4],
                    static_cast<size_t>(tile.w) * 4);
      tile.hash = texture_content_tag(tile.image.width, tile.image.height, tile.image.pixels.data());
      const assets::StageTile::Source srcpos{static_cast<int>(atlas_index), rect.x0, rect.y0};
      auto it = by_hash.find(tile.hash);
      // the hash is 32 bit: confirm the content before merging two tiles
      if (it != by_hash.end() && (stage.tiles[static_cast<size_t>(it->second)].w != tile.w || stage.tiles[static_cast<size_t>(it->second)].h != tile.h ||
                                  stage.tiles[static_cast<size_t>(it->second)].image.pixels != tile.image.pixels))
        // A collision: keep it apart under a different id.
        it = by_hash.end(), tile.hash += "x";
      if (it == by_hash.end()) {
        tile.sources.push_back(srcpos);
        stage.tiles.push_back(std::move(tile));
        it = by_hash.emplace(stage.tiles.back().hash, static_cast<int>(stage.tiles.size()) - 1).first;
      } else {
        stage.tiles[static_cast<size_t>(it->second)].sources.push_back(srcpos);
      }
      rects.push_back({static_cast<int>(atlas_index), rect, it->second});
    }
  }
  // id_map, per atlas texel: the merged rect covering it (rects are disjoint), -1 = none.
  std::vector<std::vector<int>> id_map(stage.textures.size());
  for (size_t i = 0; i < rects.size(); ++i) {
    const size_t atlas_index = static_cast<size_t>(rects[i].atlas);
    if (id_map[atlas_index].empty()) id_map[atlas_index].assign(static_cast<size_t>(stage.textures[atlas_index].image.width) * stage.textures[atlas_index].image.height, -1);
    for (int y = rects[i].bounds.y0; y <= rects[i].bounds.y1; ++y)
      for (int x = rects[i].bounds.x0; x <= rects[i].bounds.x1; ++x) id_map[atlas_index][static_cast<size_t>(y) * stage.textures[atlas_index].image.width + static_cast<size_t>(x)] = static_cast<int>(i);
  }
  auto find_rect = [&](int atlas, int x, int y) -> int {
    const auto& atlas_map = id_map[static_cast<size_t>(atlas)];
    return atlas_map.empty() ? -1 : atlas_map[static_cast<size_t>(y) * stage.textures[static_cast<size_t>(atlas)].image.width + static_cast<size_t>(x)];
  };
  // 3. one image per part
  // faces_per_object: tile -> triangles.
  std::vector<std::map<int, u32>> faces_per_object;
  for (size_t pi = 0; pi < stage.parts.size(); ++pi) {
    assets::StagePart& part = stage.parts[pi];
    if (unused_part(part)) continue;
    // This object's tiles, first use first.
    std::vector<int> tile_order;
    std::map<int, u32> faces;
    auto use = [&](int tile) {
      if (std::find(tile_order.begin(), tile_order.end(), tile) == tile_order.end()) tile_order.push_back(tile);
    };
    for (const assets::StageSubMesh& sm : part.submeshes) {
      if (!usable(sm)) continue;
      for (u32 k = 0; k + 2 < sm.index_count; k += 3) {
        const assets::StageVertex& v0 = part.vertices[part.indices[sm.first_index + k]];
        if (v0.uv_scale[0] <= 0.0f) continue;
        for (int corner = 0; corner < 3; ++corner) {
          const assets::StageVertex& vertex = part.vertices[part.indices[sm.first_index + k + static_cast<u32>(corner)]];
          int x, y;
          texel(vertex, sm.texture, x, y);
          // A scrolling corner outside its window wraps into it (the window is the rect).
          if (scroll_meshes.count(part.name)) {
            const int ru = static_cast<int>(std::lround(vertex.uv_raw[0])), rv = static_cast<int>(std::lround(vertex.uv_raw[1]));
            const import::Image& im = stage.textures[static_cast<size_t>(sm.texture)].image;
            x = std::clamp(x - ru + wrap_into(part.name, ru, 0), 0, static_cast<int>(im.width) - 1);
            y = std::clamp(y - rv + wrap_into(part.name, rv, 1), 0, static_cast<int>(im.height) - 1);
          }
          if (const int ri = find_rect(sm.texture, x, y); ri >= 0) {
            use(rects[static_cast<size_t>(ri)].tile);
            if (corner == 0) ++faces[rects[static_cast<size_t>(ri)].tile];
          }
        }
      }
    }
    if (tile_order.empty()) continue;
    assets::StageObjectTexture obj;
    obj.part = static_cast<int>(pi);
    obj.scrolls = scroll_meshes.count(part.name) != 0;
    {
      char nm[32];
      std::snprintf(nm, sizeof nm, "obj_%02d_%s", static_cast<int>(pi), part.ground ? "ground" : "scenery");
      obj.name = nm;
      if (!part.name.empty()) obj.name += "_" + part.name;
    }
    std::vector<std::array<int, 2>> sizes;
    for (int tile_id : tile_order) sizes.push_back({stage.tiles[static_cast<size_t>(tile_id)].w, stage.tiles[static_cast<size_t>(tile_id)].h});
    const ShelfPacking pk = shelf_pack(sizes, kPad);
    std::map<int, size_t> slot_of;
    for (size_t i = 0; i < tile_order.size(); ++i) {
      assets::StageObjectTexture::Slot slot;
      slot.tile = tile_order[i];
      slot.dx = pk.pos[i][0];
      slot.dy = pk.pos[i][1];
      slot.w = sizes[i][0];
      slot.h = sizes[i][1];
      slot_of[slot.tile] = i;
      obj.slots.push_back(slot);
    }
    obj.image.width = static_cast<u32>(pk.w);
    obj.image.height = static_cast<u32>(pk.h);
    obj.image.pixels.assign(static_cast<size_t>(pk.w) * static_cast<size_t>(pk.h) * 4, 0);
    // Texels + the edge-extended gutter.
    for (const auto& slot : obj.slots) {
      const assets::StageTile& tl = stage.tiles[static_cast<size_t>(slot.tile)];
      for (int y = 0; y < slot.h + 2 * kPad; ++y)
        for (int x = 0; x < slot.w + 2 * kPad; ++x)
          copy_px(obj.image.pixels, obj.image.width, slot.dx + x, slot.dy + y,
                  &tl.image.pixels[(static_cast<size_t>(std::clamp(y - kPad, 0, slot.h - 1)) * static_cast<size_t>(tl.w) + static_cast<size_t>(std::clamp(x - kPad, 0, slot.w - 1))) * 4]);
    }
    // remap the vertices: a vertex keeps its slot for the first atlas that uses it, later atlases get a copy at the end
    const std::vector<assets::StageVertex> orig = part.vertices;
    std::vector<int> owner(orig.size(), -2);
    std::map<std::pair<u32, int>, u32> dups;
    const float iw = static_cast<float>(pk.w), ih = static_cast<float>(pk.h);
    auto remapped = [&](u32 old, int tex) {
      assets::StageVertex vertex = orig[old];
      int x, y;
      texel(vertex, tex, x, y);
      // A scrolling mesh's packet texel may lie outside its window (the GPU wraps it in): look the rect up at the wrapped position, but keep
      // the vertex at its unwrapped place relative to the window (the shader derives the page origin as uv - uv_raw and wraps from there).
      int lx = x, ly = y;
      if (obj.scrolls) {
        const int ru = static_cast<int>(std::lround(vertex.uv_raw[0])), rv = static_cast<int>(std::lround(vertex.uv_raw[1]));
        const import::Image& im = stage.textures[static_cast<size_t>(tex)].image;
        lx = std::clamp(x - ru + wrap_into(part.name, ru, 0), 0, static_cast<int>(im.width) - 1);
        ly = std::clamp(y - rv + wrap_into(part.name, rv, 1), 0, static_cast<int>(im.height) - 1);
      }
      if (const int ri = find_rect(tex, lx, ly); ri >= 0) {
        const TRect& tr = rects[static_cast<size_t>(ri)];
        const assets::StageObjectTexture::Slot& slot = obj.slots[slot_of[tr.tile]];
        const float nx = static_cast<float>(slot.dx + kPad + (x - tr.bounds.x0)), ny = static_cast<float>(slot.dy + kPad + (y - tr.bounds.y0));
        vertex.uv = {(nx + 0.5f) / iw, 1.0f - (ny + 0.5f) / ih};
        vertex.uv_scale = {1.0f / iw, 1.0f / ih};
      }
      return vertex;
    };
    const int obj_index = static_cast<int>(stage.object_textures.size());
    for (assets::StageSubMesh& sm : part.submeshes) {
      if (!usable(sm)) continue;
      sm.object_texture = obj_index;
      for (u32 k = 0; k < sm.index_count; ++k) {
        u32& id = part.indices[sm.first_index + k];
        if (orig[id].uv_scale[0] <= 0.0f) continue;
        if (owner[id] == -2) {
          owner[id] = sm.texture;
          part.vertices[id] = remapped(id, sm.texture);
        } else if (owner[id] != sm.texture) {
          auto [it, fresh] = dups.try_emplace({id, sm.texture}, static_cast<u32>(part.vertices.size()));
          if (fresh) part.vertices.push_back(remapped(id, sm.texture));
          id = it->second;
        }
      }
    }
    // The pristine layout recompose_object_textures starts from.
    obj.base_slots = obj.slots;
    obj.base_w = pk.w;
    obj.base_h = pk.h;
    for (const assets::StageVertex& vertex : part.vertices) obj.base_uv.push_back({vertex.uv[0], vertex.uv[1], vertex.uv_scale[0], vertex.uv_scale[1]});
    stage.object_textures.push_back(std::move(obj));
    faces_per_object.push_back(std::move(faces));
  }
  stage.tile_map.id_map = std::move(id_map);
  for (const TRect& rect : rects) stage.tile_map.rects.push_back({rect.atlas, rect.bounds.x0, rect.bounds.y0, rect.bounds.x1 - rect.bounds.x0 + 1, rect.bounds.y1 - rect.bounds.y0 + 1, rect.tile});
  stage.tile_map.home.assign(stage.tiles.size(), {});
  for (size_t oi = 0; oi < stage.object_textures.size(); ++oi)
    for (size_t si = 0; si < stage.object_textures[oi].slots.size(); ++si) {
      auto& home = stage.tile_map.home[static_cast<size_t>(stage.object_textures[oi].slots[si].tile)];
      if (home.object < 0) home = {static_cast<int>(oi), static_cast<int>(si)};
    }
  // 4. names: tile_<first-use order>_<hash>; users
  std::vector<int> number(stage.tiles.size(), -1);
  int next = 0;
  for (size_t oi = 0; oi < stage.object_textures.size(); ++oi)
    for (const auto& slot : stage.object_textures[oi].slots) {
      assets::StageTile& tl = stage.tiles[static_cast<size_t>(slot.tile)];
      if (number[static_cast<size_t>(slot.tile)] < 0) {
        number[static_cast<size_t>(slot.tile)] = next;
        char nm[40];
        std::snprintf(nm, sizeof nm, "tile_%02d_%s", next++, tl.hash.c_str());
        tl.name = nm;
      }
      const auto faces_it = faces_per_object[oi].find(slot.tile);
      tl.users.emplace_back(stage.object_textures[oi].name, faces_it == faces_per_object[oi].end() ? 0u : faces_it->second);
    }
}

int recompose_object_textures(assets::StageAsset& stage, const TileReplacer& replace) {
  struct Repl {
    bool have = false;
    u32 w = 0, h = 0;
    std::vector<u8> rgba;
  };
  std::vector<Repl> repl(stage.tiles.size());
  int count = 0;
  for (size_t i = 0; i < stage.tiles.size(); ++i) {
    Repl& entry = repl[i];
    entry.have = replace(stage.tiles[i], entry.w, entry.h, entry.rgba) && entry.w && entry.h && entry.rgba.size() >= size_t(entry.w) * entry.h * 4;
    count += entry.have ? 1 : 0;
  }
  constexpr int kPad = 1;
  for (assets::StageObjectTexture& obj : stage.object_textures) {
    // Always composed from the pristine state (the tiles' original images, the base layout and base UVs recorded by the bake), so a re-run never accumulates
    // scale and "nothing replaced" restores the original image bit for bit.
    bool any = false;
    double scale = 1.0;
    for (const auto& slot : obj.base_slots) {
      const Repl& entry = repl[static_cast<size_t>(slot.tile)];
      if (!entry.have) continue;
      any = true;
      scale = std::max({scale, static_cast<double>(entry.w) / slot.w, static_cast<double>(entry.h) / slot.h});
    }
    // Untouched and still original: skip.
    if (!any && !obj.composed) continue;
    std::vector<std::array<int, 2>> sizes;
    for (const auto& slot : obj.base_slots) sizes.push_back({std::max(1, static_cast<int>(std::lround(slot.w * scale))), std::max(1, static_cast<int>(std::lround(slot.h * scale)))});
    const ShelfPacking pk = shelf_pack(sizes, kPad);
    std::vector<assets::StageObjectTexture::Slot> ns = obj.base_slots;
    import::Image img;
    img.width = static_cast<u32>(pk.w);
    img.height = static_cast<u32>(pk.h);
    img.pixels.assign(static_cast<size_t>(pk.w) * static_cast<size_t>(pk.h) * 4, 0);
    for (size_t i = 0; i < ns.size(); ++i) {
      const assets::StageTile& tl = stage.tiles[static_cast<size_t>(ns[i].tile)];
      const Repl& entry = repl[static_cast<size_t>(ns[i].tile)];
      const u8* src = entry.have ? entry.rgba.data() : tl.image.pixels.data();
      const int sw = entry.have ? static_cast<int>(entry.w) : tl.w, sh = entry.have ? static_cast<int>(entry.h) : tl.h;
      const int w = sizes[i][0], h = sizes[i][1];
      ns[i].dx = pk.pos[i][0];
      ns[i].dy = pk.pos[i][1];
      ns[i].w = w;
      ns[i].h = h;
      for (int y = 0; y < h + 2 * kPad; ++y)
        for (int x = 0; x < w + 2 * kPad; ++x) {
          // Nearest resample (identity at equal size).
          const int cx = std::clamp(x - kPad, 0, w - 1), cy = std::clamp(y - kPad, 0, h - 1);
          const int px = std::min(sw - 1, static_cast<int>((static_cast<long long>(cx) * 2 + 1) * sw / (2LL * w))), py = std::min(sh - 1, static_cast<int>((static_cast<long long>(cy) * 2 + 1) * sh / (2LL * h)));
          copy_px(img.pixels, img.width, ns[i].dx + x, ns[i].dy + y, &src[(static_cast<size_t>(py) * static_cast<size_t>(sw) + static_cast<size_t>(px)) * 4]);
        }
    }
    // UVs: base UV -> base object texel -> (slot, tile texel) -> the new layout
    const float biw = static_cast<float>(obj.base_w), bih = static_cast<float>(obj.base_h);
    const float niw = static_cast<float>(pk.w), nih = static_cast<float>(pk.h);
    assets::StagePart& part = stage.parts[static_cast<size_t>(obj.part)];
    for (size_t vi = 0; vi < part.vertices.size() && vi < obj.base_uv.size(); ++vi) {
      assets::StageVertex& vertex = part.vertices[vi];
      const auto& base = obj.base_uv[vi];
      if (base[2] <= 0.0f) continue;
      // Restore the exact base values.
      if (!any) {
        vertex.uv = {base[0], base[1]};
        vertex.uv_scale = {base[2], base[3]};
        continue;
      }
      const int ox = static_cast<int>(std::floor(base[0] * biw)), oy = static_cast<int>(std::floor((1.0f - base[1]) * bih));
      for (size_t i = 0; i < ns.size(); ++i) {
        const auto& base_slot = obj.base_slots[i];
        if (ox < base_slot.dx + kPad || ox >= base_slot.dx + kPad + base_slot.w || oy < base_slot.dy + kPad || oy >= base_slot.dy + kPad + base_slot.h) continue;
        const float fx = static_cast<float>(ns[i].w) / static_cast<float>(base_slot.w), fy = static_cast<float>(ns[i].h) / static_cast<float>(base_slot.h);
        const float nx = static_cast<float>(ns[i].dx + kPad) + (static_cast<float>(ox - base_slot.dx - kPad) + 0.5f) * fx;
        const float ny = static_cast<float>(ns[i].dy + kPad) + (static_cast<float>(oy - base_slot.dy - kPad) + 0.5f) * fy;
        vertex.uv = {nx / niw, 1.0f - ny / nih};
        vertex.uv_scale = {fx / niw, fy / nih};
        break;
      }
    }
    obj.slots = std::move(ns);
    obj.image = std::move(img);
    obj.scale = static_cast<float>(scale);
    obj.composed = any;
  }
  return count;
}

int build_tile_seams(assets::StageAsset& stage) {
  // kCosFlat: level 1, faces within 30 degrees of coplanar. kBandTexels: band width in original texels, measured across the edge on
  // each face.
  constexpr float kCosFlat = 0.8660254f;
  constexpr float kBandTexels = 4.0f;
  for (assets::StagePart& stage_part : stage.parts) stage_part.seams.clear();
  using V3 = std::array<float, 3>;
  auto sub3 = [](const V3& lhs, const V3& rhs) { return V3{lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2]}; };
  auto dot3 = [](const V3& lhs, const V3& rhs) { return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2]; };
  int total = 0;
  for (size_t oi = 0; oi < stage.object_textures.size(); ++oi) {
    const assets::StageObjectTexture& obj = stage.object_textures[oi];
    // Applies in every art mode, modded or not.
    if (obj.scrolls || obj.part < 0 || static_cast<size_t>(obj.part) >= stage.parts.size()) continue;
    assets::StagePart& part = stage.parts[static_cast<size_t>(obj.part)];
    const float image_w = static_cast<float>(obj.image.width), image_h = static_cast<float>(obj.image.height);
    if (image_w <= 0.0f || image_h <= 0.0f) continue;
    // texel_scale: image pixels per original texel.
    const float texel_scale = std::max(1.0f, obj.scale);
    // 1. the object's textured triangles with their normals; a quad's two triangles (c0,c1,c2) (c2,c1,c3) with one centroid know each other
    struct Tri {
      std::array<u32, 3> index;
      V3 normal;
      int sub;
      // First index of the TRIANGLE.
      u32 pos;
      // The quad's other triangle (tris index) or -1.
      int sibling;
      // The (2,1,3) half of a quad.
      bool second;
    };
    std::vector<Tri> tris;
    for (size_t si = 0; si < part.submeshes.size(); ++si) {
      const assets::StageSubMesh& sm = part.submeshes[si];
      if (sm.object_texture != static_cast<int>(oi)) continue;
      for (u32 k = 0; k + 2 < sm.index_count; k += 3) {
        Tri tri{{part.indices[sm.first_index + k], part.indices[sm.first_index + k + 1], part.indices[sm.first_index + k + 2]}, {}, static_cast<int>(si), sm.first_index + k, -1, false};
        bool ok = true;
        for (u32 id : tri.index) ok = ok && id < part.vertices.size() && part.vertices[id].uv_scale[0] > 0.0f;
        if (!ok) continue;
        const V3 e1 = sub3(part.vertices[tri.index[1]].pos, part.vertices[tri.index[0]].pos), e2 = sub3(part.vertices[tri.index[2]].pos, part.vertices[tri.index[0]].pos);
        tri.normal = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const float length = std::sqrt(dot3(tri.normal, tri.normal));
        if (length <= 0.0f) continue;
        for (float& component : tri.normal) component /= length;
        // The importer's quad: (c0,c1,c2) then (c2,c1,c3), one packet centroid (as StagePipeline::create_finish detects it).
        if (!tris.empty()) {
          Tri& prev = tris.back();
          if (prev.sibling < 0 && prev.pos + 3 == tri.pos && prev.sub == tri.sub && tri.index[0] == prev.index[2] && tri.index[1] == prev.index[1] && tri.index[2] != prev.index[0] &&
              part.vertices[tri.index[2]].centre == part.vertices[prev.index[0]].centre) {
            prev.sibling = static_cast<int>(tris.size());
            tri.sibling = static_cast<int>(tris.size()) - 1;
            tri.second = true;
          }
        }
        tris.push_back(tri);
      }
    }
    if (tris.size() < 2) continue;
    // 2. edges by quantised positions
    using PKey = std::array<long long, 3>;
    auto pkey = [&](u32 id) {
      const V3& pos = part.vertices[id].pos;
      return PKey{std::llround(pos[0] * 16.0f), std::llround(pos[1] * 16.0f), std::llround(pos[2] * 16.0f)};
    };
    // edges: (tri, corner) per edge index[corner] -> index[(corner + 1) % 3].
    std::map<std::pair<PKey, PKey>, std::vector<std::pair<int, int>>> edges;
    for (size_t ti = 0; ti < tris.size(); ++ti)
      for (int corner = 0; corner < 3; ++corner) {
        // A quad's diagonal is not an edge of the packet.
        if (tris[ti].sibling >= 0 && corner == (tris[ti].second ? 0 : 1)) continue;
        PKey ka = pkey(tris[ti].index[static_cast<size_t>(corner)]), kb = pkey(tris[ti].index[static_cast<size_t>((corner + 1) % 3)]);
        if (ka == kb) continue;
        if (kb < ka) std::swap(ka, kb);
        edges[{ka, kb}].push_back({static_cast<int>(ti), corner});
      }
    // Image pixels of a vertex.
    auto px = [&](u32 id) {
      const assets::StageVertex& vertex = part.vertices[id];
      return std::array<float, 2>{vertex.uv[0] * image_w, (1.0f - vertex.uv[1]) * image_h};
    };
    // One strip: triangle A (its edge a0 - a1, apex a2) with neighbour triangle B (b0 at a0's position, b1 at a1's, apex b2). The strip covers A's whole
    // packet (A plus its quad sibling). Every corner of the packet mirrors across the edge to the same height on B's side; B's UV there follows from B's
    // affine texture map (uv is affine over a triangle, extrapolation included).
    auto make = [&](const Tri& tri_a, u32 a0, u32 a1, u32 a2, u32 b0, u32 b1, u32 b2, bool flat) {
      const V3 edge = sub3(part.vertices[a1].pos, part.vertices[a0].pos);
      const float el2 = dot3(edge, edge);
      if (el2 <= 0.0f) return false;
      // Distance of a vertex from the edge line and its parameter along it.
      auto height = [&](u32 id, float& along) {
        const V3 delta = sub3(part.vertices[id].pos, part.vertices[a0].pos);
        along = dot3(delta, edge) / el2;
        V3 perp = delta;
        for (size_t k = 0; k < 3; ++k) perp[k] -= along * edge[k];
        return std::sqrt(dot3(perp, perp));
      };
      float tA, tB;
      const float hA = height(a2, tA), hB = height(b2, tB);
      if (hA <= 1e-3f || hB <= 1e-3f) return false;
      const std::array<float, 2> ua0 = px(a0), ua1 = px(a1), ua2 = px(a2), ub0 = px(b0), ub1 = px(b1), ub2 = px(b2);
      // original texels across the edge over each apex height -> world units per texel
      const float dA = std::hypot(ua2[0] - (ua0[0] + (ua1[0] - ua0[0]) * tA), ua2[1] - (ua0[1] + (ua1[1] - ua0[1]) * tA)) / texel_scale;
      const float dB = std::hypot(ub2[0] - (ub0[0] + (ub1[0] - ub0[0]) * tB), ub2[1] - (ub0[1] + (ub1[1] - ub0[1]) * tB)) / texel_scale;
      if (dA <= 1e-6f || dB <= 1e-6f) return false;
      // the packet's corners in packet order
      assets::StageSeam sm;
      if (tri_a.sibling >= 0) {
        const Tri& first = tri_a.second ? tris[static_cast<size_t>(tri_a.sibling)] : tri_a;
        const Tri& second = tri_a.second ? tri_a : tris[static_cast<size_t>(tri_a.sibling)];
        sm.v = {first.index[0], first.index[1], first.index[2], second.index[2]};
        sm.corners = 4;
        sm.index_pos = first.pos;
      } else {
        sm.v = {tri_a.index[0], tri_a.index[1], tri_a.index[2], tri_a.index[2]};
        sm.corners = 3;
        sm.index_pos = tri_a.pos;
      }
      // The band may not exceed half of any corner's height (the mirrored samples stay inside B's triangle = inside its tile).
      float hmin = hA;
      for (u32 corner = 0; corner < sm.corners; ++corner) {
        if (sm.v[corner] == a0 || sm.v[corner] == a1) continue;
        float along;
        hmin = std::min(hmin, height(sm.v[corner], along));
      }
      const float band = std::min({kBandTexels * hA / dA, kBandTexels * hB / dB, 0.5f * hmin, 0.5f * hB});
      if (band <= 0.0f) return false;
      sm.submesh = tri_a.sub;
      sm.flat = flat;
      auto norm = [&](float x, float y) { return std::array<float, 2>{x / image_w, 1.0f - y / image_h}; };
      // B's edge point under its apex.
      const std::array<float, 2> ufb{ub0[0] + (ub1[0] - ub0[0]) * tB, ub0[1] + (ub1[1] - ub0[1]) * tB};
      for (u32 corner = 0; corner < sm.corners; ++corner) {
        float along = 0.0f;
        const float dist = (sm.v[corner] == a0 || sm.v[corner] == a1) ? 0.0f : height(sm.v[corner], along);
        if (sm.v[corner] == a0) along = 0.0f;
        if (sm.v[corner] == a1) along = 1.0f;
        // The corner mirrored to B's side: B's edge point under it + (dist / hB) x (B's apex - B's edge point under the apex).
        const float ratio = dist / hB;
        sm.uv[corner] = norm(ub0[0] + (ub1[0] - ub0[0]) * along + ratio * (ub2[0] - ufb[0]), ub0[1] + (ub1[1] - ub0[1]) * along + ratio * (ub2[1] - ufb[1]));
        sm.g[corner] = dist / band;
      }
      part.seams.push_back(sm);
      return true;
    };
    for (const auto& [key, users] : edges) {
      if (users.size() < 2) continue;
      for (size_t user_a = 0; user_a < users.size(); ++user_a)
        for (size_t user_b = user_a + 1; user_b < users.size(); ++user_b) {
          const Tri& t1 = tris[static_cast<size_t>(users[user_a].first)];
          const Tri& t2 = tris[static_cast<size_t>(users[user_b].first)];
          // Skip the same packet.
          if (users[user_a].first == users[user_b].first || (t1.sibling >= 0 && t1.sibling == users[user_b].first)) continue;
          const int c1 = users[user_a].second, c2 = users[user_b].second;
          const u32 a0 = t1.index[static_cast<size_t>(c1)], a1 = t1.index[static_cast<size_t>((c1 + 1) % 3)], a2 = t1.index[static_cast<size_t>((c1 + 2) % 3)];
          u32 b0 = t2.index[static_cast<size_t>(c2)], b1 = t2.index[static_cast<size_t>((c2 + 1) % 3)];
          const u32 b2 = t2.index[static_cast<size_t>((c2 + 2) % 3)];
          // Align the two traversals by position.
          if (pkey(b0) != pkey(a0)) std::swap(b0, b1);
          const std::array<float, 2> pa0 = px(a0), pa1 = px(a1), pb0 = px(b0), pb1 = px(b1);
          // Continuous across the edge: no seam.
          if (std::hypot(pa0[0] - pb0[0], pa0[1] - pb0[1]) < 0.5f && std::hypot(pa1[0] - pb1[0], pa1[1] - pb1[1]) < 0.5f) continue;
          const bool flat = std::fabs(dot3(t1.normal, t2.normal)) >= kCosFlat;
          total += make(t1, a0, a1, a2, b0, b1, b2, flat) ? 1 : 0;
          total += make(t2, b0, b1, b2, a0, a1, a2, flat) ? 1 : 0;
        }
    }
  }
  return total;
}

bool tile_lookup(const assets::StageAsset& stage, int atlas, int x, int y, TileTexel& out) {
  const assets::StageTileMap& tile_map = stage.tile_map;
  if (atlas < 0 || static_cast<size_t>(atlas) >= tile_map.id_map.size() || tile_map.id_map[static_cast<size_t>(atlas)].empty() || x < 0 || y < 0) return false;
  const assets::StageTexture& at = stage.textures[static_cast<size_t>(atlas)];
  if (static_cast<u32>(x) >= at.image.width || static_cast<u32>(y) >= at.image.height) return false;
  const int ri = tile_map.id_map[static_cast<size_t>(atlas)][static_cast<size_t>(y) * at.image.width + static_cast<size_t>(x)];
  if (ri < 0) return false;
  const assets::StageTileMap::Rect& rect = tile_map.rects[static_cast<size_t>(ri)];
  const assets::StageTileMap::Home& home = tile_map.home[static_cast<size_t>(rect.tile)];
  if (home.object < 0) return false;
  const assets::StageObjectTexture::Slot& slot = stage.object_textures[static_cast<size_t>(home.object)].slots[static_cast<size_t>(home.slot)];
  out.object = home.object;
  out.rect = ri;
  out.fx = static_cast<float>(slot.w) / static_cast<float>(rect.w);
  out.fy = static_cast<float>(slot.h) / static_cast<float>(rect.h);
  out.px = static_cast<float>(slot.dx + 1) + (static_cast<float>(x - rect.sx) + 0.5f) * out.fx;
  out.py = static_cast<float>(slot.dy + 1) + (static_cast<float>(y - rect.sy) + 0.5f) * out.fy;
  return true;
}

std::string tile_path(const std::string& group, const assets::StageTile& tile) {
  const std::string category = group.substr(0, group.find('/'));
  return texture_path(is_content_named(category) ? TextureKey{category, "", -1, "tile_" + tile.hash}
                                                  : TextureKey{category, group.find('/') == std::string::npos ? std::string() : group.substr(group.find('/') + 1), -1, tile.name});
}

namespace {
}  // namespace

u32 dump_tiles(const assets::StageAsset& stage, const std::string& group, bool forced) {
  u32 written = 0;
  const std::string category = group.substr(0, group.find('/'));
  for (const assets::StageTile& tile : stage.tiles) {
    const TextureKey key = is_content_named(category) ? TextureKey{category, "", -1, "tile_" + tile.hash}
                                                      : TextureKey{category, group.find('/') == std::string::npos ? std::string() : group.substr(group.find('/') + 1), -1, tile.name};
    written += (forced ? texture_registry::dump_forced(key, tile.image.width, tile.image.height, tile.image.pixels.data())
                       : texture_registry::dump(key, tile.image.width, tile.image.height, tile.image.pixels.data()))
                   ? 1u
                   : 0u;
  }
  // No manifest: only the tile PNGs go in the folder (the hash in the file name is all a replacement needs).
  return written;
}

void apply_tile_textures(assets::StageAsset& stage, const std::string& group) {
  if (group.empty() || stage.tiles.empty()) return;
  // Re-runnable: with no active replacement (a mod switched off) the objects go back to their original images.
  const bool active = texture_registry::active();
  recompose_object_textures(stage, [&](const assets::StageTile& tile, u32& w, u32& h, std::vector<u8>& rgba) { return active && texture_registry::replace_tile(group, tile.hash, w, h, rgba); });
  // [Enhancements] EdgeBlend strips of every object (drawn by StagePipeline when render::edge_blend()).
  build_tile_seams(stage);
}

}  // namespace oc::render
