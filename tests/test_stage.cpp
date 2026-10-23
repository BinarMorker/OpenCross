// Checks for the battle stage importer on a hand-built stage: a 2-face packet mesh (F4 + GT3, 4 vertices), one 8bpp TIM,
// one camera and one sequence in a drp; tpage / CLUT word decoding and the atlas mapping.
#include <cmath>
#include <cstdio>

#include "game/free_cam.h"
#include "import/battle_stage.h"
#include "render/camera.h"

using namespace oc;

namespace {
int failures = 0;
#define CHECK(x)                                              \
  do {                                                        \
    if (!(x)) {                                               \
      std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); \
      ++failures;                                             \
    }                                                         \
  } while (0)

bool near(float lhs, float rhs, float eps = 1e-4f) { return std::fabs(lhs - rhs) <= eps; }

void p16(Bytes& out, int value) {
  out.push_back(static_cast<u8>(value & 0xFF));
  out.push_back(static_cast<u8>((value >> 8) & 0xFF));
}
void p32(Bytes& out, u32 value) {
  p16(out, static_cast<int>(value & 0xFFFF));
  p16(out, static_cast<int>(value >> 16));
}

// kTpage: x 448 halfwords, y 256, 8bpp. kClut: CLUT at (0, 224).
constexpr int kTpage = 0x0097;
constexpr int kClut = 224 << 6;

// Packet mesh: 4 vertices, 2 faces (vertex table at 0x50, faces at 0x28), two groups of one face each: an F4 (colour
// 0x80 0x40 0x20, indices 0..3) and a GT3 (three colours, texel block with kClut / kTpage, indices 0..2).
Bytes build_mesh() {
  Bytes mesh;
  p32(mesh, 0x80000001);
  p16(mesh, 4);
  p16(mesh, 2);
  p32(mesh, 0x50);
  p32(mesh, 0x28);
  p32(mesh, 0x14);
  p32(mesh, 2);
  p16(mesh, 0x28); p16(mesh, 1); p32(mesh, 0x28);
  p16(mesh, 0x34); p16(mesh, 1); p32(mesh, 0x34);
  mesh.insert(mesh.end(), {0x80, 0x40, 0x20, 0x28});
  p16(mesh, 0); p16(mesh, 1); p16(mesh, 2); p16(mesh, 3);
  mesh.insert(mesh.end(), {0x80, 0x80, 0x80, 0x34, 0xA0, 0x90, 0x80, 0, 0x80, 0x80, 0x80, 0});
  mesh.insert(mesh.end(), {4, 8});  p16(mesh, kClut);
  mesh.insert(mesh.end(), {12, 8}); p16(mesh, kTpage);
  mesh.insert(mesh.end(), {4, 16});
  p16(mesh, 0); p16(mesh, 1); p16(mesh, 2);
  const int vertices[4][3] = {{0, 0, 0}, {100, 0, 0}, {0, -50, 0}, {100, -50, 25}};
  for (const auto& vertex : vertices) { p16(mesh, vertex[0]); p16(mesh, vertex[1]); p16(mesh, vertex[2]); p16(mesh, 0); }
  return mesh;
}

// 8bpp TIM with a 256-entry CLUT at (0, 224) whose index 3 is pure red; a 4 x 4 halfword image at (448, 256) of texel index 3.
Bytes build_tim() {
  Bytes tim;
  p32(tim, 0x10);
  p32(tim, 9);
  p32(tim, 12 + 512);
  p16(tim, 0); p16(tim, 224); p16(tim, 256); p16(tim, 1);
  for (int i = 0; i < 256; ++i) p16(tim, i == 3 ? 0x001F : (i == 0 ? 0 : 0x7FFF));
  p32(tim, 12 + 4 * 4 * 2);
  p16(tim, 448); p16(tim, 256); p16(tim, 4); p16(tim, 4);
  for (int i = 0; i < 16; ++i) p16(tim, 0x0303);
  return tim;
}

// One camera of duration 30 with one key: eye (1000, -2000, 3000), h 512, target (0, -100, 0), roll 256 (= 90 degrees).
Bytes build_camera() {
  Bytes camera;
  p32(camera, 0); p16(camera, 30); p16(camera, 1); p32(camera, 0x01000000); p32(camera, 0);
  p32(camera, 1);
  p32(camera, 0); p16(camera, 1000); p16(camera, -2000); p16(camera, 3000); p16(camera, 512);
  p16(camera, 0); p16(camera, -100); p16(camera, 0); p16(camera, 256);
  return camera;
}

Bytes build_seq() {
  Bytes seq(0x14, 0);
  p32(seq, 0x20 | (4 << 9));
  p32(seq, 0x21 | (8 << 9)); p32(seq, 0);
  return seq;
}

