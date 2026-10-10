#include "nzato/player.hpp"
#include "nzato/raster.hpp"
#include "nzato/ui_metrics.hpp"

#include <algorithm>

namespace nzato {
namespace {
constexpr uint16_t orange = ui_metrics::orange;
constexpr uint16_t dim = ui_metrics::dim;
} // namespace

std::string_view Player::nav_text(NavAction action) const {
  switch (action) {
  case NavAction::start:
    return ddlc_ui_ ? std::string_view("New Game") : pack_.ui(UiText::start);
  case NavAction::log:
    return pack_.ui(UiText::log);
  case NavAction::save:
    return pack_.ui(UiText::save);
  case NavAction::load:
    return pack_.ui(UiText::load);
  case NavAction::gallery:
    return pack_.ui(UiText::gallery);
  case NavAction::title:
    return ddlc_ui_ ? std::string_view("Main Menu") : pack_.ui(UiText::title);
  case NavAction::credits:
    return pack_.ui(UiText::credits);
  case NavAction::controls:
    return pack_.ui(UiText::controls);
  case NavAction::quit:
    return pack_.ui(UiText::quit);
  case NavAction::return_to_game:
    return pack_.ui(UiText::return_to_game);
  }
  return "";
}

void Player::ensure_title_cache() const {
  if (title_base_cache_valid_)
    return;
  title_base_cache_.assign(static_cast<size_t>(screen_w) * screen_h, 0);
  title_static_cache_.resize(static_cast<size_t>(screen_w) * screen_h);
  clear(title_base_cache_.data(), 0);
  if (title_background_)
    blit(title_base_cache_.data(), *title_background_, 0, 30);
  std::copy(title_base_cache_.begin(), title_base_cache_.end(), title_static_cache_.begin());
  if (title_menu_overlay_)
    blit(title_static_cache_.data(), *title_menu_overlay_, 0, 30);
  title_base_cache_valid_ = true;
}

void Player::render_menu_chrome(uint16_t* dst) const {
  if (ddlc_ui_) {
    if (!menu_chrome_cache_valid_) {
      menu_chrome_cache_.resize(screen_w * screen_h);
      auto* canvas = menu_chrome_cache_.data();
      clear(canvas, 0xffff);
      // Static staggered pale-pink dots and a curved sidebar, drawn once.
      constexpr uint16_t dots = 0xff7f, side = 0xff3e, border = 0xfddb;
      for (int cy=-12; cy<screen_h+18; cy+=48)
        for (int cx=((cy+12)/48)%2 ? 24 : 0; cx<screen_w+18; cx+=48)
          for (int dy=-13; dy<=13; ++dy)
            for (int dx=-13; dx<=13; ++dx)
              if (dx*dx+dy*dy <= 169 && cx+dx>=0 && cx+dx<screen_w && cy+dy>=0 && cy+dy<screen_h)
                canvas[(cy+dy)*screen_w+cx+dx] = dots;
      for (int y=0; y<screen_h; ++y) {
        const int distance = y-screen_h/2;
        // Match the pack title's flattened apex, avoiding a one-pixel tip.
        const int edge = std::min(77, 60 + 18*(14400-distance*distance)/14400);
        fill_rect(canvas, 0, y, edge, 1, side);
        fill_rect(canvas, edge, y, 6, 1, border);
      }
      menu_chrome_cache_valid_ = true;
    }
    std::copy(menu_chrome_cache_.begin(), menu_chrome_cache_.end(), dst);
    return;
  }
  if (file_return_title_) {
    ensure_title_cache();
    // Ren'Py's game_menu screen uses the main-menu background together with
    // gui/overlay/game_menu.png. It does not darken the already composited
    // gui/overlay/main_menu.png screen.
    std::copy(title_base_cache_.begin(), title_base_cache_.end(), dst);
    if (history_overlay_)
      blit(dst, *history_overlay_, 0, 30);
    return;
  }
  if (!menu_chrome_cache_valid_) {
    menu_chrome_cache_.assign(static_cast<size_t>(screen_w) * screen_h, 0);
    clear(menu_chrome_cache_.data(), 0);
    if (history_background_)
      blit(menu_chrome_cache_.data(), *history_background_, 0, 30);
    if (history_overlay_)
      blit(menu_chrome_cache_.data(), *history_overlay_, 0, 30);
    menu_chrome_cache_valid_ = true;
  }
  std::copy(menu_chrome_cache_.begin(), menu_chrome_cache_.end(), dst);
}

void Player::render_sidebar(uint16_t* dst, NavAction active) const {
  const int size = ui_metrics::menu_size(pack_.chinese());
  const int line_height = ui_metrics::line_height(size);
  for (int i = 0; i < sidebar_count(); ++i) {
    const NavAction action = sidebar_action(i);
    uint16_t color = action == active ? 0xFFFF : dim;
    if (!history_focus_ && history_selection_ == i)
      color = orange;
    const int y = action == NavAction::return_to_game ? 210 : 76 + i * 18;
    if (ddlc_ui_) {
      if (!history_focus_ && history_selection_ == i)
        render_menu_text(dst, ">", 2, y, 8, size, true);
      render_menu_text(dst, nav_text(action), 11, y, 68, size, true);
    } else draw_text(dst, pack_, nav_text(action), 11, y, 68, color, line_height, size);
  }
}

void Player::render_scrollbar(uint16_t* dst, int top, int bottom, int content_height,
                              int scroll_from_top, bool focused) const {
  const int viewport = bottom - top;
  const int max_scroll = std::max(0, content_height - viewport);
  if (!max_scroll)
    return;
  fill_rect(dst, 313, top, 1, viewport, focused ? 0x7BCF : 0x528A);
  const int thumb_height = std::max(14, viewport * viewport / std::max(viewport, content_height));
  const int travel = viewport - thumb_height;
  const int thumb_y = top + travel * std::clamp(scroll_from_top, 0, max_scroll) / max_scroll;
  fill_rect(dst, focused ? 311 : 312, thumb_y, focused ? 5 : 3, thumb_height,
            focused ? 0xE71C : 0xCE79);
  fill_rect(dst, focused ? 312 : 313, thumb_y + 1, focused ? 3 : 1, std::max(1, thumb_height - 2),
            0x3186);
}

void Player::composite_scroll_cache(uint16_t* dst, int source_top, int top, int bottom, int left,
                                    int right) const {
  if (scroll_text_cache_.empty() || scroll_cache_height_ <= 0)
    return;
  const int clipped_left = std::clamp(left, 0, screen_w);
  const int clipped_right = std::clamp(right, clipped_left, screen_w);
  for (int y = top; y < bottom; ++y) {
    const int source_y = source_top + y - top - scroll_cache_origin_;
    if (source_y < 0 || source_y >= scroll_cache_height_)
      continue;
    const uint16_t* source = scroll_text_cache_.data() + source_y * screen_w + clipped_left;
    uint16_t* target = dst + y * screen_w + clipped_left;
    for (int x = clipped_left; x < clipped_right; ++x, ++source, ++target)
      if (*source)
        *target = *source;
  }
}
} // namespace nzato

