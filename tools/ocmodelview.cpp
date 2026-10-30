// Native inspection mode: opencross modelview [--game-dir PATH] [--model STEM] [--sd].
// No game session, save files, config writes or persistent mod changes.
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "game/battle/battle_pose.h"
#include "game/model_viewer.h"
#include "model_viewer_load.h"
#include "platform/config.h"
#include "platform/paths.h"
#include "platform/window.h"
#include "render/gpu_context.h"
#include "render/imgui_layer.h"
#include "render/imgui_mods_tab.h"
#include "render/mesh_pipeline.h"
#include "render/texture_registry.h"
#include "tool_util.h"
#include "vfs/mods.h"
#include "vfs/vfs.h"

namespace oc::tool {
namespace {
namespace v = game::viewer;
constexpr float kPi = 3.14159265358979323846f;

// The registry is global, unlike the VFS instances. Resolve only the modified
// asset in this scope; never send the reference through texture replacement.
struct TextureRegistryScope {
  const vfs::Vfs* previous = render::texture_registry::bound_vfs();
  render::texture_registry::Settings settings = render::texture_registry::settings();
  explicit TextureRegistryScope(const vfs::Vfs& files) {
    render::texture_registry::bind(&files);
    render::texture_registry::configure({false, true});
  }
  ~TextureRegistryScope() {
    render::texture_registry::bind(previous);
    render::texture_registry::configure(settings);
  }
};

struct Side {
  ViewerAsset loaded;
  std::unique_ptr<render::MeshModel> gpu;
  game::UnitPose pose;
};

void load_side(Side& side, const vfs::Vfs& files, const v::Entry& entry, bool hd,
               v::Variant variant, render::MeshPipeline& meshes) {
  side.pose.set_model(nullptr);
  side.gpu.reset();
  side.loaded = load_viewer_asset([&](const std::string& path) { return files.read(path); }, entry, hd);
  if (!side.loaded.asset) return;
  v::prepare_textures(*side.loaded.asset, variant, [&](assets::ModelAsset& asset) {
    TextureRegistryScope scope(files);
    render::resolve_model_textures(asset);
  });
  // create_model uploads already-resolved pixels and does not call the registry.
  side.gpu = meshes.create_model(*side.loaded.asset);
  if (!side.gpu) side.loaded.error = "GPU upload failed.";
  side.pose.set_model(&*side.loaded.asset);
}

void show_stats(const char* label, const Side& side) {
  ImGui::SeparatorText(label);
  if (!side.loaded.error.empty()) ImGui::TextWrapped("%s", side.loaded.error.c_str());
  if (!side.loaded.asset) return;
  const auto& asset = *side.loaded.asset;
  size_t vertices = 0, triangles = 0;
  for (const auto& mesh : asset.meshes) {
    vertices += mesh.pool.size();
    triangles += mesh.indices.size()/3;
  }
  ImGui::Text("%zu vertices / %zu triangles", vertices, triangles);
  ImGui::Text("%zu parts / %zu joints / %zu clips", asset.meshes.size(), asset.skeleton.joints.size(), asset.clips.size());
  for (const auto& warning : side.loaded.warnings) ImGui::TextWrapped("%s", warning.c_str());
  if (ImGui::TreeNode(label, "Resource paths")) {
    ImGui::TextWrapped("Model: %s", side.loaded.paths.obj.c_str());
    ImGui::TextWrapped("Texture: %s", side.loaded.texture_path.c_str());
    ImGui::TextWrapped("Extra: %s", side.loaded.paths.extra.c_str());
    ImGui::TextWrapped("HD: %s", side.loaded.paths.hd.c_str());
    ImGui::TreePop();
  }
}

int run_viewer(const v::Options& options) {
  platform::Config config;
  config.load(platform::exe_dir()/"opencross.ini"); // read-only; never call save/options_window
  const std::string requested = !options.game_dir.empty() ? options.game_dir :
                               !env_game_dir().empty() ? env_game_dir() : config.game_dir;
  const auto game_dir = platform::find_game_dir(requested);
  vfs::Vfs reference, modified;
  if (game_dir.empty() || !reference.open(game_dir) || !modified.open(game_dir)) {
    std::fprintf(stderr, "Game data not found. Use --game-dir PATH.\n");
    return 1;
  }
  const auto entries = v::catalog(reference.list_base("cdrom"), reference.list_base("cdrom2"));
  if (entries.empty()) {
    std::fprintf(stderr, "No supported character models found in the installed archives.\n");
    return 1;
  }
  size_t selected = 0;
  const std::string initial = options.model.empty() ? "fieldobj/player/selju/selju" : options.model;
  const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.stem == initial; });
  if (found != entries.end()) selected = static_cast<size_t>(found-entries.begin());
  else if (!options.model.empty()) {
    std::fprintf(stderr, "Model not found in catalog: %s\n", options.model.c_str());
    return 2;
  }

