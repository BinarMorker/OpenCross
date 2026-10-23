#pragma once
// FieldCamera: the field scroll state - the current scroll position, the tween toward a target, the follow mode and the pan limits - plus the
// rules that move it and clamp it to the room. It holds no room or entity data of its own: the caller passes the room view, the script global
// flags and the pose of the entity to aim at.
#include <utility>

#include "core/types.h"
#include "game/actor.h"
#include "game/room_view.h"

namespace oc::game {

struct FieldCamera {
  enum class CamMode { Auto, Fixed, FollowEnt };

  struct Camera {
    // scroll position
    int x = 0, y = 0;
    bool tween = false;
    // tween accumulators / per-tick steps
    float fx = 0, fy = 0, dx = 0, dy = 0;
    int frames = 0;
    int to_x = 0, to_y = 0;
    // mode entered when the tween ends
    CamMode after = CamMode::Auto;
    int follow_ent = -1;
    bool has_limits = false;
    RoomView::Limits limits;
  };

  Camera cam;
  CamMode cam_mode = CamMode::Auto;
  float camera_y_offset = 0;

  void reset();
  // the active pan limits, widened for widescreen
  RoomView::Limits scroll_limits(const RoomView& view) const;
  int clamp_x(const RoomView& view, u32 global_flags, int value) const;
  int clamp_y(const RoomView& view, u32 global_flags, int value) const;
  // The scroll that centres `pose` (null = no such entity: the current scroll).
  void target_for(const RoomView& view, u32 global_flags, const ActorPose* pose, int& tx, int& ty) const;
  void start_tween(int to_x, int to_y, int frames, CamMode after, int follow_ent);
  void set_position(const RoomView& view, u32 global_flags, s32 x, s32 y, int frames);
  void to_actor(const RoomView& view, u32 global_flags, const ActorPose* player_pose, int frames);
  void follow(const RoomView& view, u32 global_flags, const ActorPose* pose, int ent, int frames);
  bool busy() const { return cam.tween; }
  void set_limits(s32 x0, s32 x1, s32 y0, s32 y1);
  std::pair<s32, s32> position() const { return {cam.x, cam.y}; }
  void set_origin(s32 x, s32 y);
  // Room entry: the ease run to the player's spot, without the display step.
  void snap(const RoomView& view, u32 global_flags, const ActorPose* player_pose);
  // One tick of the tween / ease, before the shake offset. `follow_pose` = the pose of cam.follow_ent.
  void advance(const RoomView& view, u32 global_flags, const ActorPose* player_pose, const ActorPose* follow_pose);
};

}  // namespace oc::game
