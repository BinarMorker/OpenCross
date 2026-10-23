// FieldInput: the per-frame sampling of keyboard and pad.
#include "game/field_input.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <cmath>

namespace oc::game {

namespace {

// PS1-style key word (op 50 / 51 masks): bit 5 circle (confirm), 6 cross, 12 up, 13 right, 14 down, 15 left.
constexpr u16 kKeyCircle = 0x20, kKeyCross = 0x40, kKeyUp = 0x1000, kKeyRight = 0x2000, kKeyDown = 0x4000, kKeyLeft = 0x8000;

}  // namespace

void FieldInput::reset() {
  held_mask = 0;
  action_pending = false;
  input_mask = 0xFFFF;
}

FieldInput::Edges FieldInput::sample(const platform::Input& in) {
  auto key = [&](int sc) { return in.key_down[static_cast<size_t>(sc)]; };
  auto edge = [&](int sc) { return in.key_pressed[static_cast<size_t>(sc)]; };
  int mask = 0;
  if (key(SDL_SCANCODE_W) || key(SDL_SCANCODE_UP)) mask |= 1;
  if (key(SDL_SCANCODE_D) || key(SDL_SCANCODE_RIGHT)) mask |= 2;
  if (key(SDL_SCANCODE_S) || key(SDL_SCANCODE_DOWN)) mask |= 4;
  if (key(SDL_SCANCODE_A) || key(SDL_SCANCODE_LEFT)) mask |= 8;
  run_held = key(SDL_SCANCODE_LSHIFT) || key(SDL_SCANCODE_RSHIFT);
  bool confirm = edge(SDL_SCANCODE_RETURN) || edge(SDL_SCANCODE_SPACE) || edge(SDL_SCANCODE_Z);
  bool up = edge(SDL_SCANCODE_UP) || edge(SDL_SCANCODE_W), down = edge(SDL_SCANCODE_DOWN) || edge(SDL_SCANCODE_S);
  pad_analog = false;
  pad_rstick = false;
  if (in.pad_connected) {
    if (in.pad_down[SDL_GAMEPAD_BUTTON_DPAD_UP]) mask |= 1;
    if (in.pad_down[SDL_GAMEPAD_BUTTON_DPAD_RIGHT]) mask |= 2;
    if (in.pad_down[SDL_GAMEPAD_BUTTON_DPAD_DOWN]) mask |= 4;
    if (in.pad_down[SDL_GAMEPAD_BUTTON_DPAD_LEFT]) mask |= 8;
    // Pad: B (PS1 cross) = dash, X = key items, Y = menu, Start = pause.
    run_held = run_held || in.pad_down[SDL_GAMEPAD_BUTTON_EAST];
    confirm = confirm || in.pad_pressed[SDL_GAMEPAD_BUTTON_SOUTH];
    up = up || in.pad_pressed[SDL_GAMEPAD_BUTTON_DPAD_UP];
    down = down || in.pad_pressed[SDL_GAMEPAD_BUTTON_DPAD_DOWN];
    // ay: up positive. Outside the 0x4C..0xB4 dead zone the stick counts as analog.
    const float ax = in.pad_axis[SDL_GAMEPAD_AXIS_LEFTX], ay = -in.pad_axis[SDL_GAMEPAD_AXIS_LEFTY];
    if (std::fabs(ax) * 127.0f > 52.0f || std::fabs(ay) * 127.0f > 52.0f) {
      pad_analog = true;
      pad_x = ax;
      pad_y = ay;
    }
    // Analog mode: the right stick outside the same dead zone sets the leader's facing target (no camera yaw), turning him
    // on the spot; a walking tick overwrites it with the move heading.
    const float rx = in.pad_axis[SDL_GAMEPAD_AXIS_RIGHTX], ry = -in.pad_axis[SDL_GAMEPAD_AXIS_RIGHTY];
    if (std::fabs(rx) * 127.0f > 52.0f || std::fabs(ry) * 127.0f > 52.0f) {
      pad_rstick = true;
      pad_rx = rx;
      pad_ry = ry;
    }
  }
  held_mask = static_cast<u32>(mask);
  pad_word = 0;
  if (mask & 1) pad_word |= kKeyUp;
  if (mask & 2) pad_word |= kKeyRight;
  if (mask & 4) pad_word |= kKeyDown;
  if (mask & 8) pad_word |= kKeyLeft;
  // The script-visible key word (ops 50 / 51: the world map's "enter the location" test reads the circle bit) takes the pad too.
  const bool pad_a = in.pad_connected && in.pad_down[SDL_GAMEPAD_BUTTON_SOUTH], pad_b = in.pad_connected && in.pad_down[SDL_GAMEPAD_BUTTON_EAST];
  if (key(SDL_SCANCODE_RETURN) || key(SDL_SCANCODE_SPACE) || key(SDL_SCANCODE_Z) || pad_a) pad_word |= kKeyCircle;
  if (key(SDL_SCANCODE_X) || pad_b) pad_word |= kKeyCross;
  // Start bit.
  if (key(SDL_SCANCODE_RETURN) || key(SDL_SCANCODE_KP_ENTER) || in.pad_down[SDL_GAMEPAD_BUTTON_START]) pad_word |= 0x0800;
  key_latch |= static_cast<u16>(pad_word & ~0u);
  return {confirm, up, down};
}

}  // namespace oc::game