  platform::Window window;
  if (!window.create("OpenCross model viewer", 1440, 960, false)) return 1;
  render::GpuContext context;
  if (!context.init(window.handle(), true, config.gpu_driver)) return 1;
  context.set_clear_color(0.07f, 0.09f, 0.12f);
  render::MeshPipeline meshes;
  if (!meshes.init(context)) return 1;
  render::ImGuiLayer ui;
  if (!ui.init(context, window.handle())) return 1;
  window.set_event_hook(render::ImGuiLayer::event_hook, &ui);
  Side original, modded;
  // Runs before either Side, UI, pipeline or context is destroyed, also on throw.
  struct WaitBeforeRelease {
    render::GpuContext& ctx;
    ~WaitBeforeRelease() { ctx.wait_idle(); }
  } wait{context};

  vfs::ModManager mods;
  bool hd = options.hd, show_modified = true, reload = true;
  bool playing = false, loop = true, lighting = true, linear = true, show_hidden = false;
  int filter = 0, clip = -1;
  char search[128] = {};
  double seconds = 0;
  float speed = 1, yaw = 0, pitch = 0.12f, zoom = 1;
  float ambient = 0.55f, strength = 0.65f;
  v::Bounds bounds;
  platform::Input input;
  platform::FrameTimer timer;
  std::vector<render::Mat4> palette;

  auto reload_models = [&] {
    context.wait_idle();
    reference.set_hd_mode(hd);
    modified.set_hd_mode(hd);
    mods.scan(reference, game_dir/"mods", render::mod_prefs(config));
    mods.apply(modified, hd ? vfs::ModArt::Modded : vfs::ModArt::Sd);
    load_side(original, reference, entries[selected], hd, v::Variant::Original, meshes);
    load_side(modded, modified, entries[selected], hd, v::Variant::Modified, meshes);
    bounds = v::bounds_union(original.loaded.asset ? v::asset_bounds(*original.loaded.asset) : v::Bounds{},
                             modded.loaded.asset ? v::asset_bounds(*modded.loaded.asset) : v::Bounds{});
    clip = -1;
    seconds = 0;
    playing = false;
    reload = false;
  };

  while (window.pump(input)) {
    const double dt = std::clamp(timer.tick(), 0.0, 0.1);
    if (reload) reload_models();
    ui.new_frame();
    const auto& io = ImGui::GetIO();
    if (input.key_pressed[SDL_SCANCODE_ESCAPE] && !io.WantTextInput) {
      ImGui::EndFrame();
      break;
    }
    const float panel_width = std::min(390.0f, io.DisplaySize.x*0.42f);
    if (!io.WantCaptureKeyboard) {
      if (input.key_pressed[SDL_SCANCODE_SPACE]) playing = !playing;
      if (input.key_pressed[SDL_SCANCODE_TAB]) show_modified = !show_modified;
      if (input.key_pressed[SDL_SCANCODE_R]) reload = true;
    }
    if (!io.WantCaptureMouse && io.MousePos.x > panel_width) {
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        yaw += io.MouseDelta.x*0.008f;
        pitch = std::clamp(pitch+io.MouseDelta.y*0.008f, -1.4f, 1.4f);
      }
      zoom = std::clamp(zoom*std::exp(-io.MouseWheel*0.12f),0.35f,10.0f);
    }

