#pragma once
// BattleHandoff: the state of the field -> battle -> field hand-over (script ops A3 / C9): the pending request, the encounter intro and hold
// phases, the field music that is resumed afterwards, and the timeline queries the host uses to draw the white clear and the strip pass.
// The field world drives it; it reads the world's tick counter only through the parameters of its queries.
#include <vector>

#include "core/types.h"

namespace oc::ui {
class GameState;
}

namespace oc::game {

// Encounter hand-over timeline. Ticks since the request ('enct' tick e): the effect ends at e 105; the
// sequence tasks are torn down at 109 (3E off); the battle loader's white clear shows for 4 ticks 110..113; its embedded `main` sequence
// restarts the 3E bracket at 110. The battle is loaded in slices behind the swirl from the encounter request (app/app_battle.cpp
// App::field_battle_load_step); the hand-over keeps the T / U strip pass and W's fade to black. If the load is not done at the hand-over the
// white stays on screen until it is - never a frozen field.
constexpr int kEncounterWhiteFrom = 110, kEncounterWhiteTicks = 4, kEncounterHoldTicks = 104;
constexpr int kEncounterTearDown = 109, kEncounterMain3E = 110, kEncounterMainRamp = 116;

struct BattleHandoff {
  enum class BattleState { None, Intro, Hold, Ready, Running };

  struct BattleRequest {
    // A3: encounter id, C9: scene + 0x200 (data::GameTables::scene_for resolves both)
    int battle_id = 0;
    // battlefield index operand
    int field = 0;
    // C9: 1 keep field music, 2 losable (a wipe is not game over), 4/0x10/0x20/0x40 music details
    int flags = 0;
  };

  // After the white the host runs the strip pass with btlinit's `main` bracket (game/encounter_strips.h): one pass
  // per update (4 ticks), scene = the frozen last pre-white output times `scene_gain`, strips = the previous output scaled / tinted by the bracket.
  struct TransitionStrips {
    // after btlinit's white clear until the battle takes over
    bool active = false;
    // this tick runs a strip pass (every 4th); the other ticks show the last output again
    bool pass = false;
    // bracket clock (ticks since `main`'s first ramp)
    int m = 0;
    // 1, 2, ... (15 Hz updates since the white)
    int update = 0;
    float scene_gain = 1.0f;
  };

  // tick the encounter sequence started (log of its length)
  u64 intro_tick = 0;
  // BattleState::Hold: ticks since 'enct' ended (the held last field picture, then its fade to black)
  int hold_ticks = 0;
  BattleState battle_state = BattleState::None;
  // result code of the last battle (D2 tests < 2); not saved, 0 at boot
  int last_battle_code = 0;
  BattleRequest battle_req;
  bool battle_music_stopped = false;
  // song id and position (s) the field music is resumed at after the battle
  int resume_song = -1;
  double resume_seconds = 0.0;
  // FE 70: entity of the battle-intro effect "enct" (-1 = the leader)
  int battle_anchor = -1;

  // Room change: no battle is pending and the intro anchor is the leader again.
  void reset();
  bool ready() const { return battle_state == BattleState::Ready; }
  bool requested() const { return battle_state != BattleState::None; }
  bool loading() const { return battle_state == BattleState::Hold; }
  bool in_intro_or_hold() const { return battle_state == BattleState::Intro || battle_state == BattleState::Hold; }
  // BATTLE flag 4: model byte 0x3B on every party member but Poshul / Mojo
  void apply_battle_disguise(ui::GameState* game_state, const std::vector<int>& party_chars, u64 tick_count) const;
  // btlinit's white clear is on screen (4 frames); the host feeds it into the 3E strips' feed-back
  bool transition_white(u64 tick_count) const;
  // the tick before the white: the host keeps the previous output as the hold's picture
  bool transition_prefreeze(u64 tick_count) const;
  TransitionStrips transition_strips(u64 tick_count) const;
  // `main`'s bracket clock now (the battle continues it from the take-over)
  int transition_main_clock(u64 tick_count) const;
};

}  // namespace oc::game
