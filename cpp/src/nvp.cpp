#include "nzato/nvp.hpp"
#include <cstdio>
#include <cstring>

namespace nzato {
namespace {
uint16_t le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}
bool read_exact(FILE* f, void* dst, size_t bytes) {
  return std::fread(dst, 1, bytes, f) == bytes;
}
} // namespace
NvpStream::~NvpStream() {
  close();
}
void NvpStream::close() {
  if (file_)
    std::fclose(static_cast<FILE*>(file_));
  file_ = nullptr;
  header_ = {};
  std::vector<Chunk>().swap(chunks_);
  std::vector<uint8_t>().swap(chunk_bytes_);
  loaded_chunk_ = -1;
  error_ = "";
}
bool NvpStream::open(const char* path) {
  close();
  FILE* f = nullptr;
#ifdef _MSC_VER
  fopen_s(&f, path, "rb");
#else
  f = std::fopen(path, "rb");
#endif
  if (!f) {
    error_ = "cannot open NVP";
    return false;
  }
  uint8_t raw[48]{};
  if (!read_exact(f, raw, sizeof(raw)) || std::memcmp(raw, "NVP1", 4)) {
    std::fclose(f);
    error_ = "invalid NVP header";
    return false;
  }
  header_.version = le16(raw + 4);
  header_.flags = le16(raw + 6);
  header_.canvas_width = le16(raw + 8);
  header_.canvas_height = le16(raw + 10);
  header_.video_x = le16(raw + 12);
  header_.video_y = le16(raw + 14);
  header_.video_width = le16(raw + 16);
  header_.video_height = le16(raw + 18);
  header_.fps_num = le16(raw + 20);
  header_.fps_den = le16(raw + 22);
  header_.block_size = le16(raw + 24);
  header_.chunk_frames = le16(raw + 26);
  header_.frame_count = le32(raw + 28);
  header_.chunk_count = le32(raw + 32);
  header_.subtitle_count = le32(raw + 36);
  header_.index_offset = le32(raw + 40);
  header_.subtitle_offset = le32(raw + 44);
  if (header_.version != 11 || (header_.flags & 0x0f) != 1 || !header_.fps_num ||
      !header_.fps_den || !header_.chunk_count) {
    std::fclose(f);
    error_ = "unsupported NVP stream";
    return false;
  }
  if (std::fseek(f, static_cast<long>(header_.index_offset), SEEK_SET)) {
    std::fclose(f);
    error_ = "invalid NVP index";
    return false;
  }
  chunks_.resize(header_.chunk_count);
  for (auto& c : chunks_) {
    uint8_t item[24];
    if (!read_exact(f, item, sizeof(item))) {
      std::fclose(f);
      chunks_.clear();
      error_ = "truncated NVP index";
      return false;
    }
    c = {le32(item),      le32(item + 4),  le32(item + 8),
         le32(item + 12), le32(item + 16), le32(item + 20)};
    if (c.packed_size != c.unpacked_size) {
      std::fclose(f);
      chunks_.clear();
      error_ = "compressed NVP chunks are unsupported";
      return false;
    }
  }
  file_ = f;
  return true;
}
bool NvpStream::read_frame(uint32_t frame, std::vector<uint8_t>& bytes) {
  bytes.clear();
  const uint8_t* data = nullptr;
  size_t size = 0;
  if (!frame_view(frame, data, size))
    return false;
  bytes.assign(data, data + size);
  return true;
}
bool NvpStream::frame_view(uint32_t frame, const uint8_t*& data, size_t& size) {
  data = nullptr;
  size = 0;
  if (!file_ || frame >= header_.frame_count)
    return false;
  int found = loaded_chunk_;
  if (found >= 0) {
    const Chunk& loaded = chunks_[static_cast<size_t>(found)];
    if (frame < loaded.first_frame || frame >= loaded.first_frame + loaded.frame_count)
      found = -1;
  }
  for (size_t i = found < 0 ? 0 : chunks_.size(); i < chunks_.size(); i++)
    if (frame >= chunks_[i].first_frame &&
        frame < chunks_[i].first_frame + chunks_[i].frame_count) {
      found = static_cast<int>(i);
      break;
    }
  if (found < 0) {
    error_ = "frame missing from NVP index";
    return false;
  }
  const Chunk& c = chunks_[static_cast<size_t>(found)];
  if (loaded_chunk_ != found) {
    // Xvid performs aligned word look-ahead. Keep one guard at the end of the
    // resident chunk and decode frame slices in place, avoiding a heap-backed
    // copy of every compressed frame.
    constexpr size_t decoder_guard_bytes = 64;
    chunk_bytes_.assign(static_cast<size_t>(c.packed_size) + decoder_guard_bytes, 0);
    FILE* f = static_cast<FILE*>(file_);
    if (std::fseek(f, static_cast<long>(c.offset), SEEK_SET) ||
        !read_exact(f, chunk_bytes_.data(), c.packed_size)) {
      error_ = "cannot read NVP chunk";
      return false;
    }
    loaded_chunk_ = found;
  }
  const size_t table = c.frame_table_offset, local = frame - c.first_frame,
               head = 4u + static_cast<size_t>(c.frame_count) * 4u;
  if (table + head > c.packed_size) {
    error_ = "invalid NVP frame table";
    return false;
  }
  const uint32_t payload = le32(chunk_bytes_.data() + table);
  if (table + head + payload > c.packed_size) {
    error_ = "invalid NVP payload";
    return false;
  }
  const uint8_t* offsets = chunk_bytes_.data() + table + 4;
  const uint32_t begin = le32(offsets + local * 4),
                 end = local + 1 < c.frame_count ? le32(offsets + (local + 1) * 4) : payload;
  if (begin > end || end > payload) {
    error_ = "invalid NVP frame range";
    return false;
  }
  data = chunk_bytes_.data() + table + head + begin;
  size = end - begin;
  return true;
}
} // namespace nzato
