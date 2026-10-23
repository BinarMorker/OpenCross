// FieldCamera: scroll clamping, tweens, the follow ease and the room-entry snap.
#include "game/field_camera.h"

#include <algorithm>
#include <cmath>

namespace oc::game {

namespace {

// Script global-flag bits: FE 04 disables the camera pan-limit clamp, FE 11 snaps the camera.
constexpr u32 kGfNoCameraClamp = 0x100, kGfCameraSnap = 0x200;
// Scroll easing: S -= (S - T) / 12 per tick.
constexpr int kEaseDivisor = 12;
constexpr int kEntryEaseIterations = 64;

}  // namespace

void FieldCamera::reset() {
  cam = Camera{};
  cam_mode = CamMode::Auto;
  camera_y_offset = 0;
}

RoomView::Limits FieldCamera::scroll_limits(const RoomView& view) const {
  return RoomView::widen(cam.has_limits ? cam.limits : view.default_limits(), static_cast<int>(render::wide_extra()));
}
int FieldCamera::clamp_x(const RoomView& view, u32 global_flags, int value) const {
  // FE 04 skips every pan-limit clamp.
  if (global_flags & kGfNoCameraClamp) return value;
  const RoomView::Limits limits = scroll_limits(view);
  return std::clamp(value, std::min(limits.x_min, limits.x_max), std::max(limits.x_min, limits.x_max));
}
int FieldCamera::clamp_y(const RoomView& view, u32 global_flags, int value) const {
  if (global_flags & kGfNoCameraClamp) return value;
  const RoomView::Limits limits = scroll_limits(view);
  return std::clamp(value, std::min(limits.y_min, limits.y_max), std::max(limits.y_min, limits.y_max));
}

void FieldCamera::target_for(const RoomView& view, u32 global_flags, const ActorPose* pose, int& tx, int& ty) const {
  tx = cam.x;
  ty = cam.y;
  if (!pose) return;
  render::FieldCamera::Projected pr;
  if (!view.camera().project({pose->x, pose->y + camera_y_offset, pose->z}, pr)) return;
  // Target = (160 - sx, 120 - sy), clamped to the pan limits.
  tx = clamp_x(view, global_flags, static_cast<int>(160.0f - pr.sx));
  ty = clamp_y(view, global_flags, static_cast<int>(120.0f - pr.sy));
}

void FieldCamera::start_tween(int to_x, int to_y, int frames, CamMode after, int follow_ent) {
  cam.after = after;
  cam.follow_ent = follow_ent;
  if (frames <= 0) {
    cam.x = to_x;
    cam.y = to_y;
    cam.tween = false;
    cam_mode = after;
    return;
  }
  cam.tween = true;
  cam.frames = frames;
  cam.to_x = to_x;
  cam.to_y = to_y;
  cam.fx = static_cast<float>(cam.x);
  cam.fy = static_cast<float>(cam.y);
  cam.dx = static_cast<float>(to_x - cam.x) / static_cast<float>(frames);
  cam.dy = static_cast<float>(to_y - cam.y) / static_cast<float>(frames);
}

void FieldCamera::set_position(const RoomView& view, u32 global_flags, s32 x, s32 y, int frames) {
  // 5D's target is clamped like every scroll target ([x_min..x_max] x [y_min-24..y_max], the loader's bias included, or the
  // script's own limits). y_min-24 is no overscan: the field shows 216 rows, so the lowest scroll puts the bottom of the layer
  // bounding box exactly on the last displayed row.
  if (!(global_flags & kGfNoCameraClamp)) {
    const RoomView::Limits limits = scroll_limits(view);
    x = std::clamp<s32>(x, std::min(limits.x_min, limits.x_max), std::max(limits.x_min, limits.x_max));
    y = std::clamp<s32>(y, std::min(limits.y_min, limits.y_max), std::max(limits.y_min, limits.y_max));
  }
  if (frames == 0x7FFF) frames = std::max(1, static_cast<int>(std::hypot(static_cast<float>(x - cam.x), static_cast<float>(y - cam.y))));
  start_tween(x, y, frames, CamMode::Fixed, -1);
}

void FieldCamera::to_actor(const RoomView& view, u32 global_flags, const ActorPose* player_pose, int frames) {
  int tx, ty;
  target_for(view, global_flags, player_pose, tx, ty);
  if (frames == 0x7FFF) frames = std::max(1, static_cast<int>(std::hypot(static_cast<float>(tx - cam.x), static_cast<float>(ty - cam.y))));
  start_tween(tx, ty, frames, CamMode::Auto, -1);
}

void FieldCamera::follow(const RoomView& view, u32 global_flags, const ActorPose* pose, int ent, int frames) {
  int tx, ty;
  target_for(view, global_flags, pose, tx, ty);
  if (frames == 0x7FFF) frames = std::max(1, static_cast<int>(std::hypot(static_cast<float>(tx - cam.x), static_cast<float>(ty - cam.y))));
  start_tween(tx, ty, frames, CamMode::FollowEnt, ent);
}

void FieldCamera::set_limits(s32 x0, s32 x1, s32 y0, s32 y1) {
  cam.has_limits = true;
  cam.limits = {x0, x1, y0, y1};
}

// FE 50: only the scroll position is written from variables 0x330 / 0x332; the follow logic of the field loop
// goes on (the world-map Continue, ROOM_PARAM 6), so the camera mode stays as it is.
void FieldCamera::set_origin(s32 x, s32 y) {
  cam.x = x;
  cam.y = y;
  cam.tween = false;
}

void FieldCamera::snap(const RoomView& view, u32 global_flags, const ActorPose* player_pose) {
  cam_mode = CamMode::Auto;
  cam.tween = false;
  cam.x = cam.y = 0;
  // Room entry runs the ease 0x40 times.
  for (int i = 0; i < kEntryEaseIterations; ++i) {
    int tx, ty;
    target_for(view, global_flags, player_pose, tx, ty);
    cam.x -= (cam.x - tx) / kEaseDivisor;
    cam.y -= (cam.y - ty) / kEaseDivisor;
  }
}

void FieldCamera::advance(const RoomView& view, u32 global_flags, const ActorPose* player_pose, const ActorPose* follow_pose) {
  if (cam.tween) {
    cam.fx += cam.dx;
    cam.fy += cam.dy;
    cam.x = static_cast<int>(std::lround(cam.fx));
    cam.y = static_cast<int>(std::lround(cam.fy));
    if (--cam.frames <= 0) {
      cam.tween = false;
      cam.x = cam.to_x;
      cam.y = cam.to_y;
      cam_mode = cam.after;
    }
  } else if (cam_mode != CamMode::Fixed) {
    int tx, ty;
    target_for(view, global_flags, cam_mode == CamMode::FollowEnt ? follow_pose : player_pose, tx, ty);
    // FE 11: put the scroll on the target instead of easing. The ease uses C division (truncates toward zero).
    if (global_flags & kGfCameraSnap) {
      cam.x = tx;
      cam.y = ty;
    } else {
      cam.x -= (cam.x - tx) / kEaseDivisor;
      cam.y -= (cam.y - ty) / kEaseDivisor;
    }
  }
}

}  // namespace oc::game
