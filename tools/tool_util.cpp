// Helpers shared by the developer tools (see tool_util.h).
#include "tool_util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

#include "assets/model.h"
#include "vfs/vfs.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace oc::tool {

std::string env_game_dir() {
  const char* dir = std::getenv("OPENCROSS_GAMEDIR");
  return dir ? dir : "";
}

std::string room_file(const std::string& name, const char* ext) { return "cdrom/map/mapbin/" + name + ext; }

std::vector<std::string> room_stems(const vfs::Vfs& fs, const std::string& ext) {
  std::vector<std::string> stems;
  for (std::string path : fs.list("cdrom")) {
    std::replace(path.begin(), path.end(), '\\', '/');
    path = lower_ascii(path);
    const size_t at = path.find("map/mapbin/");
    if (at == std::string::npos || path.size() < at + 16 || !path.ends_with(ext)) continue;
    const std::string stem = path.substr(at + 11, path.size() - at - 15);
    if (stem.find('/') == std::string::npos) stems.push_back(stem);
  }
  std::sort(stems.begin(), stems.end());
  return stems;
}

std::string lower_ascii(std::string text) {
  for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return text;
}

bool write_png(const std::string& path, u32 width, u32 height, const Bytes& rgba) {
  auto sink = [](void* ctx, void* data, int size) { static_cast<std::ofstream*>(ctx)->write(static_cast<const char*>(data), size); };
  std::ofstream file(path, std::ios::binary);
  if (!file) return false;
  const bool ok = stbi_write_png_to_func(sink, &file, static_cast<int>(width), static_cast<int>(height), 4, rgba.data(),
                                         static_cast<int>(width * 4)) != 0;
  return ok && static_cast<bool>(file);
}

void write_obj_rest_pose(FILE* file, const assets::ModelAsset& model) {
  for (const auto& mesh : model.meshes)
    for (const auto& vert : mesh.pool)
      std::fprintf(file, "v %.3f %.3f %.3f\n", static_cast<double>(vert.bind_pos[0]), static_cast<double>(-vert.bind_pos[1]),
                   static_cast<double>(vert.bind_pos[2]));
  for (const auto& mesh : model.meshes)
    for (const auto& vert : mesh.vertices)
      std::fprintf(file, "vt %.5f %.5f\n", static_cast<double>(vert.uv[0]), static_cast<double>(1 - vert.uv[1]));
  size_t pool_base = 0, vertex_base = 0;
  for (const auto& mesh : model.meshes) {
    for (size_t tri = 0; tri + 2 < mesh.indices.size(); tri += 3) {
      std::fputc('f', file);
      for (size_t k = 0; k < 3; ++k) {
        const u32 index = mesh.indices[tri + k];
        std::fprintf(file, " %zu/%zu", pool_base + mesh.vertices[index].pool + 1, vertex_base + index + 1);
      }
      std::fputc('\n', file);
    }
    pool_base += mesh.pool.size();
    vertex_base += mesh.vertices.size();
  }
}

}  // namespace oc::tool
