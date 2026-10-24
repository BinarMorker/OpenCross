#include "ui/keymap_screen.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "core/log.h"
#include "platform/keymap.h"

namespace oc::ui {
namespace {

// Layout width of both pages.
constexpr float kImageW = 1620.0f;
constexpr float kScale = kCanvasW / kImageW;
constexpr float kStep = 52.0f, kHalfStep = 26.0f, kGlyph = 48.0f;

// Page 1 glyphs (cells: A 8, B 9, Y 10, X 11, L stick 12, D-pad 13, START 14,
// BACK 15, L3 16, R3 17, LT 22, RT 23) for the buttons OpenCross binds (nav.h / app/app_sim.cpp).
struct PadGlyph {
  u8 cell;
  float x, y;
};
constexpr PadGlyph kPadGlyphs[] = {
    // field: Talk/Confirm = A
    {8, 192.0f, 282.5f},
    // Run/Cancel = B
    {9, 192.0f, 376.5f},
    // Open key item window = Y (pad North)
    {10, 192.0f, 470.5f},
    // Menu = Start
    {14, 192.0f, 564.5f},
    // battle: Confirm = A
    {8, 192.0f, 700.5f},
    // Cancel = B
    {9, 192.0f, 794.5f},
    // Move/Select = D-pad
    {13, 916.0f, 230.5f},
    // + left stick
    {12, 968.0f, 230.5f},
};

u32 alpha_white(float a) { return render::pack_rgba(255, 255, 255, static_cast<u8>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f))); }

}  // namespace

bool KeyMapScreen::begin(UiKit& kit, import::Lang lang) {
  // Page image suffix and keyboard sheet suffix per language.
  static constexpr const char* kPage[6] = {"en", "fr", "it", "du", "es", "jp"};
  static constexpr const char* kKeys[6] = {"e", "f", "i", "g", "s", "j"};
  const int li = std::clamp(static_cast<int>(lang), 0, 5);
  counter_ = 0;
  acc_ = 0;
  next_ = false;
  // No release latch from a button held across the transition.
  pad_prev_.fill(true);
  if (loaded_lang_ != li) {
    auto load = [&](const std::string& path) {
      auto raw = kit.vfs().read(path);
      if (!raw) {
        log::warn("keymap: {} missing", path);
        return render::TexRef{};
      }
      return kit.upload_png(*raw);
    };
    pad_page_ = load(std::string("hd/userguide/steampad_") + kPage[li] + ".png");
    key_page_ = load(std::string("hd/userguide/steamkey_") + kPage[li] + ".png");
    pad_glyphs_ = load("ui/gamepad_assign.png");
    key_glyphs_ = load(std::string("ui/keyboard_") + kKeys[li] + ".png");
    loaded_lang_ = li;
  }
  done_ = !pad_page_ || !key_page_;
  return !done_;
}

void KeyMapScreen::step() {
  if (counter_ == 0) {
    counter_ = 1;
  } else if (counter_ == 15 || counter_ == 30) {
    if (next_) {
      next_ = false;
      ++counter_;
    }
  } else if (counter_ >= 45) {
    counter_ = 0;
    done_ = true;
  } else {
    ++counter_;
  }
}

void KeyMapScreen::update(const platform::Input& in, float dt) {
  if (done_) return;
  // "Next" latch: a key press (anything but the F-key toggles) or the release of a pad button other than the stick clicks and Back (the
  // settings window).
  for (size_t i = 0; i < in.key_pressed.size(); ++i)
    if (in.key_pressed[i] && (i < SDL_SCANCODE_F1 || i > SDL_SCANCODE_F12)) next_ = true;
  for (size_t button = 0; button < pad_prev_.size(); ++button) {
    const bool down = in.pad_connected && in.pad_down[button];
    if (pad_prev_[button] && !down && in.pad_connected && button != SDL_GAMEPAD_BUTTON_LEFT_STICK && button != SDL_GAMEPAD_BUTTON_RIGHT_STICK &&
        button != SDL_GAMEPAD_BUTTON_BACK)
      next_ = true;
    pad_prev_[button] = down;
  }
  acc_ += std::clamp(dt, 0.0f, 0.25f) * 60.0f;
  while (acc_ >= 1.0f && !done_) {
    acc_ -= 1.0f;
    step();
  }
}

