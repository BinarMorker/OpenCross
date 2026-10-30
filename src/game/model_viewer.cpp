#include "game/model_viewer.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace oc::game::viewer {
namespace {
std::string lower(std::string s) {
  for (char& c : s) {
    if (c == '\\') c = '/';
    else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}
bool finite(const assets::Vec3& p) {
  return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}
} // namespace

std::vector<Entry> catalog(const std::vector<std::string>& sd, const std::vector<std::string>& hd) {
  std::map<std::string, Kind> unique;
  for (const auto* members : {&sd, &hd})
    for (const auto& member : *members) {
      auto name = lower(member);
      if (!name.ends_with(".obj") || name.find("..") != std::string::npos) continue;
      Kind kind;
      if (name.starts_with("fieldobj/player/")) kind = Kind::Field;
      else if (name.starts_with("battle/player/")) kind = Kind::BattlePlayer;
      else if (name.starts_with("battle/monster/")) kind = Kind::BattleMonster;
      else continue;
      name.resize(name.size() - 4);
      unique.emplace(std::move(name), kind);
    }
  std::vector<Entry> result;
  for (const auto& [name, kind] : unique) result.push_back({name, kind});
  return result;
}

const char* kind_name(Kind kind) {
  switch (kind) {
    case Kind::Field: return "Field character";
    case Kind::BattlePlayer: return "Battle player";
    case Kind::BattleMonster: return "Battle monster";
  }
  return "Unknown";
}

bool matches(const Entry& entry, int filter, const std::string& query) {
  if (filter != 0 && filter != static_cast<int>(entry.kind) + 1) return false;
  return entry.stem.find(lower(query)) != std::string::npos;
}

ResourcePaths resource_paths(const Entry& entry) {
  const auto slash = entry.stem.find_last_of('/');
  const auto dir = entry.stem.substr(0, slash);
  const auto leaf = entry.stem.substr(slash == std::string::npos ? 0 : slash + 1);
  const auto base = "cdrom/" + entry.stem;
  ResourcePaths p;
  p.obj = base + ".obj";
  p.hd = "hd/" + entry.stem + ".tim";
  if (entry.kind == Kind::Field) {
    p.texture = base + ".tim";
    p.extra = "cdrom/fieldobj/ex/" + leaf + ".bin";
  } else {
    p.texture = base + ".ltd";
    p.texture_fallback = base + ".ltc";
    p.extra = "cdrom/" + dir + "/at0.prd";
  }
  return p;
}

ResourceData read_resources(const Reader& read, const Entry& entry, bool hd) {
  const auto paths = resource_paths(entry);
  ResourceData out;
  auto get = [&](const std::string& path) {
    if (path.empty()) return Bytes{};
    const auto bytes = read(path);
    return bytes ? *bytes : Bytes{};
  };
  out.obj = get(paths.obj);
  if (out.obj.empty()) {
    out.error = "Model not found: " + paths.obj;
    return out;
  }
  out.texture = get(paths.texture);
  out.texture_path = paths.texture;
  if (out.texture.empty() && !paths.texture_fallback.empty()) {
    out.texture = get(paths.texture_fallback);
    out.texture_path = paths.texture_fallback;
  }
  if (out.texture.empty()) {
    out.error = "Texture not found for " + entry.stem;
    return out;
  }
  out.extra = get(paths.extra);
  if (hd) out.hd = get(paths.hd);
  return out;
}

int matching_clip(const std::vector<assets::AnimationClip>& clips, const assets::AnimationClip& reference) {
  int match = -1;
  for (size_t i = 0; i < clips.size(); ++i)
    if (clips[i].source == reference.source && clips[i].index == reference.index) {
      if (match >= 0) return -1;
      match = static_cast<int>(i);
    }
  return match;
}

void prepare_textures(assets::ModelAsset& asset, Variant variant,
                      const std::function<void(assets::ModelAsset&)>& resolver) {
  if (resolve_replacements(variant)) resolver(asset);
}

double playback_frame(double seconds, u32 frame_count, bool loop) {
  if (!std::isfinite(seconds) || seconds <= 0 || frame_count == 0) return 0;
  const double frame = seconds * assets::kModelAnimFps;
  if (!std::isfinite(frame)) return 0;
  if (loop) return std::fmod(frame, static_cast<double>(frame_count));
  return std::min(frame, static_cast<double>(frame_count - 1));
}

Bounds asset_bounds(const assets::ModelAsset& asset) {
  Bounds out;
  for (const auto& mesh : asset.meshes)
    for (const auto& v : mesh.pool) {
      if (!finite(v.bind_pos)) continue;
      if (!out.valid) out.min = out.max = v.bind_pos, out.valid = true;
      else for (size_t k = 0; k < 3; ++k) {
        out.min[k] = std::min(out.min[k], v.bind_pos[k]);
        out.max[k] = std::max(out.max[k], v.bind_pos[k]);
      }
    }
  return out;
}

Bounds bounds_union(const Bounds& a, const Bounds& b) {
  if (!a.valid) return b;
  if (!b.valid) return a;
  Bounds out = a;
  for (size_t k = 0; k < 3; ++k) {
    out.min[k] = std::min(a.min[k], b.min[k]);
    out.max[k] = std::max(a.max[k], b.max[k]);
  }
  return out;
}

assets::Vec3 center(const Bounds& bounds) {
  if (!bounds.valid) return {};
  assets::Vec3 c;
  for (size_t k = 0; k < 3; ++k) c[k] = bounds.min[k] * 0.5f + bounds.max[k] * 0.5f;
  return c;
}

float radius(const Bounds& bounds) {
  if (!bounds.valid) return 1.0f;
  double squared = 0;
  for (size_t k = 0; k < 3; ++k) {
    const double half = (static_cast<double>(bounds.max[k]) - bounds.min[k]) * 0.5;
    squared += half * half;
  }
  return static_cast<float>(std::max(1.0, std::sqrt(squared)));
}

CameraMatrices camera_matrices(const Bounds& bounds, float yaw, float pitch, float zoom, float aspect) {
  if (!std::isfinite(yaw)) yaw = 0;
  if (!std::isfinite(pitch)) pitch = 0;
  if (!std::isfinite(zoom)) zoom = 1;
  if (!std::isfinite(aspect) || aspect <= 0) aspect = 1;
  pitch = std::clamp(pitch, -1.4f, 1.4f);
  zoom = std::clamp(zoom, 0.35f, 10.0f);
  const auto target = center(bounds);
  const float r = radius(bounds);
  // Fit a sphere to the limiting (horizontal or vertical) field of view.
  const float half_fov = std::atan(std::tan(0.35f)*std::min(aspect,1.0f));
  const float distance = r/std::sin(half_fov)*1.1f*zoom;
  const assets::Vec3 offset{std::sin(yaw)*std::cos(pitch)*distance, -std::sin(pitch)*distance,
                            -std::cos(yaw)*std::cos(pitch)*distance};
  const assets::Vec3 forward{-offset[0]/distance, -offset[1]/distance, -offset[2]/distance};
  const assets::Vec3 right{std::cos(yaw), 0, std::sin(yaw)};
  const assets::Vec3 down{forward[1]*right[2]-forward[2]*right[1],
                         forward[2]*right[0]-forward[0]*right[2],
                         forward[0]*right[1]-forward[1]*right[0]};
  CameraMatrices result;
  const assets::Vec3 rows[] = {right, down, forward};
  for (int row = 0; row < 3; ++row) {
    result.view.at(row, 3) = 0;
    for (int col = 0; col < 3; ++col) {
      result.view.at(row, col) = rows[row][static_cast<size_t>(col)];
      result.view.at(row, 3) -= rows[row][static_cast<size_t>(col)] * (target[static_cast<size_t>(col)]+offset[static_cast<size_t>(col)]);
    }
  }
  const float near = std::max(0.01f, r*0.01f), far = std::max(near+1, distance+r*20);
  const float focal = 1.0f/std::tan(0.35f);
  result.projection.m.fill(0);
  result.projection.at(0,0) = focal/aspect;
  result.projection.at(1,1) = focal;
  result.projection.at(2,2) = far/(far-near);
  result.projection.at(2,3) = -far*near/(far-near);
  result.projection.at(3,2) = 1;
  return result;
}

Options parse_options(const std::vector<std::string>& args) {
  Options result;
  for (size_t i = 0; i < args.size(); ++i) {
    const auto& arg = args[i];
    if (arg == "--help" || arg == "-h") result.help = true;
    else if (arg == "--sd") result.hd = false;
    else if (arg == "--game-dir" || arg == "--model") {
      if (i+1 >= args.size() || args[i+1].starts_with("--")) {
        result.error = "Missing value for " + arg;
        return result;
      }
      (arg == "--game-dir" ? result.game_dir : result.model) = args[++i];
    } else {
      result.error = "Unknown argument: " + arg;
      return result;
    }
  }
  return result;
}

} // namespace oc::game::viewer
