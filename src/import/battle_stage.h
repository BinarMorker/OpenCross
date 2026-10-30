#pragma once
// Battle stage importer: battle/bg/bg_NN.prd (+ optional .ctd) -> assets::StageAsset.
// Nothing throws across the API; failures come back as std::nullopt plus a message.
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "assets/battle_stage.h"
#include "core/types.h"

namespace oc::import {

// Packet mesh (types 0x02 / 0x12) in source form, before corner splitting.
struct StageFace {
  u8 code = 0;                       // group type (GPU primitive code without the semi-transparent bit)
  bool semi = false;
  u8 n = 3;                          // corners
  std::array<u16, 4> idx{};
  std::array<std::array<u8, 3>, 4> rgb{};  // per corner (flat packets repeat colour 0)
  bool textured = false;
  std::array<std::array<u8, 2>, 4> uv{};
  u16 clut = 0, tpage = 0;
};
struct StageMeshSrc {
  std::vector<std::array<s16, 3>> verts;
  std::vector<StageFace> faces;
};
std::optional<StageMeshSrc> parse_stage_mesh(ByteSpan data, std::string* err = nullptr);
// The six points of the object screen test (assets::StagePart::cull_points) from the vertex array: per-axis maximum and minimum
// (starting from -0x7FFF / 0x7FFF), the centre (max + min) / 2 rounded toward zero.
std::array<std::array<s16, 3>, 6> mesh_cull_points(const std::vector<std::array<s16, 3>>& verts);

// `ctd` may be empty. The returned asset is complete (parts, atlases, cameras, sequences, lens, emitters).
std::optional<assets::StageAsset> load_stage(ByteSpan prd, ByteSpan ctd, std::string* err = nullptr);

struct StageValidation {
  std::vector<std::string> errors;
  u32 meshes = 0, ground_meshes = 0, source_vertices = 0, source_faces = 0;
  u32 tims = 0, cameras = 0, sequences = 0, lens = 0;
  u32 atlas_x_lo = 0, atlas_x_hi = 0;  // loaded image extent (halfwords), 0/0 if none
  bool ok() const { return errors.empty(); }
};
// Load plus the format checks (indices, packet codes, tpage mode/rows, CLUT rows provided, UVs on loaded VRAM and
// inside the atlas, TIM rows 256..511, camera size, sequence terminator). Never throws.
StageValidation validate_stage(ByteSpan prd, ByteSpan ctd);

}  // namespace oc::import
