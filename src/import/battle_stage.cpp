#include "import/battle_stage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

#include "core/span_reader.h"
#include "import/container.h"
#include "import/tim.h"

namespace oc::import {

namespace {

using assets::StageAsset;

constexpr u8 kTMesh = 0x02, kTTim = 0x04, kTEmit = 0x0A, kTCam = 0x0C, kTSeq = 0x10, kTRing = 0x12, kTLenz = 0x15;

struct Fail : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Packet bytes: 4 per colour (one, or one per corner when gouraud), 10 / 12 of UV + CLUT + tpage words when textured
// (triangle / quad), 2 per vertex index, and 2 bytes of padding for an untextured triangle.
size_t face_size(u32 type) {
  const bool quad = type & 8, tex = type & 4, gour = type & 0x10;
  const size_t corners = quad ? 4 : 3;
  return 4 * (gour ? corners : 1) + (tex ? (quad ? 12 : 10) : 0) + 2 * corners + ((tex || quad) ? 0 : 2);
}

// Mesh: u32 0x80000001, u16 vertex count, u16 face count, u32 vertex offset, u32 face offset, u32 group table offset (0x14),
// then the group table (u32 count; u16 type, u16 count, u32 offset per group), contiguous face packets and 8-byte vertices
// ending at EOF.
//
// Textured triangle packets: the engine's renderers transform the vertices in the order idx1, idx2, idx0 (idx0 shares its
// word with uv2; the code reads the next word's pair first) and attach the packet's corner data (uv, colour) in packet order,
// so corner k belongs to vertex idx[(k + 1) % 3]. The indices are rotated so that corner k and idx[k] pair up (the cyclic
// orientation, and with it the NCLIP cull side, is unchanged). Untextured triangles (G3 / F3: indices in words 3-4) and quads
// keep the natural / Z-order pairing: the title's `eff6` glow fan has its bright corner 1 on idx[1] = the centre vertex.
StageMeshSrc parse_mesh_impl(ByteSpan data) {
  SpanReader reader(data);
  if (data.size() < 0x18 || reader.at<u32>(0) != 0x80000001u) throw Fail("mesh magic");
  const u32 vertex_count = reader.at<u16>(4), face_count = reader.at<u16>(6);
  const u32 vertex_offset = reader.at<u32>(8), face_offset = reader.at<u32>(12), group_offset = reader.at<u32>(16);
  if (group_offset != 0x14) throw Fail("group header offset");
  if (static_cast<u64>(vertex_offset) + 8ull * vertex_count != data.size()) throw Fail("vertex array does not end at EOF");
  StageMeshSrc mesh;
  mesh.verts.resize(vertex_count);
  for (u32 i = 0; i < vertex_count; ++i)
    mesh.verts[i] = {static_cast<s16>(reader.at<u16>(vertex_offset + 8ull * i)), static_cast<s16>(reader.at<u16>(vertex_offset + 8ull * i + 2)),
                     static_cast<s16>(reader.at<u16>(vertex_offset + 8ull * i + 4))};
  const u32 group_count = reader.at<u32>(group_offset);
  if (group_count > 64 || face_offset != group_offset + 4 + 8ull * group_count) throw Fail("face data does not follow the group table");
  size_t expect = face_offset;
  for (u32 group = 0; group < group_count; ++group) {
    const u32 type = reader.at<u16>(group_offset + 4 + 8ull * group), count = reader.at<u16>(group_offset + 6 + 8ull * group),
              offset = reader.at<u32>(group_offset + 8 + 8ull * group);
    if (type != 0x20 && type != 0x24 && type != 0x28 && type != 0x2C && type != 0x30 && type != 0x34 && type != 0x38 && type != 0x3C)
      throw Fail("face type");
    if (offset != expect) throw Fail("group not contiguous");
    const size_t size = face_size(type);
    const bool quad = type & 8, tex = type & 4, gour = type & 0x10;
    const u8 corners = quad ? 4 : 3;
    for (u32 i = 0; i < count; ++i) {
      const size_t face_pos = offset + i * size;
      if (face_pos + size > vertex_offset) throw Fail("face overruns");
      StageFace face;
      face.n = corners;
      face.code = static_cast<u8>(type);
      const u8 code = data[face_pos + 3];
      if ((code & ~2u) != type) throw Fail("code byte != group type");
      face.semi = (code & 2) != 0;
      const u8 colour_count = gour ? corners : 1;
      for (u8 k = 0; k < colour_count; ++k) face.rgb[k] = {data[face_pos + 4u * k], data[face_pos + 4u * k + 1], data[face_pos + 4u * k + 2]};
      for (u8 k = colour_count; k < 4; ++k) face.rgb[k] = face.rgb[0];
      size_t pos = face_pos + 4u * colour_count;
      if (tex) {
        face.textured = true;
        face.uv[0] = {data[pos], data[pos + 1]};
        face.clut = reader.at<u16>(pos + 2);
        face.uv[1] = {data[pos + 4], data[pos + 5]};
        face.tpage = reader.at<u16>(pos + 6);
        face.uv[2] = {data[pos + 8], data[pos + 9]};
        if (quad) face.uv[3] = {data[pos + 10], data[pos + 11]};
        pos += quad ? 12 : 10;
      }
      for (u8 k = 0; k < corners; ++k) {
        face.idx[k] = reader.at<u16>(pos + 2u * k);
        if (face.idx[k] >= vertex_count) throw Fail("vertex index out of range");
      }
      if (corners == 3 && tex) {
        const u16 first = face.idx[0];
        face.idx[0] = face.idx[1];
        face.idx[1] = face.idx[2];
        face.idx[2] = first;
      }
      mesh.faces.push_back(face);
    }
    expect = offset + static_cast<size_t>(count) * size;
  }
  if (expect != vertex_offset) throw Fail("face data does not end at the vertex array");
  if (mesh.faces.size() != face_count) throw Fail("face count != header");
  return mesh;
}

struct CtdSection {
  u32 type = 0;
  int x = 0, y = 0, w = 0, h = 0;
  std::vector<u16> data;
};

// ctd: 2048-byte sectors. A header sector (u32 type 0x1200 image / 0x1201 CLUT, u16 x, y, x offset, y offset, u16 width in
// halfwords, u32 section count at +20 in the first header, u32 data sector count at +24, u16 row counts per data sector from
// +28) is followed by its data sectors of raw VRAM rows.
std::vector<CtdSection> parse_ctd(ByteSpan ctd) {
  std::vector<CtdSection> out;
  SpanReader reader(ctd);
  size_t sector = 0;
  u32 first_count = 0;
  while (sector * 2048 < ctd.size()) {
    const size_t offset = sector * 2048;
    CtdSection section;
    section.type = reader.at<u32>(offset);
    section.x = reader.at<u16>(offset + 4) + reader.at<u16>(offset + 8);
    section.y = reader.at<u16>(offset + 6) + reader.at<u16>(offset + 10);
    const u32 width_hw = reader.at<u16>(offset + 12), sector_count = reader.at<u32>(offset + 24);
    if ((section.type != 0x1200 && section.type != 0x1201) || width_hw == 0 || offset + 2048ull * (1ull + sector_count) > ctd.size())
      throw Fail("bad ctd section header");
    if (out.empty()) first_count = reader.at<u32>(offset + 20);
    section.w = static_cast<int>(width_hw);
    for (u32 i = 0; i < sector_count; ++i) {
      const u32 rows = reader.at<u16>(offset + 28 + 2ull * i);
      const size_t sector_offset = offset + 2048ull * (1 + i);
      if (static_cast<size_t>(rows) * width_hw * 2 > 2048) throw Fail("ctd rows exceed sector");
      for (size_t k = 0; k < static_cast<size_t>(rows) * width_hw; ++k) section.data.push_back(reader.at<u16>(sector_offset + 2 * k));
      section.h += static_cast<int>(rows);
    }
    out.push_back(std::move(section));
    sector += 1 + sector_count;
  }
  if (!out.empty() && first_count != out.size()) throw Fail("ctd section count mismatch");
  return out;
}

struct Parsed {
  Drp drp;
  std::vector<CtdSection> ctd;
  // (entry index, tim) pairs.
  std::vector<std::pair<size_t, Tim>> tims;
  std::unique_ptr<Vram> vram = std::make_unique<Vram>();
};

void parse_container(Parsed& parsed, ByteSpan prd, ByteSpan ctd) {
  auto drp = parse_drp(prd);
  if (!drp) throw Fail("not a drp");
  parsed.drp = std::move(*drp);
  if (!ctd.empty()) parsed.ctd = parse_ctd(ctd);
  for (size_t i = 0; i < parsed.drp.entries.size(); ++i)
    if (parsed.drp.entries[i].type == kTTim) {
      auto tim = parse_tim(parsed.drp.entries[i].data);
      if (!tim) throw Fail("bad TIM '" + parsed.drp.entries[i].name + "'");
      parsed.tims.emplace_back(i, std::move(*tim));
    }
  for (const auto& section : parsed.ctd) parsed.vram->blit(section.x, section.y, section.w, section.h, section.data);
  for (const auto& tim : parsed.tims) parsed.vram->blit_tim(tim.second);
}

std::string name4(ByteSpan bytes) {
  std::string name;
  for (u8 byte : bytes) {
    if (byte == 0) break;
    name.push_back(static_cast<char>(byte));
  }
  return name;
}

bool is_alnum(u8 byte) { return (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z'); }

// The first 4-char alphanumeric token (zero padded, or three characters + '_') in the payload equal to a mesh name.
std::string emitter_mesh_ref(ByteSpan payload, const std::set<std::string>& names) {
  for (size_t offset = 0x18; offset + 3 < payload.size(); ++offset) {
    const u8* token = payload.data() + offset;
    size_t len = 4;
    while (len > 0 && token[len - 1] == 0) --len;
    bool ok = token[0] != 0 && len > 0;
    for (size_t k = 0; ok && k < len; ++k) ok = is_alnum(token[k]);
    if (!ok) ok = is_alnum(token[0]) && is_alnum(token[1]) && is_alnum(token[2]) && token[3] == '_';
    if (!ok) continue;
    const std::string name = name4(ByteSpan(token, 4));
    if (names.count(name)) return name;
  }
  return {};
}

// Static placement data of a type 0x0A script: walks the op stream (op = word & 0x1FF, length = (word >> 9) & 0x3F) and keeps
// OT_BIAS (0x03), the SPRITE (0x05) mesh name and type, FLAGS (0x06), POS (0x08), VEL (0x0A), LIFE (0x0F), SCALE (0x12),
// ANGLE (0x19), COLOR (0x20), EXT35 (0x35) and the texture-window UV scroll (0x36: u8 off_u +2, u8 off_v +4, s16 win_w +6,
// win_h +8; the rates at +10 / +12 are not kept).
void parse_emitter_placement(ByteSpan payload, assets::StageEmitter& emitter, const std::set<std::string>& names) {
  SpanReader reader(payload);
  size_t offset = 0x18;
  while (offset + 2 <= payload.size()) {
    const u16 word = reader.at<u16>(offset);
    const u32 op = word & 0x1FF, len = (word >> 9) & 0x3F;
    if (op == 0 || len < 2 || offset + len > payload.size()) break;
    auto s16_at = [&](size_t delta) { return static_cast<int>(static_cast<int16_t>(reader.at<u16>(offset + delta))); };
    auto u16_at = [&](size_t delta) { return static_cast<int>(reader.at<u16>(offset + delta)); };
    switch (op) {
      case 0x03: if (len >= 4) emitter.bias = s16_at(2); break;
      case 0x05:
        if (len >= 12) {
          const std::string sprite_name = name4(ByteSpan(payload.data() + offset + 8, 4));
          emitter.sprite = sprite_name;
          if (names.count(sprite_name)) emitter.mesh = sprite_name;
          if (len >= 14) emitter.sprite_type = s16_at(12);
        }
        break;
      case 0x06: if (len >= 8) emitter.flags = reader.at<u32>(offset + 4); break;
      case 0x08: if (len >= 8) emitter.placed = true, emitter.pos = {s16_at(2), s16_at(4), s16_at(6)}; break;
      case 0x0A: if (len >= 8) emitter.vel = {s16_at(2), s16_at(4), s16_at(6)}; break;
      case 0x0F: if (len >= 4) emitter.life = u16_at(2); break;
      case 0x12: if (len >= 8) emitter.scale = {u16_at(2), u16_at(4), u16_at(6)}; break;
      case 0x19: if (len >= 8) emitter.angle = {u16_at(2), u16_at(4), u16_at(6)}; break;
      case 0x20: if (len >= 8) emitter.color = {u16_at(2), u16_at(4), u16_at(6)}; break;
      case 0x35: if (len >= 8) emitter.ext = reader.at<u32>(offset + 4); break;
      case 0x36:
        if (len >= 10)
          emitter.uv_window = {reader.at<u8>(offset + 2), reader.at<u8>(offset + 4), static_cast<s16>(reader.at<u16>(offset + 6)),
                               static_cast<s16>(reader.at<u16>(offset + 8))};
        break;
      default: break;
    }
    offset += len;
  }
}

// Camera: u32 flags, u16 duration, u16 mode, u32 word 8, 4 reference bytes, u32 key count at +16, then 20-byte keys (u32 word 0,
// s16 eye x y z, s16 projection, s16 target x y z, s16 roll in 1/1024 turns). y is negated into the engine's frame.
assets::StageCamera parse_camera(const DrpEntry& entry) {
  SpanReader reader(entry.data);
  if (entry.data.size() < 20) throw Fail("camera too short");
  const u32 count = reader.at<u32>(16);
  if (20ull + 20ull * count != entry.data.size()) throw Fail("camera size != 20+20*count");
  assets::StageCamera camera;
  camera.name = entry.name;
  camera.flags = reader.at<u32>(0);
  camera.duration = reader.at<u16>(4);
  camera.mode = reader.at<u16>(6);
  camera.w8 = reader.at<u32>(8);
  for (size_t i = 0; i < 4; ++i) camera.ref[i] = entry.data[12 + i];
  auto coord = [&](size_t off) { return static_cast<float>(static_cast<s16>(reader.at<u16>(off))); };
  for (u32 i = 0; i < count; ++i) {
    const size_t key_offset = 20 + 20ull * i;
    assets::StageCameraKey key;
    key.w0 = reader.at<u32>(key_offset);
    key.eye = {coord(key_offset + 4), -coord(key_offset + 6), coord(key_offset + 8)};
    key.projection = coord(key_offset + 10);
    key.target = {coord(key_offset + 12), -coord(key_offset + 14), coord(key_offset + 16)};
    key.roll_raw = static_cast<s16>(reader.at<u16>(key_offset + 18));
    key.roll_deg = static_cast<float>(key.roll_raw) * 360.0f / 1024.0f;
    camera.keys.push_back(key);
  }
  return camera;
}

// Sequence: 0x14-byte header, then commands (u32 word: op = bits 0-8, length = bits 9-14, jump = bit 15) up to END (0x21).
// A printable 4-char name at +8 of the operands is the command's target. Trailing bytes after END are either none or the
// 8-byte pad {0x21, 0x10, 0...}.
assets::StageSequence parse_sequence(const DrpEntry& entry) {
  assets::StageSequence sequence;
  sequence.name = entry.name;
  SpanReader reader(entry.data);
  size_t offset = 0x14;
  while (offset + 4 <= entry.data.size()) {
    const u32 word = reader.at<u32>(offset);
    const u32 op = word & 0x1FF, length = (word >> 9) & 0x3F;
    if (length < 4 || offset + length > entry.data.size()) throw Fail("bad sequence command length");
    assets::StageSeqCommand command;
    command.offset = static_cast<u32>(offset);
    command.op = static_cast<u16>(op);
    command.length = static_cast<u16>(length);
    command.jump = (word >> 15) & 1;
    command.data.assign(entry.data.begin() + static_cast<std::ptrdiff_t>(offset + 4),
                        entry.data.begin() + static_cast<std::ptrdiff_t>(offset + length));
    if (command.data.size() >= 12) {
      const ByteSpan name_bytes(command.data.data() + 8, 4);
      bool ok = name_bytes[0] != 0;
      for (u8 ch : name_bytes) ok = ok && (ch == 0 || (ch >= 0x20 && ch < 0x7F));
      if (ok) command.target = name4(name_bytes);
    }
    sequence.commands.push_back(std::move(command));
    offset += length;
    if (op == 0x21) break;
  }
  sequence.trailing = static_cast<u32>(entry.data.size() - offset);
  static constexpr u8 kPad[8] = {0x21, 0x10, 0, 0, 0, 0, 0, 0};
  sequence.trailing_is_pad =
      sequence.trailing == 0 ||
      (sequence.trailing == 8 && std::equal(kPad, kPad + 8, entry.data.begin() + static_cast<std::ptrdiff_t>(offset)));
  return sequence;
}

// Lens flare (timinfo layout): u16 table count at +2 (bit 15 ignored), u16 table offsets from +0x0C; each table is a u16
// sprite count and 12-byte sprites (s16 x, y, u8 u, v, u16 clut, u8 w, h, m, kind).
assets::StageLens parse_lenz(const DrpEntry& entry) {
  assets::StageLens lens;
  lens.name = entry.name;
  lens.raw.assign(entry.data.begin(), entry.data.end());
  SpanReader reader(entry.data);
  const u32 table_count = reader.at<u16>(2) & 0x7FFF;
  for (u32 i = 0; i < table_count; ++i) {
    const u32 table_offset = reader.at<u16>(0x0C + 2ull * i);
    if (table_offset + 4ull > entry.data.size()) throw Fail("lenz table offset");
    const u32 count = reader.at<u16>(table_offset);
    std::vector<assets::StageLensSprite> table;
    for (u32 k = 0; k < count; ++k) {
      const size_t record = table_offset + 4ull + 12ull * k;
      if (record + 12 > entry.data.size()) throw Fail("lenz entry overruns");
      assets::StageLensSprite sprite;
      sprite.x = static_cast<s16>(reader.at<u16>(record));
      sprite.y = static_cast<s16>(reader.at<u16>(record + 2));
      sprite.u = entry.data[record + 4];
      sprite.v = entry.data[record + 5];
      sprite.clut = reader.at<u16>(record + 6);
      sprite.w = entry.data[record + 8];
      sprite.h = entry.data[record + 9];
      sprite.m = entry.data[record + 10];
      sprite.kind = entry.data[record + 11];
      table.push_back(sprite);
    }
    lens.tables.push_back(std::move(table));
  }
  return lens;
}

struct AtlasExtent {
  // Atlas placement in halfwords: x0 = first column (64-aligned), width_hw = width (384 when nothing is loaded),
  // lo/hi = loaded image extent (0 / 0 when nothing is loaded).
  int x0 = 0;
  int width_hw = 0;
  int lo = 0, hi = 0;
};

AtlasExtent atlas_extent(const Parsed& parsed) {
  AtlasExtent extent;
  extent.lo = 1 << 20;
  extent.hi = -1;
  for (const auto& tim : parsed.tims) {
    extent.lo = std::min<int>(extent.lo, tim.second.image.x);
    extent.hi = std::max<int>(extent.hi, tim.second.image.x + tim.second.image.w);
  }
  for (const auto& section : parsed.ctd)
    if (section.type == 0x1200) {
      extent.lo = std::min(extent.lo, section.x);
      extent.hi = std::max(extent.hi, section.x + section.w);
    }
  if (extent.hi < 0) {
    extent.lo = extent.hi = 0;
    extent.x0 = 0;
    extent.width_hw = 384;
    return extent;
  }
  extent.x0 = (extent.lo / 64) * 64;
  extent.width_hw = extent.hi - extent.x0;
  return extent;
}

struct TexKey {
  int mode, cx, cy;
  // px/py >= 0: a room effect mesh on its own texture page (tpage x halfwords / y rows, e.g. (960, 0)): a one-page atlas of that page.
  int px = -1, py = -1;
  bool operator==(const TexKey&) const = default;
};

// Mesh parts: corners are split per packet and deduplicated on (vertex, colour, texture mode, atlas uv, packet centroid).
// The packet centroid is carried by every corner because the GPU primitive (triangle or quad) gets one ordering-table slot
// from the average of its corners' camera z. A tpage outside the stage's background rows 256..511 (room effect meshes on the
// effect page (960, 0)) gets its own one-page atlas. Textured polygons are modulated (colour / 128 = neutral); an untextured
// polygon is drawn with its raw colour (colour / 255, a 0x80 vertex is mid grey) and the renderer samples a white texel for it.
// Texel centres: a PS1 packet u addresses texel u itself and RDE nudges every polygon u, v by half a texel before its bilinear
// sampler; sampling at the integer boundary blended half of the neighbouring atlas tile along every face edge.
// Submeshes are runs of equal (texture, data semi bit, tpage abr): the renderer picks the blend state per run. Packet corner
// order (0,1,2) / (2,1,3): the GPU draws the face only when NCLIP(0,1,2) > 0, which the renderer reproduces with back-face
// culling. The stock env page (VRAM page 0x95 = (320, 256) 8-bit through CLUT row (0, 239)) is the texture the battle unit
// packer reads for env-mapped parts without shading flag 0x80; it is blank when no stage image reaches it.
StageAsset build_asset(const Parsed& parsed) {
  StageAsset asset;
  const AtlasExtent extent = atlas_extent(parsed);
  asset.atlas_x0_hw = extent.x0;
  asset.atlas_width_hw = extent.width_hw;
  asset.tim_count = static_cast<u32>(parsed.tims.size());
  std::set<std::string> mesh_names;
  for (const auto& entry : parsed.drp.entries)
    if (entry.type == kTMesh || entry.type == kTRing) mesh_names.insert(entry.name);

  std::vector<TexKey> keys;

  for (size_t entry_index = 0; entry_index < parsed.drp.entries.size(); ++entry_index) {
    const DrpEntry& entry = parsed.drp.entries[entry_index];
    if (entry.type == kTMesh || entry.type == kTRing) {
      StageMeshSrc mesh = parse_mesh_impl(entry.data);
      assets::StagePart part;
      part.name = entry.name;
      part.entry = static_cast<u32>(entry_index);
      part.ground = entry.type == kTRing;
      part.source_vertices = static_cast<u32>(mesh.verts.size());
      part.source_faces = static_cast<u32>(mesh.faces.size());
      std::map<std::array<u64, 3>, u32> vertex_map;
      int cur_key = -2;
      for (const StageFace& face : mesh.faces) {
        if (face.semi) ++part.semi_transparent_faces;
        std::array<float, 3> centroid{0, 0, 0};
        for (int k = 0; k < face.n; ++k) {
          const auto& pos = mesh.verts[face.idx[static_cast<size_t>(k)]];
          centroid[0] += static_cast<float>(pos[0]);
          centroid[1] -= static_cast<float>(pos[1]);
          centroid[2] += static_cast<float>(pos[2]);
        }
        for (float& component : centroid) component /= static_cast<float>(face.n);
        u64 k3 = 0;
        for (int k = 0; k < 3; ++k)
          k3 |= static_cast<u64>(static_cast<u32>(std::lround(centroid[static_cast<size_t>(k)] * 4.0f) + (1 << 20)) & 0x1FFFFF) << (21 * k);
        int tex = -1, mode = 0, ppw = 2, abr = 0;
        int tpx = 0, tpy = 256;
        bool own_page = false;
        if (face.textured) {
          const auto tp = assets::decode_tpage(face.tpage);
          const auto cl = assets::decode_clut(face.clut);
          mode = tp.mode;
          ppw = mode == 0 ? 4 : 2;
          tpx = tp.x;
          tpy = tp.y;
          abr = tp.abr;
          own_page = tpy < 256 || tpy >= 512;
          const TexKey tex_key = own_page ? TexKey{mode, cl[0], cl[1], tpx, tpy} : TexKey{mode, cl[0], cl[1]};
          auto it = std::find(keys.begin(), keys.end(), tex_key);
          if (it == keys.end()) {
            keys.push_back(tex_key);
            it = keys.end() - 1;
          }
          tex = static_cast<int>(it - keys.begin());
        }
        const int tex_width = own_page ? 64 * ppw : asset.atlas_width_hw * ppw;
        u32 corner[4] = {};
        for (int k = 0; k < face.n; ++k) {
          const size_t ks = static_cast<size_t>(k);
          const auto& colour = face.rgb[ks];
          int u = 0, v = 0;
          if (face.textured) {
            u = own_page ? face.uv[ks][0] : (tpx - asset.atlas_x0_hw) * ppw + face.uv[ks][0];
            v = own_page ? face.uv[ks][1] : (tpy - 256) + face.uv[ks][1];
          }
          const u64 k1 = static_cast<u64>(face.idx[ks]) | (static_cast<u64>(colour[0]) << 16) | (static_cast<u64>(colour[1]) << 24) |
                         (static_cast<u64>(colour[2]) << 32) | (static_cast<u64>(face.textured) << 40) |
                         (static_cast<u64>(mode & 3) << 41);
          const u64 k2 = face.textured ? (static_cast<u64>(static_cast<u32>(u)) << 32) | static_cast<u32>(v) : 0;
          const std::array<u64, 3> key{k1, k2, k3};
          auto it = vertex_map.find(key);
          if (it == vertex_map.end()) {
            assets::StageVertex sv;
            const auto& pos = mesh.verts[face.idx[ks]];
            sv.pos = {static_cast<float>(pos[0]), -static_cast<float>(pos[1]), static_cast<float>(pos[2])};
            sv.centre = centroid;
            const float colour_scale = face.textured ? 128.0f : 255.0f;
            sv.color = {colour[0] / colour_scale, colour[1] / colour_scale, colour[2] / colour_scale};
            if (face.textured) {
              sv.uv = {(static_cast<float>(u) + 0.5f) / static_cast<float>(tex_width), 1.0f - (static_cast<float>(v) + 0.5f) / 256.0f};
              sv.uv_raw = {static_cast<float>(face.uv[ks][0]), static_cast<float>(face.uv[ks][1])};
              sv.uv_scale = {1.0f / static_cast<float>(tex_width), 1.0f / 256.0f};
            }
            it = vertex_map.emplace(key, static_cast<u32>(part.vertices.size())).first;
            part.vertices.push_back(sv);
          }
          corner[k] = it->second;
        }
        const int key = (tex + 1) * 8 + (face.semi ? 4 : 0) + (face.textured ? abr : 0);
        if (key != cur_key) {
          cur_key = key;
          assets::StageSubMesh submesh;
          submesh.texture = tex;
          submesh.first_index = static_cast<u32>(part.indices.size());
          submesh.semi = face.semi;
          submesh.abr = face.textured ? abr : 0;
          part.submeshes.push_back(submesh);
        }
        static constexpr int kTri[3] = {0, 1, 2}, kQuad[6] = {0, 1, 2, 2, 1, 3};
        const int* order = face.n == 3 ? kTri : kQuad;
        for (int tri = 0; tri < (face.n == 3 ? 3 : 6); tri += 3) {
          part.indices.push_back(corner[order[tri]]);
          part.indices.push_back(corner[order[tri + 1]]);
          part.indices.push_back(corner[order[tri + 2]]);
          part.submeshes.back().index_count += 3;
        }
      }
      asset.parts.push_back(std::move(part));
    } else if (entry.type == kTCam) {
      asset.cameras.push_back(parse_camera(entry));
    } else if (entry.type == kTSeq) {
      asset.sequences.push_back(parse_sequence(entry));
    } else if (entry.type == kTLenz) {
      asset.lens.push_back(parse_lenz(entry));
    }
  }
  for (const auto& entry : parsed.drp.entries)
    if (entry.type == kTEmit) {
      assets::StageEmitter emitter;
      emitter.name = entry.name;
      emitter.mesh = emitter_mesh_ref(entry.data, mesh_names);
      parse_emitter_placement(entry.data, emitter, mesh_names);
      if (!emitter.mesh.empty())
        for (auto& part : asset.parts)
          if (part.name == emitter.mesh) part.emitters.push_back(emitter.name);
      asset.emitters.push_back(std::move(emitter));
    }

  for (const TexKey& tex_key : keys) {
    assets::StageTexture texture;
    texture.mode = tex_key.mode;
    texture.clut_x = tex_key.cx;
    texture.clut_y = tex_key.cy;
    const int ppw = tex_key.mode == 0 ? 4 : 2, colours = tex_key.mode == 0 ? 16 : 256;
    std::vector<u32> palette(static_cast<size_t>(colours));
    for (int i = 0; i < colours; ++i) palette[static_cast<size_t>(i)] = bgr555_to_rgba(parsed.vram->at(tex_key.cx + i, tex_key.cy));
    const int atlas_x0 = tex_key.px >= 0 ? tex_key.px : asset.atlas_x0_hw, atlas_w = tex_key.px >= 0 ? 64 : asset.atlas_width_hw,
              atlas_y0 = tex_key.px >= 0 ? tex_key.py : 256;
    texture.image.width = static_cast<u32>(atlas_w * ppw);
    texture.image.height = 256;
    texture.image.pixels.resize(static_cast<size_t>(texture.image.width) * 256 * 4);
    for (int y = 0; y < 256; ++y)
      for (int i = 0; i < atlas_w; ++i) {
        const u16 halfword = parsed.vram->at(atlas_x0 + i, atlas_y0 + y);
        for (int texel = 0; texel < ppw; ++texel) {
          const u32 colour_index = tex_key.mode == 0 ? (halfword >> (4 * texel)) & 15u : (halfword >> (8 * texel)) & 255u;
          store_rgba(&texture.image.pixels[(static_cast<size_t>(y) * texture.image.width + static_cast<size_t>(i * ppw + texel)) * 4],
                     palette[colour_index]);
        }
      }
    asset.textures.push_back(std::move(texture));
  }
  {
    bool any = false;
    for (int y = 0; y < 256 && !any; ++y)
      for (int x = 0; x < 128; ++x)
        if (parsed.vram->at(320 + x, 256 + y)) {
          any = true;
          break;
        }
    if (any) {
      const std::vector<u16> palette = parsed.vram->read_clut(0, 239, 256);
      asset.env_rgba.resize(256u * 256u * 4u);
      for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
          const u32 rgba = bgr555_to_rgba_stp(parsed.vram->texel(640 + x, 256 + y, 8, palette));
          std::memcpy(&asset.env_rgba[(static_cast<size_t>(y) * 256u + static_cast<size_t>(x)) * 4u], &rgba, 4);
        }
    }
  }
  return asset;
}

}  // namespace

std::optional<StageMeshSrc> parse_stage_mesh(ByteSpan data, std::string* err) {
  try {
    return parse_mesh_impl(data);
  } catch (const std::exception& exception) {
    if (err) *err = exception.what();
    return std::nullopt;
  }
}

std::optional<assets::StageAsset> load_stage(ByteSpan prd, ByteSpan ctd, std::string* err) {
  try {
    Parsed parsed;
    parse_container(parsed, prd, ctd);
    return build_asset(parsed);
  } catch (const std::exception& exception) {
    if (err) *err = exception.what();
    return std::nullopt;
  }
}

StageValidation validate_stage(ByteSpan prd, ByteSpan ctd) {
  StageValidation validation;
  try {
    Parsed parsed;
    parse_container(parsed, prd, ctd);
    // Loaded-image coverage and CLUT rows provided by TIMs / ctd.
    std::vector<u8> cover(static_cast<size_t>(Vram::kWidth) * Vram::kHeight, 0);
    std::set<std::pair<int, int>> cluts;
    auto at = [](int x, int y) {
      return static_cast<size_t>(y & (Vram::kHeight - 1)) * Vram::kWidth + static_cast<size_t>(x & (Vram::kWidth - 1));
    };
    auto mark = [&](int x, int y, int w, int h) {
      for (int row = 0; row < h; ++row)
        for (int column = 0; column < w; ++column) cover[at(x + column, y + row)] = 1;
    };
    auto add_cluts = [&](int x, int y, int w, int h) {
      for (int row = 0; row < h; ++row)
        for (int column = 0; column < w; column += 16) cluts.insert({x + column, y + row});
    };
    for (const auto& tim : parsed.tims) {
      const auto& image = tim.second.image;
      mark(image.x, image.y, image.w, image.h);
      if (tim.second.has_clut) add_cluts(tim.second.clut.x, tim.second.clut.y, tim.second.clut.w, tim.second.clut.h);
      if (image.y < 256 || image.y + image.h > 512)
        validation.errors.push_back("tim " + parsed.drp.entries[tim.first].name + " outside rows 256..511");
    }
    for (const auto& section : parsed.ctd) {
      if (section.type == 0x1201) {
        add_cluts(section.x, section.y, section.w, section.h);
      } else {
        mark(section.x, section.y, section.w, section.h);
        if (section.y < 256 || section.y + section.h > 512) validation.errors.push_back("ctd image outside rows 256..511");
      }
    }
    const AtlasExtent extent = atlas_extent(parsed);
    validation.atlas_x_lo = static_cast<u32>(extent.lo);
    validation.atlas_x_hi = static_cast<u32>(extent.hi);
    validation.tims = static_cast<u32>(parsed.tims.size());
    for (const auto& entry : parsed.drp.entries) {
      try {
        if (entry.type == kTMesh || entry.type == kTRing) {
          const StageMeshSrc mesh = parse_mesh_impl(entry.data);
          ++(entry.type == kTRing ? validation.ground_meshes : validation.meshes);
          validation.source_vertices += static_cast<u32>(mesh.verts.size());
          validation.source_faces += static_cast<u32>(mesh.faces.size());
          for (const StageFace& face : mesh.faces) {
            if (!face.textured) continue;
            const auto tp = assets::decode_tpage(face.tpage);
            const auto cl = assets::decode_clut(face.clut);
            if ((tp.mode != 0 && tp.mode != 1) || tp.y != 256) {
              validation.errors.push_back(entry.name + " tpage " + std::to_string(face.tpage));
              break;
            }
            if (!cluts.count({cl[0], cl[1]})) {
              validation.errors.push_back(entry.name + " clut not provided");
              break;
            }
            const int ppw = tp.mode == 0 ? 4 : 2;
            bool ok = true;
            for (int k = 0; k < face.n; ++k) {
              const size_t ks = static_cast<size_t>(k);
              const int hx = tp.x + face.uv[ks][0] / ppw, hy = tp.y + face.uv[ks][1];
              if (!cover[at(hx, hy)] || hx < extent.x0 || hx >= extent.x0 + extent.width_hw) ok = false;
            }
            if (!ok) {
              validation.errors.push_back(entry.name + " uv on unloaded VRAM / outside atlas");
              break;
            }
          }
        } else if (entry.type == kTCam) {
          ++validation.cameras;
          if (parse_camera(entry).keys.empty()) validation.errors.push_back("camera " + entry.name + " empty");
        } else if (entry.type == kTSeq) {
          ++validation.sequences;
          const auto sequence = parse_sequence(entry);
          if (sequence.commands.empty() || sequence.commands.back().op != 0x21 || !sequence.trailing_is_pad)
            validation.errors.push_back("sequence " + entry.name + " not terminated by 0x21 at end");
        } else if (entry.type == kTLenz) {
          ++validation.lens;
          parse_lenz(entry);
        }
      } catch (const std::exception& exception) {
        validation.errors.push_back(entry.name + ": " + exception.what());
      }
    }
  } catch (const std::exception& exception) {
    validation.errors.push_back(exception.what());
  }
  return validation;
}

}  // namespace oc::import
