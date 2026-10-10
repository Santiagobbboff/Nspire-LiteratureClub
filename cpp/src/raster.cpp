#include "nzato/raster.hpp"
#include "nzato/player.hpp"
#include <algorithm>
#include <array>
#include <string>
namespace nzato {
namespace {
uint16_t blend565(uint16_t target, uint16_t color, int opacity);

// Exact rounded division by 255 for the range used by 8-bit alpha products.
// ARM9 has no integer divide instruction, so spelling this as `/ 255` calls a
// comparatively expensive software helper in the hottest raster loops.
inline unsigned div255(unsigned value) {
  const unsigned rounded = value + 128U;
  return (rounded + (rounded >> 8U)) >> 8U;
}

constexpr std::array<uint8_t, 32 * 32> make_multiply5() {
  std::array<uint8_t, 32 * 32> result{};
  for (unsigned a = 0; a < 32; ++a)
    for (unsigned b = 0; b < 32; ++b)
      result[a * 32 + b] = static_cast<uint8_t>((a * b + 15) / 31);
  return result;
}

constexpr std::array<uint8_t, 64 * 64> make_multiply6() {
  std::array<uint8_t, 64 * 64> result{};
  for (unsigned a = 0; a < 64; ++a)
    for (unsigned b = 0; b < 64; ++b)
      result[a * 64 + b] = static_cast<uint8_t>((a * b + 31) / 63);
  return result;
}

constexpr auto multiply5 = make_multiply5();
constexpr auto multiply6 = make_multiply6();
} // namespace

void clear(uint16_t* d, uint16_t c) {
  std::fill(d, d + screen_w * screen_h, c);
}
void fill_rect(uint16_t* d, int x, int y, int w, int h, uint16_t c) {
  for (int yy = std::max(0, y); yy < std::min(screen_h, y + h); ++yy)
    std::fill(d + yy * screen_w + std::max(0, x), d + yy * screen_w + std::min(screen_w, x + w), c);
}
void blit_clipped(uint16_t* d, const Image& im, int x, int y, uint8_t opacity, int clip_left,
                  int clip_top, int clip_right, int clip_bottom) {
  if (!opacity)
    return;
  const int source_left = std::max({0, -x, clip_left - x});
  const int source_right = std::min({static_cast<int>(im.width), screen_w - x, clip_right - x});
  const int source_top = std::max({0, -y, clip_top - y});
  const int source_bottom = std::min({static_cast<int>(im.height), screen_h - y, clip_bottom - y});
  if (source_left >= source_right || source_top >= source_bottom)
    return;
  if (!im.direct_pixels.empty()) {
    for (int sy = source_top; sy < source_bottom; ++sy) {
      uint16_t* target = d + (y + sy) * screen_w + x + source_left;
      size_t at = static_cast<size_t>(sy) * im.width + source_left;
      for (int sx = source_left; sx < source_right; ++sx, ++at, ++target) {
        int a = im.alpha ? im.coverage[at] : 255;
        if (opacity != 255)
          a = static_cast<int>(div255(static_cast<unsigned>(a) * opacity));
        if (a == 255)
          *target = im.direct_pixels[at];
        else if (a)
          *target = blend565(*target, im.direct_pixels[at], a);
      }
    }
    return;
  }
  // Most backgrounds and the opaque parts of sprites use this path.  Avoid
  // evaluating alpha (and, formerly, a division) for every one of their
  // pixels.
  if (opacity == 255 && !im.alpha) {
    for (int sy = source_top; sy < source_bottom; ++sy) {
      uint16_t* target = d + (y + sy) * screen_w + x + source_left;
      size_t at = static_cast<size_t>(sy) * im.width + source_left;
      for (int sx = source_left; sx < source_right; ++sx, ++at, ++target) {
        const uint8_t p = im.pixels[at];
        if (p < im.palette.size())
          *target = im.palette[p];
      }
    }
    return;
  }
  for (int sy = source_top; sy < source_bottom; ++sy) {
    uint16_t* target = d + (y + sy) * screen_w + x + source_left;
    size_t at = static_cast<size_t>(sy) * im.width + source_left;
    for (int sx = source_left; sx < source_right; ++sx, ++at, ++target) {
      uint8_t p = im.pixels[at];
      if (p >= im.palette.size())
        continue;
      uint16_t s = im.palette[p];
      int a = im.alpha ? im.coverage[at] : 255;
      if (opacity != 255)
        a = static_cast<int>(div255(static_cast<unsigned>(a) * opacity));
      if (a == 0)
        continue;
      if (a == 255) {
        *target = s;
        continue;
      }
      *target = blend565(*target, s, a);
    }
  }
}
void blit(uint16_t* d, const Image& im, int x, int y, uint8_t opacity) {
  blit_clipped(d, im, x, y, opacity, 0, 0, screen_w, screen_h);
}
void blit_resized(uint16_t* dst, const Image& im, int x, int y, int width, int height,
                  uint8_t opacity) {
  if (!opacity || width <= 0 || height <= 0 || !im.width || !im.height) return;
  const int left = std::max(0, x), right = std::min(screen_w, x + width);
  const int top = std::max(0, y), bottom = std::min(screen_h, y + height);
  std::array<int, screen_w> columns{};
  for (int dx = left; dx < right; ++dx)
    columns[dx] = (dx - x) * im.width / width;
  for (int dy = top; dy < bottom; ++dy) {
    const size_t row = static_cast<size_t>((dy - y) * im.height / height) * im.width;
    for (int dx = left; dx < right; ++dx) {
      const size_t source = row + columns[dx];
      uint16_t color;
      if (!im.direct_pixels.empty()) color = im.direct_pixels[source];
      else {
        const uint8_t index = im.pixels[source];
        if (index >= im.palette.size()) continue;
        color = im.palette[index];
      }
      const unsigned alpha = im.alpha ? div255(im.coverage[source] * opacity) : opacity;
      uint16_t& pixel = dst[dy * screen_w + dx];
      if (alpha == 255) pixel = color;
      else if (alpha) pixel = blend565(pixel, color, alpha);
    }
  }
}
void blit_scaled(uint16_t* d, const Image& im, int x, int y, int width, int height) {
  for (int dy = 0; dy < height; dy++) {
    const int sy = dy * im.height / height;
    const int py = y + dy;
    if (py < 0 || py >= screen_h)
      continue;
    for (int dx = 0; dx < width; dx++) {
      const int sx = dx * im.width / width;
      const int px = x + dx;
      if (px < 0 || px >= screen_w)
        continue;
      const size_t at = static_cast<size_t>(sy) * im.width + sx;
      const uint8_t index = im.pixels[at];
      if (index >= im.palette.size())
        continue;
      d[py * screen_w + px] = im.palette[index];
    }
  }
}
void blit_transform(uint16_t* d, const Image& im, int x, int y, int pivot_x, int pivot_y, int scale,
                    uint8_t opacity, uint8_t blend_mode, bool xflip) {
  if (!opacity || scale <= 0)
    return;
  const size_t source_pixels = static_cast<size_t>(im.width) * im.height;
  if ((im.direct_pixels.empty() && im.pixels.size() < source_pixels) ||
      (!im.direct_pixels.empty() && im.direct_pixels.size() < source_pixels) ||
      (im.alpha && im.coverage.size() < source_pixels))
    return;
  if (im.full_screen) {
    uint16_t color = 0xffff;
    if (!im.direct_pixels.empty())
      color = im.direct_pixels[0];
    else if (!im.pixels.empty() && im.pixels[0] < im.palette.size())
      color = im.palette[im.pixels[0]];
    fill_rect_alpha(d, 0, 0, screen_w, screen_h, color, opacity);
    return;
  }
  if (scale == 1024 && !blend_mode && !xflip) {
    blit(d, im, x, y, opacity);
    return;
  }
  auto transformed = [&](int coordinate, int pivot) {
    const int64_t value = static_cast<int64_t>(coordinate - pivot) * scale;
    return pivot + static_cast<int>((value + (value >= 0 ? 512 : -512)) / 1024);
  };
  int left = transformed(x, pivot_x);
  const int top = transformed(y, pivot_y);
  const int right_edge = transformed(x + im.width, pivot_x);
  const int bottom_edge = transformed(y + im.height, pivot_y);
  const int width = std::max(1, right_edge - left);
  const int height = std::max(1, bottom_edge - top);
  if (xflip)
    left = 2 * pivot_x - left - width;
  const int first_x = std::max(0, left), last_x = std::min(screen_w, left + width);
  const int first_y = std::max(0, top), last_y = std::min(screen_h, top + height);
  // Division is especially expensive on ARM9. A transformed layer used to do
  // two software divisions per output pixel; calculate the nearest-neighbour
  // maps once per blit instead (at most 560 divisions for a whole screen).
  std::array<uint16_t, screen_w> source_x{};
  std::array<uint16_t, screen_h> source_y{};
  for (int px = first_x; px < last_x; ++px) {
    int sx = std::min<int>(im.width - 1, ((2 * (px - left) + 1) * im.width) / (2 * width));
    source_x[px] = static_cast<uint16_t>(xflip ? im.width - 1 - sx : sx);
  }
  for (int py = first_y; py < last_y; ++py)
    source_y[py] = static_cast<uint16_t>(
        std::min<int>(im.height - 1, ((2 * (py - top) + 1) * im.height) / (2 * height)));
  for (int py = first_y; py < last_y; ++py) {
    const int sy = source_y[py];
    uint16_t* target = d + py * screen_w + first_x;
    for (int px = first_x; px < last_x; ++px, ++target) {
      const int sx = source_x[px];
      const size_t at = static_cast<size_t>(sy) * im.width + sx;
      uint16_t source;
      if (!im.direct_pixels.empty())
        source = im.direct_pixels[at];
      else {
        const uint8_t index = im.pixels[at];
        if (index >= im.palette.size())
          continue;
        source = im.palette[index];
      }
      int alpha = im.alpha ? im.coverage[at] : 255;
      if (opacity != 255)
        alpha = static_cast<int>(div255(static_cast<unsigned>(alpha) * opacity));
      if (!alpha)
        continue;
      uint16_t color = source;
      if (blend_mode == 1) {
        const unsigned r = multiply5[(*target >> 11U) * 32U + (source >> 11U)];
        const unsigned g = multiply6[(((*target >> 5U) & 63U) * 64U) + ((source >> 5U) & 63U)];
        const unsigned b = multiply5[(*target & 31U) * 32U + (source & 31U)];
        color = static_cast<uint16_t>((r << 11U) | (g << 5U) | b);
      } else if (blend_mode == 2) {
        const unsigned r = std::min(31U, static_cast<unsigned>(*target >> 11U) +
                                             static_cast<unsigned>(source >> 11U));
        const unsigned g = std::min(63U, static_cast<unsigned>((*target >> 5U) & 63U) +
                                             static_cast<unsigned>((source >> 5U) & 63U));
        const unsigned b = std::min(31U, static_cast<unsigned>(*target & 31U) +
                                             static_cast<unsigned>(source & 31U));
        color = static_cast<uint16_t>((r << 11U) | (g << 5U) | b);
      }
      *target = blend565(*target, color, alpha);
    }
  }
}
uint32_t utf8_next(std::string_view s, size_t& i) {
  uint8_t c = static_cast<uint8_t>(s[i++]);
  if (c < 0x80)
    return c;
  uint32_t v = c & ((c < 0xE0) ? 0x1F : (c < 0xF0 ? 0x0F : 0x07));
  int n = (c < 0xE0) ? 1 : (c < 0xF0 ? 2 : 3);
  while (n-- && i < s.size())
    v = (v << 6) | (static_cast<uint8_t>(s[i++]) & 0x3F);
  return v;
}
namespace {
uint16_t blend565(uint16_t target, uint16_t color, int opacity) {
  if (opacity >= 255)
    return color;
  // Five-bit fixed-point alpha is much cheaper than six integer divisions
  // on ARM9 while retaining the native precision of RGB565.
  const unsigned a = static_cast<unsigned>(opacity + 4) >> 3;
  if (!a)
    return target;
  if (a >= 32)
    return color;
  const unsigned inverse = 32 - a;
  const unsigned rb = (((color & 0xf81fU) * a + (target & 0xf81fU) * inverse) >> 5) & 0xf81fU;
  const unsigned g = (((color & 0x07e0U) * a + (target & 0x07e0U) * inverse) >> 5) & 0x07e0U;
  return static_cast<uint16_t>(rb | g);
}

bool skip_tag(std::string_view s, size_t& at, bool* italic = nullptr, bool* bold = nullptr) {
  if (at >= s.size() || s[at] != '{')
    return false;
  const size_t end = s.find('}', at + 1);
  if (end == std::string_view::npos)
    return false;
  const auto tag = s.substr(at, end - at + 1);
  if (italic && (tag == "{i}" || tag == "{I}"))
    *italic = true;
  if (italic && (tag == "{/i}" || tag == "{/I}"))
    *italic = false;
  if (bold && (tag == "{b}" || tag == "{B}"))
    *bold = true;
  if (bold && (tag == "{/b}" || tag == "{/B}"))
    *bold = false;
  at = end + 1;
  return true;
}
bool text_space(uint32_t cp) {
  // Each poem font has its own space advance, including authors sharing a size.
  return cp == ' ' || cp == 0xE220 || cp == 0xE420 || cp == 0xE620 || cp == 0xE820;
}
bool cjk_opening_punctuation(uint32_t cp) {
  switch (cp) {
  case 0x2018: // ‘
  case 0x201c: // “
  case 0x3008: // 〈
  case 0x300a: // 《
  case 0x300c: // 「
  case 0x300e: // 『
  case 0x3010: // 【
  case 0x3014: // 〔
  case 0xff08: // （
    return true;
  default:
    return false;
  }
}
bool cjk_closing_punctuation(uint32_t cp) {
  switch (cp) {
  case 0x2019: // ’
  case 0x201d: // ”
  case 0x2026: // …
  case 0x3001: // 、
  case 0x3002: // 。
  case 0x3009: // 〉
  case 0x300b: // 》
  case 0x300d: // 」
  case 0x300f: // 』
  case 0x3011: // 】
  case 0x3015: // 〕
  case 0xff01: // ！
  case 0xff09: // ）
  case 0xff0c: // ，
  case 0xff1a: // ：
  case 0xff1b: // ；
  case 0xff1f: // ？
    return true;
  default:
    return false;
  }
}
int closing_run_width(const Pack& p, std::string_view s, size_t at, int font_size) {
  int width = 0;
  while (at < s.size()) {
    if (skip_tag(s, at))
      continue;
    const uint32_t cp = utf8_next(s, at);
    if (!cjk_closing_punctuation(cp))
      break;
    const Glyph* glyph = p.glyph(cp, static_cast<uint8_t>(font_size));
    if (!glyph)
      glyph = p.glyph('?', static_cast<uint8_t>(font_size));
    if (glyph)
      width += glyph->advance;
  }
  return width;
}
int unbreakable_suffix_width(const Pack& p, std::string_view s, size_t at, int font_size,
                             bool opening) {
  int width = 0;
  if (opening) {
    while (at < s.size() && skip_tag(s, at)) {
    }
    if (at < s.size()) {
      const uint32_t cp = utf8_next(s, at);
      const Glyph* glyph = p.glyph(cp, static_cast<uint8_t>(font_size));
      if (!glyph)
        glyph = p.glyph('?', static_cast<uint8_t>(font_size));
      if (glyph)
        width += glyph->advance;
    }
  }
  return width + closing_run_width(p, s, at, font_size);
}
void blend_pixel(uint16_t* d, int surface_width, int surface_height, int px, int py, uint16_t color,
                 int a) {
  if (px < 0 || px >= surface_width || py < 0 || py >= surface_height || !a)
    return;
  uint16_t& target = d[py * surface_width + px];
  // Color1 is the near-black ink reserved for zero-keyed menu text caches.
  // Preserve its outline mask rather than rounding alpha-blended ink to the
  // transparent zero key, which would punch holes in the outline on white.
  if (color == 1) {
    if (a >= 64) target = 1;
    return;
  }
  if (a == 255) {
    target = color;
    return;
  }
  target = blend565(target, color, a);
}
} // namespace
void fill_rect_alpha(uint16_t* d, int x, int y, int w, int h, uint16_t c, uint8_t opacity) {
  if (!opacity)
    return;
  if (opacity == 255) {
    fill_rect(d, x, y, w, h, c);
    return;
  }
  const int left = std::max(0, x), right = std::min(screen_w, x + w);
  const int top = std::max(0, y), bottom = std::min(screen_h, y + h);
  for (int yy = top; yy < bottom; ++yy) {
    uint16_t* row = d + yy * screen_w + left;
    for (int xx = left; xx < right; ++xx, ++row)
      *row = blend565(*row, c, opacity);
  }
}
int text_width(const Pack& p, std::string_view s, int font_size) {
  int width = 0, line = 0;
  size_t at = 0;
  while (at < s.size()) {
    if (skip_tag(s, at))
      continue;
    uint32_t cp = utf8_next(s, at);
    if (cp == '\n') {
      width = std::max(width, line);
      line = 0;
      continue;
    }
    const Glyph* g = p.glyph(cp, static_cast<uint8_t>(font_size));
    if (!g)
      g = p.glyph('?', static_cast<uint8_t>(font_size));
    if (g)
      line += g->advance;
  }
  return std::max(width, line);
}
int text_lines(const Pack& p, std::string_view s, int mw, int font_size) {
  int cx = 0, lines = 1;
  size_t at = 0;
  while (at < s.size()) {
    if (skip_tag(s, at))
      continue;
    uint32_t cp = utf8_next(s, at);
    if (cp == '\n') {
      cx = 0;
      lines++;
      continue;
    }
    const Glyph* g = p.glyph(cp, static_cast<uint8_t>(font_size));
    if (!g)
      g = p.glyph('?', static_cast<uint8_t>(font_size));
    if (!g)
      continue;
    if (text_space(cp)) {
      size_t look = at;
      int word = 0;
      while (look < s.size()) {
        if (skip_tag(s, look))
          continue;
        uint32_t next = utf8_next(s, look);
        if (text_space(next) || next == '\n') {
          break;
        }
        const Glyph* ng = p.glyph(next, static_cast<uint8_t>(font_size));
        if (ng)
          word += ng->advance;
      }
      if (cx && cx + g->advance + word > mw) {
        cx = 0;
        lines++;
        continue;
      }
    }
    const int suffix =
        text_space(cp) ? 0 : unbreakable_suffix_width(p, s, at, font_size, cjk_opening_punctuation(cp));
    if (cx && cx + g->advance + suffix > mw) {
      cx = 0;
      lines++;
    }
    cx += g->advance;
  }
  return lines;
}
int text_height_compact(const Pack& p, std::string_view s, int mw, int line_height, int font_size,
                        int blank_line_height) {
  int cx = 0, height = 0, step = line_height, active = font_size;
  size_t at = 0;
  bool italic = false, bold = false, line_has_glyph = false;
  while (at < s.size()) {
    if (s[at] == '{') {
      const size_t end = s.find('}', at + 1);
      if (end != std::string_view::npos) {
        const auto tag = s.substr(at + 1, end - at - 1);
        if (tag.rfind("size=", 0) == 0) {
          int value = 0;
          bool neg = false;
          size_t n = 5;
          if (n < tag.size() && (tag[n] == '+' || tag[n] == '-')) {
            neg = tag[n] == '-';
            n++;
          }
          for (; n < tag.size() && tag[n] >= '0' && tag[n] <= '9'; n++)
            value = value * 10 + (tag[n] - '0');
          if (neg)
            value = -value;
          active = std::clamp(font_size + value / 4, 9, 13);
          if (active == 12)
            active = 13;
        } else if (tag == "/size")
          active = font_size;
      }
    }
    if (skip_tag(s, at, &italic, &bold))
      continue;
    uint32_t cp = utf8_next(s, at);
    if (cp == '\n') {
      height += line_has_glyph || !blank_line_height ? step : blank_line_height;
      step = line_height;
      cx = 0;
      line_has_glyph = false;
      continue;
    }
    const Glyph* g = p.glyph(cp, static_cast<uint8_t>(active));
    if (!g)
      g = p.glyph('?', static_cast<uint8_t>(active));
    if (!g)
      continue;
    step = std::max(step, static_cast<int>(g->height) + 1);
    if (text_space(cp)) {
      size_t look = at;
      int word = 0;
      while (look < s.size()) {
        if (skip_tag(s, look))
          continue;
        const uint32_t next = utf8_next(s, look);
        if (text_space(next) || next == '\n')
          break;
        const Glyph* next_g = p.glyph(next, static_cast<uint8_t>(active));
        if (!next_g)
          next_g = p.glyph('?', static_cast<uint8_t>(active));
        if (next_g)
          word += next_g->advance;
      }
      if (cx + g->advance + word > mw) {
        height += step;
        step = line_height;
        cx = 0;
        line_has_glyph = false;
        continue;
      }
    }
    const int suffix =
        text_space(cp) ? 0 : unbreakable_suffix_width(p, s, at, active, cjk_opening_punctuation(cp));
    if (cx && cx + g->advance + suffix + (bold ? 1 : 0) > mw) {
      height += step;
      step = std::max(line_height, static_cast<int>(g->height) + 1);
      cx = 0;
    }
    cx += g->advance;
    line_has_glyph = true;
  }
  return height + (line_has_glyph || !blank_line_height ? step : blank_line_height);
}
int text_height(const Pack& p, std::string_view s, int mw, int line_height, int font_size) {
  return text_height_compact(p, s, mw, line_height, font_size, 0);
}
static void draw_text_surface_compact(uint16_t* d, int surface_width, int surface_height,
                                      const Pack& p, std::string_view s, int x, int y, int mw,
                                      uint16_t color, int line_height, int font_size,
                                      uint8_t opacity, int clip_top, int clip_bottom,
                                      int blank_line_height) {
  int cx = x, cy = y, line_step = line_height;
  const int right = std::min(surface_width, x + mw);
  size_t at = 0;
  bool italic = false, bold = false, line_has_glyph = false;
  const uint16_t base_color = color;
  int active_size = font_size;
  while (at < s.size()) {
    if (at < s.size() && s[at] == '{') {
      const size_t end = s.find('}', at + 1);
      if (end != std::string_view::npos) {
        const auto tag = s.substr(at + 1, end - at - 1);
        if (tag.rfind("size=", 0) == 0) {
          int value = 0;
          bool neg = false;
          size_t n = 5;
          if (n < tag.size() && (tag[n] == '+' || tag[n] == '-')) {
            neg = tag[n] == '-';
            n++;
          }
          for (; n < tag.size() && tag[n] >= '0' && tag[n] <= '9'; n++)
            value = value * 10 + (tag[n] - '0');
          if (neg)
            value = -value;
          active_size = std::clamp(font_size + value / 4, 9, 13);
          if (active_size == 12)
            active_size = 13;
        } else if (tag == "/size")
          active_size = font_size;
        else if (tag.rfind("color=#", 0) == 0) {
          unsigned value = 0;
          const auto hex = tag.substr(7);
          for (char c : hex) {
            value <<= 4;
            if (c >= '0' && c <= '9')
              value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
              value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
              value |= static_cast<unsigned>(c - 'A' + 10);
          }
          if (hex.size() == 3)
            value = ((value & 0xF00) << 12) | ((value & 0xF00) << 8) | ((value & 0x0F0) << 8) |
                    ((value & 0x0F0) << 4) | ((value & 0x00F) << 4) | (value & 0x00F);
          const unsigned r = (value >> 16) & 255, g = (value >> 8) & 255, b = value & 255;
          color = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        } else if (tag == "/color")
          color = base_color;
      }
    }
    if (skip_tag(s, at, &italic, &bold))
      continue;
    uint32_t cp = utf8_next(s, at);
    if (cp == '\n') {
      cx = x;
      cy += line_has_glyph || !blank_line_height ? line_step : blank_line_height;
      line_step = line_height;
      line_has_glyph = false;
      continue;
    }
    const Glyph* g = p.glyph(cp, static_cast<uint8_t>(active_size));
    if (!g)
      g = p.glyph('?', static_cast<uint8_t>(active_size));
    if (!g)
      continue;
    line_step = std::max(line_step, static_cast<int>(g->height) + 1);
    if (text_space(cp)) {
      size_t look = at;
      int word = 0;
      while (look < s.size()) {
        if (skip_tag(s, look))
          continue;
        uint32_t next = utf8_next(s, look);
        if (text_space(next) || next == '\n') {
          break;
        }
        const Glyph* ng = p.glyph(next, static_cast<uint8_t>(active_size));
        if (ng)
          word += ng->advance;
      }
      if (cx + g->advance + word > x + mw) {
        cx = x;
        cy += line_step;
        line_step = line_height;
        line_has_glyph = false;
        continue;
      }
    }
    const int suffix =
        text_space(cp) ? 0
                  : unbreakable_suffix_width(p, s, at, active_size, cjk_opening_punctuation(cp));
    if (cx != x && cx + g->advance + suffix + (bold ? 1 : 0) > x + mw) {
      cx = x;
      cy += line_step;
      line_step = std::max(line_height, static_cast<int>(g->height) + 1);
    }
    if (cy + g->height > surface_height)
      break;
    for (int yy = 0; yy < g->height; yy++) {
      const int py = cy + yy;
      if (py < clip_top || py >= clip_bottom)
        continue;
      for (int xx = 0; xx < g->width; xx++) {
        int a = static_cast<int>(div255(
            static_cast<unsigned>(g->pixels[static_cast<size_t>(yy) * g->width + xx]) * opacity));
        const int slant = italic ? (g->height - 1 - yy) / 5 : 0;
        const int px = cx + g->bearing_x + xx + slant;
        if (px >= x && px < right)
          blend_pixel(d, surface_width, surface_height, px, py, color, a);
        if (bold) {
          if (px + 1 < right)
            blend_pixel(d, surface_width, surface_height, px + 1, py, color, a);
          if (py + 1 < clip_bottom && px >= x && px < right)
            blend_pixel(d, surface_width, surface_height, px, py + 1, color, a / 2);
        }
      }
    }
    cx += g->advance;
    line_has_glyph = true;
  }
}

namespace {
struct VisualLines {
  // Even the smallest supported font cannot put this many visible lines on
  // the 240 px display. Keeping extra entries lets height calculation remain
  // useful for deliberately off-screen blocks without touching the heap.
  std::array<int, 128> widths{};
  size_t count{1};

