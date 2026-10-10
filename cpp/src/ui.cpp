#include "nzato/player.hpp"
#include "nzato/raster.hpp"
#include "nzato/ui_metrics.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nzato {
namespace {
constexpr int history_name_width = 58;
constexpr int history_name_right = 132;
constexpr int history_body_left = 140;
constexpr int history_body_width = 167;
constexpr int history_content_left = history_name_right - history_name_width;

void blend_frame(uint16_t* output, const uint16_t* old_frame, const uint16_t* new_frame, int pixels,
                 int old_opacity) {
  const unsigned old_a = static_cast<unsigned>(old_opacity + 4) >> 3U;
  if (!old_a) {
    if (output != new_frame)
      std::memcpy(output, new_frame, static_cast<size_t>(pixels) * sizeof(uint16_t));
    return;
  }
  if (old_a >= 32) {
    if (output != old_frame)
      std::memcpy(output, old_frame, static_cast<size_t>(pixels) * sizeof(uint16_t));
    return;
  }

  // A transition otherwise performs roughly 300k software multiplications
  // for every 320x240 frame. Build two tiny channel-pair tables once per
  // opacity step and turn the hot loop into three indexed loads.
  static std::array<uint8_t, 32 * 32> blend5{};
  static std::array<uint8_t, 64 * 64> blend6{};
  static unsigned cached_alpha = 33;
  if (cached_alpha != old_a) {
    const unsigned new_a = 32U - old_a;
    for (unsigned old_value = 0; old_value < 32; ++old_value)
      for (unsigned new_value = 0; new_value < 32; ++new_value)
        blend5[old_value * 32 + new_value] =
            static_cast<uint8_t>((old_value * old_a + new_value * new_a) >> 5U);
    for (unsigned old_value = 0; old_value < 64; ++old_value)
      for (unsigned new_value = 0; new_value < 64; ++new_value)
        blend6[old_value * 64 + new_value] =
            static_cast<uint8_t>((old_value * old_a + new_value * new_a) >> 5U);
    cached_alpha = old_a;
  }
  for (int index = 0; index < pixels; ++index) {
    const uint16_t old_color = old_frame[index], new_color = new_frame[index];
    output[index] = static_cast<uint16_t>(
        (blend5[((old_color >> 11U) & 31U) * 32U + ((new_color >> 11U) & 31U)] << 11U) |
        (blend6[((old_color >> 5U) & 63U) * 64U + ((new_color >> 5U) & 63U)] << 5U) |
        blend5[(old_color & 31U) * 32U + (new_color & 31U)]);
  }
}

void fade_frame_to_black(uint16_t* output, const uint16_t* source, int pixels, int opacity) {
  const unsigned alpha = static_cast<unsigned>(opacity + 4) >> 3U;
  static std::array<uint8_t, 32> scale5{};
  static std::array<uint8_t, 64> scale6{};
  static unsigned cached_alpha = 33;
  if (cached_alpha != alpha) {
    for (unsigned value = 0; value < 32; ++value)
      scale5[value] = static_cast<uint8_t>((value * alpha) >> 5U);
    for (unsigned value = 0; value < 64; ++value)
      scale6[value] = static_cast<uint8_t>((value * alpha) >> 5U);
    cached_alpha = alpha;
  }
  for (int index = 0; index < pixels; ++index) {
    const uint16_t color = source[index];
    output[index] =
        static_cast<uint16_t>((scale5[(color >> 11U) & 31U] << 11U) |
                              (scale6[(color >> 5U) & 63U] << 5U) | scale5[color & 31U]);
  }
}

unsigned div255(unsigned value) {
  const unsigned rounded = value + 128U;
  return (rounded + (rounded >> 8U)) >> 8U;
}

uint32_t ease1024(uint32_t elapsed, uint32_t total) {
  if (!total)
    return 1024;
  const uint32_t t = std::min(elapsed, total) * 1024U / total;
  return t * t * (3072U - 2U * t) >> 20U;
}

constexpr uint16_t rgb565(int r, int g, int b) {
  return static_cast<uint16_t>((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

// Small integer shapes keep the mobile-style frame independent of pack assets.
// Rounded corners retain the actual stage underneath rather than painting black.
int rounded_inset(int row, int height, int radius) {
  const int distance = std::min(row, height - 1 - row);
  if (distance >= radius) return 0;
  const int dy = radius - distance - 1;
  int dx = radius;
  while (dx * dx + dy * dy > radius * radius) --dx;
  return radius - dx;
}

void pink_frame(uint16_t* dst, int x, int y, int width, int height, uint8_t opacity,
                bool dots) {
  const auto body_alpha = static_cast<uint8_t>(div255(opacity * (dots ? 213U : 230U)));
  constexpr uint16_t edge = rgb565(255, 238, 246);
  constexpr uint16_t pink = rgb565(255, 168, 210);
  constexpr uint16_t tab = rgb565(255, 238, 246);
  for (int row = 0; row < height; ++row) {
    const int inset = rounded_inset(row, height, 6);
    fill_rect_alpha(dst, x + inset, y + row, width - 2 * inset, 1,
                    dots ? pink : tab, body_alpha);
  }
  if (dots) {
    // Staggered polka dots with a faint lower highlight, like the original sheet.
    constexpr int spans[] = {1, 2, 3, 3, 2, 1};
    for (int cy = 6, band = 0; cy < height; cy += 12, ++band)
      for (int cx = 6 + (band & 1) * 6; cx < width; cx += 12)
        for (int dy = -3; dy < 3; ++dy) {
          const int row = cy + dy;
          if (row <= 0 || row >= height - 1) continue;
          const int inset = rounded_inset(row, height, 6) + 1;
          const int left = std::max(inset, cx - spans[dy + 3]);
          const int right = std::min(width - inset, cx + spans[dy + 3]);
          if (right > left)
            fill_rect_alpha(dst, x + left, y + row, right - left, 1,
                            rgb565(232, 112, 174), static_cast<uint8_t>(div255(opacity * 95U)));
        }
    for (int row = height * 2 / 3; row < height; ++row) {
      const int inset = rounded_inset(row, height, 6);
      const uint8_t alpha = static_cast<uint8_t>(div255(opacity * 25U));
      fill_rect_alpha(dst, x + inset, y + row, width - 2 * inset, 1, edge, alpha);
    }
  }
  for (int row = 0; row < height; ++row) {
    const int outer = rounded_inset(row, height, 6);
    const int inner = row == 0 || row == height - 1 ? width / 2
                       : 1 + rounded_inset(row - 1, height - 2, 5);
    fill_rect_alpha(dst, x + outer, y + row, std::max(1, inner - outer), 1, edge, opacity);
    fill_rect_alpha(dst, x + width - inner, y + row, std::max(1, inner - outer), 1, edge, opacity);
  }
}

void outlined_text(uint16_t* dst, const Pack& pack, std::string_view text, int x, int y,
                   int width, uint16_t outline, int leading, int size, uint8_t opacity,
                   int clip_top = 0, int clip_bottom = 240) {
  for (const auto& offset : {std::array<int, 2>{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
    draw_text(dst, pack, text, x + offset[0], y + offset[1], width, outline,
              leading, size, opacity, clip_top, clip_bottom);
  draw_text(dst, pack, text, x, y, width, 0xffff, leading, size, opacity, clip_top, clip_bottom);
}
} // namespace

void Player::render(uint16_t* dst) const {
  if (startup_phase_ == StartupPhase::notice || startup_phase_ == StartupPhase::fade_to_white) {
    clear(dst, 0xffff);
    const uint8_t opacity = startup_phase_ == StartupPhase::notice ? 255
        : static_cast<uint8_t>(255 - startup_elapsed_ms_ * 255 / startup_fade_ms);
    const auto line = [&](std::string_view text, int y, int size = 11) {
      draw_text_centered(dst, pack_, text, text, 160, y, 284, 0, size + 4, size, opacity);
    };
    line("DDLC TI-Nspire", 19, 14);
    line("Unofficial fan port", 44);
    line("This game is not affiliated in any way", 75);
    line("with Team Salvato.", 91);
    line("Please complete the original", 119);
    line("Doki Doki Literature Club before playing.", 135);
    line("Download the original game:", 164);
    line("https://ddlc.moe", 180);
    line("ENTER to continue   ESC to quit", 215, 10);
    framebuffer_valid_ = true;
    return;
  }
  if (credits_roll_active_) {
    clear(dst, 0);
    if (credits_roll_ms_ < credits_delay_ms) return;
    const uint32_t roll_ms = credits_roll_ms_ - credits_delay_ms;
    const auto credits = pack_.credits();
    constexpr int leading = 24, font_size = 11;
    int lines = 1;
    for (char c : credits) if (c == '\n') ++lines;
    const int final_y = 120 - (lines - 1) * leading;
    const int travel = screen_h - final_y;
    int y = screen_h - static_cast<int>(
        static_cast<uint64_t>(std::min(roll_ms, credits_scroll_ms)) * travel / credits_scroll_ms);
    size_t start = 0;
    while (start <= credits.size()) {
      size_t end = credits.find('\n', start);
      if (end == std::string_view::npos) end = credits.size();
      const auto line = credits.substr(start, end - start);
      // During the final hold retain only the requested closing card.
      if (roll_ms < credits_scroll_ms || y >= 40) {
        const uint16_t color = line == "act 2 coming soon?" ? 0xFBD7 : 0xFFFF;
        draw_text_centered(dst, pack_, line, line, 160, y, 308, color, leading, font_size);
      }
      y += leading;
      if (end == credits.size()) break;
      start = end + 1;
    }
    return;
  }
  // The backend keeps its framebuffer between presentations. Capture it only
  // when a new screen transition starts; continuously maintaining a second
  // copy cost 150 KiB of memory traffic on every typewriter/particle frame.
  if (screen_fade_left_ > rendered_screen_fade_left_ && framebuffer_valid_) {
    std::copy_n(dst, static_cast<size_t>(screen_w) * screen_h, last_frame_.begin());
    last_frame_valid_ = true;
  }
  pin_active_images();
  auto shake = [&]() {
    if (!shake_left_)
      return;
    const int offset = ((shake_left_ / 35) & 1) ? 1 : -1;
    if (shake_horizontal_) {
      if (offset > 0) {
        for (int y = 0; y < screen_h; ++y) {
          std::memmove(dst + y * screen_w + offset, dst + y * screen_w,
                       static_cast<size_t>(screen_w - offset) * sizeof(uint16_t));
          std::fill(dst + y * screen_w, dst + y * screen_w + offset, uint16_t{});
        }
      } else {
        const int amount = -offset;
        for (int y = 0; y < screen_h; ++y) {
          std::memmove(dst + y * screen_w, dst + y * screen_w + amount,
                       static_cast<size_t>(screen_w - amount) * sizeof(uint16_t));
          std::fill(dst + (y + 1) * screen_w - amount, dst + (y + 1) * screen_w, uint16_t{});
        }
      }
      return;
    }
    if (offset > 0) {
      for (int y = screen_h - 1; y >= offset; y--)
        std::memmove(dst + y * screen_w, dst + (y - offset) * screen_w, screen_w * 2);
      std::fill(dst, dst + offset * screen_w, uint16_t{});
    } else {
      const int n = -offset;
      for (int y = 0; y < screen_h - n; y++)
        std::memmove(dst + y * screen_w, dst + (y + n) * screen_w, screen_w * 2);
      std::fill(dst + (screen_h - n) * screen_w, dst + screen_h * screen_w, uint16_t{});
    }
  };
  auto capture_game = [&]() {
    for (int y = 0; y < 60; y++)
      for (int x = 0; x < 80; x++)
        game_thumbnail_[y * 80 + x] = dst[(y * screen_h / 60) * screen_w + x * screen_w / 80];
  };
  auto screen_fade = [&]() {
    if (screen_fade_left_ && last_frame_valid_) {
      const int old_a = static_cast<int>(screen_fade_left_ * 255 / 240);
      blend_frame(dst, last_frame_.data(), dst, screen_w * screen_h, old_a);
    }
    // Modal confirmation is drawn after this point. Keep the unobscured page
    // as the transition source, matching the previous behavior without
    // continuously copying every ordinary gameplay frame.
    if (confirm_open_) {
      std::copy_n(dst, static_cast<size_t>(screen_w) * screen_h, last_frame_.begin());
      last_frame_valid_ = true;
      framebuffer_valid_ = false;
    } else
      framebuffer_valid_ = true;
    rendered_screen_fade_left_ = screen_fade_left_;
  };
  if (video_active_) {
    video_.render(dst);
    screen_fade();
    return;
  }
  const bool zh = pack_.chinese();
  constexpr uint16_t orange = ui_metrics::orange, dim = ui_metrics::dim;
  auto snow = [&](const Image* const images[2]) {
    for (const auto& p : particles_) {
      const Image* im = images[p.kind];
      if (im)
        blit(dst, *im, p.x, p.y, 135);
    }
  };
  auto auto_icon = [&]() {
    if (!auto_play_ && !skip_indicator_)
      return;
    fill_rect_alpha(dst, 291, 218, 25, 18, 0, 180);
    auto triangle = [&](int left) {
      for (int row = 0; row < 11; row++)
        fill_rect(dst, left, 221 + row, (row < 6 ? row + 1 : 11 - row), 1, 0xFFFF);
    };
    if (skip_indicator_) {
      triangle(296);
      triangle(306);
    } else {
      const int icon_size = ui_metrics::base_size(zh);
      draw_text(dst, pack_, "A", 295, 221, 9, 0xFFFF, ui_metrics::line_height(icon_size),
                icon_size);
      triangle(307);
    }
  };
  auto confirm = [&]() {
    if (!confirm_open_)
      return;
    fill_rect_alpha(dst, 0, 0, 320, 240, 0, 153);
    fill_rect(dst, 19, 77, 282, 88, 0xFFFF);
    fill_rect(dst, 21, 79, 278, 84, 0);
    UiText message_key;
    if (confirm_action_ == 2)
      message_key = UiText::confirm_quit;
    else if (confirm_action_ == 3)
      message_key = UiText::confirm_overwrite;
    else if (confirm_action_ == 4)
      message_key = UiText::confirm_load;
    else if (confirm_action_ == 5)
      message_key = UiText::confirm_quick_save;
    else
      message_key = UiText::confirm_main_menu;
    const std::string_view message = pack_.ui(message_key);
    const int message_size = ui_metrics::modal_size(zh);
    const int message_line_height = ui_metrics::line_height(message_size) + 1;
    const std::string_view whole(message);
    constexpr int message_width = 260, message_top = 85, message_region_height = 47;
    const int message_height =
        centered_text_height(pack_, whole, message_width, message_line_height, message_size);
    const int message_y = message_top + std::max(0, (message_region_height - message_height) / 2);
    draw_text_centered(dst, pack_, whole, whole, 160, message_y, message_width, 0xFFFF,
                       message_line_height, message_size);
    const std::string_view yes = pack_.ui(UiText::yes);
    const std::string_view no = pack_.ui(UiText::no);
    const int button_size = ui_metrics::base_size(zh);
    draw_text(dst, pack_, yes, 111, 139, 42, confirm_selection_ == 0 ? orange : dim,
              ui_metrics::line_height(button_size), button_size);
    draw_text(dst, pack_, no, 203, 139, 42, confirm_selection_ == 1 ? orange : dim,
              ui_metrics::line_height(button_size), button_size);
  };
  if (title_active_) {
    if (simple_title_) {
      render_menu_chrome(dst);
      if (settings_open_) {
        render_menu_text(dst, "Settings", 11, 27, 68, 12, true);
        render_menu_text(dst, "Background display", 94, 40, 210, 11);
        constexpr std::string_view labels[] = {"Original (16:9)", "Stretch to fill", "Crop sides to fill", "Back"};
        for (int i=0;i<4;++i) {
          const int y = i==3 ? 176 : 76+i*28;
          if (settings_selection_==i) render_menu_text(dst, ">", 87,y,10,10);
          render_menu_text(dst,labels[i],100,y,204,11);
          if (i==static_cast<int>(background_mode_)) render_menu_text(dst,"*",298,y,12,11);
        }
        constexpr std::string_view descriptions[] = {"Keeps proportions and borders.", "Fills screen; changes proportions.",
                                                     "Fills screen; crops the sides.", "Return to the main menu."};
        render_menu_text(dst,descriptions[settings_selection_],94,201,210,9);
        render_menu_text(dst,"ENTER: apply  ESC: back",94,224,210,9);
        screen_fade(); confirm(); return;
      }
      if (ddlc_ui_) render_ddlc_title(dst);
      else render_menu_text(dst,pack_.ui(UiText::title),94,55,210,12);
      static constexpr std::string_view items[] = {"New Game", "Load", "Settings", "Quit"};
      for (int i=0;i<4;++i) {
        if (title_selection_==i) render_menu_text(dst,">",2,140+i*24,8,10,true);
        render_menu_text(dst,items[i],11,140+i*24,68,10,true);
      }
      if (startup_phase_ == StartupPhase::fade_to_menu)
        fill_rect_alpha(dst, 0, 0, screen_w, screen_h, 0xffff,
                        static_cast<uint8_t>(255 - startup_elapsed_ms_ * 255 / startup_fade_ms));
      screen_fade();
      confirm();
      return;
    }
    ensure_title_cache();
    // The mostly full-screen translucent menu overlay used to be blended again
    // for every snow step and key repeat. Start from the precomposited static
    // title, restore only particle rectangles to the underlay, draw snow, then
    // reapply the overlay to those small dirty rectangles.
    std::copy(title_static_cache_.begin(), title_static_cache_.end(), dst);
    struct DirtyRect {
      int left{}, top{}, right{}, bottom{};
    };
    std::array<DirtyRect, 20> dirty{};
    size_t dirty_count = 0;
    for (const auto& particle : particles_) {
      const Image* image = title_snow_images_[particle.kind];
      if (!image)
        continue;
      // The original title overlay owns the complete sidebar. Snow belongs to
      // the illustrated area on its right and must never shine through the
      // translucent left-hand chrome.
      DirtyRect incoming{std::max(74, particle.x), std::max(0, particle.y),
                         std::min(screen_w, particle.x + static_cast<int>(image->width)),
                         std::min(screen_h, particle.y + static_cast<int>(image->height))};
      if (incoming.left >= incoming.right || incoming.top >= incoming.bottom)
        continue;
      bool merged = true;
      while (merged) {
        merged = false;
        for (size_t i = 0; i < dirty_count; ++i) {
          const auto& current = dirty[i];
          if (incoming.right < current.left || current.right < incoming.left ||
              incoming.bottom < current.top || current.bottom < incoming.top)
            continue;
          incoming = {std::min(incoming.left, current.left), std::min(incoming.top, current.top),
                      std::max(incoming.right, current.right),
                      std::max(incoming.bottom, current.bottom)};
          dirty[i] = dirty[--dirty_count];
          merged = true;
          break;
        }
      }
      dirty[dirty_count++] = incoming;
    }
    for (size_t i = 0; i < dirty_count; ++i)
      for (int y = dirty[i].top; y < dirty[i].bottom; ++y)
        std::memcpy(dst + y * screen_w + dirty[i].left,
                    title_base_cache_.data() + y * screen_w + dirty[i].left,
                    static_cast<size_t>(dirty[i].right - dirty[i].left) * sizeof(uint16_t));
    for (const auto& particle : particles_) {
      const Image* image = title_snow_images_[particle.kind];
      if (image)
        blit_clipped(dst, *image, particle.x, particle.y, 135, 74, 0, screen_w, screen_h);
    }
    if (title_menu_overlay_)
      for (size_t i = 0; i < dirty_count; ++i)
        blit_clipped(dst, *title_menu_overlay_, 0, 30, 255, dirty[i].left, dirty[i].top,
                     dirty[i].right, dirty[i].bottom);
    const NavAction items[6] = {NavAction::start,   NavAction::load,     NavAction::gallery,
                                NavAction::credits, NavAction::controls, NavAction::quit};
    for (int i = 0; i < 6; i++) {
      const int item_size = ui_metrics::menu_size(zh);
      draw_text(dst, pack_, nav_text(items[i]), 12, 75 + i * 18, 62,
                title_selection_ == i ? orange : dim, ui_metrics::line_height(item_size),
                item_size);
    }
    screen_fade();
    confirm();
    return;
  }
  if (name_entry_open_) {
    clear(dst, 0);
    if (title_background_)
      blit(dst, *title_background_, 0, 30);
    const int heading_size = ui_metrics::menu_size(language_ == Language::chinese);
    const int body_size = ui_metrics::base_size(language_ == Language::chinese);
    constexpr std::string_view heading = "What is your name?";
    const int heading_width = text_width(pack_, heading, heading_size);
    draw_text(dst, pack_, heading, (screen_w - heading_width) / 2, 75, heading_width, 0xFFFF,
              ui_metrics::line_height(heading_size), heading_size);
    const std::string_view visible_name = player_name_.empty() ? std::string_view("_") :
                                                               std::string_view(player_name_);
    const int name_width = std::min(260, text_width(pack_, visible_name, heading_size) + 20);
    fill_rect_alpha(dst, (screen_w - name_width) / 2 - 6, 112, name_width + 12, 27, 0, 220);
    draw_text(dst, pack_, visible_name, (screen_w - name_width) / 2, 118, name_width, 0xFFFF,
              ui_metrics::line_height(heading_size), heading_size);
    constexpr std::string_view hint = "Type a name, then press ENTER";
    const int hint_width = text_width(pack_, hint, body_size);
    draw_text(dst, pack_, hint, (screen_w - hint_width) / 2, 163, hint_width, 0xBDF7,
              ui_metrics::line_height(body_size), body_size);
    screen_fade();
    confirm();
    return;
  }
  if (gallery_open_) {
    clear(dst, 0);
    static constexpr int offsets[12] = {0, 3, 5, 6, 9, 10, 12, 13, 17, 28, 29, 35};
    static constexpr int counts[12] = {3, 2, 1, 3, 1, 2, 1, 4, 11, 1, 6, 8};
    static constexpr uint64_t masks[12] = {
        0x7ULL,    0x18ULL,    0x20ULL,       0x1c0ULL,      0x200ULL,       0xc00ULL,
        0x1000ULL, 0x1e000ULL, 0x0ffe0000ULL, 0x10000000ULL, 0x7e0000000ULL, 0x800000000ULL};
    auto group_unlocked = [&](int group) {
      const bool all = group == 1 || group == 7 || group == 8 || group == 10;
      return all ? (gallery_unlocks_ & masks[group]) == masks[group]
                 : (gallery_unlocks_ & masks[group]) != 0;
    };
    if (gallery_view_) {
      const int group = std::clamp(gallery_selection_, 0, 11);
      std::array<int, 11> available{};
      int available_count = 0;
      for (int i = 0; i < counts[group]; ++i) {
        bool unlocked = group_unlocked(group);
        if (group == 0)
          unlocked = gallery_unlocks_ & (uint64_t{1} << i);
        else if (group == 3)
          unlocked = gallery_unlocks_ & (uint64_t{1} << (6 + i));
        else if (group == 5)
          unlocked = gallery_unlocks_ & (uint64_t{1} << (10 + i));
        if (unlocked)
          available[available_count++] = i;
      }
      if (available_count) {
        const int selected =
            available[(gallery_variant_ % available_count + available_count) % available_count];
        if (const Image* image =
                pack_.image(static_cast<uint16_t>(gallery_base_ + 13 + offsets[group] + selected)))
          blit(dst, *image, 0, 30);
        char counter[24];
        std::snprintf(counter, sizeof(counter), "%d/%d", selected + 1, counts[group]);
        const int counter_size = ui_metrics::base_size(zh);
        const int counter_width = text_width(pack_, counter, counter_size);
        // Lock the right edge so 9/11 -> 10/11 grows to the left instead of
        // making the counter appear to jump.
        draw_text(dst, pack_, counter, 316 - counter_width, 218, counter_width, 0xBDF7,
                  ui_metrics::line_height(counter_size), counter_size);
      }
    } else {
      if (const Image* overlay = pack_.image(gallery_base_))
        blit(dst, *overlay, 0, 30);
      for (int i = 0; i < 12; ++i) {
        const int col = i % 4, row = i / 4;
        const int x = 18 + col * 76, y = 48 + row * 46;
        const bool unlocked = group_unlocked(i);
        if (unlocked) {
          if (const Image* thumb = pack_.image(static_cast<uint16_t>(gallery_base_ + 1 + i)))
            blit(dst, *thumb, x, y);
        } else {
          fill_rect(dst, x, y, 65, 37, 0);
          const std::string_view no_signal = pack_.ui(UiText::no_signal);
          const int label_size = ui_metrics::small_size(zh);
          const int label_width = text_width(pack_, no_signal, label_size);
          draw_text(dst, pack_, no_signal, x + (65 - label_width) / 2, y + 14, 65, dim,
                    ui_metrics::line_height(label_size), label_size);
        }
        const uint16_t edge = i == gallery_selection_ ? orange : 0xCE79;
        fill_rect(dst, x - 1, y - 1, 67, 1, edge);
        fill_rect(dst, x - 1, y + 37, 67, 1, edge);
        fill_rect(dst, x - 1, y - 1, 1, 39, edge);
        fill_rect(dst, x + 65, y - 1, 1, 39, edge);
      }
      const std::string_view back = pack_.ui(UiText::return_to_game);
      const int back_size = zh ? ui_metrics::base_size(zh) : ui_metrics::small_size(zh);
      const int back_width = text_width(pack_, back, back_size);
      draw_text(dst, pack_, back, 276 - back_width / 2, 197, 48,
                gallery_selection_ == 12 ? orange : dim, ui_metrics::line_height(back_size),
                back_size);
    }
    screen_fade();
    return;
  }
  if (history_open_) {
    clear(dst, 0);
    render_menu_chrome(dst);
    const std::string_view log = pack_.ui(UiText::log);
    render_menu_text(dst, log, 11, 27, 68, ui_metrics::page_title_size, true);
    render_sidebar(dst, NavAction::log);
    const int history_size = ddlc_ui_ ? font_size_ : ui_metrics::base_size(zh);
    const int history_line_height = ui_metrics::line_height(history_size);
    constexpr int content_top = 62, content_bottom = 231, viewport = content_bottom - content_top;
    const size_t available = history_.empty() ? 0 : history_.size() - 1;
    if (history_layout_revision_ != history_revision_) {
      history_content_height_ = 0;
      for (size_t i = 0; i < available; ++i) {
        const auto& h = history_[i];
        history_heights_[i] = std::max(text_height(pack_, h.speaker, history_name_width,
                                                   history_line_height, history_size),
                                       text_height(pack_, h.text, history_body_width,
                                                   history_line_height, history_size)) +
                              6;
        history_content_height_ += history_heights_[i];
      }
      history_layout_revision_ = history_revision_;
    }
    if (ddlc_ui_ && !available) render_menu_text(dst,"History is empty.",96,70,210,11);
    const int total = history_content_height_;
    const int max_scroll = std::max(0, total - viewport),
              scroll = std::min(history_scroll_px_, max_scroll);
    const int source_top = max_scroll ? max_scroll - scroll : 0;
    // Keep a bounded window around the visible history. A full late-game log
    // could allocate almost 3 MiB at once and fail on a fragmented 32 MiB CM
    // heap. The overscan still makes ordinary two-pixel scrolling a cached
    // framebuffer copy instead of rerasterizing glyphs every frame.
    constexpr int max_cached_rows = 512;
    const int cache_rows = std::min(total, max_cached_rows);
    const bool cache_covers =
        scroll_cache_kind_ == 1 && scroll_cache_revision_ == history_revision_ &&
        scroll_cache_content_height_ == total && source_top >= scroll_cache_origin_ &&
        source_top + viewport <= scroll_cache_origin_ + scroll_cache_height_;
    // Zero-keyed RGB text caches discard edge alpha. DDLC history must
    // blend directly over the pale menu, exactly like live dialogue.
    // Only visible entries are rasterized; generic nZATO keeps its cache.
    const bool rebuild_cache = !ddlc_ui_ && total > 0 && !cache_covers;
    if (rebuild_cache) {
      scroll_cache_kind_ = 1;
      scroll_cache_revision_ = history_revision_;
      scroll_cache_content_height_ = total;
      scroll_cache_height_ = cache_rows;
      const int overscan = std::max(0, cache_rows - viewport);
      scroll_cache_origin_ =
          std::clamp(source_top - overscan / 2, 0, std::max(0, total - cache_rows));
      scroll_text_cache_.assign(static_cast<size_t>(screen_w) * cache_rows, 0);
    } else if (!total || ddlc_ui_) {
      scroll_cache_kind_ = 1;
      scroll_cache_revision_ = history_revision_;
      scroll_cache_height_ = scroll_cache_origin_ = scroll_cache_content_height_ = 0;
      scroll_text_cache_.clear();
    }
    // Match dialogue's anti-aliased dark outline rather than the hard black
    // mask used by small menu labels. This color remains nonzero in the
    // transparent scroll cache and does not trigger its binary-mask path.
    auto history_text = [&](uint16_t* target, int height, std::string_view value,
                            int x, int y, int width, int top, int bottom) {
      if (ddlc_ui_)
        draw_outlined_surface(target, screen_w, height, pack_, value, x, y, width,
                              rgb565(61,49,58), history_line_height, history_size, top, bottom);
      else render_menu_surface_text(target,height,value,x,y,width,history_line_height,history_size,top,bottom);
    };
    auto name = [&](uint16_t* target, int surface_height, std::string_view value, int top,
                    int clip_top, int clip_bottom) {
      if (value.empty())
        return;
      size_t at = 0;
      while (at < value.size()) {
        size_t end = at;
        int width = 0;
        while (end < value.size()) {
          size_t next = end;
          utf8_next(value, next);
          const int candidate = text_width(pack_, value.substr(at, next - at), history_size);
          if (candidate > history_name_width && end > at)
            break;
          width = candidate;
          end = next;
          if (width >= history_name_width)
            break;
        }
        const auto line = value.substr(at, end - at);
        history_text(target, surface_height, line,
                     history_name_right - std::min(history_name_width,text_width(pack_,line,history_size)),
                     top,history_name_width,clip_top,clip_bottom);
        top += history_line_height;
        at = end;
      }
    };
    if (!scroll_text_cache_.empty()) {
      if (rebuild_cache) {
        int cache_y = 0;
        for (size_t i = 0; i < available; i++) {
          const auto& h = history_[i];
          const int local_y = cache_y - scroll_cache_origin_;
          if (local_y + history_heights_[i] > 0 && local_y < scroll_cache_height_) {
            name(scroll_text_cache_.data(), scroll_cache_height_, h.speaker, local_y, 0,
                 scroll_cache_height_);
            history_text(scroll_text_cache_.data(),scroll_cache_height_,h.text,history_body_left,local_y,history_body_width,0,scroll_cache_height_);
          }
          cache_y += history_heights_[i];
        }
      }
      composite_scroll_cache(dst, source_top, content_top, content_bottom, history_content_left,
                             308);
    } else {
      // Pathological oversized histories retain the old zero-extra-memory path.
      int y = content_top - source_top;
      for (size_t i = 0; i < available; i++) {
        const auto& h = history_[i];
        const int height = history_heights_[i];
        if (y + height > content_top && y < content_bottom) {
          name(dst, screen_h, h.speaker, y, content_top, content_bottom);
          history_text(dst,screen_h,h.text,history_body_left,y,history_body_width,content_top,content_bottom);
        }
        y += height;
      }
    }
    render_scrollbar(dst, content_top, content_bottom, total, max_scroll - scroll,
                     history_focus_ != 0);
    screen_fade();
    confirm();
    return;
  }
  if (info_open_) {
    clear(dst, 0);
    render_info_page(dst);
    screen_fade();
    confirm();
    return;
  }
  if (save_open_ || load_open_) {
    clear(dst, 0);
    render_menu_chrome(dst);
    const NavAction active = save_open_ ? NavAction::save : NavAction::load;
    const int file_size = ui_metrics::base_size(zh);
    const int slot_size = ui_metrics::file_slot_size(zh);
    const int file_line_height = ui_metrics::line_height(file_size);
    const int slot_line_height = ui_metrics::line_height(slot_size);
    render_menu_text(dst, nav_text(active), 11, 27, 68, ui_metrics::page_title_size, true);
    render_sidebar(dst, active);
    const std::string_view save = nav_text(NavAction::save);
    std::string page_heading;
    const std::string_view pattern = pack_.ui(UiText::page_pattern);
    const size_t marker = ddlc_ui_ ? 5 : pattern.find("{}");
    page_heading.assign(ddlc_ui_ ? std::string_view("Page {}") : pattern);
    if (marker != std::string_view::npos)
      page_heading.replace(marker, 2, std::to_string(file_page_ + 1));
    const int hw = text_width(pack_, page_heading, file_size);
    render_menu_text(dst, page_heading, 190 - hw / 2, 52, 120, file_size);
    for (int i = 0; i < slots_per_page; i++) {
      const int col = i % 3, row = i / 3, x = 82 + col * 76, y = 72 + row * 62;
      const bool selected = history_focus_ && !file_region_ && i == file_selection_;
      fill_rect(dst, x - 2, y - 2, 70, 54, ddlc_ui_ ? (selected ? 0xb272 : 0xfddb) : (selected ? orange : 0xCE79));
      fill_rect(dst, x - 1, y - 1, 68, 52, ddlc_ui_ ? 0xffff : 0);
      const auto& s = saves_[file_page_ * slots_per_page + i];
      if (s.valid && (s.thumbnail.size() == 80 * 60 || s.thumbnail.size() == 80 * 45)) {
        const int source_height = static_cast<int>(s.thumbnail.size() / 80);
        const int draw_width = std::min(66, 50 * 80 / source_height);
        const int draw_height = std::min(50, 66 * source_height / 80);
        const int draw_x = x + (66 - draw_width) / 2;
        const int draw_y = y + (50 - draw_height) / 2;
        for (int yy = 0; yy < draw_height; yy++)
          for (int xx = 0; xx < draw_width; xx++)
            dst[(draw_y + yy) * screen_w + draw_x + xx] =
                s.thumbnail[(yy * source_height / draw_height) * 80 + xx * 80 / draw_width];
      } else {
        const std::string_view empty = selected && save_open_ ? save : pack_.ui(UiText::no_signal);
        const int ew = text_width(pack_, empty, slot_size);
        if (ddlc_ui_) render_menu_text(dst,empty,x+33-ew/2,y+20,66,slot_size);
        else draw_text(dst, pack_, empty, x + 33 - ew / 2, y + 20, 66,
                       selected && save_open_ ? orange : dim, slot_line_height, slot_size);
      }
    }
    const char* pages[4] = {"1", "2", "3", "4"};
    constexpr int cell = 36, page_left = 88;
    for (int i = 0; i < file_pages; i++) {
      const uint16_t color =
          i == file_page_ ? (file_region_ && history_focus_ ? orange : 0xFFFF) : dim;
      const int pw = text_width(pack_, pages[i], file_size);
      if (ddlc_ui_ && i==file_page_) fill_rect_alpha(dst,page_left+i*cell,207,cell,18,0xfe18,130);
      if (ddlc_ui_) render_menu_text(dst,pages[i],page_left+i*cell+(cell-pw)/2,210,cell,file_size);
      else draw_text(dst, pack_, pages[i], page_left + i * cell + (cell - pw) / 2, 210, cell, color,
                     file_line_height, file_size);
    }
    screen_fade();
    confirm();
    return;
  }
  if (choice_open_ && choice_first_ && word_menu_page(choice_first_ - 1)) {
    if (const Image* notebook = pack_.image(pack_.ddlc_notebook_id()))
      blit(dst, *notebook, 0, 0);
    const std::string counter = std::to_string(word_menu_page(choice_first_ - 1)) + "/20";
    draw_text(dst, pack_, counter, 279-text_width(pack_, counter, 11)/2, 20, 40, 0, 13, 11);
    for (uint8_t i=0; i<choice_count_; ++i) {
      const int x = 146 + (i/5)*78, y = 53 + (i%5)*30;
      const auto word = word_choice_text(i);
      int size = 11;
      while (size>8 && text_width(pack_, word, size)>74) --size;
      if (i == choice_selection_) fill_rect_alpha(dst, x-2, y-2, 77, 17, 0xfcbf, 125);
      draw_text(dst, pack_, word, x, y, 76, 0, 13, size);
    }
    capture_game(); screen_fade(); confirm(); return;
  }
  auto draw_background = [&](const Image* bg, int bg_y, uint8_t opacity, int xoff) {
    if (ddlc_ui_ && bg->background && background_mode_ != BackgroundMode::original) {
      const int width = background_mode_ == BackgroundMode::stretch ? screen_w
                          : (bg->width * screen_h + bg->height / 2) / bg->height;
      blit_resized(dst, *bg, xoff + (screen_w - width) / 2, 0, width, screen_h, opacity);
    } else
      blit(dst, *bg, xoff, bg_y, opacity);
  };
  auto stage_base = [&](const Image* bg, int bg_y, uint8_t opacity, int xoff = 0) {
    if (bg && bg->base_layer) {
      if (bg->full_screen)
        blit_transform(dst, *bg, 0, 0, 160, 120, 1024, opacity, 0, false);
      else
        draw_background(bg, bg_y, opacity, xoff);
    }
  };
  auto stage_foreground = [&](const Image* bg, int bg_y, uint8_t opacity, int xoff = 0) {
    if (bg && !bg->base_layer) {
      if (bg->full_screen)
        blit_transform(dst, *bg, 0, 0, 160, 120, 1024, opacity, 0, false);
      else
        draw_background(bg, bg_y, opacity, xoff);
    }
  };
  // Sprite coordinates use the old row-185 anchor. Position the complete
  // character at the visible background bottom without trimming any pixels.
  // Stored coordinates stay unchanged so existing saves receive the correction.
  auto draw_layer = [&](const Layer& layer, uint8_t opacity, int xoff = 0) {
    const Image& image = *layer.image;
    const bool character = ddlc_ui_ && image.alpha && !image.background &&
                           !image.particle && !image.screen_layer && !image.full_screen;
    const int character_bottom = background_mode_ == BackgroundMode::original &&
                                         background_ && background_->background && !background_->full_screen
                                     ? std::clamp(background_y_ + static_cast<int>(background_->height),
                                                  0, screen_h)
                                     : screen_h;
    const int character_y = character ? character_bottom - 185 : 0;
    const int jitter = layer.shaking ? ((animation_ms_ / 50) & 1 ? 1 : -1) : 0;
    const int origin_x = image.screen_layer ? image.patch_x : 0;
    const int origin_y = image.screen_layer ? image.patch_y : 0;
    blit_transform(dst, image, layer.x + origin_x + xoff + jitter,
                   layer.y + origin_y + character_y - jitter,
                   layer.pivot_x + xoff, layer.pivot_y + character_y, layer.scale,
                   static_cast<uint8_t>(div255(static_cast<unsigned>(layer.opacity) * opacity)),
                   layer.blend_mode, layer.xflip);
  };
  auto stage_layers = [&](const std::array<Layer, 12>& ls, uint8_t opacity, int xoff = 0) {
    for (size_t index = 0; index < ls.size(); ++index) {
      const auto& layer = ls[index];
      if (layer.image) {
        // Several expressions can intentionally coexist as separate Ren'Py
        // tags (for example gusmirk below gulaugh). Their diced common body is
        // pixel-identical, so drawing both only strengthens semi-transparent
        // outline pixels. Let the upper expression own that body while still
        // retaining the lower tag for a later hide/reveal.
        bool covered_by_shared_body = false;
        if (layer.image->composite_base)
          for (size_t upper = index + 1; upper < ls.size(); ++upper)
            if (ls[upper].image && ls[upper].image->composite_base == layer.image->composite_base &&
                ls[upper].x == layer.x && ls[upper].y == layer.y) {
              covered_by_shared_body = true;
              break;
            }
        if (covered_by_shared_body)
          continue;
        draw_layer(layer, opacity, xoff);
      }
    }
  };
  // Dialogue typing changes the UI but usually leaves the stage untouched.
  // Cache that expensive background/sprite composition and restore it with
  // one linear copy until a transition, animation, movement or snow step
  // changes its signature.
  uint64_t stage_signature = 1469598103934665603ULL;
  auto mix_stage = [&](uint64_t value) {
    stage_signature ^= value;
    stage_signature *= 1099511628211ULL;
  };
  auto mix_image = [&](const Image* image) {
    mix_stage(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(image)));
  };
  auto mix_layers = [&](const std::array<Layer, 12>& ls) {
    for (const auto& layer : ls) {
      mix_image(layer.image);
      mix_stage(static_cast<uint32_t>(layer.x));
      mix_stage(static_cast<uint32_t>(layer.y));
      mix_stage(static_cast<uint32_t>(layer.opacity));
      mix_stage(layer.shaking ? animation_ms_ / 50 : 0);
      mix_stage(layer.below_particles ? 1U : 0U);
      mix_stage(static_cast<uint32_t>(layer.scale));
      mix_stage(static_cast<uint32_t>(layer.pivot_x));
      mix_stage(static_cast<uint32_t>(layer.pivot_y));
      mix_stage(static_cast<uint32_t>(layer.blend_mode));
      mix_stage(layer.xflip ? 1U : 0U);
    }
  };
  mix_image(background_);
  mix_stage(static_cast<uint8_t>(background_mode_));
  mix_image(old_background_);
  mix_stage(static_cast<uint32_t>(background_y_));
  mix_stage(static_cast<uint32_t>(old_background_y_));
  mix_stage(stage_transition_left_);
  mix_stage(stage_transition_total_);
  mix_stage(stage_transition_kind_);
  mix_stage(static_cast<uint32_t>(stage_transition_direction_));
  mix_layers(layers_);
  mix_layers(old_layers_);
  for (const auto* image : snow_images_)
    mix_image(image);
  if (snow_images_[0] || snow_images_[1])
    for (const auto& particle : particles_) {
      mix_stage(static_cast<uint32_t>(particle.x));
      mix_stage(static_cast<uint32_t>(particle.y));
      mix_stage(static_cast<uint32_t>(particle.kind));
    }

  bool moving_layer = false;
  for (const auto& layer : layers_)
    moving_layer |= layer.image && layer.move_total && layer.transition_left;
  const auto particles_above = [](const std::array<Layer, 12>& stage_layers) {
    return std::any_of(stage_layers.begin(), stage_layers.end(),
                       [](const Layer& layer) { return layer.image && layer.below_particles; });
  };

  if (stage_cache_valid_ && stage_cache_signature_ == stage_signature) {
    std::copy(stage_cache_.begin(), stage_cache_.end(), dst);
  } else {
    clear(dst, 0);
    if (stage_transition_left_ && stage_transition_total_) {
      const uint32_t elapsed = stage_transition_total_ - stage_transition_left_;
      if (!transition_cache_valid_) {
        uint16_t* output = dst;
        dst = transition_old_cache_.data();
        clear(dst, 0);
        stage_base(old_background_, old_background_y_, 255);
        stage_foreground(old_background_, old_background_y_, 255);
        if (particles_above(old_layers_)) {
          stage_layers(old_layers_, 255);
          snow(snow_images_);
        } else {
          snow(snow_images_);
          stage_layers(old_layers_, 255);
        }
        dst = transition_new_cache_.data();
        clear(dst, 0);
        stage_base(background_, background_y_, 255);
        stage_foreground(background_, background_y_, 255);
        if (particles_above(layers_)) {
          stage_layers(layers_, 255);
          snow(snow_images_);
        } else {
          snow(snow_images_);
          stage_layers(layers_, 255);
        }
        dst = output;
        transition_cache_valid_ = true;
      }
      if (stage_transition_kind_ == 2) {
        const uint32_t half = std::max<uint32_t>(1, stage_transition_total_ / 2);
        const uint16_t* source;
        uint8_t opacity;
        if (elapsed < half) {
          source = transition_old_cache_.data();
          opacity = static_cast<uint8_t>(255 - (elapsed * 255 / half));
        } else {
          source = transition_new_cache_.data();
          opacity = static_cast<uint8_t>((elapsed - half) * 255 /
                                         std::max<uint32_t>(1, stage_transition_total_ - half));
        }
        fade_frame_to_black(dst, source, screen_w * screen_h, opacity);
      } else if (stage_transition_kind_ == 3) {
        const int travel =
            static_cast<int>(ease1024(elapsed, stage_transition_total_) * screen_w / 1024U);
        for (int y = 0; y < screen_h; ++y) {
          uint16_t* row = dst + y * screen_w;
          const uint16_t* old_row = transition_old_cache_.data() + y * screen_w;
          const uint16_t* new_row = transition_new_cache_.data() + y * screen_w;
          if (stage_transition_direction_ < 0) {
            if (travel < screen_w)
              std::memcpy(row, old_row + travel,
                          static_cast<size_t>(screen_w - travel) * sizeof(uint16_t));
            if (travel)
              std::memcpy(row + screen_w - travel, new_row,
                          static_cast<size_t>(travel) * sizeof(uint16_t));
          } else {
            if (travel < screen_w)
              std::memcpy(row + travel, old_row,
                          static_cast<size_t>(screen_w - travel) * sizeof(uint16_t));
            if (travel)
              std::memcpy(row, new_row + screen_w - travel,
                          static_cast<size_t>(travel) * sizeof(uint16_t));
          }
        }
      } else {
        const auto a = static_cast<uint8_t>(elapsed * 255 / stage_transition_total_);
        blend_frame(dst, transition_old_cache_.data(), transition_new_cache_.data(),
                    screen_w * screen_h, 255 - a);
      }
    } else if (moving_layer) {
      uint64_t base_signature = 1469598103934665603ULL;
      auto mix_base = [&](uint64_t value) {
        base_signature ^= value;
        base_signature *= 1099511628211ULL;
      };
      mix_base(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(background_)));
      mix_base(static_cast<uint32_t>(background_y_));
      for (const auto* image : snow_images_)
        mix_base(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(image)));
      if (snow_images_[0] || snow_images_[1])
        for (const auto& particle : particles_) {
          mix_base(static_cast<uint32_t>(particle.x));
          mix_base(static_cast<uint32_t>(particle.y));
          mix_base(static_cast<uint32_t>(particle.kind));
        }
      for (const auto& layer : layers_) {
        if (!layer.image || (layer.move_total && layer.transition_left))
          continue;
        mix_base(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(layer.image)));
        mix_base(static_cast<uint32_t>(layer.x));
        mix_base(static_cast<uint32_t>(layer.y));
        mix_base(static_cast<uint32_t>(layer.opacity));
        mix_base(layer.shaking ? animation_ms_ / 50 : 0);
        mix_base(layer.below_particles ? 1U : 0U);
        mix_base(static_cast<uint32_t>(layer.scale));
        mix_base(static_cast<uint32_t>(layer.blend_mode));
        mix_base(layer.xflip ? 1U : 0U);
      }
      if (!movement_base_cache_valid_ || movement_base_cache_signature_ != base_signature) {
        uint16_t* output = dst;
        dst = movement_base_cache_.data();
        clear(dst, 0);
        stage_base(background_, background_y_, 255);
        stage_foreground(background_, background_y_, 255);
        const auto draw_stationary = [&](bool below_particles) {
          for (const auto& layer : layers_)
            if (layer.image && !(layer.move_total && layer.transition_left) &&
                layer.below_particles == below_particles) {
              draw_layer(layer, 255);
            }
        };
        if (particles_above(layers_)) {
          draw_stationary(true);
          snow(snow_images_);
          draw_stationary(false);
        } else {
          snow(snow_images_);
          for (const auto& layer : layers_)
            if (layer.image && !(layer.move_total && layer.transition_left)) {
              draw_layer(layer, 255);
            }
        }
        dst = output;
        movement_base_cache_signature_ = base_signature;
        movement_base_cache_valid_ = true;
      }
      std::copy(movement_base_cache_.begin(), movement_base_cache_.end(), dst);
      for (const auto& layer : layers_)
        if (layer.image && layer.move_total && layer.transition_left) {
          draw_layer(layer, 255);
        }
    } else {
      stage_base(background_, background_y_, 255);
      stage_foreground(background_, background_y_, 255);
      if (particles_above(layers_)) {
        stage_layers(layers_, 255);
        snow(snow_images_);
      } else {
        snow(snow_images_);
        stage_layers(layers_, 255);
      }
    }
    // stage_transition_left_ is part of the signature and changes every
    // frame, so caching an intermediate blend can never hit. Avoid a wasted
    // 150 KiB copy on every fade frame; cache the settled stage afterwards.
    if (!stage_transition_left_ && !moving_layer) {
      std::copy(dst, dst + screen_w * screen_h, stage_cache_.begin());
      stage_cache_signature_ = stage_signature;
      stage_cache_valid_ = true;
    } else
      stage_cache_valid_ = false;
  }
  if (poem_open_ && !save_open_ && !load_open_ && !history_open_ && !info_open_) {
    if (poem_paper_)
      blit(dst, *poem_paper_, 26, 0);
    draw_text(dst, pack_, poem_text_, 40, 12 - poem_scroll_px_, 238, 0,
              poem_line_height_, poem_font_size_, 255, 12, 210);
    fill_rect(dst, 26, 215, 267, 25, 0xffff);
    draw_text(dst, pack_, "UP/DOWN: scroll   LEFT/RIGHT: page", 38, 216, 250, 0, 12, 9);
    draw_text(dst, pack_, "ENTER: return", 38, 228, 250, 0, 12, 9);
    if (poem_scroll_max_) {
      fill_rect(dst, 285, 12, 2, 198, 0xc618);
      const int thumb = std::max(10, 198 * 198 / (198 + poem_scroll_max_));
      fill_rect(dst, 285, 12 + (198 - thumb) * poem_scroll_px_ / poem_scroll_max_,
                2, thumb, 0x630c);
    }
    capture_game();
    screen_fade();
    confirm();
    return;
  }
  if (!overlay_text_.empty()) {
    int size = overlay_positioned_ ? 10 : font_size_;
    const int max_width = overlay_positioned_ ? 280 : 304;
    while (size > 8 && centered_text_height(pack_, overlay_text_, max_width,
                                            ui_metrics::line_height(size), size) > 226)
      --size;
    const int h =
        centered_text_height(pack_, overlay_text_, max_width, ui_metrics::line_height(size), size);
    const int x = overlay_positioned_ ? overlay_x_ : 160;
    const int y = overlay_positioned_ ? overlay_y_ : std::max(0, (240 - h) / 2);
    if (overlay_positioned_)
      draw_text(dst, pack_, overlay_text_, x, y, max_width, 0xFFFF, ui_metrics::line_height(size),
                size, overlay_opacity_);
    else
      draw_text_centered(dst, pack_, overlay_text_, overlay_text_, x, y, max_width, 0xFFFF,
                         ui_metrics::line_height(size), size, overlay_opacity_);
  }
  if (centered_) {
    int centered_size = font_size_;
    constexpr int centered_width = 304;
    while (centered_size > 8 &&
           (centered_text_height(pack_, text_, centered_width,
                                 ui_metrics::line_height(centered_size), centered_size) > 226 ||
            (centered_no_wrap_ && text_width(pack_, text_, centered_size) > centered_width)))
      --centered_size;
    const int line_height = ui_metrics::line_height(centered_size);
    const int h = centered_text_height(pack_, text_, centered_width, line_height, centered_size);
    const int top = std::max(0, (240 - h) / 2);
    draw_text_centered(dst, pack_, std::string_view(text_).substr(0, reveal_bytes_), text_, 160,
                       top, centered_width, 0xFFFF, line_height, centered_size, centered_opacity_);
    auto_icon();
    shake();
    capture_game();
    screen_fade();
    confirm();
    return;
  }
  if (nvl_mode_ && !text_.empty()) {
    fill_rect_alpha(dst, 0, 0, screen_w, screen_h, 0, 205);
    // Ordinary NVL begins at the top and accumulates downward. Explicit
    // Ren'Py `centered` statements use the separate centered renderer above.
    constexpr int nvl_top = 14, nvl_bottom = 230, nvl_paragraph_gap = 7;
    const int body_line_height = ui_metrics::line_height(font_size_);
    const auto visible_text = std::string_view(text_).substr(0, reveal_bytes_);
    if (nvl_scroll_max_ > 0) {
      if (nvl_scroll_follow_) {
        const int visible_height = text_height_compact(pack_, visible_text, 272, body_line_height,
                                                       font_size_, nvl_paragraph_gap);
        nvl_scroll_px_ =
            std::min(nvl_scroll_max_, std::max(0, visible_height - (nvl_bottom - nvl_top)));
      }
      nvl_scroll_px_ = std::clamp(nvl_scroll_px_, 0, nvl_scroll_max_);
    } else
      nvl_scroll_px_ = 0;
    draw_text_compact(dst, pack_, visible_text, 24, nvl_top - nvl_scroll_px_, 272, 0xFFFF,
                      body_line_height, font_size_, nvl_paragraph_gap, 255, nvl_top, nvl_bottom);
    if (nvl_scroll_max_ > 0)
      render_scrollbar(dst, nvl_top, nvl_bottom, nvl_bottom - nvl_top + nvl_scroll_max_,
                       nvl_scroll_px_, true);
    auto_icon();
    shake();
    capture_game();
    screen_fade();
    confirm();
    return;
  }
  const uint8_t ui_opacity = (manual_ui_hidden_ || pause_ui_hidden_) ? 0 : window_opacity_;
  if (!ui_opacity) {
    auto_icon();
    shake();
    capture_game();
    screen_fade();
    confirm();
    return;
  }

  constexpr uint16_t edge = 0xBDF7, panel = 0x0000;
  const int name_font_size = ui_metrics::dialogue_name_size(pack_.chinese());
  const int name_width =
      speaker_.empty() ? 0 : std::clamp(text_width(pack_, speaker_, name_font_size) + 16,
                                       ddlc_ui_ ? 64 : 32, 144);
  auto draw_ui_chrome = [&](uint16_t* target, uint8_t opacity) {
    if (ddlc_ui_) {
      if (name_width) {
        pink_frame(target, 18, 146, name_width, 22, opacity, false);
        const int name_x = 18 + std::max(8, (name_width - text_width(pack_, speaker_, name_font_size)) / 2);
        outlined_text(target, pack_, speaker_, name_x, 149, name_width - 16,
                      rgb565(179, 76, 144), ui_metrics::line_height(name_font_size),
                      name_font_size, opacity);
      }
      pink_frame(target, 6, 164, 308, 72, opacity, true);
      return;
    }
    fill_rect_alpha(target, 38, 158, 245, 91, panel, opacity);
    fill_rect_alpha(target, 38, 158, 245, 1, edge, opacity);
    fill_rect_alpha(target, 38, 248, 245, 1, edge, opacity);
    fill_rect_alpha(target, 38, 158, 1, 91, edge, opacity);
    fill_rect_alpha(target, 282, 158, 1, 91, edge, opacity);
    if (name_width) {
      fill_rect_alpha(target, 50, 146, name_width, 25, panel, opacity);
      fill_rect_alpha(target, 50, 146, name_width, 1, edge, opacity);
      fill_rect_alpha(target, 50, 170, name_width, 1, edge, opacity);
      fill_rect_alpha(target, 50, 146, 1, 25, edge, opacity);
      fill_rect_alpha(target, 49 + name_width, 146, 1, 25, edge, opacity);
      draw_text(target, pack_, speaker_, 57, 151, name_width - 14, 0xFFFF,
                ui_metrics::line_height(name_font_size), name_font_size, opacity);
    }
    if (face_) {
      fill_rect_alpha(target, 200, 152, 72, 72, panel, opacity);
      fill_rect_alpha(target, 200, 152, 72, 1, edge, opacity);
      fill_rect_alpha(target, 200, 223, 72, 1, edge, opacity);
      fill_rect_alpha(target, 200, 152, 1, 72, edge, opacity);
      fill_rect_alpha(target, 271, 152, 1, 72, edge, opacity);
      blit(target, *face_, 202, 154, opacity);
    }
  };
  if (ui_opacity == 255) {
    uint64_t signature = 1469598103934665603ULL;
    for (const unsigned char value : speaker_) {
      signature ^= value;
      signature *= 1099511628211ULL;
    }
    signature ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(face_));
    signature *= 1099511628211ULL;
    if (ddlc_ui_) {
      // Alpha depends on the stage pixels, so include the full stage signature.
      signature ^= stage_signature;
      signature *= 1099511628211ULL;
    }
    if (!dialogue_ui_cache_valid_ || dialogue_ui_cache_signature_ != signature) {
      if (ddlc_ui_)
        std::copy_n(dst, screen_w * screen_h, dialogue_ui_cache_.begin());
      else
        clear(dialogue_ui_cache_.data(), 0);
      draw_ui_chrome(dialogue_ui_cache_.data(), 255);
      dialogue_ui_cache_signature_ = signature;
      dialogue_ui_cache_valid_ = true;
    }
    auto copy_rect = [&](int left, int top, int width, int height) {
      for (int y = top; y < std::min(screen_h, top + height); ++y)
        std::memcpy(dst + y * screen_w + left, dialogue_ui_cache_.data() + y * screen_w + left,
                    static_cast<size_t>(width) * sizeof(uint16_t));
    };
    if (ddlc_ui_) {
      if (name_width) copy_rect(18, 146, name_width, 22);
      copy_rect(6, 164, 308, 72);
    } else {
      copy_rect(38, 158, 245, 82);
      if (name_width) copy_rect(50, 146, name_width, 25);
      if (face_) copy_rect(200, 152, 72, 72);
    }
  } else
    draw_ui_chrome(dst, ui_opacity);
  const int dialogue_top = ui_metrics::dialogue_top(ddlc_ui_);
  const int dialogue_bottom = ui_metrics::dialogue_bottom(ddlc_ui_);
  const int dialogue_width = ui_metrics::dialogue_width(ddlc_ui_, face_ != nullptr);
  const int body_line_height = ui_metrics::line_height(font_size_);
  const auto visible_text = std::string_view(text_).substr(0, reveal_bytes_);
  if (dialogue_scroll_max_ > 0) {
    if (dialogue_scroll_follow_) {
      const int visible_height =
          text_height(pack_, visible_text, dialogue_width, body_line_height, font_size_);
      dialogue_scroll_px_ = std::min(
          dialogue_scroll_max_, std::max(0, visible_height - (dialogue_bottom - dialogue_top)));
    }
    dialogue_scroll_px_ = std::clamp(dialogue_scroll_px_, 0, dialogue_scroll_max_);
  } else
    dialogue_scroll_px_ = 0;
  if (ddlc_ui_)
    outlined_text(dst, pack_, visible_text, ui_metrics::dialogue_left(true),
                  dialogue_top - dialogue_scroll_px_, dialogue_width, rgb565(61, 49, 58),
                  body_line_height, font_size_, ui_opacity, dialogue_top, dialogue_bottom);
  else
    draw_text(dst, pack_, visible_text, 50, dialogue_top - dialogue_scroll_px_, dialogue_width,
              0xFFFF, body_line_height, font_size_, ui_opacity, dialogue_top, dialogue_bottom);
  if (choice_open_ && choice_count_) {
    const int row_height = 20;
    const int panel_height = static_cast<int>(choice_count_) * row_height + 12;
    const int panel_top = std::max(26, 128 - panel_height);
    const bool poem_recipients = ddlc_ui_ && text_.find("Who should I show my poem to") != std::string::npos;
    if (poem_recipients) {
      pink_frame(dst,31,panel_top,258,panel_height,255,true);
      for (uint16_t i=0;i<choice_count_;++i) {
        const int y=panel_top+6+i*row_height;
        if (i==choice_selection_) {
          fill_rect_alpha(dst,38,y-1,244,row_height-1,0xffff,55);
          outlined_text(dst,pack_,">",43,y,10,0,ui_metrics::line_height(font_size_),font_size_,255);
        }
        outlined_text(dst,pack_,pack_.text(pack_.events()[choice_first_+i].text),59,y,216,
                      0,ui_metrics::line_height(font_size_),font_size_,255);
      }
    } else {
    fill_rect_alpha(dst, 31, panel_top, 258, panel_height, 0, 235);
    fill_rect(dst, 31, panel_top, 258, 1, 0xFFFF);
    fill_rect(dst, 31, panel_top + panel_height - 1, 258, 1, 0xFFFF);
    fill_rect(dst, 31, panel_top, 1, panel_height, 0xFFFF);
    fill_rect(dst, 288, panel_top, 1, panel_height, 0xFFFF);
    for (uint16_t i = 0; i < choice_count_; ++i) {
      const Event& option = pack_.events()[choice_first_ + i];
      const int y = panel_top + 6 + i * row_height;
      const uint16_t color = i == choice_selection_ ? orange : 0xFFFF;
      if (i == choice_selection_)
        draw_text(dst, pack_, ">", 43, y, 10, color, ui_metrics::line_height(font_size_),
                  font_size_);
      draw_text(dst, pack_, pack_.text(option.text), 59, y, 216, color,
                ui_metrics::line_height(font_size_), font_size_);
    }
    }
  }
  if (dialogue_scroll_max_ > 0 && ui_opacity) {
    // Keep the control anchored to the dialogue frame. Moving it beside the
    // narrower text column when Asya's portrait is present looks like a second
    // portrait border and makes the UI jump between speakers.
    const int track_x = ddlc_ui_ ? 308 : 277;
    const int viewport = dialogue_bottom - dialogue_top;
    const int content_height = viewport + dialogue_scroll_max_;
    const int thumb_height = std::max(12, viewport * viewport / content_height);
    const int thumb_y =
        dialogue_top + (viewport - thumb_height) * dialogue_scroll_px_ / dialogue_scroll_max_;
    fill_rect_alpha(dst, track_x, dialogue_top, 1, viewport, 0x528A, ui_opacity);
    fill_rect_alpha(dst, track_x - 1, thumb_y, 3, thumb_height, 0xCE79, ui_opacity);
    fill_rect_alpha(dst, track_x, thumb_y + 1, 1, std::max(1, thumb_height - 2), 0x3186,
                    ui_opacity);
  }
  auto_icon();
  shake();
  capture_game();
  screen_fade();
  confirm();
}

} // namespace nzato