Bytes build_drp() {
  struct Entry { const char* name; u8 type; Bytes data; };
  const std::vector<Entry> entries = {{"far1", 0x02, build_mesh()}, {"tex0", 0x04, build_tim()}, {"zen0", 0x0C, build_camera()},
                                      {"bti0", 0x10, build_seq()}};
  Bytes drp = {'d', 'r', 'p', 0, 0, 0, 0, 0};
  p32(drp, static_cast<u32>(entries.size()) * 64);
  size_t next_offset = 12 + 4 * entries.size();
  std::vector<size_t> offsets;
  for (const auto& entry : entries) { offsets.push_back(next_offset); next_offset += 12 + entry.data.size(); }
  for (size_t offset : offsets) p32(drp, static_cast<u32>(offset));
  for (const auto& entry : entries) {
    p32(drp, 0);
    for (int i = 0; i < 4; ++i) drp.push_back(static_cast<u8>(entry.name[i]));
    drp.push_back(entry.type);
    const u32 len = static_cast<u32>(entry.data.size()) * 16;
    drp.push_back(static_cast<u8>(len)); drp.push_back(static_cast<u8>(len >> 8)); drp.push_back(static_cast<u8>(len >> 16));
    drp.insert(drp.end(), entry.data.begin(), entry.data.end());
  }
  return drp;
}

// tpage / CLUT word decoding: 0x0097 = x 448, y 256, 8bpp, abr 0; 0x0015 | 2 << 5 = x 320, 4bpp, abr 2; 239 << 6 | 16 = CLUT x 256, row 239.
void test_words() {
  const auto tpage = assets::decode_tpage(kTpage);
  CHECK(tpage.x == 448 && tpage.y == 256 && tpage.mode == 1 && tpage.abr == 0);
  const auto tpage_4bpp = assets::decode_tpage(0x0015 | (2 << 5));
  CHECK(tpage_4bpp.x == 320 && tpage_4bpp.y == 256 && tpage_4bpp.mode == 0 && tpage_4bpp.abr == 2);
  const auto clut = assets::decode_clut(kClut);
  CHECK(clut[0] == 0 && clut[1] == 224);
  const auto clut_row239 = assets::decode_clut((239 << 6) | 16);
  CHECK(clut_row239[0] == 256 && clut_row239[1] == 239);
}

// parse_stage_mesh on the packet mesh; a code byte that no longer matches its group type is refused.
void test_mesh() {
  const Bytes mesh_bytes = build_mesh();
  std::string err;
  auto src = import::parse_stage_mesh(mesh_bytes, &err);
  CHECK(src.has_value());
  if (!src) return;
  CHECK(src->verts.size() == 4 && src->faces.size() == 2);
  CHECK(src->faces[0].n == 4 && !src->faces[0].textured && src->faces[0].rgb[3][0] == 0x80 && src->faces[0].rgb[0][1] == 0x40);
  CHECK(src->faces[1].n == 3 && src->faces[1].textured && src->faces[1].clut == kClut && src->faces[1].tpage == kTpage);
  CHECK(src->faces[1].uv[1][0] == 12 && src->faces[1].uv[2][1] == 16 && src->faces[1].rgb[1][0] == 0xA0);
  CHECK(src->verts[3][1] == -50 && src->verts[3][2] == 25);
  Bytes bad_code = mesh_bytes;
  bad_code[0x28 + 3] = 0x30;
  CHECK(!import::parse_stage_mesh(bad_code, &err).has_value());
}

