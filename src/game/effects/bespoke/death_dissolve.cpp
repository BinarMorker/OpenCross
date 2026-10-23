// Synthetic "boss / monster death dissolve" drawn on the dummy-stage mesh: a growing hole fades the target away from a centre while debris
// points fly off the fading faces, with the screen-negative strip, a brightness fade and the screen fades of the lasdes2 scene manager.
//   * wipe task (op 0x100): threshold T(t) = peak * t / D + W (shrinking instead when `grows` is off). Operands in command() order: dur,
//     peak, width W, y offset, spawn rate, kie1 modulus, z offset, x offset.
//   * per vertex: d = |v - ref| (ref = actor position + mesh centre + operand offsets); brightness b = 0x80 when d >= T, else clamp((d - T
//     + W) * 0x80 / W, floor). Faces with every vertex at 0x80 go out opaque, the others as additive copies with the per-vertex brightness.
//   * debris: a face whose first vertex has b < 0x40 is a candidate; each tick `rate` candidates spawn a point at their centroid, every
//     (modulus + 1)-th spawn a bigger white one (kie1).
//   * strip (op_strip): the 5-column screen negative (util::NegativeStrip).
//   * brightness fade (op_fade / op_restore) and, with screen_ops, the fades to black (0x104 / 0x105) and to white (0x106).
// Only lasdes2 is served by this stand-in: the other deaths are ports in death_ports.cpp, registered later under the same
// names. The units of the operands (scaled here by the mesh radius), particle motion and sizes are approximations.
#include <algorithm>

#include "game/effects/bespoke/bespoke.h"
#include "game/effects/bespoke/bespoke_util.h"

namespace oc::effects {
namespace {

using util::add;
using util::len;
using util::mul;
using util::opnd;
using util::sub;

struct DissolveParams {
  const char* name;
  bool grows;         // false = the threshold shrinks
  int floor_b;        // brightness floor
  bool kie0, kie1;    // debris resources loaded
  bool first_vertex_test;  // opaque test on vertex 0 only instead of all three
  int fixed_rate;     // >0: spawn this many per tick regardless of the operand
  bool clut_fade;     // CLUT fade op present
  int op_strip, op_fade, op_restore;
  bool drift;         // 0x101 vertex drift
  bool screen_ops;    // 0x104..0x106 fades
  bool core_op;       // has the 0x100 wipe task (else 0x100 is the strip)
};

class DeathDissolve : public BespokeEffect {
 public:
  explicit DeathDissolve(const DissolveParams& params) : params_(params) {}

  void start(const FxContext& ctx) override {
    ctx_ = ctx;
    rng_.state = ctx.seed ? ctx.seed : 1;
    // The overlay re-emits the target mesh itself.
    if (ctx.hooks.set_visible) ctx.hooks.set_visible(1, false);
    mesh_ = ctx.target.mesh;
    const float radius = mesh_ ? mesh_->radius : 150.0f;
    const int dur = ctx.duration > 0 ? ctx.duration : 90;
    const int rate = ctx.arg[0] > 0 ? ctx.arg[0] : 3;
    const int mod = ctx.arg[1] > 0 ? ctx.arg[1] : 3;
    // Defaults of the battle script operands (peak, W relative to the model size).
    const int operands[8] = {dur, static_cast<int>(radius * 1.1f), static_cast<int>(radius * 0.3f), 0, rate, mod, 0, 0};
    if (params_.core_op) command(0x100, operands, 8);
    else {
      // The colour ramp runs on its own timer.
      const int ramp_operands[8] = {dur, static_cast<int>(radius * 1.1f), static_cast<int>(radius * 0.3f), 0, 0, 0, 0, 0};
      command(0x100, ramp_operands, 8);
    }
    strip_at_ = dur / 2;
    if (params_.clut_fade) fade_at_ = dur * 7 / 10;
    if (params_.screen_ops) black_at_ = dur * 85 / 100;
  }

  void command(int op, const int* args, int count) override {
    if (op == 0x100 && (params_.core_op || !core_)) {
      core_ = true;
      duration_ = std::max(1, opnd(args, count, 0, 90));
      peak_ = static_cast<float>(opnd(args, count, 1, 150));
      width_ = std::max(1.0f, static_cast<float>(opnd(args, count, 2, 40)));
      off_ = {static_cast<float>(opnd(args, count, 7, 0)), static_cast<float>(opnd(args, count, 3, 0)), static_cast<float>(opnd(args, count, 6, 0))};
      rate_ = params_.fixed_rate > 0 ? params_.fixed_rate : opnd(args, count, 4, 3);
      mod_ = opnd(args, count, 5, 3);
      tick_ = 0;
    } else if (op == params_.op_strip) {
      strip_.start(opnd(args, count, 0, 12));
      strip_at_ = -1;
    } else if (params_.clut_fade && op == params_.op_fade) {
      fade_dur_ = std::max(1, opnd(args, count, 0, 30));
      fade_t_ = 0;
      fade_at_ = -1;
    } else if (params_.clut_fade && op == params_.op_restore) {
      fade_t_ = -1;
    } else if (params_.drift && op == 0x101) {
      drift_dur_ = std::max(1, opnd(args, count, 0, 30));
      drift_end_ = {static_cast<float>(opnd(args, count, 1, 0)), static_cast<float>(opnd(args, count, 2, -40)), static_cast<float>(opnd(args, count, 3, 0))};
      drift_t_ = 0;
    } else if (params_.screen_ops) {
      // 0x104 fades in from black, 0x105 fades out to black, 0x106 flashes to white.
      if (op == 0x104) {
        black_dur_ = std::max(1, opnd(args, count, 0, 20));
        black_t_ = -2 - black_dur_;
      } else if (op == 0x105) {
        black_dur_ = std::max(1, opnd(args, count, 0, 20));
        black_t_ = 0;
        black_at_ = -1;
      } else if (op == 0x106) {
        white_dur_ = std::max(1, opnd(args, count, 0, 20));
        white_t_ = 0;
      }
    }
  }

