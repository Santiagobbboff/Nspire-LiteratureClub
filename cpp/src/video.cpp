#include "nzato/video.hpp"
#include "../third_party/nvp/mpeg4_xvid.h"
#include "nzato/player.hpp"
#include <algorithm>

namespace nzato {
NvpVideo::~NvpVideo() {
  close();
}
void NvpVideo::close() {
  mpeg4_xvid_destroy(decoder_);
  decoder_ = nullptr;
  stream_.close();
  std::vector<uint16_t>().swap(pixels_);
  width_ = height_ = 0;
  frame_ = 0;
  external_framebuffer_ = nullptr;
}
bool NvpVideo::open(const char* path, uint16_t* external_framebuffer) {
  close();
  if (!stream_.open(path))
    return false;
  width_ = stream_.header().video_width;
  height_ = stream_.header().video_height;
  frame_ms_ = stream_.frame_ms();
  if (width_ <= 0 || height_ <= 0 || !mpeg4_xvid_global_init(nullptr, 0) ||
      !mpeg4_xvid_create(&decoder_, width_, height_)) {
    close();
    return false;
  }
  if (width_ > screen_w || height_ > screen_h) {
    close();
    return false;
  }
  external_framebuffer_ = external_framebuffer;
  if (external_framebuffer_)
    std::fill_n(external_framebuffer_, static_cast<size_t>(screen_w) * screen_h, uint16_t{});
  else
    pixels_.assign(static_cast<size_t>(screen_w) * screen_h, uint16_t{});
  return next();
}
bool NvpVideo::next(bool output) {
  const uint8_t* encoded = nullptr;
  size_t encoded_size = 0;
  if (!decoder_ || frame_ >= stream_.header().frame_count ||
      !stream_.frame_view(frame_, encoded, encoded_size))
    return false;
  const int x = (screen_w - width_) / 2, y = (screen_h - height_) / 2;
  uint16_t* canvas = external_framebuffer_ ? external_framebuffer_ : pixels_.data();
  uint16_t* output_pixels = canvas + static_cast<size_t>(y) * screen_w + x;
  const bool ok =
      mpeg4_xvid_decode_frame(decoder_, encoded, encoded_size, output_pixels, width_, height_,
                              screen_w * static_cast<int>(sizeof(uint16_t)), output, frame_ == 0);
  if (ok)
    ++frame_;
  return ok;
}
void NvpVideo::render(uint16_t* dst) const {
  const uint16_t* source = framebuffer();
  if (!source || source == dst)
    return;
  std::copy_n(source, static_cast<size_t>(screen_w) * screen_h, dst);
}
} // namespace nzato
