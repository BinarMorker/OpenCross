#pragma once
// MenuDemo: drives MainMenu with real data. It loads a save file (never written back),
// takes slot 4 as the game state and saves to a separate menu save file. Wire-up in the app (app/app.h):
//   #include "ui/menu/menu_demo.h"
//   ui::MenuDemo menu_demo;  const bool menu_ok = menu_demo.init(ctx, files, sprites, lang);   // after sprites.init()
//   if (menu_ok && input.key_pressed[SDL_SCANCODE_F3]) menu_demo.open();                        // each frame, before updates
//   if (menu_ok && input.key_pressed[SDL_SCANCODE_F4]) menu_demo.screenshot_sequence(ctx, exe / "menu_shots");
//   const bool in_menu = menu_ok && menu_demo.update(input);   // true while open: skip world.update(input, ...) then
//   if (in_menu) menu_demo.draw(frame, vw, vh, true);          // after the field overlay, before ui.new_frame()
#include <filesystem>
#include <memory>

#include "data/tables.h"
#include "render/gpu_context.h"
#include "ui/menu/main_menu.h"

namespace oc::ui {

class MenuDemo {
 public:
  MenuDemo();
  ~MenuDemo();
  MenuDemo(const MenuDemo&) = delete;
  MenuDemo& operator=(const MenuDemo&) = delete;

  // Loads the tables, the save copy and the menu. False (and logged) if any of them is missing.
  // `slot` = CHRONOnn slot of card 1 used as the game state (default 4).
  bool init(render::GpuContext& ctx, const vfs::Vfs& vfs, render::SpriteBatch& sprites, import::Lang lang = import::Lang::En, int slot = 4);
  void shutdown();

  // Opens the main menu on the loaded state (edits persist between opens).
  void open();
  bool is_open() const;
  void set_mode(ArtMode mode);
  // Returns true while the menu is open (the caller should then not feed `in` to anything else).
  bool update(const platform::Input& in);
  void draw(const render::Frame& frame, float vw, float vh, bool resume = false);

  // Cycles through the screens in HD, then original art, writing one PNG per screen into `out_dir`
  // (<n>_<screen>_<hd|orig>.png) through ctx.request_screenshot. Driven by update().
  void screenshot_sequence(render::GpuContext& ctx, std::filesystem::path out_dir);
  bool sequence_active() const;
  const std::vector<std::filesystem::path>& shots() const;

  MainMenu& menu();
  GameState& state();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace oc::ui
