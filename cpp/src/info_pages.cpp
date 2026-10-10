#include "nzato/player.hpp"
#include "nzato/raster.hpp"
#include "nzato/ui_metrics.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace nzato {
namespace {
struct ControlRow {
  std::string_view key_english;
  std::string_view key_chinese;
  std::string_view key_fallback;
  std::string_view english;
  std::string_view chinese;
  std::string_view key_fallback_chinese{};
};

constexpr std::array<ControlRow, 9> controls = {{
    {"\uE0B7/\uE035/\uE05F", "\uE1B7/\uE135/\uE15F", "enter/5/\nspace",
     "Advance dialogue / activate interface.", "推进对话并激活界面。", "enter/5/space"},
    {"\uE032\uE034\uE036\uE038\n\uE0A3\uE0A4\uE0A1\uE0A2",
     "\uE132\uE134\uE136\uE138\n\uE1A3\uE1A4\uE1A1\uE1A2", "2/4/6/8\n↑/↓/←/→",
     "Navigate the interface.", "导航界面。"},
    {"\uE038/\uE0A3", "\uE138/\uE1A3", "8/↑", "Show history.", "显示历史记录。"},
    {"\uE064", "\uE164", "esc", "Game menu / back.", "游戏菜单／返回。"},
    {"\uE02F", "\uE12F", "ctrl", "Hold to skip dialogue.", "按住时快进对话。"},
    {"\uE065", "\uE165", "tab", "Toggle skipping.", "切换快进。"},
    {"\uE041", "\uE141", "A", "Toggle automatic playback.", "切换自动播放。"},
    {"\uE048", "\uE148", "H", "Hide the user interface.", "隐藏用户界面。"},
    {"\uE063", "\uE163", "on", "Quickly exit the game.", "快速退出游戏。"},
}};

constexpr int control_key_right = 157;
constexpr int control_key_width = 84;
constexpr int control_key_size = ui_metrics::keycap_size;
constexpr int control_key_line_height = 16;

int control_key_height(std::string_view key) {
  return control_key_line_height * (1 + static_cast<int>(std::count(key.begin(), key.end(), '\n')));
}

void draw_control_key(uint16_t* surface, int surface_height, const Pack& pack, std::string_view key,
                      int y, int font_size, bool outlined) {
  size_t start = 0;
  int line = 0;
  while (start <= key.size()) {
    const size_t end = key.find('\n', start);
    const std::string_view text =
        key.substr(start, end == std::string_view::npos ? key.size() - start : end - start);
    const int width = std::min(control_key_width, text_width(pack, text, font_size));
    if (outlined) draw_outlined_surface(surface,screen_w,surface_height,pack,text,control_key_right-width,
                                        y+line*control_key_line_height,control_key_width,1,
                                        control_key_line_height,font_size,0,surface_height);
    else draw_text_surface(surface, screen_w, surface_height, pack, text, control_key_right - width,
                           y + line * control_key_line_height, control_key_width, 0xFFFF,
                           control_key_line_height, font_size, 255, 0, surface_height);
    if (end == std::string_view::npos)
      break;
    start = end + 1;
    ++line;
  }
}

std::string_view control_key(const ControlRow& row, bool chinese, bool key_font_available) {
  if (!key_font_available) {
    if (chinese && !row.key_fallback_chinese.empty())
      return row.key_fallback_chinese;
    return row.key_fallback;
  }
  return chinese ? row.key_chinese : row.key_english;
}
} // namespace

