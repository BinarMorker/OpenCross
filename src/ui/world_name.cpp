#include "ui/world_name.h"

#include <algorithm>
#include <string>

#include "core/log.h"
#include "render/camera.h"

namespace oc::ui {

namespace {
constexpr float kX0 = 20.0f, kY0 = 7.0f, kX1 = 330.0f, kY1 = 207.0f;
constexpr double kFadeIn = 0.0625, kFadeOut = 0.065;
}  // namespace

bool WorldName::init(render::GpuContext& ctx, const vfs::Vfs& vfs, render::SpriteBatch& sprites) {
  ctx_ = &ctx, vfs_ = &vfs, sprites_ = &sprites;
  return linear_.create(ctx, true);
}

void WorldName::shutdown() {
  if (!ctx_) return;
  for (int i = 0; i < 2; ++i) {
    set_[i] = {};
    art_[i].destroy();
  }
  linear_.destroy();
  ctx_ = nullptr;
}

bool WorldName::home_map(int map) {
  switch (map) {
    case 0x8F:
    case 0xFA:
    case 0xFC:
    case 0xFE:
    case 0x102:
    case 0x1BB:
    case 0x213: return true;
    default: return false;
  }
}

void WorldName::load(import::Lang lang) {
  loaded_lang_ = static_cast<int>(lang);
  static const char* const kNames[2] = {"home", "another"};
  for (int i = 0; i < 2; ++i) {
    set_[i] = {};
    art_[i].destroy();
    const std::string path = std::string("hd/map/HomeAnother/") + kNames[i] + "_" + import::lang_code(lang) + ".png";
    render::DecodedImage img;
    if (auto raw = vfs_->read(path); raw && render::decode_image(*raw, img) &&
                                     art_[i].create(*ctx_, img.width, img.height, render::TextureFormat::Rgba8, img.rgba.data()))
      set_[i] = render::TexRef(art_[i], linear_);
    else
      log::warn("world name: {} unavailable", path);
  }
}

void WorldName::draw(const render::Frame& frame, float vw, float vh, float left, bool show, bool home, import::Lang lang) {
  const auto now = std::chrono::steady_clock::now();
  const double frames = last_ == std::chrono::steady_clock::time_point{} ? 1.0 : std::chrono::duration<double>(now - last_).count() * 60.0;
  last_ = now;
  if (!ctx_) return;
  alpha_ = show ? std::min(1.0, alpha_ + kFadeIn * frames) : std::max(0.0, alpha_ - kFadeOut * frames);
  if (alpha_ <= 0.0) return;
  if (loaded_lang_ != static_cast<int>(lang)) load(lang);
  const render::TexRef set = set_[home ? 0 : 1];
  if (!set) return;
  const render::CanvasFit fit = render::canvas_fit(vw, vh);
  sprites_->begin(frame, vw, vh, true);
  sprites_->draw(set, left + kX0 * fit.s, fit.oy + kY0 * fit.s, (kX1 - kX0) * fit.s, (kY1 - kY0) * fit.s, 0.0f, 0.0f, 1.0f, 1.0f,
                 render::pack_rgba(255, 255, 255, static_cast<u8>(alpha_ * 255.0 + 0.5)));
  sprites_->end();
}

}  // namespace oc::ui