void KeyMapScreen::draw(DrawList& dl, UiKit& kit) const {
  if (done_) return;
  const int tick = counter_;
  float a0 = 0.0f, a1 = 0.0f;
  if (tick < 15) {
    a0 = static_cast<float>(tick) / 15.0f;
  } else if (tick < 30) {
    a0 = 1.0f;
    a1 = static_cast<float>(tick - 15) / 15.0f;
  } else if (tick < 45) {
    a1 = static_cast<float>(45 - tick) / 15.0f;
  }
  dl.rect(kit.white(), 0, 0, kCanvasW, kCanvasHeight, render::pack_rgba(0, 0, 0, 255));
  auto quad = [&](render::TexRef set, float x, float y, float w, float h, float u0, float v0, float du, float dv, float a) {
    dl.image(set, x * kScale, y * kScale, w * kScale, h * kScale, alpha_white(a), u0, v0, u0 + du, v0 + dv);
  };
  auto key_glyph = [&](int vk, float x, float y, float a) {
    const int cell = platform::keyboard_cell(vk);
    if (cell <= 0 || !key_glyphs_) return;
    quad(key_glyphs_, x, y, kGlyph, kGlyph, static_cast<float>(cell & 15) / 16.0f, static_cast<float>(cell >> 4) / 7.0f, 1.0f / 16.0f, 1.0f / 7.0f, a);
  };
  if (a0 > 0.0f) {
    dl.image(pad_page_, 0, 0, kCanvasW, kCanvasHeight, alpha_white(a0));
    if (pad_glyphs_)
      for (const PadGlyph& glyph : kPadGlyphs)
        quad(pad_glyphs_, glyph.x - 4.0f, glyph.y - 4.0f, kGlyph, kGlyph, static_cast<float>(glyph.cell % 14) / 14.0f, static_cast<float>((glyph.cell / 14) * 2 + 1) * 0.25f,
             1.0f / 14.0f, 0.25f, a0);
  }
  if (a1 > 0.0f) {
    dl.image(key_page_, 0, 0, kCanvasW, kCanvasHeight, alpha_white(a1));
    using platform::Action;
    // Move/Select: set 0 = the arrows (y 247.5), set 1 = W S A D (y 301.5), four cells from x 110 (an unbound key moves the row half a step).
    int sets[2][4];
    const Action dirs[4] = {Action::Up, Action::Down, Action::Left, Action::Right};
    for (int set_index = 0; set_index < 2; ++set_index)
      for (int k = 0; k < 4; ++k) sets[set_index][k] = platform::action_vks(dirs[k])[static_cast<size_t>(set_index)];
    for (int set_index = 0; set_index < 2; ++set_index) {
      float x = 110.0f;
      for (int vk : sets[set_index]) x += vk == 0 ? kHalfStep : 0.0f;
      for (int vk : sets[set_index]) {
        if (vk == 0) continue;
        key_glyph(vk, x, set_index == 0 ? 247.5f : 301.5f, a1);
        x += kStep;
      }
    }
    // Two-key rows: both move half a step right when either key is unbound.
    auto pair = [&](Action action, float x, float y) {
      const std::array<int, 2> vks = platform::action_vks(action);
      if (vks[0] == 0 || vks[1] == 0) x += kHalfStep;
      key_glyph(vks[0], x, y, a1);
      key_glyph(vks[1], x + kStep, y, a1);
    };
    // Rows: Talk/Confirm, Run/Cancel, key item window, Menu (both RDE key sets each), then the ESC menu (the options overlay).
    pair(Action::Ok, 162.0f, 415.5f);
    pair(Action::Cancel, 162.0f, 525.5f);
    pair(Action::Item, 162.0f, 644.5f);
    pair(Action::Menu, 162.0f, 772.5f);
    key_glyph(0x1B, 938.0f, 793.5f, a1);
  }
}

}  // namespace oc::ui
