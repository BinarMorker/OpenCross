// GPU-free model-viewer behavior; synthetic assets only, no installed game needed.
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

#include "game/model_viewer.h"

using namespace oc;
namespace v = game::viewer;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
bool near(double a, double b) { return std::fabs(a - b) < 1e-5; }

void test_catalog() {
  const auto entries = v::catalog({"fieldobj/player/selju/selju.obj", "battle/player/kid/kid.obj", "battle/monster/xxx/xxx.obj", "battle/wepon/x.obj", "../fieldobj/player/bad.obj"},
                                  {"FIELDOBJ\\PLAYER\\SELJU\\SELJU.OBJ", "fieldobj/player/onlyhd/onlyhd.obj", "fieldobj/player/a.tim"});
  CHECK(entries.size() == 4);
  size_t fields = 0;
  for (const auto& entry : entries) {
    fields += entry.kind == v::Kind::Field;
    CHECK(entry.stem.find('\\') == std::string::npos);
    if (entry.stem == "fieldobj/player/selju/selju") {
      CHECK(v::matches(entry, 0, "SELJU"));
      CHECK(v::matches(entry, 1, "selju"));
      CHECK(!v::matches(entry, 2, "selju"));
      CHECK(!v::matches(entry, 0, "kid"));
    }
  }
  CHECK(fields == 2);
}

void test_resource_pairing_and_optional_files() {
  v::Entry field{"fieldobj/player/selju/selju", v::Kind::Field};
  const auto paths = v::resource_paths(field);
  CHECK(paths.obj == "cdrom/fieldobj/player/selju/selju.obj");
  CHECK(paths.texture == "cdrom/fieldobj/player/selju/selju.tim");
  CHECK(paths.extra == "cdrom/fieldobj/ex/selju.bin");
  CHECK(paths.hd == "hd/fieldobj/player/selju/selju.tim");
  CHECK(paths.texture_fallback.empty());
  // The caller supplies one art-aware source for every request; do not force
  // read_disc() for the mesh while reading an HD texture through another source.
  std::map<std::string, Bytes> files{{paths.obj, {2}}, {paths.texture, {3}}, {paths.hd, {4}}};
  auto reader = [&](const std::string& path) -> std::optional<Bytes> {
    const auto it = files.find(path);
    return it == files.end() ? std::nullopt : std::optional<Bytes>(it->second);
  };
  const auto hd = v::read_resources(reader, field, true);
  CHECK(hd.error.empty());
  CHECK(hd.obj == Bytes{2});
  CHECK(hd.texture == Bytes{3});
  CHECK(hd.hd == Bytes{4});
  CHECK(hd.extra.empty());
  CHECK(v::read_resources(reader, field, false).hd.empty());
  files.erase(paths.texture);
  CHECK(!v::read_resources(reader, field, true).error.empty());
  files.erase(paths.obj);
  CHECK(!v::read_resources(reader, field, true).error.empty());

  v::Entry battle{"battle/player/selju/selju", v::Kind::BattlePlayer};
  const auto bp = v::resource_paths(battle);
  CHECK(bp.extra == "cdrom/battle/player/selju/at0.prd");
  CHECK(bp.texture.ends_with(".ltd"));
  CHECK(bp.texture_fallback.ends_with(".ltc"));
  files = {{bp.obj, {9}}, {bp.texture_fallback, {8}}};
  const auto fallback = v::read_resources(reader, battle, false);
  CHECK(fallback.error.empty());
  CHECK(fallback.texture == Bytes{8});
  CHECK(fallback.texture_path == bp.texture_fallback);
}

void test_original_reference_isolation_policy() {
  const v::Entry e{"fieldobj/player/selju/selju", v::Kind::Field};
  const auto p = v::resource_paths(e);
  const std::map<std::string, Bytes> stock{{p.obj,{1}},{p.texture,{2}}};
  const std::map<std::string, Bytes> overlay{{p.obj,{9}},{p.texture,{8}}};
  auto read = [](const auto& files, const std::string& path) -> std::optional<Bytes> {
    const auto it = files.find(path);
    return it == files.end() ? std::nullopt : std::optional<Bytes>(it->second);
  };
  const auto original = v::read_resources([&](const auto& path) { return read(stock,path); },e,false);
  const auto modded = v::read_resources([&](const auto& path) { return read(overlay,path); },e,false);
  CHECK(original.obj == Bytes{1});
  CHECK(modded.obj == Bytes{9});
  CHECK(!v::resolve_replacements(v::Variant::Original));
  CHECK(v::resolve_replacements(v::Variant::Modified));
  assets::ModelAsset original_asset, modified_asset;
  original_asset.textures.emplace_back();
  original_asset.textures[0].width=original_asset.textures[0].height=1;
  original_asset.textures[0].pixels={1,2,3,255};
  modified_asset=original_asset;
  auto replacement=[](assets::ModelAsset& asset) {
    asset.mod_textures=asset.textures;
    asset.mod_textures[0].pixels={9,8,7,255};
  };
  // Exercise the SAME preparation function used by the GPU upload caller.
  v::prepare_textures(original_asset,v::Variant::Original,replacement);
  v::prepare_textures(modified_asset,v::Variant::Modified,replacement);
  CHECK(original_asset.mod_textures.empty());
  CHECK(original_asset.drawn_texture(0,false).pixels[0]==1);
  CHECK(modified_asset.drawn_texture(0,false).pixels[0]==9);
}

