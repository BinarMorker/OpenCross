#include "ui/lowres.h"

#include "core/log.h"

namespace oc::ui {

namespace {
bool g_ps1_menus = true;
constexpr float kCanvasWidth = 1280.0f, kCanvasHeightUnits = 960.0f;
}  // namespace

void set_ps1_menus(bool on) { g_ps1_menus = on; }
bool ps1_menus() { return g_ps1_menus; }

bool LowRes::init(render::GpuContext& ctx, render::SpriteBatch& sprites) {
  shutdown();
  if (!nearest_.create(ctx, false)) return false;
  ctx_ = &ctx;
  sprites_ = &sprites;
  return true;
}

void LowRes::shutdown() {
  if (!ctx_) return;
  set_ = {};
  rt_.destroy();
  nearest_.destroy();
  ctx_ = nullptr;
  sprites_ = nullptr;
  text_ = nullptr;
  active_ = false;
}

// The target is recreated when its size changes and cleared transparent so end() can composite it over the frame.
bool LowRes::begin(const render::Frame& frame, const Ps1Display& display, render::TextBatch* text, render::Frame& out, float canvas_w,
                   float canvas_h) {
  if (!ctx_ || active_) return false;
  if (!rt_.valid() || rt_.width() != display.w || rt_.height() != display.h || !set_) {
    if (!rt_.create(*ctx_, display.w, display.h)) return false;
    set_ = rt_.ref(nearest_);
  }
  static const float kClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  ctx_->begin_target(rt_, frame, out, kClear);
  old_filter_ = sprites_->filter();
  sprites_->set_filter(render::SpriteFilter::Point);
  sprites_->set_log_tag(display.tag);
  text_ = text;
  if (text_) text_->set_pixel_grid(static_cast<float>(display.w) / canvas_w, static_cast<float>(display.h) / canvas_h);
  active_ = true;
  return true;
}

void LowRes::end(const render::Frame& frame, float vw, float vh, const render::Letterbox::Rect* dst) {
  if (!active_) return;
  active_ = false;
  ctx_->end_target(rt_);
  sprites_->set_filter(old_filter_);
  sprites_->set_log_tag(nullptr);
  if (text_) text_->set_pixel_grid(0.0f, 0.0f);
  text_ = nullptr;
  // The canvas rectangle of the view (render::canvas_fit: the 4:3 area centred in the window at the shared zoom), unless given.
  const render::CanvasFit fit = render::canvas_fit(vw, vh, kCanvasWidth, kCanvasHeightUnits);
  const float zoom = fit.s;
  const float ox = dst ? dst->x : fit.ox, oy = dst ? dst->y : fit.oy;
  const float dw = dst ? dst->w : kCanvasWidth * zoom, dh = dst ? dst->h : kCanvasHeightUnits * zoom;
  const float tw = static_cast<float>(rt_.width()), th = static_cast<float>(rt_.height());
  // Nearest upscale with a 1/64-texel uv bias (the same bias as the field's own upscale): window pixel centres never sit on a texel
  // boundary, where the quad's two triangles could round differently.
  const float bu = 1.0f / (64.0f * tw), bv = 1.0f / (64.0f * th);
  sprites_->begin(frame, vw, vh, true);
  sprites_->set_blend(render::BlendMode::Premultiplied);
  sprites_->draw(set_, ox, oy, dw, dh, bu, bv, 1.0f + bu, 1.0f + bv);
  sprites_->set_blend(render::BlendMode::Alpha);
  sprites_->end();
}

}  // namespace oc::ui