    ImGui::SetNextWindowPos({0,0});
    ImGui::SetNextWindowSize({panel_width,io.DisplaySize.y});
    ImGui::Begin("Model viewer", nullptr, ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
    ImGui::TextWrapped("%s", entries[selected].stem.c_str());
    ImGui::TextDisabled("Drag model to orbit / wheel to zoom");
    ImGui::InputTextWithHint("##search", "Search model path", search, sizeof search);
    ImGui::Combo("Group", &filter, "All\0Field characters\0Battle players\0Battle monsters\0");
    if (ImGui::BeginChild("catalog", {0,190}, ImGuiChildFlags_Borders)) {
      for (size_t i = 0; i < entries.size(); ++i) {
        if (!v::matches(entries[i],filter,search)) continue;
        if (ImGui::Selectable(entries[i].stem.c_str(), i==selected) && i!=selected) {
          selected=i;reload=true;yaw=0;pitch=0.12f;zoom=1;
        }
      }
    }
    ImGui::EndChild();
    if (ImGui::Checkbox("HD art (cdrom2 + matching texture)",&hd)) reload=true;
    if (ImGui::Button("Reload models and mods (R)")) reload=true;
    ImGui::Checkbox("Modified (Tab switches A/B)",&show_modified);
    ImGui::TextWrapped("Original bypasses all mods. Modified uses enabled mods from opencross.ini; settings are read-only here.");
    ImGui::SeparatorText("Animation");
    const assets::ModelAsset* master = original.loaded.asset ? &*original.loaded.asset :
                                      modded.loaded.asset ? &*modded.loaded.asset : nullptr;
    if (master) {
      const char* title = clip >= 0 && static_cast<size_t>(clip)<master->clips.size() ? master->clips[static_cast<size_t>(clip)].name.c_str() : "Rest pose";
      if (ImGui::BeginCombo("Clip", title)) {
        if (ImGui::Selectable("Rest pose", clip<0)) clip=-1,seconds=0,playing=false;
        for (size_t i=0;i<master->clips.size();++i) {
          const auto& candidate=master->clips[i];
          const std::string label=candidate.source+"["+std::to_string(candidate.index)+"] "+candidate.name;
          if (ImGui::Selectable(label.c_str(),clip==static_cast<int>(i))) clip=static_cast<int>(i),seconds=0;
        }
        ImGui::EndCombo();
      }
      if (clip>=0 && static_cast<size_t>(clip)<master->clips.size()) {
        const auto frames=master->clips[static_cast<size_t>(clip)].frame_count;
        ImGui::Checkbox("Play (Space)",&playing);ImGui::SameLine();ImGui::Checkbox("Loop",&loop);
        ImGui::SliderFloat("Speed",&speed,0.1f,3.f,"%.2fx");
        float shown=static_cast<float>(v::playback_frame(seconds,frames,loop));
        if (ImGui::SliderFloat("Frame",&shown,0.f,static_cast<float>(frames>0?frames-1:0),"%.2f")) seconds=shown/assets::kModelAnimFps,playing=false;
        if (playing) {
          seconds+=dt*speed;
          if (!loop && seconds*assets::kModelAnimFps >= (frames>0?frames-1:0)) playing=false;
        }
      }
    }
    ImGui::SeparatorText("Camera and light");
    if (ImGui::Button("Front")) yaw=0,pitch=0;
    ImGui::SameLine();if (ImGui::Button("Side")) yaw=kPi*0.5f,pitch=0;
    ImGui::SameLine();if (ImGui::Button("Back")) yaw=kPi,pitch=0;
    if (ImGui::Button("Reset framing")) yaw=0,pitch=0.12f,zoom=1;
    ImGui::SliderFloat("Zoom",&zoom,0.35f,5.f,"%.2f");
    ImGui::Checkbox("Controlled lighting",&lighting);
    if (lighting) {
      ImGui::SliderFloat("Ambient",&ambient,0.f,1.f);
      ImGui::SliderFloat("Key strength",&strength,0.f,1.5f);
    }
    ImGui::Checkbox("Bilinear texture filter",&linear);
    ImGui::Checkbox("Show initially hidden parts",&show_hidden);
    ImGui::TextWrapped("Inspection lighting, not a reproduction of a map or battle scene. Raw clips do not run game scripts or effects.");
    show_stats("Original",original);
    show_stats("Modified",modded);
    ImGui::End();

    Side& side=show_modified?modded:original;
    int side_clip=-1;
    std::string mismatch;
    if (master && clip>=0 && static_cast<size_t>(clip)<master->clips.size() && side.loaded.asset) {
      const auto& canonical=master->clips[static_cast<size_t>(clip)];
      side_clip=v::matching_clip(side.loaded.asset->clips,canonical);
      if (side_clip<0) mismatch="Clip missing or ambiguous in this version; showing rest pose.";
      else if (side.loaded.asset->clips[static_cast<size_t>(side_clip)].frame_count!=canonical.frame_count)
        mismatch="Clip lengths differ; both versions use the same playback time.";
    }
    ImGui::SetNextWindowPos({panel_width+15,15});
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##viewer_status",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs);
    ImGui::Text("%s / %s",show_modified?"MODIFIED":"ORIGINAL",hd?"HD":"SD");
    if (!side.loaded.error.empty()) ImGui::TextWrapped("%s",side.loaded.error.c_str());
    if (!mismatch.empty()) ImGui::TextWrapped("%s",mismatch.c_str());
    ImGui::End();

    if (reload) { ImGui::EndFrame();continue; }
    render::Frame frame;
    if (!context.begin_frame(frame)) { ImGui::EndFrame();SDL_Delay(10);continue; }
    const float pixel_ratio = io.DisplaySize.x>0 ? static_cast<float>(frame.extent.width)/io.DisplaySize.x : 1;
    const auto left=static_cast<u32>(panel_width*pixel_ratio);
    const auto width=frame.extent.width>left?frame.extent.width-left:1u;
    if (side.gpu && side.loaded.asset) {
      const auto camera=v::camera_matrices(bounds,yaw,pitch,zoom,static_cast<float>(width)/static_cast<float>(std::max(1u,frame.extent.height)));
      double sample_frame=0;
      if (side_clip>=0) sample_frame=v::playback_frame(seconds,side.loaded.asset->clips[static_cast<size_t>(side_clip)].frame_count,loop);
      side.pose.palette_at(side_clip,static_cast<float>(sample_frame),palette);
      render::MeshPipeline::DrawParams params;
      params.view=camera.view;params.view_proj=camera.projection*camera.view;params.palette=palette;
      params.hd=hd;params.linear=linear;
      if (!show_hidden) for (size_t k=0;k<side.loaded.asset->meshes.size() && k<16;++k)
        if (side.loaded.asset->shading[k].flags&1) params.hidden_meshes |= 1u<<k;
      params.light.on=lighting;
      for (size_t channel=0;channel<3;++channel) {
        params.light.bk[channel]=ambient;
        params.light.rgb[0][channel]=strength;
      }
      // Fixed model-space light: rotating the camera changes the view, not the light.
      params.light.dir[0][0]=-0.35f;params.light.dir[0][1]=-0.70f;params.light.dir[0][2]=-0.62f;
      const SDL_GPUViewport viewport{static_cast<float>(left),0,static_cast<float>(width),static_cast<float>(frame.extent.height),0,1};
      SDL_SetGPUViewport(context.pass(),&viewport);
      meshes.begin(frame,{static_cast<s32>(left),0,width,frame.extent.height});
      meshes.draw(*side.gpu,params);meshes.end();
    }
    const SDL_GPUViewport full{0,0,static_cast<float>(frame.extent.width),static_cast<float>(frame.extent.height),0,1};
    SDL_SetGPUViewport(context.pass(),&full);
    ui.render(frame);context.end_frame();
  }
  window.set_event_hook(nullptr,nullptr);
  return 0;
}
} // namespace
} // namespace oc::tool

int ocmodelview_main(int argc, char** argv) {
  std::vector<std::string> args;
  for (int i=1;i<argc;++i) args.emplace_back(argv[i]);
  const auto options=oc::game::viewer::parse_options(args);
  if (options.help || !options.error.empty()) {
    if (!options.error.empty()) std::fprintf(stderr,"%s\n",options.error.c_str());
    std::fprintf(options.error.empty()?stdout:stderr,
      "usage: opencross modelview [--game-dir PATH] [--model MEMBER_STEM] [--sd]\n"
      "Original/modded A/B, orbit camera, raw animation clips and controlled lighting.\n");
    return options.error.empty()?0:2;
  }
  try { return oc::tool::run_viewer(options); }
  catch (const std::exception& error) {
    std::fprintf(stderr,"Model viewer: %s\n",error.what());
    return 1;
  }
}
