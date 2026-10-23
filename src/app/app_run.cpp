// The main loop: a fixed-step 60 Hz simulation with interpolated drawing. Every iteration reads the hotkeys and the live [Video]
// settings, runs the Debug tab's requests, advances the game in its current mode (route / replay / boot logo / battle / sweep /
// field), composes the frame and applies what the Esc options window changed.
#include "app/app.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include "core/log.h"
#include "game/effects/gpu_effects.h"
#include "render/camera.h"

namespace oc::app {

int App::run() {
  frametime_log = std::getenv("OC_FRAMETIME") != nullptr;
  ft_prev = std::chrono::steady_clock::now();
  while (window.pump(input) && !quit_requested && !restart_for_update) {
    log_frametime();
    frame_hotkeys();
    frame_settings();
    debug_tools_frame();
    frame_dt = timer.tick();
    // [Game] FastMode: more 60 Hz ticks per frame.
    if (config.fast_mode && !movie_open && !routing && !replaying) frame_dt *= config.fast_speed;
    step = stepper.advance(frame_dt);
    // The art mode cycles SD -> HD -> Modded -> SD (also in the options window).
    if (input.key_pressed[SDL_SCANCODE_F1]) {
      config.art_mode = config.art_mode == platform::ArtMode::Original ? platform::ArtMode::Hd
                        : config.art_mode == platform::ArtMode::Hd     ? platform::ArtMode::Modded
                                                                       : platform::ArtMode::Original;
      config.save(config_file);
      apply_art_mode();
      ui.toast(config.art_mode == platform::ArtMode::Modded ? "Art: Modded" : config.art_mode == platform::ArtMode::Hd ? "Art: HD" : "Art: Original");
    }
    if (!simulate()) break;

    ++frame_count;
    if (screenshot_pending && (battle_ok ? frame_count >= screenshot_tick : (world_ok && world.tick_count() >= screenshot_tick))) {
      ctx.request_screenshot(exe / screenshot_name);
      screenshot_pending = false;
      exit_when_idle = config.debug_exit_after_screenshot;
    }
    // The field background is composed over a BLACK framebuffer (PS1: semi-transparent tiles blend with whatever is behind them, and the
    // base layer of many rooms is itself semi-transparent: a navy clear colour would wash every dark area). The battle buffer is cleared
    // black as well, also for [Debug] Battle.
    const bool boot_logo = flow_ready && flow.boot_logo_active();
    const bool black_clear = field_visible() || battle_ok || boot_logo;
    if (!movie_open) ctx.set_clear_color(black_clear ? 0.0f : 0.08f, black_clear ? 0.0f : 0.09f, black_clear ? 0.0f : 0.14f);
    render::Frame frame;
    if (!ctx.begin_frame(frame)) {
      // Minimised or swapchain being rebuilt.
      SDL_Delay(10);
      continue;
    }
    draw_frame(frame);

    ui.new_frame();
    // The room's entity / script tables stay while a battle, menu, movie or flow screen covers the field.
    debug_field = field_visible();
    apply_options_change(ui.options_window(config, config_file));
    ui.render(frame);
    ctx.end_frame();
    if (!routing && !replaying && !sweeping) limiter.wait(config.frame_cap);

    if (exit_when_idle && ++exit_countdown > 3) break;
    if (route_finished) break;
  }

  if (world_ok && (routing || replaying) && world.view().loaded()) {
    float fx = 0, fz = 0;
    world.player_xz(fx, fz);
    log::info("final state: room {} player {:.0f},{:.0f} field ticks {} BATTLE_RESULT {} scenario {}", world.room_name(), fx, fz, world.tick_count(),
              world.battle_result(), world.vm().var(0));
  }
  if (movie_open) movie_player.close();
  mixer.close_device();
  log::info("exiting after {} simulation ticks, {} validation messages", world_ok ? world.tick_count() : 0,
            render::GpuContext::validation_message_count());
  ctx.wait_idle();
  if (restart_for_update) platform::Updater::restart();
  return exit_code;
}

// OC_FRAMETIME=1 logs the wall time of every main-loop iteration while a field -> battle hand-over is running.
void App::log_frametime() {
  if (!frametime_log) return;
  const auto now = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(now - ft_prev).count();
  ft_prev = now;
  if (world_ok && (world.battle_active() || battle_in_field) && (ms > 20.0 || std::getenv("OC_FRAMETIME")[0] == '2'))
    log::info("frametime: frame {} tick {} {:.1f} ms{}", ++ft_frame, world.tick_count(), ms, ms > 20.0 ? "  <-- SLOW" : "");
}

void App::frame_hotkeys() {
  if (input.key_pressed[SDL_SCANCODE_ESCAPE] && !routing && !replaying) ui.options_open = !ui.options_open;
  if (input.key_pressed[SDL_SCANCODE_F2]) ui.toggle("Debug");
  // A mode switch / monitor move rebuilds the swapchain at the new size.
  if (window.take_resized()) ctx.request_resize();
  // [Video] Widescreen on / off, live and saved (Graphics tab too).
  if (input.key_pressed[SDL_SCANCODE_F3] && !routing && !replaying) {
    config.widescreen = !config.widescreen;
    config.save(config_file);
    ui.toast(config.widescreen ? "Widescreen: On" : "Widescreen: Off");
  }
  // [Game] FastMode / FastSpeed / FastKey: the simulation (field ticks, battle, menus' fades) runs FastSpeed x faster; movies do not.
  if (const SDL_Scancode fast_sc = SDL_GetScancodeFromName(config.fast_key.c_str());
      fast_sc != SDL_SCANCODE_UNKNOWN && input.key_pressed[fast_sc] && !routing && !replaying) {
    config.fast_mode = !config.fast_mode;
    config.save(config_file);
    ui.toast(config.fast_mode ? "Fast mode: " + std::to_string(config.fast_speed) + "x" : std::string("Fast mode: Off"));
    // The key must not also count as an "any button" for the title / flow screens.
    input.key_pressed[fast_sc] = false;
  }
}

// The live [Video] settings read every frame, so the overlay's changes apply at once.
void App::frame_settings() {
  // Room edge fade / texture filters of the field view (Graphics tab).
  if (world_ok) {
    world.view().bg_linear = config.filter_rooms == platform::TexFilter::Bilinear;
    world.view().edge_fade = config.widescreen ? static_cast<float>(config.wide_edge_fade) : 0.0f;
  }
  effects::GpuEffects::set_linear(config.filter_effects == platform::TexFilter::Bilinear);
  // [Video] Widescreen: the extra layer columns of the field / world-map / battle picture, for this frame's
  // ticks (the widened scroll limits) and its drawing, from the window's own shape (any width / height above 4:3); 0 = the 4:3 framing
  // with black bars. screen_extra = the same for the screen veils and the title's 3D sea (never forced to 0 below).
  wide_extra = config.widescreen ? render::wide_extra_for(static_cast<float>(ctx.extent().width), static_cast<float>(ctx.extent().height))
                                 : 0.0f;
  render::set_wide_extra(wide_extra);
  render::set_screen_extra(wide_extra);
  // [Video] FovDeg (horizontal degrees of the 4:3 picture) -> the H scale: the PS1's H 512 gives 2 atan(160 / 512); 35 = exactly the PS1 view.
  render::set_fov_scale(config.fov_deg == 35 ? 1.0f
                                             : (160.0f / 512.0f) / std::tan(static_cast<float>(config.fov_deg) * 0.5f * 3.14159265f / 180.0f));
  // [Video] WideHud ("Wide Menus/HUD"): the columns per side the battle HUD edge groups and the main-menu screens spread out by -
  // default 0 (the 4:3 canvas), 16:9 = at most a 16:9 frame's columns (49 at the 236-row fit), full = the window's.
  hud_cols = !config.widescreen || config.wide_hud == 0 ? 0.0f
             : config.wide_hud == 1 ? std::min(wide_extra, render::wide_extra_for(16.0f, 9.0f)) : wide_extra;
  if (battle_ready) battle.set_wide_hud_columns(hud_cols);
  if (menu_ok) menu.set_wide_columns(hud_cols);
  if (battle_ready) {
    battle.set_auto(config.debug_battle_auto);
    // Cheats: Auto battle = the command picks only; the results page stays manual (RDE).
    battle.set_auto_commands(config.cheats.auto_battle);
  }
}

// Advances the game in its current mode.
bool App::simulate() {
  if (routing && world_ok) {
    route_frame();
  } else if (replaying) {
    replay_frame();
  } else if (flow_ready && flow.boot_logo_active()) {
    // RDE's boot logo: nothing of the game runs until it is over; one image frame
    // per 60 Hz tick; any key / pad button jumps to the end of the current image.
    const bool settings_pause = ui.options_open && ui.current_tab() != "Debug";
    flow.tick_boot_logo(settings_pause ? 0 : step.ticks, !settings_pause && game::GameFlow::any_button(input));
  } else if (battle_ok || battle_in_field) {
    advance(input, frame_dt, step.ticks);
    if (recorder.active()) recorder.write(frame_dt, step.ticks, input);
  } else if (world_ok && sweeping) {
    if (!sweep_frame()) return false;
  } else if (world_ok) {
    field_frame();
  }
  return true;
}

void App::field_frame() {
  // PageUp / PageDown: cycle through the rooms (rooms that fail to load are skipped); only in the field without the flow.
  int dir = 0;
  if (!flow_enabled && !movie_open) {
    if (input.key_pressed[SDL_SCANCODE_PAGEDOWN]) dir = 1;
    if (input.key_pressed[SDL_SCANCODE_PAGEUP]) dir = -1;
  }
  if (dir != 0) {
    const auto& rooms = world.room_list();
    const int room_count = static_cast<int>(rooms.size());
    auto it = std::find(rooms.begin(), rooms.end(), world.room_name());
    int idx = it == rooms.end() ? 0 : static_cast<int>(it - rooms.begin());
    for (int tries = 0; tries < room_count; ++tries) {
      idx = (idx + dir + room_count) % room_count;
      if (world.load_room(rooms[static_cast<size_t>(idx)], 0)) break;
    }
  } else {
    // The settings window pauses the game (the Debug tab watches it running). While paused the render interpolation stays at the
    // last tick's state: the accumulator's alpha kept cycling between the previous and current states (the title sea jittered).
    const bool settings_pause = ui.options_open && ui.current_tab() != "Debug";
    const int ticks = settings_pause ? 0 : step.ticks;
    if (settings_pause) step.alpha = 1.0f;
    advance(input, frame_dt, ticks);
    if (recorder.active()) recorder.write(frame_dt, ticks, input);
  }
}

// What the Esc options window changed this frame.
void App::apply_options_change(const render::OptionsChange& change) {
  if (change.vsync) ctx.set_vsync(config.vsync);
  // [Video] WindowMode / Width / Height / Display apply live; the swapchain follows.
  if (change.window) {
    window.apply_video(static_cast<int>(config.window_mode), config.width, config.height, config.display);
    ctx.request_resize();
  }
  if (change.art_mode || mods_dirty) apply_art_mode();
  mods_dirty = false;
  if (change.audio) apply_audio_config();
  // A live switch remastered <-> AKAO, field and battle.
  if (change.bgm_source) {
    const bool remastered = config.bgm_source != "original";
    // A battle's own song switches itself.
    const bool battle_owns = battle_ready && battle.set_bgm_remastered(remastered);
    if (world_ok) world.set_bgm_remastered(remastered, !battle_owns);
  }
  if (world_ok && change.portraits) {
    world.set_portrait_set(config.portraits);
    if (menu_ok) menu.set_portrait_set(config.portraits);
    if (results_ok) results.set_portrait_set(config.portraits);
  }
  if (change.language) {
    import::Lang nl;
    if (import::parse_lang(config.language, nl)) {
      if (world_ok) world.set_language(nl);
      if (flow_ready) flow.set_language(nl);
      ext_host.language = static_cast<u32>(nl);
      if (menu_ok) menu.set_language(nl);
      if (shop_ok) shop.set_language(nl);
      if (results_ok) results.set_language(nl);
    }
  }
}

}  // namespace oc::app
