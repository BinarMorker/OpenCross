#pragma once
// CPU-only policy and math for the native model viewer. No UI, GPU or save state.
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "assets/model.h"
#include "render/mat4.h"

namespace oc::game::viewer {

enum class Kind { Field, BattlePlayer, BattleMonster };
enum class Variant { Original, Modified };
struct Entry {
  std::string stem; // logical member stem, without archive or .obj
  Kind kind = Kind::Field;
};
std::vector<Entry> catalog(const std::vector<std::string>& sd, const std::vector<std::string>& hd);
const char* kind_name(Kind kind);
// filter: 0 all, 1 field, 2 battle players, 3 battle monsters.
bool matches(const Entry& entry, int filter, const std::string& query);

struct ResourcePaths {
  std::string obj, texture, texture_fallback, extra, hd;
};
ResourcePaths resource_paths(const Entry& entry);
using Reader = std::function<std::optional<Bytes>(const std::string&)>;
struct ResourceData {
  Bytes obj, texture, extra, hd;
  std::string texture_path, error;
};
// The reader must resolve every logical cdrom path through the SAME art-aware VFS.
ResourceData read_resources(const Reader& read, const Entry& entry, bool hd);
constexpr bool resolve_replacements(Variant variant) { return variant == Variant::Modified; }
// Shared preparation gate used immediately before GPU upload. The resolver is
// the engine's texture-replacement pass, supplied by the native tool.
void prepare_textures(assets::ModelAsset& asset, Variant variant,
                      const std::function<void(assets::ModelAsset&)>& resolver);

// Match identity, not vector position: s4[0] and ex[0] are different clips.
// Missing or ambiguous identity returns -1; the caller must show a warning.
int matching_clip(const std::vector<assets::AnimationClip>& clips, const assets::AnimationClip& reference);
double playback_frame(double seconds, u32 frame_count, bool loop);

struct Bounds {
  assets::Vec3 min{}, max{};
  bool valid = false;
};
Bounds asset_bounds(const assets::ModelAsset& asset);
Bounds bounds_union(const Bounds& a, const Bounds& b);
assets::Vec3 center(const Bounds& bounds);
float radius(const Bounds& bounds);

struct CameraMatrices { render::Mat4 view, projection; };
// Model-space Y points down, camera-space depth is positive, as MeshPipeline expects.
CameraMatrices camera_matrices(const Bounds& bounds, float yaw, float pitch, float zoom, float aspect);

struct Options {
  std::string game_dir, model, error;
  bool hd = true, help = false;
};
Options parse_options(const std::vector<std::string>& args);

} // namespace oc::game::viewer
