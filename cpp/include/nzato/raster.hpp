#pragma once
#include "nzato/pack.hpp"
#include <cstdint>
#include <string_view>
namespace nzato {
void clear(uint16_t* dst, uint16_t color);
void blit(uint16_t* dst, const Image& image, int x, int y, uint8_t opacity = 255);
void blit_clipped(uint16_t* dst, const Image& image, int x, int y, uint8_t opacity, int clip_left,
                  int clip_top, int clip_right, int clip_bottom);
void blit_scaled(uint16_t* dst, const Image& image, int x, int y, int width, int height);
// Nearest-neighbor sampling of the existing RGB565 image, with clipping/alpha.
void blit_resized(uint16_t* dst, const Image& image, int x, int y, int width, int height,
                  uint8_t opacity = 255);
void blit_transform(uint16_t* dst, const Image& image, int x, int y, int pivot_x, int pivot_y,
                    int scale, uint8_t opacity, uint8_t blend_mode, bool xflip);
void fill_rect(uint16_t* dst, int x, int y, int w, int h, uint16_t color);
void fill_rect_alpha(uint16_t* dst, int x, int y, int w, int h, uint16_t color, uint8_t opacity);
int text_width(const Pack& pack, std::string_view text, int font_size = 11);
int text_lines(const Pack& pack, std::string_view text, int max_width, int font_size = 11);
int text_height(const Pack& pack, std::string_view text, int max_width, int line_height = 13,
                int font_size = 11);
int centered_text_height(const Pack& pack, std::string_view text, int max_width, int line_height,
                         int font_size);
int text_height_compact(const Pack& pack, std::string_view text, int max_width, int line_height,
                        int font_size, int blank_line_height);
void draw_text(uint16_t* dst, const Pack& pack, std::string_view text, int x, int y, int max_width,
               uint16_t color, int line_height = 13, int font_size = 11, uint8_t opacity = 255,
               int clip_top = 0, int clip_bottom = 240);
void draw_text_compact(uint16_t* dst, const Pack& pack, std::string_view text, int x, int y,
                       int max_width, uint16_t color, int line_height, int font_size,
                       int blank_line_height, uint8_t opacity = 255, int clip_top = 0,
                       int clip_bottom = 240);
// Draw rich text with every visual (including automatically wrapped) line
// centered independently. `layout` is the complete statement while `text`
// may be its typewriter-visible prefix.
void draw_text_centered(uint16_t* dst, const Pack& pack, std::string_view text,
                        std::string_view layout, int center_x, int y, int max_width, uint16_t color,
                        int line_height, int font_size, uint8_t opacity = 255);
// White text with a one-pixel outline; 0x0001 stays visible in zero-keyed scroll caches.
void draw_outlined_surface(uint16_t* dst, int surface_width, int surface_height, const Pack& pack,
                           std::string_view text, int x, int y, int max_width, uint16_t outline,
                           int line_height, int font_size, int clip_top, int clip_bottom);
void draw_text_surface(uint16_t* dst, int surface_width, int surface_height, const Pack& pack,
                       std::string_view text, int x, int y, int max_width, uint16_t color,
                       int line_height = 13, int font_size = 11, uint8_t opacity = 255,
                       int clip_top = 0, int clip_bottom = 240);
uint32_t utf8_next(std::string_view s, size_t& at);
} // namespace nzato
