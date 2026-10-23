#include "ui/shop/shop_demo.h"

#include <SDL3/SDL_scancode.h>

#include <format>

#include "core/log.h"
#include "platform/paths.h"

namespace oc::ui {

namespace {
constexpr int kRet = SDL_SCANCODE_RETURN, kDown = SDL_SCANCODE_DOWN, kX = SDL_SCANCODE_X, kRight = SDL_SCANCODE_RIGHT,
              kUp = SDL_SCANCODE_UP;

struct Step {
  std::vector<int> keys;
  // until_confirm (trading post): offer Elements until the confirmation opens.
  bool until_confirm = false;
  std::string tag;
  // Frames before the first key (the trading post gates its input until its strings are prepared).
  int delay = 0;
};
struct Group {
  ShopKind kind;
  int id;
  std::vector<Step> steps;
};

const char* kind_name(ShopKind kind) { return kind == ShopKind::Element ? "element" : kind == ShopKind::Smith ? "smith" : "trade"; }

std::filesystem::path find_save_copy() {
  std::filesystem::path dir = platform::exe_dir();
  for (int i = 0; i < 8 && !dir.empty(); ++i) {
    if (std::filesystem::exists(dir / "tmp" / "save.dat")) return dir / "tmp";
    if (dir == dir.parent_path()) break;
    dir = dir.parent_path();
  }
  return {};
}

template <class NonEmptyFn>
std::vector<int> first_rows(size_t rows, NonEmptyFn nonempty) {
  std::vector<int> out;
  for (size_t i = 0; i < rows && out.size() < 2; ++i)
    if (nonempty(static_cast<int>(i))) out.push_back(static_cast<int>(i));
  return out;
}

std::vector<Group> make_groups(const shop::ShopData& sd) {
  std::vector<Group> groups;
  for (int id : first_rows(sd.el_shop.size(), [&](int i) { return !sd.element_row(i).empty(); }))
    groups.push_back({ShopKind::Element, id,
                 {{{}, false, "top"}, {{kRet}, false, "buy_list"}, {{kRet}, false, "buy_qty"}, {{kRight, kRight, kUp}, false, "buy_qty_more"},
                  {{kX, kX, kDown, kRet}, false, "sell_list"}, {{kRet}, false, "sell_qty"}}});
  for (int id : first_rows(sd.waa_shop.size(), [&](int i) { return !sd.gear_row(i).empty(); }))
    groups.push_back({ShopKind::Smith, id,
                 {{{}, false, "top"}, {{kRet}, false, "category"}, {{kRet}, false, "forge_list"}, {{kRet}, false, "forge_confirm"},
                  {{kX, kX, kX, kDown, kRet, kRet}, false, "break_list"}, {{kRet}, false, "break_confirm"},
                  {{kX, kX, kX, kDown, kRet, kRet}, false, "sell_list"}, {{kRet}, false, "sell_qty"}}});
  // Trading post: greeting, goods list, offer list, the confirmation (Circle / Down until it opens), the trade (Circle after 30 frames).
  for (int id : first_rows(sd.trade.size(), [&](int i) { return shop::trade_row(sd, i).count > 0; }))
    groups.push_back({ShopKind::Trading, id,
                      {{{}, false, "top"}, {{kRet}, false, "list", 0}, {{kRet}, false, "offer", 24}, {{}, true, "confirm", 16},
                       {{kRet}, false, "traded", 36}}});
  return groups;
}
}  // namespace

struct ShopDemo::Impl {
  render::GpuContext* ctx = nullptr;
  data::GameTables tables;
  MenuRules rules;
  shop::ShopData sd;
  render::TextBatch text;
  ShopScreen screen;
  GameState state;
  bool ready = false;
  // sequence
  std::vector<Group> groups;
  size_t gi = 0, si = 0;
  // phase: 0 start group, 1 keys, 2 wait for the shot. pre: frames waited for Step::delay.
  int phase = 0;
  int wait = 0;
  size_t key_i = 0;
  int pair_i = 0;
  int pre = 0;
  int shot_no = 0;
  bool active = false;
  bool hd_pass = true;
  std::filesystem::path out_dir;
  std::vector<std::filesystem::path> shots;
  ArtMode saved_mode = ArtMode::Hd;

