#pragma once
// Battle camera: keyframe shots (drp type 0x0C) and the timeline scripts that select them (type 0x10).
//
// Shot: progress advances 4096 / duration per 60 Hz tick, u = ease(progress, mode) (0 linear, 1 sin, 2 1-cos, 3 sin half-steps, 4 cosine
// S-curve); eye / target / (H, roll) are read at u from the smoothed key tracks, then the previous pose chases the goal by smoothing / 256 per
// tick (w8 >> 16: 0x100 = cut). Shots with ref bytes are in a unit-relative frame: origin at the unit, yaw of the unit; two refs = midpoint
// frame scaled by distance / 2048. The frame is taken when the shot is selected; flag bit 0 makes it follow the units every tick.
//
// Timeline: wait counter += delay (the high half of the command word) before executing the next command; consecutive 0x23 commands whose
// delays are 0 form a random choice group; 0x2B / 0x2C screen fades, 0x4C sound by tag, 0x4D / 0x4E loop marker / jump back.
//
// Keys: 3+ keys are smoothed by the retail cubic B-spline and stamped by the chord length of the master track; frame anchors use the unit
// position + height and yaw only.
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "assets/battle_stage.h"
#include "core/types.h"

namespace oc::game {

struct CamPose {
  std::array<float, 3> eye{0, 0, 0}, target{0, 0, 1};
  float h = 512, roll_deg = 0;
};

// One command of a type 0x10 timeline.
struct CamCmd {
  u16 op = 0;
  // delay: ticks to wait after the command (high half of the command word).
  // name: 4-char tag (camera / sound / emitter), may be empty.
  // frames, mode, r, g, b: fade length / mode / colour of ops 0x2B / 0x2C.
  // data: for ops 0x2B / 0x2C and 0x3E-0x40 (the screen copy warp), the bytes after the command word, for the view's hooks.
  u16 delay = 0;
  std::string name;
  s16 frames = 0;
  u8 mode = 0, r = 0, g = 0, b = 0;
  Bytes data;
};
struct CamSeq {
  std::string name;
  std::vector<CamCmd> cmds;
};

class CamBank {
 public:
  // Reads every 0x0C and 0x10 entry of a drp (recursing into nested drps). Later adds do not replace earlier names unless `replace`.
  void add_drp(ByteSpan drp, bool replace = false);
  const assets::StageCamera* camera(const std::string& name) const;
  const CamSeq* sequence(const std::string& name) const;
  // Names missing here are looked up in `parent` (the unit's cam0 bank falls back to the stage / battle.prd bank).
  void set_parent(const CamBank* parent) { parent_ = parent; }
  size_t cameras() const { return cams_.size(); }
  size_t sequences() const { return seqs_.size(); }

 private:
  std::map<std::string, assets::StageCamera> cams_;
  std::map<std::string, CamSeq> seqs_;
  const CamBank* parent_ = nullptr;
};

// `OC_BATTLE_AUDIT=1`: one `AUDIT ...` warning per anomaly of the battle flow; false when the variable is unset.
bool battle_audit();
void battle_audit_note(const std::string& what);

// Unit frame for anchored shots (our world: x/z ground, y up).
struct CamFrame {
  bool valid = false;
  // Origin.
  float x = 0, y = 0, z = 0;
  // yaw: radians, rotation about +Y: local (x, z) -> (x cos + z sin, -x sin + z cos); a unit's frame uses its model yaw (forward = -z).
  // pitch: two-anchor frames: tilt of local +X toward the second point, in the PS1 sense (y down): atan2(dy_ps1, |dxz|).
  // mscale: the anchored model's effect scale / 4096 (single-model anchors; applied by shots with flag 0x20).
  float yaw = 0;
  float pitch = 0;
  float mscale = 1.0f;
};
using AnchorFn = std::function<CamFrame(int ref_type, int attach)>;

struct CamEvents {
  // sounds: the 0x4C tags this tick. fade_target: 0..1 black, -1 = none requested.
  std::vector<std::string> sounds;
  float fade_target = -1;
  int fade_frames = 0;
  std::array<u8, 3> fade_rgb{0, 0, 0};
};

class BattleCamera {
 public:
  void set_bank(const CamBank* bank) { bank_ = bank; }
  const CamBank* bank() const { return bank_; }
  void set_anchor(AnchorFn fn) { anchor_ = std::move(fn); }
  // Timeline op 0x22 (start the stage emitter named by the command's tag): the stage objects live elsewhere (StageScene), so the view hooks it.
  void set_emitter_hook(std::function<void(const std::string&)> fn) { emitter_hook_ = std::move(fn); }
  // Timeline ops 0x3E / 0x3F / 0x40 (the screen copy warp, one global state shared with the effect packs': the view draws it).
  void set_warp_hook(std::function<void(const CamCmd&)> fn) { warp_hook_ = std::move(fn); }
  // Timeline ops 0x2B / 0x2C (the screen colour layers 0 / 1, shared with the effect packs; the view draws them).
  void set_screen_hook(std::function<void(const CamCmd&)> fn) { screen_hook_ = std::move(fn); }
  // Set by timeline op 0x73 and cleared by a new BattleCamera: the battle start waits for the intro timeline / its shot before the party intro
  // when it is set, even in a field battle.
  bool intro_hold() const { return intro_hold_; }

