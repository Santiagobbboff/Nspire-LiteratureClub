// Native Ndless backend. Built by the ARM toolchain, not desktop CMake.
#include <libndls.h>
#include "monotonic_clock.hpp"
#include "nzato/launcher_config.hpp"
#include "nzato/pack.hpp"
#include "nzato/player.hpp"
#include "nzato/raster.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <string>

// Newlib's exit path references these conventional hooks. Ndless supplies its
// own C++ init/fini trampoline instead of the usual hosted CRT definitions.
extern "C" void _init() {}
extern "C" void _fini() {}

namespace {
int touchpad_arrow() {
  touchpad_report_t report{};
  return touchpad_scan(&report) == 0 && report.pressed ? static_cast<int>(report.arrow) : 0;
}

struct RepeatKey {
  bool down{};
  uint32_t next_repeat{};

  bool update(bool pressed, uint32_t now, bool repeat = false, uint32_t repeat_ms = 70) {
    if (!pressed) {
      down = false;
      return false;
    }
    if (!down) {
      down = true;
      next_repeat = now + 300;
      return true;
    }
    if (repeat && static_cast<int32_t>(now - next_repeat) >= 0) {
      next_repeat = now + repeat_ms;
      return true;
    }
    return false;
  }
};
} // namespace

int main(int argc, char** argv) {
  // ENTER normally launches the document.  Wait only for that key instead of
  // scanning the complete matrix: CM-C and CX do not agree on the idle value
  // returned by every matrix row under the legacy 3.1 ABI.
  while (isKeyPressed(KEY_NSPIRE_ENTER))
    ;
  constexpr bool debug_launcher = nzato::launcher_debug;
  const char* pack_name = nzato::launcher_pack_name_ndless;
  std::string adjacent_pack;
  if (argc > 0 && argv[0]) {
    const char* slash = std::strrchr(argv[0], '/');
    if (slash)
      adjacent_pack.assign(argv[0], static_cast<size_t>(slash - argv[0] + 1));
  }
  adjacent_pack += pack_name;
  // Language/package selection belongs to the compiled launcher target.
  // Ndless may supply an unrelated argv[1] when resuming an application;
  // interpreting it as a content path caused false missing-pack failures.
  const char* pack_path = adjacent_pack.c_str();
  FILE* pack_file = std::fopen(pack_path, "rb");
  if (!pack_file) {
    show_msgbox("ddlc content missing", "The required ddlc content pack was not found.");
    return 2;
  }
  std::fclose(pack_file);
  nzato::Pack pack;
  if (!pack.load(pack_path)) {
    show_msgbox("ddlc pack error", pack.error());
    return 2;
  }
  const std::string op_path = opening_video_path(pack);
  FILE* op_file = std::fopen(op_path.c_str(), "rb");
  const bool op_available = op_file != nullptr;
  if (op_file)
    std::fclose(op_file);
  static std::array<uint16_t, nzato::screen_w * nzato::screen_h> fb{};
  if (!has_colors) {
    show_msgbox("ddlc display error", "this build requires a color model");
    return 3;
  }
  if (!lcd_init(SCR_320x240_565)) {
    show_msgbox("ddlc display error", "cannot initialize framebuffer");
    return 3;
  }
  // Zehn and bFLT share the same zero-copy decoder path on native 320x240
  // LCDs. HW-W is physically rotated and must still use lcd_blit.
  auto* const direct_video_framebuffer = lcd_type() == SCR_320x240_565
                                             ? reinterpret_cast<uint16_t*>(REAL_SCREEN_BASE_ADDRESS)
                                             : nullptr;
  nzato::Player player(pack, op_available, direct_video_framebuffer);
  nzato::ndless::MonotonicClock clock;
  clock.init();
  uint32_t last_tick = clock.now_ms();
  uint32_t skip_elapsed = 0;
  RepeatKey up_key, down_key, left_key, right_key, confirm_key, escape_key, space_key;
  RepeatKey hide_key, auto_key, tab_key, on_key;
  RepeatKey name_letter_keys[26], name_backspace_key;
  static const auto name_letter_codes = std::array{
      KEY_NSPIRE_A, KEY_NSPIRE_B, KEY_NSPIRE_C, KEY_NSPIRE_D, KEY_NSPIRE_E, KEY_NSPIRE_F,
      KEY_NSPIRE_G, KEY_NSPIRE_H, KEY_NSPIRE_I, KEY_NSPIRE_J, KEY_NSPIRE_K, KEY_NSPIRE_L,
      KEY_NSPIRE_M, KEY_NSPIRE_N, KEY_NSPIRE_O, KEY_NSPIRE_P, KEY_NSPIRE_Q, KEY_NSPIRE_R,
      KEY_NSPIRE_S, KEY_NSPIRE_T, KEY_NSPIRE_U, KEY_NSPIRE_V, KEY_NSPIRE_W, KEY_NSPIRE_X,
      KEY_NSPIRE_Y, KEY_NSPIRE_Z};
  RepeatKey debug_previous_key, debug_next_key, debug_gallery_key;
  bool skip_toggled = false;
  bool redraw = true;
  constexpr uint32_t frame_interval_ms = 16;
  while (!player.ended()) {
    const uint32_t now = clock.now_ms();
    const uint32_t pending_elapsed = now - last_tick;
    if (pending_elapsed >= frame_interval_ms) {
      const uint32_t elapsed = std::min<uint32_t>(250, pending_elapsed);
      const bool video_tick = player.video_playing();
      redraw |= player.tick(elapsed);
      const uint32_t after_tick = clock.now_ms();
      // Automatic play can enter a new scene and synchronously stream several
      // images from flash inside tick(). Do not charge that I/O time to the
      // newly-created pause/animation on the next iteration.
      last_tick = !video_tick && after_tick - now >= frame_interval_ms ? after_tick : now;
      if (!player.skip_allowed())
        skip_toggled = false;
      const bool skip_pressed =
          player.skip_allowed() && (isKeyPressed(KEY_NSPIRE_CTRL) || skip_toggled);
      redraw |= player.set_skip_indicator(skip_pressed);
      if (skip_pressed) {
        skip_elapsed += elapsed;
        if (skip_elapsed >= 32) {
          redraw |= player.fast_forward();
          skip_elapsed = 0;
          // Resource streaming performed by fast-forward must not be counted
          // again as story/ATL elapsed time on the following loop.
          last_tick = clock.now_ms();
        }
      } else
        skip_elapsed = 0;
    }
    // Poll before rendering. On CM-C a complex frame can take longer than a
    // short key tap, so sampling after render silently lost Esc and navigation
    // input. Refresh the timestamp too: `now` was captured before tick(), which
    // may synchronously stream resources from flash.
    const uint32_t input_now = clock.now_ms();
    // Poll once per logical frame, before rendering. This is frequent enough
    // for short taps without continuously occupying the touchpad I2C bus while
    // a static screen spins between frames.
    if (pending_elapsed >= frame_interval_ms) {
      // During the opening movie only the inexpensive skip keys have meaning.
      // Avoiding touchpad I2C and the rest of the key matrix closes the
      // performance gap between the standalone decoder probe and normal
      // playback on CM hardware without removing the familiar confirm keys.
      if (player.video_playing()) {
        const bool escape_skip = escape_key.update(isKeyPressed(KEY_NSPIRE_ESC), input_now);
        const bool confirm_skip = confirm_key.update(
            isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_5), input_now);
        const bool space_skip = space_key.update(isKeyPressed(KEY_NSPIRE_SPACE), input_now);
        if (escape_skip) {
          player.input_escape();
          last_tick = clock.now_ms();
          redraw = true;
        } else if (confirm_skip || space_skip) {
          player.input_confirm();
          last_tick = clock.now_ms();
          redraw = true;
        }
      } else {
        const int pad_arrow = touchpad_arrow();
        if (player.name_entry_open()) {
          const bool uppercase = isKeyPressed(KEY_NSPIRE_SHIFT);
          for (size_t i = 0; i < 26; ++i) {
            const bool pressed = isKeyPressed(name_letter_codes[i]);
            if (name_letter_keys[i].update(pressed, input_now)) {
              const char letter = static_cast<char>((uppercase ? 'A' : 'a') + i);
              redraw |= player.input_character(letter);
            }
          }
          if (name_backspace_key.update(isKeyPressed(KEY_NSPIRE_DEL), input_now))
            redraw |= player.input_backspace();
        }
        const uint32_t vertical_repeat_ms = player.pixel_scroll_active() ? 16 : 70;
        if (escape_key.update(isKeyPressed(KEY_NSPIRE_ESC), input_now)) {
          player.input_escape();
          redraw = true;
        }
        // touchpad_arrow() already performs the I2C scan. Calling
        // isKeyPressed(KEY_NSPIRE_UP/DOWN/...) repeated that slow transaction
        // four more times per poll and could lose the report between reads.
        const bool up_pressed = pad_arrow == 1 || isKeyPressed(KEY_NSPIRE_8);
        const bool up_fresh = up_pressed && !up_key.down;
        if (up_key.update(up_pressed, input_now, true, vertical_repeat_ms)) {
          player.input_up(up_fresh);
          redraw = true;
        }
        if (down_key.update(pad_arrow == 5 || isKeyPressed(KEY_NSPIRE_2), input_now, true,
                            vertical_repeat_ms)) {
          player.input_down();
          redraw = true;
        }
        if (left_key.update(pad_arrow == 7 || isKeyPressed(KEY_NSPIRE_4), input_now, true)) {
          player.input_left();
          redraw = true;
        }
        if (right_key.update(pad_arrow == 3 || isKeyPressed(KEY_NSPIRE_6), input_now, true)) {
          if (player.navigation_open())
            player.input_right();
          redraw = true;
        }
        if (confirm_key.update(pad_arrow == 9 || isKeyPressed(KEY_NSPIRE_ENTER) ||
                                   isKeyPressed(KEY_NSPIRE_5),
                               input_now)) {
          player.input_confirm();
          last_tick = clock.now_ms();
          redraw = true;
        }
        if (space_key.update(isKeyPressed(KEY_NSPIRE_SPACE), input_now)) {
          if (player.name_entry_open())
            player.input_character(' ');
          else
            player.input_confirm();
          last_tick = clock.now_ms();
          redraw = true;
        }
        if (tab_key.update(isKeyPressed(KEY_NSPIRE_TAB), input_now) && player.skip_allowed()) {
          skip_toggled = !skip_toggled;
          redraw = true;
        }
        if (on_key.update(on_key_pressed() != 0, input_now)) {
          player.open_quick_quit();
          redraw = true;
        }
        if (!player.name_entry_open() && hide_key.update(isKeyPressed(KEY_NSPIRE_H), input_now)) {
          player.toggle_ui();
          redraw = true;
        }
        if (!player.name_entry_open() && auto_key.update(isKeyPressed(KEY_NSPIRE_A), input_now)) {
          player.toggle_auto();
          redraw = true;
        }
        if (debug_launcher && debug_previous_key.update(isKeyPressed(KEY_NSPIRE_7), input_now)) {
          redraw |= player.debug_jump(-1);
          last_tick = clock.now_ms();
        }
        if (debug_launcher && debug_next_key.update(isKeyPressed(KEY_NSPIRE_9), input_now)) {
          redraw |= player.debug_jump(1);
          last_tick = clock.now_ms();
        }
        if (debug_launcher && debug_gallery_key.update(isKeyPressed(KEY_NSPIRE_0), input_now))
          redraw |= player.debug_unlock_gallery();
      }
    }
    if (redraw) {
      const uint32_t render_started = clock.now_ms();
      const bool first_transition_frame = player.screen_transition_first_frame_pending();
      const uint16_t* source = player.presentation_framebuffer();
      if (!source) {
        player.render(fb.data());
        source = fb.data();
      }
      // Ndless declares the input buffer as mutable for historical ABI
      // reasons; lcd_blit only reads it.
      if (source != direct_video_framebuffer)
        lcd_blit(const_cast<uint16_t*>(source), SCR_320x240_565);
      redraw = false;
      const uint32_t after_render = clock.now_ms();
      // A cold page layout or image read must not consume the complete fade
      // duration before its second frame. Video stays tied to real time.
      if (first_transition_frame && !player.video_playing() &&
          after_render - render_started >= frame_interval_ms)
        last_tick = after_render;
    }
    if (player.quit_requested())
      break;
  }
  clock.shutdown();
  lcd_init(SCR_TYPE_INVALID);
  return 0;
}
