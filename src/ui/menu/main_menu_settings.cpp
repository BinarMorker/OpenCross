#include "ui/menu/main_menu_impl.h"

#include <algorithm>
#include <cstdio>

#include "ui/menu/menu_common.h"
#include "ui/ps1_window.h"
#include "ui/window_frame.h"

namespace oc::ui {

namespace {

// Customize rows in the overlay's order: the option bit of the options word and the menutest.txt rows of the label and its two choices.
// In order: Sound (Stereo / Monaural), Window Frame (Default / Custom), Cursor Position (Default / Memorized), Accuracy Selection
// (Manual / Auto), Controller (Default / Custom), [button] Pressed (Run / Walk), Vibration Function (Off / On), Left Stick Sensitivity
// (Digital / Analog).
struct SettingRow {
  u16 mask;
  u32 label, left, right;
};
const SettingRow kSettings[8] = {
    {0x04, 0x801F4A7C, 0x801F4A8E, 0x801F4AA0},
    {0x08, 0x801F4AB2, 0x801F4AC4, 0x801F4AD6},
    {0x01, 0x801F4AE8, 0x801F4AFA, 0x801F4B0C},
    {0x02, 0x801F4B1E, 0x801F4B30, 0x801F4B42},
    {0x100, 0x801F4B54, 0x801F4B66, 0x801F4B78},
    {0x200, 0x801F4BD2, 0x801F4BE4, 0x801F4BF6},
    {0x400, 0x801F4B8A, 0x801F4BAE, 0x801F4BC0},
    {0x800, 0x801F4C08, 0x801F4C2C, 0x801F4C3E}};

}  // namespace

// Customize: left / right / OK toggle the row's option bit; the two stick rows need an analog pad. OK on "Window Frame: Custom" opens
// the skin picker, which steps the skin byte 0..14 (wrapping) to the next skin whose bit in the owned-frames mask is set.
void MainMenu::Impl::update_settings(const Nav& nav, const platform::Input& in) {
  if (skin_pick) {
    auto step_skin = [&](int direction) {
      // An empty mask would loop forever below.
      if ((gs->slot.frames & 0x7FFF) == 0) return;
      int skin = gs->skin;
      do skin = wrap(skin + direction, 15);
      while (((gs->slot.frames >> skin) & 1) == 0);
      gs->skin = static_cast<u8>(skin);
      sfx(1);
    };
    if (nav.left) step_skin(-1);
    if (nav.right) step_skin(1);
    if (nav.ok || nav.cancel) skin_pick = false, sfx(nav.ok ? 8 : 6);
    return;
  }
  if (nav.up) set_cur = wrap(set_cur - 1, 8), sfx(1);
  if (nav.down) set_cur = wrap(set_cur + 1, 8), sfx(1);
  const SettingRow& row = kSettings[set_cur];
  const bool enabled = set_cur < 6 || in.pad_connected;
  if (nav.cancel) {
    back_to_main(4);
    sfx(6);
    return;
  }
  if (!enabled) {
    if (nav.left || nav.right || nav.ok) sfx(0x18);
    return;
  }
  if (nav.left) gs->set_option(row.mask, false), sfx(1);
  if (nav.right) gs->set_option(row.mask, true), sfx(1);
  if (nav.ok) {
    if (set_cur == 1 && gs->option(opt::kCustomWindow)) skin_pick = true;
    else gs->set_option(row.mask, !gs->option(row.mask));
    sfx(8);
  }
}

// ---- settings (state 0): option panel frame 0x40 at (48,44); rows y = 60 + 24 i; label x 72; choices x 278 / 376
// with the 82x20 highlight quad (0xA0,0x60,0) behind the chosen one; hand cursor at (40, 60 + 24 row). The rows follow the
// overlay's mask table {4, 8, 1, 2, 0x100, 0x200, 0x400, 0x800}.
void MainMenu::Impl::build_settings() {
  Frame(64, 48, 44);
  // "Customize" in the frame's top notch.
  notch_caption(48, 44, 5);
  for (int i = 0; i < 8; ++i) {
    const SettingRow& setting = kSettings[i];
    const float y = 60.0f + 24.0f * static_cast<float>(i);
    const bool enabled = i < 6 || pad_present;
    const bool right = gs->option(setting.mask);
    const u32 dis = ps1_colour(0x404040), chosen = ps1_colour(0x004048);
    const std::u32string label = utf32(menu_str(setting.label, "?"));
    TP(i == 5 ? 100.0f : 72.0f, y, label, enabled ? kText : dis, 0.9f);
    // Button glyph of function 1 at (0x48, 0xB0), 24 x 24.
    if (i == 5) btn(72, 176, 1);
    // Chosen-choice highlight, drawn before the choices.
    if (enabled) {
      rect(PX(right ? 375.0f : 277.0f), PY(y - 2), PX(82), PY(20), ps1_colour(0x0060A0));
    }
    TP(278, y, utf32(menu_str(setting.left, "-")), !enabled ? dis : (!right ? chosen : kText), 0.9f);
    TP(376, y, utf32(menu_str(setting.right, "-")), !enabled ? dis : (right ? chosen : kText), 0.9f);
  }
  if (skin_pick) {
    skin_preview();
  } else {
    tri(40, 60.0f + 24.0f * static_cast<float>(set_cur), true);
  }
}

// State 1 (ui/window_frame.h): the skin of the chosen frame as a real
// window (7 x 4 opaque fill grid, corner / edge pieces, page leaf bottom-right), "Custom Window Frame" on one line near the top (y 0x132) and the skin
// name (y 0x162, one menutest.txt row per skin byte) centred below it, both white (0x808080) with the menu font's shadow; the side pointers
// (frames 3 / 1) at (0x5E - a, 0x152) and (a + 0x1AE, 0x152).
void MainMenu::Impl::skin_preview() {
  const int skin_byte = gs->skin;
  // Config byte -> texture index.
  const int idx = skin_byte == 0 ? 3 : skin_byte <= 3 ? skin_byte - 1 : skin_byte <= 14 ? skin_byte : 15;
  static constexpr int kNn[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17};
  const int nn = kNn[idx];
  char fn[48];
  std::snprintf(fn, sizeof fn, "hd/window/w%02d.png", nn);
  // Classic art + Ps1ResMenus: the PS1 sheet cdrom/map/window/wN.tim (128 x 96; wNN.png is its 4x redraw with the same layout).
  Tex* p1 = ps1_screen() ? texs.ps1("ps1:w" + std::to_string(nn), [&] { return ps1win::skin(*vfs, nn); }) : nullptr;
  const SkinWindow window = build_skin_window(idx, (frames / 6) % 5);
  float x0 = 1e9f, x1 = -1e9f;
  // The wNN.png skin is drawn in both art modes.
  if (Tex* tex = p1 ? p1 : texs.load(fn)) {
    for (const WinQuad& quad : window.quads) {
      Img(tex, quad.x, quad.y, quad.w, quad.h, quad.u0, quad.v0, quad.u0 + quad.du, quad.v0 + quad.dv);
      if (quad.w < 100) x0 = std::min(x0, quad.x), x1 = std::max(x1, quad.x + quad.w);
    }
    Img(tex, window.leaf.x, window.leaf.y, window.leaf.w, window.leaf.h, window.leaf.u0, window.leaf.v0, window.leaf.u0 + window.leaf.du,
        window.leaf.v0 + window.leaf.dv);
  }
  if (x0 > x1) x0 = window.left, x1 = window.right;
  // Window centre in PS1 px.
  const float cx = (x0 + x1) * 0.5f / xscale;
  std::u32string title = utf32(menu_str(0x801F4AD6, "Custom"));
  if (!jp()) title += U" ";
  title += utf32(menu_str(0x801F4AB2, "Window Frame"));
  const std::u32string nm = utf32(menu_str(0x801F4EA4 + 0x14u * static_cast<u32>(gs->skin), "?"));
  const float rel = fit_rel(title, 170.0f, 0.85f);
  TP(cx - twp(title, rel) * 0.5f, 306, title, kWhite, rel);
  TP(cx - twp(nm, 0.85f) * 0.5f, 354, nm, kWhite, 0.85f);
  const float drift = static_cast<float>(((frames % 23) * 0x3000) >> 16);
  pointer(94.0f - drift, 338, true);
  pointer(430.0f + drift, 338, false);
}

}  // namespace oc::ui