  // Gold, materials for the first recipes, gear in the bag, a few Elements.
  void top_up() {
    state.set_gold(500000);
    for (int row = 0; row < static_cast<int>(sd.waa_shop.size()); ++row) {
      int per_cat[3] = {0, 0, 0};
      for (int id : sd.gear_row(row)) {
        const auto* item_info = tables.item(id);
        if (!item_info || item_info->kind() > 2 || per_cat[item_info->kind()] >= 2) continue;
        ++per_cat[item_info->kind()];
        for (const shop::Mat& material : shop::recipe(state, id))
          while (state.item_count(material.id) < material.qty + 1)
            if (!state.add_item(material.id)) break;
        if (state.item_count(id) < 2) state.add_item(id), state.add_item(id);
      }
    }
    int added = 0;
    for (int id = 1; id < 0x80 && added < 30; ++id) {
      const data::ElemRec* record = rules.elem(id);
      if (!record || record->level() < 1 || record->level() > 3 || !rules.is_element(id)) continue;
      if (state.element_count(id) < 3) {
        for (int k = 0; k < 3; ++k) state.add_element(id);
        ++added;
      }
    }
  }
};

ShopDemo::ShopDemo() : impl_(std::make_unique<Impl>()) {}
ShopDemo::~ShopDemo() { shutdown(); }

bool ShopDemo::init(render::GpuContext& ctx, const vfs::Vfs& vfs, render::SpriteBatch& sprites, import::Lang lang) {
  Impl& impl = *impl_;
  impl.ctx = &ctx;
  std::string err;
  if (!impl.tables.load(vfs, &err)) {
    log::warn("shop demo: tables: {}", err);
    return false;
  }
  impl.rules.tables = &impl.tables;
  if (!impl.rules.load_pg(vfs)) log::warn("shop demo: menu_pg.bin missing; prices are 0");
  const std::filesystem::path tmp = find_save_copy();
  if (tmp.empty()) {
    log::warn("shop demo: tmp/save.dat not found");
    return false;
  }
  try {
    save::SaveDat dat = save::SaveDat::load(tmp / "save.dat");
    const save::MemCard* card = dat.card(1);
    if (!card) throw std::runtime_error("no memory card 1");
    bool found = false;
    for (const auto& listed : save::list_slots(*card))
      if (listed.slot == 4) {
        impl.state = GameState(save::read_slot(*card, listed.block), &impl.rules);
        found = true;
      }
    if (!found) throw std::runtime_error("slot 4 missing");
  } catch (const std::exception& error) {
    log::warn("shop demo: save: {}", error.what());
    return false;
  }
  if (!impl.text.init(ctx, sprites)) return false;
  if (!impl.screen.init(ctx, vfs, sprites, impl.text, impl.tables, lang)) return false;
  impl.screen.set_rules(&impl.rules);
  impl.sd = shop::ShopData::from_tables(impl.tables);
  impl.sd.load_waa_shop(vfs);
  impl.top_up();
  impl.ready = true;
  log::info("shop demo: ready (slot 4 of {}, never written back)", (tmp / "save.dat").string());
  return true;
}

void ShopDemo::shutdown() {
  Impl& impl = *impl_;
  if (!impl.ctx) return;
  impl.screen.shutdown();
  impl.text.destroy();
  impl.ctx = nullptr;
  impl.ready = false;
}

void ShopDemo::open(ShopKind kind, int shop_id) {
  if (impl_->ready) impl_->screen.open(impl_->state, kind, shop_id);
}
bool ShopDemo::is_open() const { return impl_->ready && !impl_->screen.closed(); }
void ShopDemo::set_mode(ArtMode mode) { impl_->screen.set_mode(mode); }
ShopScreen& ShopDemo::screen() { return impl_->screen; }
GameState& ShopDemo::state() { return impl_->state; }
bool ShopDemo::sequence_active() const { return impl_->active; }
const std::vector<std::filesystem::path>& ShopDemo::shots() const { return impl_->shots; }

void ShopDemo::screenshot_sequence(render::GpuContext& ctx, std::filesystem::path out_dir) {
  Impl& impl = *impl_;
  if (!impl.ready) return;
  impl.ctx = &ctx;
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  impl.out_dir = std::move(out_dir);
  impl.groups = make_groups(impl.sd);
  impl.gi = impl.si = 0;
  impl.phase = 0;
  impl.shot_no = 0;
  impl.hd_pass = true;
  impl.active = true;
  impl.shots.clear();
  impl.saved_mode = impl.screen.mode();
}

bool ShopDemo::update(const platform::Input& in) {
  Impl& impl = *impl_;
  if (!impl.ready) return false;
  if (!impl.active) {
    if (impl.screen.closed()) return false;
    impl.screen.update(in);
    return !impl.screen.closed();
  }
  impl.screen.set_mode(impl.hd_pass ? ArtMode::Hd : ArtMode::Original);
  if (impl.gi >= impl.groups.size()) {
    if (impl.hd_pass) {
      impl.hd_pass = false;
      impl.gi = 0;
      impl.phase = 0;
      return true;
    }
    impl.active = false;
    impl.screen.set_mode(impl.saved_mode);
    log::info("shop demo: {} screenshots written to {}", impl.shots.size(), impl.out_dir.string());
    return !impl.screen.closed();
  }
  const Group& group = impl.groups[impl.gi];
  if (impl.phase == 0) {
    impl.state.set_gold(500000);
    impl.screen.open(impl.state, group.kind, group.id);
    impl.si = 0;
    impl.key_i = 0;
    impl.pair_i = 0;
    impl.pre = 0;
    impl.wait = 0;
    impl.phase = 1;
  }
  const Step& step = group.steps[impl.si];
  platform::Input fake;
  if (impl.phase == 1 && impl.pre < step.delay) {
    ++impl.pre;
    impl.screen.update(fake);
    return true;
  }
  if (impl.phase == 1) {
    // One key every 3 frames (pressed on the first, released on the next); until_confirm alternates Enter / Down.
    const bool last = step.until_confirm ? (impl.screen.state_name() == "confirm" || impl.pair_i >= 200) : impl.key_i >= step.keys.size();
    if (last) {
      impl.phase = 2;
      impl.wait = 0;
    } else {
      if (impl.wait % 3 == 0) {
        const int scancode = step.until_confirm ? ((impl.pair_i & 1) ? kDown : kRet) : step.keys[impl.key_i];
        fake.key_pressed[static_cast<size_t>(scancode)] = fake.key_down[static_cast<size_t>(scancode)] = true;
        ++impl.key_i;
        ++impl.pair_i;
        if (step.until_confirm && impl.pair_i % 20 == 0) log::info("shop demo: offering, press {} state {}", impl.pair_i, impl.screen.state_name());
      }
      ++impl.wait;
      impl.screen.update(fake);
      return true;
    }
  }
  if (impl.phase == 2) {
    ++impl.wait;
    // Layout audit (OC_TEXT_AUDIT=1): the frame drawn after this update.
    if (impl.wait == 5) impl.text.audit_clear();
    if (impl.wait == 6) {
      const auto path = impl.out_dir / std::format("{:03}_{}{}_{}_{}.png", ++impl.shot_no, kind_name(group.kind), group.id, step.tag, impl.hd_pass ? "hd" : "orig");
      impl.text.audit_report(path.stem().string());
      impl.ctx->request_screenshot(path);
      impl.shots.push_back(path);
      log::info("shop demo: {} state={} ({})", path.filename().string(), impl.screen.state_name(), impl.screen.closed() ? "closed" : "open");
    } else if (impl.wait >= 9) {
      impl.phase = 1;
      impl.wait = 0;
      impl.key_i = 0;
      impl.pair_i = 0;
      impl.pre = 0;
      if (++impl.si >= group.steps.size()) {
        ++impl.gi;
        impl.phase = 0;
      }
    }
  }
  impl.screen.update(fake);
  return true;
}

void ShopDemo::draw(const render::Frame& frame, float vw, float vh, bool resume) {
  if (impl_->ready && !impl_->screen.closed()) impl_->screen.draw(frame, vw, vh, resume);
}

}  // namespace oc::ui
