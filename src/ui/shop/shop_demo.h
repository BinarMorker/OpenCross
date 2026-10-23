#pragma once
// ShopDemo: drives ShopScreen with real data. It loads a copy of a save (never written back), takes
// slot 4 as the game state (in memory only; the demo tops up gold / materials / elements so every list has content).
// Wire-up in the app (debug hook, like MenuDemo):
//   ui::ShopDemo shop_demo;  const bool shop_ok = shop_demo.init(ctx, files, sprites, lang);
//   if (shop_ok && input.key_pressed[SDL_SCANCODE_F5]) shop_demo.open(ui::ShopKind::Element, 0);
//   const bool in_shop = shop_ok && shop_demo.update(input);   // true while open: skip world updates
//   if (in_shop) shop_demo.draw(frame, vw, vh, true);
#include <filesystem>
#include <memory>

#include "data/tables.h"
#include "render/gpu_context.h"
#include "ui/shop/shop_screen.h"

namespace oc::ui {

class ShopDemo {
 public:
  ShopDemo();
  ~ShopDemo();
  ShopDemo(const ShopDemo&) = delete;
  ShopDemo& operator=(const ShopDemo&) = delete;

  bool init(render::GpuContext& ctx, const vfs::Vfs& vfs, render::SpriteBatch& sprites, import::Lang lang = import::Lang::En);
  void shutdown();

  void open(ShopKind kind, int shop_id);
  bool is_open() const;
  void set_mode(ArtMode mode);
  // Returns true while a shop is open.
  bool update(const platform::Input& in);
  void draw(const render::Frame& frame, float vw, float vh, bool resume = false);

  // Scripted walk through every shop kind (a few shop ids each), HD then original art, one PNG per step:
  // <n>_<kind><id>_<step>_<hd|orig>.png. Driven by update().
  void screenshot_sequence(render::GpuContext& ctx, std::filesystem::path out_dir);
  bool sequence_active() const;
  const std::vector<std::filesystem::path>& shots() const;

  ShopScreen& screen();
  GameState& state();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace oc::ui