  void tick() override {
    ++age_;
    strip_.tick();
    if (strip_at_ >= 0 && age_ == strip_at_) strip_.start(12);
    if (fade_at_ >= 0 && age_ == fade_at_) {
      fade_dur_ = std::max(1, duration_ - age_);
      fade_t_ = 0;
    }
    if (black_at_ >= 0 && age_ == black_at_) {
      black_dur_ = std::max(1, duration_ - age_ + 10);
      black_t_ = 0;
    }
    if (fade_t_ >= 0 && fade_t_ <= fade_dur_) ++fade_t_;
    if (black_t_ >= 0 && black_t_ <= black_dur_) ++black_t_;
    else if (black_t_ <= -2) ++black_t_;
    if (white_t_ >= 0 && white_t_ <= white_dur_) ++white_t_;
    if (drift_t_ >= 0 && drift_t_ <= drift_dur_) ++drift_t_;
    if (core_ && tick_ <= duration_) ++tick_;
    compute();
    spawn();
    for (auto& shard : shards_) {
      shard.pos = add(shard.pos, shard.vel);
      shard.vel[1] += 0.18f;
      ++shard.age;
    }
    shards_.erase(std::remove_if(shards_.begin(), shards_.end(), [](const Shard& shard) { return shard.age > shard.life; }), shards_.end());
  }

  void draw(std::vector<FxPrim>& out) const override {
    if (mesh_ && core_) {
      const float fade = fade_t_ >= 0 ? std::max(0.0f, 1.0f - static_cast<float>(fade_t_) / static_cast<float>(fade_dur_)) : 1.0f;
      for (size_t face = 0; face < mesh_->tris.size(); ++face) {
        const auto& tri = mesh_->tris[face];
        int bright_sum = 0;
        bool full = true;
        for (int k = 0; k < 3; ++k) {
          const int bright = bright_[tri[static_cast<size_t>(k)]];
          bright_sum += bright;
          if (params_.first_vertex_test ? (k == 0 && bright < 128) : bright < 128) full = false;
        }
        if (bright_sum == 0) continue;
        FxPrim prim;
        prim.kind = FxPrim::Kind::Tri;
        float depth_sum = 0;
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
          const size_t vi = tri[static_cast<size_t>(k)];
          float z;
          ok = fx_project(ctx_.view, world(vi), prim.v[k].x, prim.v[k].y, z);
          depth_sum += z;
          const float scale = full ? 1.0f : static_cast<float>(bright_[vi]) / 128.0f;
          prim.v[k].r = util::clamp8(static_cast<float>(mesh_->colours[vi][0]) * scale * fade);
          prim.v[k].g = util::clamp8(static_cast<float>(mesh_->colours[vi][1]) * scale * fade);
          prim.v[k].b = util::clamp8(static_cast<float>(mesh_->colours[vi][2]) * scale * fade);
        }
        if (!ok) continue;
        prim.blend = full ? 0 : 2;
        prim.slot = fx_slot_of_depth(depth_sum / 3.0f);
        prim.order = static_cast<u32>(face);
        out.push_back(prim);
      }
    }
    for (const auto& shard : shards_) {
      const float remaining = 1.0f - static_cast<float>(shard.age) / static_cast<float>(shard.life);
      if (shard.big) util::emit_point(ctx_.view, shard.pos, 7.0f * remaining + 1.0f, 255, 255, 255, 2, out);
      else
        util::emit_point(ctx_.view, shard.pos, 4.0f * remaining + 1.0f, util::clamp8(static_cast<float>(ctx_.colour[0]) * remaining),
                         util::clamp8(static_cast<float>(ctx_.colour[1]) * remaining), util::clamp8(static_cast<float>(ctx_.colour[2]) * remaining), 2, out);
    }
    strip_.draw(out);
    if (params_.screen_ops) {
      if (black_t_ >= 0) util::screen_fade(out, 3, 255 * std::min(black_t_, black_dur_) / black_dur_);
      else if (black_t_ <= -2) util::screen_fade(out, 3, 255 * (-black_t_ - 2) / black_dur_);
      if (white_t_ >= 0) util::screen_fade(out, 2, 255 * std::min(white_t_, white_dur_) / white_dur_);
    }
  }

