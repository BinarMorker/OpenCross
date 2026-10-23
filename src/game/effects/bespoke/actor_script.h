#pragma once
// Base of the "scripted actor" overlays (family F-E, the ones that steer the battle actors' objects instead of emitting packets).
// The original overlays write the actors' position (16.12), rotation (4096 = turn), scale (0x1000 = 1.0) and colour bytes
// (0x80 = neutral) through the "target" list (side 1) and the "caster" list (side 0).
// Here the module owns the poses of the actors it takes over: it hides the original (FxHooks::set_visible), animates a copy and
// redraws the mesh (FxHooks::actor_pose is called as well for hosts that move their own actors). Tick = one 60 Hz step.
#include <algorithm>
#include <cmath>

#include "game/effects/bespoke/bespoke.h"
#include "game/effects/bespoke/bespoke_util.h"

namespace oc::effects {

inline float fl(int value) { return static_cast<float>(value); }
inline float turns_to_rad(float a4096) { return a4096 * 6.2831853f / 4096.0f; }

struct ActorState {
  Vec3 home{0, 0, 0}, pos{0, 0, 0};
  float rot[3] = {0, 0, 0};            // 4096 = one turn (x, y, z)
  float scale = 1.0f;                  // 1.0 = 0x1000
  float tint[3] = {1.0f, 1.0f, 1.0f};  // 1.0 = colour byte 0x80
  bool present = false;                // the context supplied a mesh
};

class ActorScript : public BespokeEffect {
 public:
  void start(const FxContext& ctx) override;
  void tick() override;
  void draw(std::vector<FxPrim>& out) const override;
  bool done() const override { return done_; }

 protected:
  virtual int default_duration() const { return 60; }
  // false = the host keeps drawing that actor
  virtual bool takes_over(int side) const { return side == 1; }
  virtual void begin() {}
  virtual void step() = 0;
  virtual void extra(std::vector<FxPrim>&) const {}
  virtual void draw_actor(int side, std::vector<FxPrim>& out) const;
  // Restores the actors (visible, home pose) and ends the effect.
  void finish();
  static int clampi(int value, int lo, int hi) { return std::clamp(value, lo, hi); }
  void spawn(std::string_view name) const {
    if (ctx_.hooks.spawn) ctx_.hooks.spawn(name);
  }

  ActorState a_[2];
  Rng rng_;
  int tick_ = 0, D_ = 60;
  // y of the target's feet (model origin y + lowest vertex)
  float ground_ = 0;
  // World scale: dummy-stage units per original (PS1 battle) unit; see world_unit().
  float wu_ = 1.0f;
  bool done_ = false;

 private:
  void push_poses() const;
};

// The overlay constants are in PS1 battle units (a party member is roughly 1000-1100 high); the dummy stage figure is ~270 high.
float world_unit(const FxContext& ctx);

// Mesh height (y range) of an actor mesh, 0 when absent.
float mesh_height(const FxMesh* mesh);

}  // namespace oc::effects