void Player::render_info_page(uint16_t* dst) const {
  const bool zh = pack_.chinese();
  const NavAction active = info_page_ == 1 ? NavAction::credits : NavAction::controls;
  render_menu_chrome(dst);
  render_menu_text(dst,nav_text(active),11,27,68,ui_metrics::page_title_size,true);
  render_sidebar(dst, active);

  constexpr int top = 57;
  constexpr int bottom = 231;
  if (info_page_ == 1) {
    const std::string_view body = pack_.credits();
    const int body_size = ui_metrics::credits_size(zh);
    const int body_line_height = ui_metrics::line_height(body_size);
    if (!credits_content_height_)
      credits_content_height_ = text_height(pack_, body, 225, body_line_height, body_size);
    const int total = credits_content_height_;
    const int max_scroll = std::max(0, total - (bottom - top));
    info_scroll_px_ = std::clamp(info_scroll_px_, 0, max_scroll);
    constexpr int max_cached_rows = 512;
    constexpr int viewport = bottom - top;
    const int cache_rows = std::min(total, max_cached_rows);
    const bool cache_covers =
        scroll_cache_kind_ == 2 && scroll_cache_content_height_ == total &&
        info_scroll_px_ >= scroll_cache_origin_ &&
        info_scroll_px_ + viewport <= scroll_cache_origin_ + scroll_cache_height_;
    const bool rebuild_cache = total > 0 && !cache_covers;
    if (rebuild_cache) {
      scroll_cache_kind_ = 2;
      scroll_cache_revision_ = 0;
      scroll_cache_content_height_ = total;
      scroll_cache_height_ = cache_rows;
      const int overscan = std::max(0, cache_rows - viewport);
      scroll_cache_origin_ =
          std::clamp(info_scroll_px_ - overscan / 2, 0, std::max(0, total - cache_rows));
      scroll_text_cache_.assign(static_cast<size_t>(screen_w) * cache_rows, 0);
      draw_text_surface(scroll_text_cache_.data(), screen_w, cache_rows, pack_, body, 82,
                        -scroll_cache_origin_, 225, 0xFFFF, body_line_height, body_size, 255, 0,
                        cache_rows);
    }
    if (!scroll_text_cache_.empty())
      composite_scroll_cache(dst, info_scroll_px_, top, bottom, 82, 308);
    else
      draw_text(dst, pack_, body, 82, top - info_scroll_px_, 225, 0xFFFF, body_line_height,
                body_size, 255, top, bottom);
    render_scrollbar(dst, top, bottom, total, info_scroll_px_, history_focus_ != 0);
    return;
  }

  std::array<int, controls.size()> row_heights{};
  const int description_size = ui_metrics::controls_size(zh);
  const int description_line_height = ui_metrics::line_height(description_size);
  const bool key_font_available = pack_.glyph(zh ? 0xE1B7U : 0xE0B7U, control_key_size) != nullptr;
  const int key_size = key_font_available ? control_key_size : ui_metrics::keycap_fallback_size;
  int total = 0;
  for (size_t index = 0; index < controls.size(); ++index) {
    const auto& row = controls[index];
    const std::string_view key = control_key(row, zh, key_font_available);
    const int key_height = control_key_height(key);
    const int description_height = text_height(pack_, zh ? row.chinese : row.english, 143,
                                               description_line_height, description_size);
    row_heights[index] = std::max(key_height, description_height) + 6;
    total += row_heights[index];
  }
  const int max_scroll = std::max(0, total - (bottom - top));
  info_scroll_px_ = std::clamp(info_scroll_px_, 0, max_scroll);
  const bool rebuild_cache = scroll_cache_kind_ != 3 || scroll_cache_content_height_ != total;
  if (rebuild_cache) {
    scroll_cache_kind_ = 3;
    scroll_cache_revision_ = 0;
    scroll_cache_origin_ = 0;
    scroll_cache_content_height_ = total;
    scroll_cache_height_ = total;
    scroll_text_cache_.assign(static_cast<size_t>(screen_w) * total, 0);
    int y = 0;
    for (size_t index = 0; index < controls.size(); ++index) {
      const auto& row = controls[index];
      const std::string_view key = control_key(row, zh, key_font_available);
      const std::string_view description = zh ? row.chinese : row.english;
      const int description_height =
          text_height(pack_, description, 143, description_line_height, description_size);
      const int content_height = row_heights[index] - 6;
      const int key_height = control_key_height(key);
      const int key_y = y + std::max(0, (content_height - key_height) / 2);
      const int description_y = y + std::max(0, (content_height - description_height) / 2);
      draw_control_key(scroll_text_cache_.data(), total, pack_, key, key_y, key_size, ddlc_ui_);
      render_menu_surface_text(scroll_text_cache_.data(),total,description,164,description_y,143,
                               description_line_height,description_size,0,total);
      y += row_heights[index];
    }
  }
  composite_scroll_cache(dst, info_scroll_px_, top, bottom, 82, 308);
  render_scrollbar(dst, top, bottom, total, info_scroll_px_, history_focus_ != 0);
}
} // namespace nzato