void test_clips_and_playback() {
  assets::AnimationClip a, b, duplicate;
  a.source="s4"; a.index=0; a.frame_count=30;
  b.source="ex"; b.index=0; b.frame_count=12;
  duplicate=a;
  CHECK(v::matching_clip({b,a},a) == 1);
  CHECK(v::matching_clip({b},a) == -1);
  CHECK(v::matching_clip({a,duplicate},a) == -1); // ambiguous identities are not a safe match
  CHECK(near(v::playback_frame(1.,30,true),15.));
  CHECK(near(v::playback_frame(2.,30,true),0.));
  CHECK(near(v::playback_frame(99.,30,false),29.));
  CHECK(near(v::playback_frame(-1.,30,true),0.));
  CHECK(near(v::playback_frame(1.,0,true),0.));
  CHECK(near(v::playback_frame(std::numeric_limits<double>::infinity(),30,true),0.));
}

void test_bounds() {
  assets::ModelAsset a;
  CHECK(!v::asset_bounds(a).valid);
  a.meshes.emplace_back();
  assets::SkinVertex x,y,bad;
  x.bind_pos={-1,-2,-3};y.bind_pos={3,4,5};
  bad.bind_pos={std::numeric_limits<float>::quiet_NaN(),0,0};
  a.meshes[0].pool={x,y,bad};
  auto bounds=v::asset_bounds(a);
  CHECK(bounds.valid);
  CHECK(bounds.min[0]==-1 && bounds.max[2]==5);
  CHECK(near(v::center(bounds)[0],1.));
  CHECK(v::radius(bounds)>0);
  v::Bounds empty;
  CHECK(v::radius(empty)>0 && std::isfinite(v::radius(empty)));
  CHECK(v::bounds_union(empty,bounds).min==bounds.min);
  CHECK(v::bounds_union(bounds,empty).max==bounds.max);
  v::Bounds other;other.valid=true;other.min={-10,-20,-30};other.max={0,0,0};
  auto both=v::bounds_union(bounds,other);
  CHECK(both.min[1]==-20 && both.max[1]==4);
}

void test_camera_and_arguments() {
  v::Bounds bounds;bounds.valid=true;bounds.min={-1,-2,-3};bounds.max={1,2,3};
  auto camera=v::camera_matrices(bounds,0.f,0.f,1.f,16.f/9.f);
  CHECK(camera.view.at(2,3)>0);
  CHECK(camera.projection.at(3,2)==1.f);
  CHECK(camera.projection.at(3,3)==0.f);
  // Reset must fit the entire comparison union even in a portrait viewport.
  auto portrait=v::camera_matrices(bounds,0.f,0.f,1.f,.5f);
  const auto vp=portrait.projection*portrait.view;
  for(float x:{-1.f,1.f}) for(float y:{-2.f,2.f}) for(float z:{-3.f,3.f}) {
    float clip[4]{};
    for(int r=0;r<4;++r) clip[r]=vp.at(r,0)*x+vp.at(r,1)*y+vp.at(r,2)*z+vp.at(r,3);
    CHECK(clip[3]>0);
    CHECK(std::fabs(clip[0]/clip[3])<1 && std::fabs(clip[1]/clip[3])<1);
  }
  for (float x:camera.view.m) CHECK(std::isfinite(x));
  camera=v::camera_matrices({},0.f,0.f,0.f,0.f);
  for (float x:camera.projection.m) CHECK(std::isfinite(x));
  auto options=v::parse_options({"--sd","--game-dir","C:/Game","--model","fieldobj/player/selju/selju"});
  CHECK(options.error.empty() && !options.hd && options.game_dir=="C:/Game");
  CHECK(options.model=="fieldobj/player/selju/selju");
  CHECK(!v::parse_options({"--game-dir"}).error.empty());
  CHECK(!v::parse_options({"--unknown"}).error.empty());
  CHECK(v::parse_options({"--help"}).help);
}
} // namespace

int main() {
  test_catalog();test_resource_pairing_and_optional_files();test_original_reference_isolation_policy();test_clips_and_playback();test_bounds();test_camera_and_arguments();
  std::printf("model viewer: %d failure(s)\n",failures);
  return failures ? 1 : 0;
}
