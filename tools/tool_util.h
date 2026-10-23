// Helpers shared by the developer tools (tools/*.cpp): game-dir discovery, room file paths, PNG / OBJ writers.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "core/types.h"

namespace oc::vfs {
class Vfs;
}
namespace oc::assets {
struct ModelAsset;
}

namespace oc::tool {

// $OPENCROSS_GAMEDIR, or "" when unset.
std::string env_game_dir();

// "cdrom/map/mapbin/<name><ext>" (ext includes the dot).
std::string room_file(const std::string& name, const char* ext);

// Sorted lower-case stems of every cdrom/map/mapbin/<stem><ext> member that is not in a sub-directory; ext is a 4-character
// extension such as ".bin".
std::vector<std::string> room_stems(const vfs::Vfs& fs, const std::string& ext);

std::string lower_ascii(std::string text);

// RGBA8 PNG through stb_image_write (the implementation is linked from oc_render).
bool write_png(const std::string& path, u32 width, u32 height, const Bytes& rgba);

// Wavefront OBJ body of a model's rest pose with y up (bind y negated): one `v` per pool vertex, one `vt` per mesh vertex, `f` lines
// indexed pool/uv across all constructs.
void write_obj_rest_pose(FILE* file, const assets::ModelAsset& model);

}  // namespace oc::tool
