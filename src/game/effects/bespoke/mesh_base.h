#pragma once
// Shared base of the mesh re-emit / deform modules (family F-A): holds the target mesh, a per-vertex deformed copy and the
// duration/timer bookkeeping of the original overlays (private ops 0x180.. start / progress / end / hide).
#include "game/effects/bespoke/bespoke.h"
#include "game/effects/bespoke/bespoke_util.h"

namespace oc::effects {

class MeshFx : public BespokeEffect {
 public:
  void start(const FxContext& ctx) override {
    ctx_ = ctx;
    rng_.state = ctx.seed ? ctx.seed : 1;
    mesh_ = ctx.target.mesh ? ctx.target.mesh : ctx.caster.mesh;
    base_ = ctx.target.mesh ? ctx.target.pos : ctx.caster.pos;
    duration_ = ctx.duration > 0 ? ctx.duration : default_duration();
    if (mesh_) {
      ymin_ = ymax_ = mesh_->verts.empty() ? 0.0f : mesh_->verts[0][1];
      for (const auto& vertex : mesh_->verts) {
        ymin_ = std::min(ymin_, vertex[1]);
        ymax_ = std::max(ymax_, vertex[1]);
      }
      pos_ = mesh_->verts;
      col_ = mesh_->colours;
    }
    if (ctx.hooks.set_visible && hides_actor()) ctx.hooks.set_visible(ctx.target.mesh ? 1 : 0, false);
    begin();
  }
  void tick() override {
    ++tick_;
    step();
  }
  void draw(std::vector<FxPrim>& out) const override {
    if (mesh_ && show_mesh_) {
      if (bright_.empty()) util::emit_mesh(ctx_.view, *mesh_, world_, col_, blend_, out);
      else draw_gated(out);
    }
    extra(out);
  }

 protected:
  virtual int default_duration() const { return 90; }
  virtual bool hides_actor() const { return true; }
  virtual void begin() {}
  // Fills pos_ (actor-local deformed vertices) and col_ for the tick.
  virtual void step() = 0;
  virtual void extra(std::vector<FxPrim>&) const {}
  // pos_ -> world_.
  void finish_step() {
    world_.resize(pos_.size());
    for (size_t i = 0; i < pos_.size(); ++i) world_[i] = util::add(base_, pos_[i]);
  }

  // Per-vertex brightness gate (0x80 neutral, 0 hidden, > 0x80 over-bright): faces with every vertex at 0x80 keep blend_, faces with any
  // other vertex go out additive with the brightness folded into the vertex colours; fully hidden faces are skipped.
  void draw_gated(std::vector<FxPrim>& out) const {
    for (size_t tri_index = 0; tri_index < mesh_->tris.size(); ++tri_index) {
      const auto& tri = mesh_->tris[tri_index];
      int bsum = 0;
      bool full = true;
      for (int k = 0; k < 3; ++k) {
        const int brightness = bright_[tri[static_cast<size_t>(k)]];
        bsum += brightness;
        if (brightness != 128) full = false;
      }
      if (bsum == 0) continue;
      FxPrim pr;
      pr.kind = FxPrim::Kind::Tri;
      float zs = 0;
      bool ok = true;
      for (int k = 0; k < 3 && ok; ++k) {
        const size_t vi = tri[static_cast<size_t>(k)];
        float z;
        ok = fx_project(ctx_.view, world_[vi], pr.v[k].x, pr.v[k].y, z);
        zs += z;
        const float kb = static_cast<float>(bright_[vi]) / 128.0f;
        pr.v[k].r = util::clamp8(static_cast<float>(col_[vi][0]) * kb);
        pr.v[k].g = util::clamp8(static_cast<float>(col_[vi][1]) * kb);
        pr.v[k].b = util::clamp8(static_cast<float>(col_[vi][2]) * kb);
      }
      if (!ok) continue;
      pr.blend = full ? blend_ : 2;
      pr.slot = fx_slot_of_depth(zs / 3.0f);
      pr.order = static_cast<u32>(tri_index);
      out.push_back(pr);
    }
  }

  const FxMesh* mesh_ = nullptr;
  std::vector<int> bright_;
  Vec3 base_{0, 0, 0};
  std::vector<Vec3> pos_, world_;
  std::vector<std::array<u8, 3>> col_;
  Rng rng_;
  int tick_ = 0, duration_ = 90, blend_ = 0;
  bool show_mesh_ = true;
  float ymin_ = 0, ymax_ = 0;
};

void register_mesh_sweeps(BespokeRegistry& registry);
void register_mesh_flights(BespokeRegistry& registry);
void register_mesh_water(BespokeRegistry& registry);

}  // namespace oc::effects
