#pragma once
#include "nzato/nvp.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace nzato {
class NvpVideo {
public:
  ~NvpVideo();
  NvpVideo() = default;
  NvpVideo(const NvpVideo&) = delete;
  NvpVideo& operator=(const NvpVideo&) = delete;
  bool open(const char* path, uint16_t* external_framebuffer = nullptr);
  void close();
  bool next(bool output = true);
  void render(uint16_t* framebuffer) const;
  const uint16_t* framebuffer() const {
    return external_framebuffer_ ? external_framebuffer_
                                 : (pixels_.empty() ? nullptr : pixels_.data());
  }
  uint32_t frame_ms() const {
    return frame_ms_;
  }

private:
  NvpStream stream_;
  void* decoder_{};
  std::vector<uint16_t> pixels_;
  uint16_t* external_framebuffer_{};
  int width_{}, height_{};
  uint32_t frame_{}, frame_ms_{63};
};
} // namespace nzato
