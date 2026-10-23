#include "ui/keymap_screen.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "core/log.h"

namespace oc::ui {
namespace {

// Layout width of both pages.
constexpr float kImageW = 1620.0f;
constexpr float kScale = kCanvasW / kImageW;
constexpr float kStep = 52.0f, kHalfStep = 26.0f, kGlyph = 48.0f;

// Windows virtual key -> cell of ui/keyboard_<l>.png (16 x 7 cells); 0 / absent = no glyph.
int keyboard_cell(int vk) {
  struct Cell {
    u8 vk;
    u8 index;
  };
  static constexpr Cell cells[] = {
      {0x08, 51}, {0x09, 50}, {0x0D, 49}, {0x13, 66}, {0x14, 62}, {0x1B, 48}, {0x20, 1},   {0x21, 58}, {0x22, 59},  {0x23, 61},  {0x24, 60},
      {0x25, 55}, {0x26, 57}, {0x27, 54}, {0x28, 56}, {0x2C, 65}, {0x2D, 52}, {0x2E, 53},  {0x30, 7},  {0x31, 8},   {0x32, 9},   {0x33, 10},
      {0x34, 11}, {0x35, 12}, {0x36, 13}, {0x37, 14}, {0x38, 15}, {0x39, 16}, {0x41, 19},  {0x42, 20}, {0x43, 21},  {0x44, 22},  {0x45, 23},
      {0x46, 24}, {0x47, 25}, {0x48, 26}, {0x49, 27}, {0x4A, 28}, {0x4B, 29}, {0x4C, 30},  {0x4D, 31}, {0x4E, 32},  {0x4F, 33},  {0x50, 34},
      {0x51, 35}, {0x52, 36}, {0x53, 37}, {0x54, 38}, {0x55, 39}, {0x56, 40}, {0x57, 41},  {0x58, 42}, {0x59, 43},  {0x5A, 44},  {0x5B, 98},
      {0x5C, 102}, {0x5D, 103}, {0x60, 79}, {0x61, 80}, {0x62, 81}, {0x63, 82}, {0x64, 83}, {0x65, 84}, {0x66, 85}, {0x67, 86},  {0x68, 87},
      {0x69, 88}, {0x6A, 91}, {0x6B, 93}, {0x6C, 94}, {0x6D, 92}, {0x6E, 89}, {0x6F, 90},  {0x70, 67}, {0x71, 68},  {0x72, 69},  {0x73, 70},
      {0x74, 71}, {0x75, 72}, {0x76, 73}, {0x77, 74}, {0x78, 75}, {0x79, 76}, {0x7A, 77},  {0x7B, 78}, {0x90, 64},  {0x91, 63},  {0xA0, 95},
      {0xA1, 99}, {0xA2, 96}, {0xA3, 100}, {0xA4, 97}, {0xA5, 101}, {0xBA, 2}, {0xBB, 17}, {0xBC, 3},  {0xBD, 4},   {0xBE, 5},   {0xBF, 6},
      {0xC0, 45}, {0xDB, 46}, {0xDD, 47}, {0xDE, 18}};
  for (const Cell& cell : cells)
    if (cell.vk == vk) return cell.index;
  return 0;
}

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
  // "Next" latch: a key press (anything but the F-key toggles) or the release of a pad button other than the stick clicks.
  for (size_t i = 0; i < in.key_pressed.size(); ++i)
    if (in.key_pressed[i] && (i < SDL_SCANCODE_F1 || i > SDL_SCANCODE_F12)) next_ = true;
  for (size_t button = 0; button < pad_prev_.size(); ++button) {
    const bool down = in.pad_connected && in.pad_down[button];
    if (pad_prev_[button] && !down && in.pad_connected && button != SDL_GAMEPAD_BUTTON_LEFT_STICK && button != SDL_GAMEPAD_BUTTON_RIGHT_STICK) next_ = true;
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
    const int cell = keyboard_cell(vk);
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
    // Move/Select: set 0 = the arrows (y 247.5), set 1 = W S A D (y 301.5), four cells from x 110 (an unbound key moves the row half a step).
    const int sets[2][4] = {{0x26, 0x28, 0x25, 0x27}, {0x57, 0x53, 0x41, 0x44}};
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
    auto pair = [&](int vk0, int vk1, float x, float y) {
      if (vk0 == 0 || vk1 == 0) x += kHalfStep;
      key_glyph(vk0, x, y, a1);
      key_glyph(vk1, x + kStep, y, a1);
    };
    // Rows: Talk/Confirm (Enter, Z; Space too), Run/Cancel (X cancels, Shift runs), key item window (F), Menu (Tab),
    // then the ESC menu (the options overlay).
    pair(0x0D, 0x5A, 162.0f, 415.5f);
    pair(0x58, 0xA0, 162.0f, 525.5f);
    pair(0x46, 0, 162.0f, 644.5f);
    pair(0x09, 0, 162.0f, 772.5f);
    key_glyph(0x1B, 938.0f, 793.5f, a1);
  }
}

}  // namespace oc::ui
