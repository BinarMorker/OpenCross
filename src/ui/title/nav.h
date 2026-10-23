#pragma once
// Menu navigation from the platform input snapshot: edges plus key repeat for held directions.
#include <SDL3/SDL.h>

#include "platform/window.h"

namespace oc::ui {

struct Nav {
  bool up = false, down = false, left = false, right = false;
  bool confirm = false, cancel = false, l1 = false, r1 = false, any = false;
  // start: Tab / pad Start.
  bool start = false;
};

class NavPoller {
 public:
  // dt in seconds. Directions repeat after 0.4 s at 0.08 s intervals.
  Nav poll(const platform::Input& in, float dt) {
    Nav nav;
    auto edge = [&](int scancode) { return in.key_pressed[static_cast<size_t>(scancode)]; };
    auto held = [&](int scancode) { return in.key_down[static_cast<size_t>(scancode)]; };
    auto pad_edge = [&](int button) { return in.pad_connected && in.pad_pressed[static_cast<size_t>(button)]; };
    auto pad_held = [&](int button) { return in.pad_connected && in.pad_down[static_cast<size_t>(button)]; };
    const bool up = held(SDL_SCANCODE_UP) || held(SDL_SCANCODE_W) || pad_held(SDL_GAMEPAD_BUTTON_DPAD_UP);
    const bool down = held(SDL_SCANCODE_DOWN) || held(SDL_SCANCODE_S) || pad_held(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    const bool left = held(SDL_SCANCODE_LEFT) || held(SDL_SCANCODE_A) || pad_held(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    const bool right = held(SDL_SCANCODE_RIGHT) || held(SDL_SCANCODE_D) || pad_held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    nav.up = step(0, up, dt);
    nav.down = step(1, down, dt);
    nav.left = step(2, left, dt);
    nav.right = step(3, right, dt);
    nav.confirm = edge(SDL_SCANCODE_RETURN) || edge(SDL_SCANCODE_SPACE) || edge(SDL_SCANCODE_Z) || pad_edge(SDL_GAMEPAD_BUTTON_SOUTH);
    nav.cancel = edge(SDL_SCANCODE_X) || edge(SDL_SCANCODE_BACKSPACE) || pad_edge(SDL_GAMEPAD_BUTTON_EAST);
    nav.l1 = edge(SDL_SCANCODE_Q) || edge(SDL_SCANCODE_PAGEUP) || pad_edge(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    nav.r1 = edge(SDL_SCANCODE_E) || edge(SDL_SCANCODE_PAGEDOWN) || pad_edge(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    nav.start = edge(SDL_SCANCODE_TAB) || pad_edge(SDL_GAMEPAD_BUTTON_START);
    nav.any = nav.confirm || nav.cancel || nav.l1 || nav.r1;
    for (int i = 0; i < platform::Input::kKeys && !nav.any; ++i)
      if (in.key_pressed[static_cast<size_t>(i)] && i != SDL_SCANCODE_ESCAPE && i != SDL_SCANCODE_F1 && i != SDL_SCANCODE_F2 &&
          i != SDL_SCANCODE_F3)
        nav.any = true;
    for (int i = 0; i < platform::Input::kButtons && !nav.any; ++i)
      if (in.pad_pressed[static_cast<size_t>(i)]) nav.any = true;
    return nav;
  }

 private:
  // One direction's edge-or-repeat: true on the press and then once per 0.08 s step after 0.4 s held.
  bool step(int direction, bool down, float dt) {
    if (!down) {
      held_[direction] = 0;
      return false;
    }
    if (held_[direction] == 0) {
      held_[direction] = 1e-6f;
      return true;
    }
    const float before = held_[direction];
    held_[direction] += dt;
    if (held_[direction] < 0.4f) return false;
    const float repeat_now = (held_[direction] - 0.4f) / 0.08f, repeat_before = before < 0.4f ? -1.0f : (before - 0.4f) / 0.08f;
    return static_cast<int>(repeat_now) != static_cast<int>(repeat_before);
  }
  float held_[4] = {0, 0, 0, 0};
};

}  // namespace oc::ui