namespace nzato {
void Player::render_menu_text(uint16_t* dst, std::string_view text, int x, int y, int width,
                              int size, bool sidebar, int top, int bottom) const {
  if (ddlc_ui_)
    draw_outlined_surface(dst, screen_w, screen_h, pack_, text, x,y,width,
                          sidebar ? uint16_t(0xb272) : uint16_t(1),
                          ui_metrics::line_height(size),size,top,bottom);
  else draw_text(dst,pack_,text,x,y,width,0xffff,ui_metrics::line_height(size),size,255,top,bottom);
}
void Player::render_menu_surface_text(uint16_t* dst, int height, std::string_view text,
                                      int x,int y,int width,int leading,int size,int top,int bottom) const {
  if (ddlc_ui_) draw_outlined_surface(dst,screen_w,height,pack_,text,x,y,width,1,leading,size,top,bottom);
  else draw_text_surface(dst,screen_w,height,pack_,text,x,y,width,0xffff,leading,size,255,top,bottom);
}
}

namespace nzato {
void Player::render_ddlc_title(uint16_t* dst) const {
  if (!title_base_cache_valid_) {
    title_static_cache_.resize(screen_w*screen_h);
    clear(title_static_cache_.data(), 0);
    if (const Image* title = pack_.image(pack_.ddlc_title_id()))
      blit(title_static_cache_.data(), *title, 0, 0);
    title_base_cache_valid_ = true;
  }
  std::copy(title_static_cache_.begin(),title_static_cache_.end(),dst);
}
}