  bool done() const override {
    return core_ && tick_ > duration_ && shards_.empty() && !strip_.active() && (black_t_ < 0 || black_t_ > black_dur_ || black_t_ <= -2);
  }

 private:
  struct Shard {
    Vec3 pos, vel;
    int age = 0, life = 24;
    bool big = false;
  };

  Vec3 world(size_t vi) const {
    Vec3 pos = add(ctx_.target.pos, mesh_->verts[vi]);
    if (drift_t_ >= 0) pos = add(pos, mul(drift_end_, static_cast<float>(std::min(drift_t_, drift_dur_)) / static_cast<float>(drift_dur_)));
    return pos;
  }
  Vec3 ref() const { return add(add(ctx_.target.pos, mesh_ ? mesh_->centre : Vec3{0, 0, 0}), off_); }

  float threshold() const {
    const float progress = static_cast<float>(std::min(tick_, duration_)) / static_cast<float>(duration_);
    return params_.grows ? peak_ * progress + width_ : peak_ * (1.0f - progress) + width_;
  }

  void compute() {
    if (!mesh_ || !core_) return;
    bright_.assign(mesh_->verts.size(), 128);
    const Vec3 ref_point = ref();
    const float threshold_now = threshold();
    cand_.clear();
    for (size_t i = 0; i < mesh_->verts.size(); ++i) {
      const float distance = len(sub(add(ctx_.target.pos, mesh_->verts[i]), ref_point));
      float bright;
      if (params_.grows) bright = distance < threshold_now ? std::max(0.0f, (distance - threshold_now + width_) * 128.0f / width_) : 128.0f;
      else bright = distance <= threshold_now ? 128.0f : std::max(0.0f, (threshold_now + width_ - distance) * 128.0f / width_);
      bright_[i] = std::clamp(static_cast<int>(bright), params_.floor_b, 128);
    }
    for (size_t face = 0; face < mesh_->tris.size(); ++face)
      if (bright_[mesh_->tris[face][0]] < 0x40 && tick_ <= duration_) cand_.push_back(static_cast<u32>(face));
  }

  void spawn() {
    if (!core_ || tick_ > duration_ || cand_.empty() || rate_ <= 0 || !(params_.kie0 || params_.kie1)) return;
    const Vec3 ref_point = ref();
    for (int i = 0; i < rate_; ++i) {
      const auto& tri = mesh_->tris[cand_[static_cast<size_t>(rng_.next()) % cand_.size()]];
      Vec3 centroid{0, 0, 0};
      for (int k = 0; k < 3; ++k) centroid = add(centroid, mesh_->verts[tri[static_cast<size_t>(k)]]);
      centroid = add(ctx_.target.pos, mul(centroid, 1.0f / 3.0f));
      Vec3 dir = sub(centroid, ref_point);
      const float length = std::max(1.0f, len(dir));
      Shard shard;
      shard.pos = centroid;
      shard.vel = add(mul(dir, (1.5f + static_cast<float>(rng_.next() % 30) * 0.1f) / length), {0, -1.0f, 0});
      shard.life = 16 + static_cast<int>(rng_.next() % 16);
      ++spawned_;
      shard.big = params_.kie1 && mod_ > 0 && (spawned_ % static_cast<u32>(mod_ + 1)) == 0;
      if (shards_.size() < 512) shards_.push_back(shard);
    }
  }

  DissolveParams params_;
  Rng rng_;
  const FxMesh* mesh_ = nullptr;
  bool core_ = false;
  int age_ = 0, tick_ = 0, duration_ = 90, rate_ = 3, mod_ = 3;
  float peak_ = 150, width_ = 40;
  Vec3 off_{0, 0, 0};
  std::vector<int> bright_;
  std::vector<u32> cand_;
  std::vector<Shard> shards_;
  u32 spawned_ = 0;
  util::NegativeStrip strip_;
  int strip_at_ = -1, fade_at_ = -1, fade_t_ = -1, fade_dur_ = 30;
  int black_at_ = -1, black_t_ = -1, black_dur_ = 20, white_t_ = -1, white_dur_ = 20;
  int drift_t_ = -1, drift_dur_ = 30;
  Vec3 drift_end_{0, 0, 0};
};

//                        name        grows floor kie0  kie1  v0only rate clut  strip  fade   restore drift  screen core
constexpr DissolveParams kParams[] = {
    {"lasdes2", true, 0, true, true, false, 0, true, 0x10C, 0x109, 0x10A, false, true, true},
};

}  // namespace

void register_death_dissolves(BespokeRegistry& registry) {
  for (const DissolveParams& params : kParams)
    registry.add({params.name, "death-dissolve", "DeathDissolve", [params] { return std::make_unique<DeathDissolve>(params); }, false});
}

}  // namespace oc::effects