  void set(int width) {
    if (count <= widths.size())
      widths[count - 1] = width;
  }
  void next() {
    ++count;
  }
};

VisualLines visual_line_widths(const Pack& p, std::string_view s, int mw, int font_size) {
  VisualLines lines;
  int cx = 0, active = font_size;
  bool italic = false, bold = false;
  size_t at = 0;
  while (at < s.size()) {
    if (s[at] == '{') {
      const size_t end = s.find('}', at + 1);
      if (end != std::string_view::npos) {
        const auto tag = s.substr(at + 1, end - at - 1);
        if (tag.rfind("size=", 0) == 0) {
          int value = 0;
          bool neg = false;
          size_t n = 5;
          if (n < tag.size() && (tag[n] == '+' || tag[n] == '-')) {
            neg = tag[n] == '-';
            ++n;
          }
          for (; n < tag.size() && tag[n] >= '0' && tag[n] <= '9'; ++n)
            value = value * 10 + tag[n] - '0';
          active = std::clamp(font_size + (neg ? -value : value) / 4, 9, 13);
          if (active == 12)
            active = 13;
        } else if (tag == "/size")
          active = font_size;
      }
    }
    if (skip_tag(s, at, &italic, &bold))
      continue;
    const uint32_t cp = utf8_next(s, at);
    if (cp == '\n') {
      lines.set(cx);
      lines.next();
      cx = 0;
      continue;
    }
    const Glyph* g = p.glyph(cp, static_cast<uint8_t>(active));
    if (!g)
      g = p.glyph('?', static_cast<uint8_t>(active));
    if (!g)
      continue;
    if (text_space(cp)) {
      size_t look = at;
      int word = 0;
      while (look < s.size()) {
        if (skip_tag(s, look))
          continue;
        const uint32_t next = utf8_next(s, look);
        if (text_space(next) || next == '\n')
          break;
        const Glyph* ng = p.glyph(next, static_cast<uint8_t>(active));
        if (!ng)
          ng = p.glyph('?', static_cast<uint8_t>(active));
        if (ng)
          word += ng->advance;
      }
      if (cx && cx + g->advance + word > mw) {
        lines.set(cx);
        lines.next();
        cx = 0;
        continue;
      }
    }
    const int suffix =
        text_space(cp) ? 0 : unbreakable_suffix_width(p, s, at, active, cjk_opening_punctuation(cp));
    if (cx && cx + g->advance + suffix + (bold ? 1 : 0) > mw) {
      lines.set(cx);
      lines.next();
      cx = 0;
    }
    cx += g->advance;
    lines.set(cx);
  }
  return lines;
}
} // namespace

int centered_text_height(const Pack& p, std::string_view s, int mw, int line_height,
                         int font_size) {
  return static_cast<int>(visual_line_widths(p, s, mw, font_size).count) * line_height;
}

void draw_text_centered(uint16_t* d, const Pack& p, std::string_view s, std::string_view layout,
                        int center_x, int y, int mw, uint16_t color, int line_height, int font_size,
                        uint8_t opacity) {
  const auto widths = visual_line_widths(p, layout, mw, font_size);
  // Render once per visual line. Prefixing active style tags preserves italic
  // and bold when an automatic wrap lands inside a tagged span.
  size_t at = 0, line_start = 0;
  int cx = 0, line = 0, active = font_size;
  int line_size = font_size;
  bool italic = false, bold = false, line_italic = false, line_bold = false;
  auto emit = [&](size_t end) {
    if (line_start >= s.size() || line >= static_cast<int>(widths.count) ||
        line >= static_cast<int>(widths.widths.size()))
      return;
    end = std::min(end, s.size());
    if (end <= line_start)
      return;
    const auto visible = s.substr(line_start, end - line_start);
    if (line_size == font_size && !line_italic && !line_bold) {
      draw_text(d, p, visible, center_x - widths.widths[line] / 2, y + line * line_height, mw,
                color, line_height, font_size, opacity);
      return;
    }
    std::string decorated;
    decorated.reserve(visible.size() + 24);
    if (line_size != font_size) {
      const int delta = (line_size - font_size) * 4;
      decorated += "{size=";
      if (delta > 0)
        decorated += '+';
      decorated += std::to_string(delta);
      decorated += '}';
    }
    if (line_italic)
      decorated += "{i}";
    if (line_bold)
      decorated += "{b}";
    decorated.append(visible);
    draw_text(d, p, decorated, center_x - widths.widths[line] / 2, y + line * line_height, mw,
              color, line_height, font_size, opacity);
  };
  while (at < layout.size()) {
    const size_t token_start = at;
    if (layout[at] == '{') {
      const size_t end = layout.find('}', at + 1);
      if (end != std::string_view::npos) {
        const auto tag = layout.substr(at + 1, end - at - 1);
        if (tag.rfind("size=", 0) == 0) {
          int value = 0;
          bool neg = false;
          size_t n = 5;
          if (n < tag.size() && (tag[n] == '+' || tag[n] == '-')) {
            neg = tag[n] == '-';
            ++n;
          }
          for (; n < tag.size() && tag[n] >= '0' && tag[n] <= '9'; ++n)
            value = value * 10 + tag[n] - '0';
          active = std::clamp(font_size + (neg ? -value : value) / 4, 9, 13);
          if (active == 12)
            active = 13;
        } else if (tag == "/size")
          active = font_size;
      }
    }
    const bool old_i = italic, old_b = bold;
    if (skip_tag(layout, at, &italic, &bold))
      continue;
    const uint32_t cp = utf8_next(layout, at);
    if (cp == '\n') {
      emit(token_start);
      ++line;
      line_start = at;
      cx = 0;
      line_size = active;
      line_italic = italic;
      line_bold = bold;
      continue;
    }
    const Glyph* g = p.glyph(cp, static_cast<uint8_t>(active));
    if (!g)
      g = p.glyph('?', static_cast<uint8_t>(active));
    if (!g)
      continue;
    bool wrap = false;
    if (text_space(cp)) {
      size_t look = at;
      int word = 0;
      while (look < layout.size()) {
        if (skip_tag(layout, look))
          continue;
        const uint32_t next = utf8_next(layout, look);
        if (text_space(next) || next == '\n')
          break;
        const Glyph* ng = p.glyph(next, static_cast<uint8_t>(active));
        if (!ng)
          ng = p.glyph('?', static_cast<uint8_t>(active));
        if (ng)
          word += ng->advance;
      }
      wrap = cx && cx + g->advance + word > mw;
    }
    const int suffix =
        text_space(cp) ? 0
                  : unbreakable_suffix_width(p, layout, at, active, cjk_opening_punctuation(cp));
    wrap = wrap || (cx && cx + g->advance + suffix + (bold ? 1 : 0) > mw);
    if (wrap) {
      italic = old_i;
      bold = old_b;
      emit(token_start);
      ++line;
      line_start = text_space(cp) ? at : token_start;
      cx = text_space(cp) ? 0 : g->advance;
      line_size = active;
      line_italic = old_i;
      line_bold = old_b;
    } else
      cx += g->advance;
  }
  emit(layout.size());
}
void draw_text_surface(uint16_t* d, int surface_width, int surface_height, const Pack& p,
                       std::string_view s, int x, int y, int mw, uint16_t color, int line_height,
                       int font_size, uint8_t opacity, int clip_top, int clip_bottom) {
  draw_text_surface_compact(d, surface_width, surface_height, p, s, x, y, mw, color, line_height,
                            font_size, opacity, clip_top, clip_bottom, 0);
}
void draw_text(uint16_t* d, const Pack& p, std::string_view s, int x, int y, int mw, uint16_t color,
               int line_height, int font_size, uint8_t opacity, int clip_top, int clip_bottom) {
  draw_text_surface(d, screen_w, screen_h, p, s, x, y, mw, color, line_height, font_size, opacity,
                    clip_top, clip_bottom);
}
void draw_text_compact(uint16_t* d, const Pack& p, std::string_view s, int x, int y, int mw,
                       uint16_t color, int line_height, int font_size, int blank_line_height,
                       uint8_t opacity, int clip_top, int clip_bottom) {
  draw_text_surface_compact(d, screen_w, screen_h, p, s, x, y, mw, color, line_height, font_size,
                            opacity, clip_top, clip_bottom, blank_line_height);
}
} // namespace nzato

namespace nzato {
void draw_outlined_surface(uint16_t* dst, int width, int height, const Pack& pack,
                           std::string_view text, int x, int y, int max_width, uint16_t outline,
                           int leading, int size, int top, int bottom) {
  for (const auto& offset : {std::array<int,2>{-1,0}, {1,0}, {0,-1}, {0,1}})
    draw_text_surface(dst, width, height, pack, text, x+offset[0], y+offset[1], max_width,
                      outline, leading, size, 255, top, bottom);
  draw_text_surface(dst, width, height, pack, text, x, y, max_width, 0xffff,
                    leading, size, 255, top, bottom);
}
}
