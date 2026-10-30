// Actual importer error paths; no GPU and no copyrighted fixtures.
#include <cstdio>
#include <map>

#include "model_viewer_load.h"

using namespace oc;
namespace v = game::viewer;
int main() {
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  const v::Entry entry{"fieldobj/player/test/test",v::Kind::Field};
  const auto paths=v::resource_paths(entry);
  std::map<std::string,Bytes> data;
  v::Reader reader=[&](const std::string& path)->std::optional<Bytes> {
    const auto it=data.find(path);
    return it==data.end()?std::nullopt:std::optional<Bytes>(it->second);
  };
  auto result=tool::load_viewer_asset(reader,entry,true);
  check(!result.asset && result.error.find(paths.obj)!=std::string::npos);
  data[paths.obj]=Bytes(32,0);
  result=tool::load_viewer_asset(reader,entry,true);
  check(!result.asset && result.error.find("Texture")!=std::string::npos);
  data[paths.texture]=Bytes(32,0);
  result=tool::load_viewer_asset(reader,entry,true);
  check(!result.asset && !result.error.empty());
  check(result.paths.obj==paths.obj && result.texture_path==paths.texture);
  std::printf("model viewer loader: %d failure(s)\n",failures);
  return failures?1:0;
}
