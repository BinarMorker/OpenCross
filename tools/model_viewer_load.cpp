#include "model_viewer_load.h"

#include "import/model.h"

namespace oc::tool {
ViewerAsset load_viewer_asset(const game::viewer::Reader& read, const game::viewer::Entry& entry, bool hd) {
  namespace v = game::viewer;
  ViewerAsset result;
  result.paths = v::resource_paths(entry);
  const auto bytes = v::read_resources(read, entry, hd);
  result.texture_path = bytes.texture_path;
  if (!bytes.error.empty()) {
    result.error = bytes.error;
    return result;
  }
  auto decode = [&](ByteSpan extra, std::string& error) {
    return entry.kind == v::Kind::Field
      ? import::load_field_model(bytes.obj, bytes.texture, extra, &error, bytes.hd)
      : import::load_model(bytes.obj, bytes.texture, extra, &error, bytes.hd);
  };
  result.asset = decode(bytes.extra, result.error);
  if (!result.asset && !bytes.extra.empty()) {
    const auto original_error = result.error;
    result.error.clear();
    result.asset = decode({}, result.error);
    if (result.asset) result.warnings.push_back("External animation pack was rejected: " + original_error);
  }
  if (!result.asset) {
    if (result.error.empty()) result.error = "Model importer rejected " + result.paths.obj;
    return result;
  }
  result.error.clear();
  result.asset->source = entry.stem;
  if (bytes.extra.empty()) result.warnings.push_back("No same-name external animation pack; showing internal clips only.");
  if (hd && bytes.hd.empty()) result.warnings.push_back("No matching hd.dat TIM; using this art set's base texture.");
  if (hd && !bytes.hd.empty() && !result.asset->has_hd(0))
    result.warnings.push_back("The HD TIM was not accepted as a larger matching atlas; using the base texture.");
  if (result.asset->skeleton.joints.empty() || result.asset->skeleton.joints.size() > 64) {
    result.error = "Skeleton is outside the renderer's 1..64 joint range.";
    result.asset.reset();
  }
  return result;
}
} // namespace oc::tool
