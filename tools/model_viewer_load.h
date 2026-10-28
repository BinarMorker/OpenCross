#pragma once
#include "game/model_viewer.h"

namespace oc::tool {
struct ViewerAsset {
  std::optional<assets::ModelAsset> asset;
  game::viewer::ResourcePaths paths;
  std::string texture_path, error;
  std::vector<std::string> warnings;
};
// CPU-only loading. No global texture replacement and no filesystem writes.
ViewerAsset load_viewer_asset(const game::viewer::Reader& read, const game::viewer::Entry& entry, bool hd);
} // namespace oc::tool
