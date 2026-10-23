// Height-field water stand-ins: ripple (33 x 33 grid), 2boll and jitanda (21 x 21 grids). One integer relaxation on ping-pong buffers,
// interior cells only:
//     relaxed = ((N + S + E + W) >> 1) - old;  new = relaxed - (relaxed >> damp)
// The field starts with one impulse under the target; `drop_every` ticks a random half-height impulse is added while the effect runs.
// Op 0x180 stamps an impulse patch (size, height operands), 0x181 clears the field. The grid is drawn as half-transparent quads on the
// target's floor, each corner lifted by height / 64 and shaded by 0x80 + (height >> 4) over a blue tint.
#include <algorithm>

#include "game/effects/bespoke/mesh_base.h"

namespace oc::effects {
namespace {

using util::add;
using util::opnd;

// cells = grid points per side, damp = damping shift, drop_every = auto impulse period in ticks (0 = only the start impulse),
// impulse = default impulse height, extent = half size of the grid in world units.
struct WaterParams {
  const char* name;
  int cells;
  int damp;
  int drop_every;
  int impulse;
  float extent;
};

class WaterGrid : public BespokeEffect {
 public:
  explicit WaterGrid(const WaterParams& params) : params_(params) {}

  void start(const FxContext& ctx) override {
    ctx_ = ctx;
    rng_.state = ctx.seed ? ctx.seed : 1;
    duration_ = ctx.duration > 0 ? ctx.duration : 180;
    side_ = params_.cells;
    height_[0].assign(static_cast<size_t>(side_ * side_), 0);
    height_[1] = height_[0];
    floor_y_ = ctx.target.pos[1] + (ctx.target.mesh ? ctx.target.mesh->verts.empty() ? 0.0f : ctx.target.mesh->centre[1] + ctx.target.mesh->radius * 0.6f : 0.0f);
    stamp_world(ctx.target.pos, params_.impulse, 2);
  }

  void command(int op, const int* args, int nargs) override {
    if (op == 0x180) {
      const int size = std::max(1, opnd(args, nargs, 0, 2)), height = opnd(args, nargs, 1, params_.impulse);
      stamp_world(ctx_.target.pos, height, size);
    } else if (op == 0x181) {
      std::fill(height_[0].begin(), height_[0].end(), 0);
      std::fill(height_[1].begin(), height_[1].end(), 0);
    }
  }

  void tick() override {
    ++tick_;
    if (params_.drop_every > 0 && tick_ % params_.drop_every == 0 && tick_ < duration_) {
      const int cx = 2 + static_cast<int>(rng_.next() % static_cast<u32>(side_ - 4)), cy = 2 + static_cast<int>(rng_.next() % static_cast<u32>(side_ - 4));
      stamp(cx, cy, params_.impulse / 2, 1);
    }
    const auto& from = height_[static_cast<size_t>(cur_)];
    auto& into = height_[static_cast<size_t>(1 - cur_)];
    for (int y = 1; y < side_ - 1; ++y)
      for (int x = 1; x < side_ - 1; ++x) {
        const size_t i = static_cast<size_t>(y * side_ + x);
        const int relaxed = ((from[i - 1] + from[i + 1] + from[i - static_cast<size_t>(side_)] + from[i + static_cast<size_t>(side_)]) >> 1) - into[i];
        into[i] = relaxed - (relaxed >> params_.damp);
      }
    cur_ = 1 - cur_;
  }

  void draw(std::vector<FxPrim>& out) const override {
    const auto& heights = height_[static_cast<size_t>(cur_)];
    const float cell = 2.0f * params_.extent / static_cast<float>(side_ - 1);
    const Vec3 origin{ctx_.target.pos[0] - params_.extent, floor_y_, ctx_.target.pos[2] - params_.extent};
    std::vector<float> sx(heights.size()), sy(heights.size()), sz(heights.size());
    std::vector<u8> ok(heights.size());
    for (int y = 0; y < side_; ++y)
      for (int x = 0; x < side_; ++x) {
        const size_t i = static_cast<size_t>(y * side_ + x);
        const float lift = static_cast<float>(heights[i]) / 64.0f;
        ok[i] = fx_project(ctx_.view, add(origin, {static_cast<float>(x) * cell, -lift, static_cast<float>(y) * cell}), sx[i], sy[i], sz[i]) ? 1 : 0;
      }
    auto shade = [&](size_t i, FxVert& vertex) {
      const float gain = std::clamp((128.0f + static_cast<float>(heights[i] >> 4)) / 128.0f, 0.1f, 2.0f);
      vertex.r = util::clamp8(60.0f * gain);
      vertex.g = util::clamp8(110.0f * gain);
      vertex.b = util::clamp8(190.0f * gain);
    };
    for (int y = 0; y + 1 < side_; ++y)
      for (int x = 0; x + 1 < side_; ++x) {
        const size_t corner[4] = {static_cast<size_t>(y * side_ + x), static_cast<size_t>(y * side_ + x + 1), static_cast<size_t>((y + 1) * side_ + x),
                                  static_cast<size_t>((y + 1) * side_ + x + 1)};
        if (!(ok[corner[0]] && ok[corner[1]] && ok[corner[2]] && ok[corner[3]])) continue;
        FxPrim quad;
        quad.kind = FxPrim::Kind::Quad;
        quad.blend = 1;
        for (int k = 0; k < 4; ++k) {
          quad.v[k].x = sx[corner[k]];
          quad.v[k].y = sy[corner[k]];
          shade(corner[k], quad.v[k]);
        }
        quad.slot = fx_slot_of_depth((sz[corner[0]] + sz[corner[3]]) * 0.5f);
        quad.order = static_cast<u32>(y * side_ + x);
        out.push_back(quad);
      }
  }

  bool done() const override { return tick_ > duration_; }

 private:
  void stamp(int cx, int cy, int height, int side) {
    auto& field = height_[static_cast<size_t>(cur_)];
    for (int y = cy - side; y <= cy + side; ++y)
      for (int x = cx - side; x <= cx + side; ++x)
        if (x > 0 && y > 0 && x < side_ - 1 && y < side_ - 1) field[static_cast<size_t>(y * side_ + x)] += height;
  }
  void stamp_world(const Vec3& world, int height, int side) {
    const float cell = 2.0f * params_.extent / static_cast<float>(side_ - 1);
    const int cx = static_cast<int>((world[0] - (ctx_.target.pos[0] - params_.extent)) / cell), cy = static_cast<int>((world[2] - (ctx_.target.pos[2] - params_.extent)) / cell);
    stamp(std::clamp(cx, 1, side_ - 2), std::clamp(cy, 1, side_ - 2), height, side);
  }

  WaterParams params_;
  Rng rng_;
  int tick_ = 0, duration_ = 180, side_ = 33, cur_ = 0;
  float floor_y_ = 0;
  std::vector<int> height_[2];
};

constexpr WaterParams kWater[] = {
    {"ripple", 33, 3, 24, 0x600, 420.0f},
    {"2boll", 21, 2, 30, 0x800, 400.0f},
    {"jitanda", 21, 4, 30, 0x600, 400.0f},
};

}  // namespace

void register_mesh_water(BespokeRegistry& registry) {
  for (const WaterParams& params : kWater)
    registry.add({params.name, "mesh-deform", "WaterGrid", [params] { return std::make_unique<WaterGrid>(params); }, false});
}

void register_mesh_deforms(BespokeRegistry& registry) {
  register_mesh_sweeps(registry);
  register_mesh_flights(registry);
  register_mesh_water(registry);
}

}  // namespace oc::effects
