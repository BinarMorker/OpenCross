#pragma once
// Debug free camera (Debug tab > Free camera). Pure pose maths + the shared state, no SDL / ImGui: the battle
// (BattleView::apply_camera) and the title's 3D sea (TitleBackdrop::view) replace their camera pose with pose() while on().
// DebugTools owns the UI and the input polling (it calls look() / move() each frame). Never persisted: always off at start.
#include <algorithm>
#include <array>
#include <cmath>

#include "render/camera.h"

namespace oc::game {

class FreeCam {
 public:
  static constexpr float kPi = 3.14159265358979f;
  // World units per second at speed 1x: crosses the ~8000-unit battle arena in ~2 s (Serge is ~1100 tall).
  static constexpr float kBaseSpeed = 4000.0f;
  // Shift / left stick click multiplier.
  static constexpr float kSprint = 4.0f;

  bool on() const { return on_; }
  // Switching on starts from the shot's pose at that moment; the PS1 packet-drop rules are off while the free camera flies.
  void set_on(bool on) {
    if (on && !on_) need_init_ = true;
    on_ = on;
    render::set_cull_bypass(on);
  }
  // Gates the 2D overlays: only effective while the camera is on.
  bool hide_overlays() const { return on_ && hide_; }
  bool hide_setting() const { return hide_; }
  void set_hide_overlays(bool hide) { hide_ = hide; }
  float speed() const { return speed_; }
  void set_speed(float speed) { speed_ = std::clamp(speed, 0.05f, 20.0f); }
  // Back to the shot's pose: the next camera query re-reads it.
  void reset() { need_init_ = true; }
  bool needs_init() const { return on_ && need_init_; }

  // Takes the pose from an eye / target pair (the host's current shot).
  void init_from(const std::array<float, 3>& eye, const std::array<float, 3>& target) {
    eye_ = eye;
    const std::array<float, 3> dir = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len < 1e-4f) {
      yaw_ = 0;
      pitch_ = 0;
    } else {
      yaw_ = std::atan2(dir[0], dir[2]);
      pitch_ = std::asin(std::clamp(dir[1] / len, -1.0f, 1.0f));
    }
    need_init_ = false;
  }
  // Mouse / stick look: radians, yaw right-positive (turning toward +x from +z), pitch up-positive (our world is y up),
  // pitch clamped to +-89 degrees.
  void look(float dyaw, float dpitch) {
    yaw_ += dyaw;
    if (yaw_ > kPi) yaw_ -= 2 * kPi;
    if (yaw_ < -kPi) yaw_ += 2 * kPi;
    pitch_ = std::clamp(pitch_ + dpitch, -1.553f, 1.553f);
  }
  // fwd / strafe / up in -1..1; dt seconds. Forward follows the view (including its pitch); up is the world axis.
  void move(float fwd, float strafe, float up, float dt, bool sprint = false) {
    const std::array<float, 3> forward_dir = forward();
    const std::array<float, 3> right = {std::cos(yaw_), 0, -std::sin(yaw_)};
    const float step = kBaseSpeed * speed_ * dt * (sprint ? kSprint : 1.0f);
    for (size_t i = 0; i < 3; ++i) eye_[i] += (forward_dir[i] * fwd + right[i] * strafe) * step;
    eye_[1] += up * step;
  }
  std::array<float, 3> forward() const {
    return {std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)};
  }
  const std::array<float, 3>& eye() const { return eye_; }
  std::array<float, 3> target() const {
    const std::array<float, 3> dir = forward();
    return {eye_[0] + dir[0] * 1000, eye_[1] + dir[1] * 1000, eye_[2] + dir[2] * 1000};
  }
  float yaw() const { return yaw_; }
  float pitch() const { return pitch_; }

 private:
  bool on_ = false, hide_ = true, need_init_ = false;
  float speed_ = 1.0f;
  std::array<float, 3> eye_{0, 0, 0};
  float yaw_ = 0, pitch_ = 0;
};

// The one shared state (single-threaded main loop).
inline FreeCam& free_cam() {
  static FreeCam cam;
  return cam;
}

}  // namespace oc::game