  // Hard cut to a shot (the default "zen0" etc.). Returns false when the shot does not exist.
  bool select(const std::string& name);
  // Starts a timeline script (several can run at once, like the engine's script tasks); its 0x23 commands call select().
  bool play_sequence(const std::string& name);
  void stop_sequences() { runners_.clear(); }
  bool sequence_running() const { return !runners_.empty(); }
  // Screen fade to black (to = 1) or back (to = 0) over `frames` ticks.
  void start_fade(float to, int frames, std::array<u8, 3> rgb = {0, 0, 0});
  bool shot_running() const { return shot_ != nullptr && progress_ < 4095.0f; }
  // A camera task is alive. A single-key shot (zen0) never creates one, so it is not counted however long its duration word is; a multi-key head
  // before its end or any queued task is.
  bool task_running() const { return (shot_ != nullptr && shot_->keys.size() > 1 && progress_ < 4096.0f) || !bg_.empty(); }
  float progress() const { return progress_; }
  // Ends the running shot at its last key (a later mode-2 select is no longer dropped): the confirm cut apr0 / apr1 gives way to the charge shot.
  void finish_shot() { bg_.clear(); if (shot_) progress_ = 4096.0f; }
  // The charge -> element pack hand-over unlinks every camera task; the pose registers keep their last values, so the camera holds where it is
  // until the next select.
  void kill_shot() { bg_.clear(); if (shot_) { prev_ = cur_; shot_ = nullptr; } }
  // Task birth credit (dev harness, OC_FX_DT): the camera task a pack command creates is run at once with the pass delta, so it starts `ticks`
  // ahead of the select.
  void credit_ticks(int ticks) {
    if (shot_ && ticks > 0 && progress_ < 4096.0f) progress_ = std::min(4096.0f, progress_ + static_cast<float>(ticks) * 4096.0f / static_cast<float>(std::max<u16>(1, shot_->duration)));
  }
  const std::string& shot_name() const { return shot_ ? shot_->name : empty_; }

  void tick(CamEvents& events);
  CamPose pose(float alpha) const;
  // Debug free camera (game/free_cam.h): while set, pose() returns this instead of the shot's pose (the shot timeline keeps running underneath).
  // The host clears it before reading the shot's pose each frame and sets it again when the free camera is on.
  void set_free_pose(const CamPose& pose) { free_ = pose; has_free_ = true; }
  void clear_free_pose() { has_free_ = false; }
  bool is_free() const { return has_free_; }
  // Current fade: 0 clear .. 1 black.
  float fade() const { return fade_cur_; }
  float fade_alpha(float alpha) const { return fade_prev_ + (fade_cur_ - fade_prev_) * alpha; }
  std::array<u8, 3> fade_rgb() const { return fade_rgb_; }
  void reset_fade() { fade_prev_ = fade_cur_ = fade_to_ = 0; fade_left_ = 0; }

  // Easing curve of a shot mode; progress and result in 0..1.
  static float ease(int mode, float progress);
  // Evaluates a shot at u in 0..1 (keys in the shot's own frame, no anchoring).
  static CamPose eval(const assets::StageCamera& shot, float eased);

 private:
  struct Runner {
    const CamSeq* seq = nullptr;
    size_t pos = 0;
    int wait = 0;
  };
  void exec(const CamCmd& cmd, CamEvents& events);
  void run(Runner& runner, CamEvents& events);
  void goal_pose(CamPose& out) const;
  bool make_frame(const assets::StageCamera& shot, CamFrame& frame, float& scale) const;

  const CamBank* bank_ = nullptr;
  AnchorFn anchor_;
  std::function<void(const std::string&)> emitter_hook_;
  std::function<void(const CamCmd&)> warp_hook_;
  std::function<void(const CamCmd&)> screen_hook_;
  // Set by timeline op 0x73.
  bool intro_hold_ = false;
  // The camera task list: shot_ is the HEAD (the one that drives the pose registers); bg_ holds the tasks behind it, which keep advancing
  // their own progress. A select of a mode 0 / 3+ shot pushes the running head back (it is not killed), a mode 1 shot queues at the tail;
  // when the head ends it is unlinked and the next task takes over at ITS progress.
  struct Task {
    const assets::StageCamera* shot = nullptr;
    float progress = 0;
    CamFrame frozen;
    float frozen_scale = 1.0f;
  };
  // bg_[0] is the task right behind the head.
  std::vector<Task> bg_;
  const assets::StageCamera* shot_ = nullptr;
  // Head progress, 0..4096.
  float progress_ = 0;
  CamPose prev_, cur_;
  bool have_ = false;
  CamPose free_;
  bool has_free_ = false;
  // The frame at the moment the shot was selected; only flag bit 0 shots keep tracking the units.
  CamFrame frozen_;
  float frozen_scale_ = 1.0f;
  std::string empty_;

  std::vector<Runner> runners_;

  float fade_prev_ = 0, fade_cur_ = 0, fade_to_ = 0;
  int fade_left_ = 0;
  std::array<u8, 3> fade_rgb_{0, 0, 0};
};

}  // namespace oc::game
