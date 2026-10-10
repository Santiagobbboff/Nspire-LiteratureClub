#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nzato {
struct NvpHeader {
  uint16_t version{}, flags{}, canvas_width{}, canvas_height{}, video_x{}, video_y{};
  uint16_t video_width{}, video_height{}, fps_num{}, fps_den{}, block_size{}, chunk_frames{};
  uint32_t frame_count{}, chunk_count{}, subtitle_count{}, index_offset{}, subtitle_offset{};
};

class NvpStream {
public:
  NvpStream() = default;
  ~NvpStream();
  NvpStream(const NvpStream&) = delete;
  NvpStream& operator=(const NvpStream&) = delete;
  bool open(const char* path);
  void close();
  bool read_frame(uint32_t frame, std::vector<uint8_t>& bytes);
  bool frame_view(uint32_t frame, const uint8_t*& data, size_t& size);
  const NvpHeader& header() const {
    return header_;
  }
  uint32_t frame_ms() const {
    return header_.fps_num
               ? static_cast<uint32_t>((1000ull * header_.fps_den + header_.fps_num / 2) /
                                       header_.fps_num)
               : 0;
  }
  const char* error() const {
    return error_;
  }

private:
  struct Chunk {
    uint32_t offset{}, packed_size{}, unpacked_size{}, first_frame{}, frame_count{},
        frame_table_offset{};
  };
  void* file_{};
  NvpHeader header_{};
  std::vector<Chunk> chunks_;
  std::vector<uint8_t> chunk_bytes_;
  int loaded_chunk_{-1};
  const char* error_ = "";
};
} // namespace nzato