// load_stage on the drp: the quad becomes two triangles and the triangle one (9 indices), corners are split per
// (index, colour, uv) (4 + 3 vertices), y points up, untextured corners carry raw colour / 255, the packet corner order
// (0,1,2)(2,1,3) is kept because the renderer culls back faces as the PS1 NCLIP does, textured corners carry the packet
// texels and the texel size for the texture-window scroll; the atlas starts at x 448 and is 4 halfwords wide (the minimum
// span) = 8 x 256 px with index 3 decoded red; the GT3 uvs are texel centres (u + 0.5) / W (corner 5 lies beyond the tiny
// atlas on purpose); the camera key and the sequence decode; junk is refused.
void test_stage() {
  const Bytes drp = build_drp();
  std::string err;
  auto st = import::load_stage(drp, {}, &err);
  CHECK(st.has_value());
  if (!st) { std::printf("  %s\n", err.c_str()); return; }
  CHECK(st->parts.size() == 1 && st->parts[0].name == "far1" && !st->parts[0].ground);
  const auto& part = st->parts[0];
  CHECK(part.source_vertices == 4 && part.source_faces == 2);
  CHECK(part.indices.size() == 9);
  CHECK(part.vertices.size() == 7);
  CHECK(part.submeshes.size() == 2 && part.submeshes[0].texture == -1 && part.submeshes[0].index_count == 6 &&
        part.submeshes[1].texture == 0 && part.submeshes[1].first_index == 6 && part.submeshes[1].index_count == 3);
  CHECK(near(part.vertices[3].pos[1], 50.0f) && near(part.vertices[3].pos[2], 25.0f));
  CHECK(near(part.vertices[0].color[0], 128.0f / 255.0f) && near(part.vertices[0].color[1], 64.0f / 255.0f));
  CHECK(part.indices[0] == 0 && part.indices[1] == 1 && part.indices[2] == 2 && part.indices[3] == 2 && part.indices[4] == 1 && part.indices[5] == 3);
  CHECK(near(part.vertices[4].uv_raw[0], 4.0f) && near(part.vertices[4].uv_raw[1], 8.0f) && near(part.vertices[4].uv_scale[1], 1.0f / 256.0f));
  CHECK(part.vertices[0].uv_scale[0] == 0.0f && !part.submeshes[1].semi && part.submeshes[1].abr == 0);
  CHECK(st->atlas_x0_hw == 448 && st->atlas_width_hw == 4);
  CHECK(st->textures.size() == 1 && st->textures[0].mode == 1 && st->textures[0].clut_x == 0 && st->textures[0].clut_y == 224);
  CHECK(st->find_texture(1, 0, 224) == 0 && st->find_texture(0, 0, 224) == -1);
  const auto& img = st->textures[0].image;
  CHECK(img.width == 8 && img.height == 256);
  CHECK(img.pixels[0] == 0xFF && img.pixels[1] == 0 && img.pixels[2] == 0 && img.pixels[3] == 0xFF);
  const auto& textured_corner = part.vertices[4];
  CHECK(near(textured_corner.uv[0], 4.5f / 8) && near(textured_corner.uv[1], 1.0f - 8.5f / 256));
  CHECK(near(part.vertices[5].uv[0], 12.5f / 8));
  CHECK(st->cameras.size() == 1 && st->cameras[0].keys.size() == 1 && st->cameras[0].duration == 30);
  const auto& key = st->cameras[0].keys[0];
  CHECK(near(key.eye[0], 1000) && near(key.eye[1], 2000) && near(key.eye[2], 3000) && near(key.projection, 512));
  CHECK(near(key.target[1], 100) && near(key.roll_deg, 90.0f));
  CHECK(st->sequences.size() == 1 && st->sequences[0].commands.size() == 2 && st->sequences[0].commands[1].op == 0x21 &&
        st->sequences[0].trailing == 0);
  CHECK(!import::load_stage(Bytes{1, 2, 3}, {}, &err).has_value());
}

}  // namespace

// The culling frame: identical to the PS1 constants (x -1024..1023) at extra width 0, so every decision of a sweep matches
// the literal rule; at 32:9 (extra 260 columns) the x limits scale about x = 160 by 1 + 260 / 160 while y stays.
void test_cull_frame() {
  const render::CullFrame frame_4x3 = render::cull_frame(0.0f);
  CHECK(frame_4x3.x_lo == -1024.0f && frame_4x3.x_hi == 1023.0f && frame_4x3.x_span == 1023.0f);
  for (float value = -1100.0f; value <= 1100.0f; value += 0.5f) {
    const bool literal_ok = !(value < -1024.0f || value > 1023.0f);
    CHECK(render::cull_screen_ok(frame_4x3, value, 0.0f) == literal_ok);
    CHECK(render::cull_screen_ok(frame_4x3, 0.0f, value) == literal_ok);
  }
  CHECK(render::cull_span_ok(frame_4x3, 1023.0f, 511.0f) && !render::cull_span_ok(frame_4x3, 1023.5f, 0) && !render::cull_span_ok(frame_4x3, 0, 511.5f));
  const render::CullFrame frame_wide = render::cull_frame(260.0f);
  CHECK(frame_wide.x_lo < -1024.0f && frame_wide.x_hi > 1023.0f && frame_wide.x_span > 1023.0f);
  CHECK(near(frame_wide.x_span, 1023.0f * (1.0f + 260.0f / 160.0f), 1e-2f));
  CHECK(render::cull_screen_ok(frame_wide, 1500.0f, 0.0f) && !render::cull_screen_ok(frame_4x3, 1500.0f, 0.0f));
  CHECK(!render::cull_screen_ok(frame_wide, 0.0f, 1024.0f));
}

// The PS1 packet-drop rules are bypassed only while the free camera is on.
void test_cull_bypass() {
  CHECK(!render::cull_bypass());
  game::free_cam().set_on(true);
  CHECK(render::cull_bypass());
  game::free_cam().set_on(false);
  CHECK(!render::cull_bypass());
  CHECK(render::kBypassNear > 0.0f);
}

int main() {
  test_words();
  test_mesh();
  test_stage();
  test_cull_frame();
  test_cull_bypass();
  if (failures) std::printf("%d failure(s)\n", failures);
  else std::printf("stage tests ok\n");
  return failures ? 1 : 0;
}
