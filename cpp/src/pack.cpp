#include "nzato/pack.hpp"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace nzato {
namespace {
// A pack may be split into consecutive part files (ddlc.nzp.tns, ddlc.nzp2.tns, ...)
// because large single transfers to the calculator are unreliable. Reads and
// seeks address the concatenation.
struct PackFile {
  std::vector<FILE*> parts;
  std::vector<uint64_t> starts; // starts[i] = offset of part i; starts.back() = total size
  uint64_t pos{};
  bool add(FILE* f) {
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) return false;
    const long size = std::ftell(f);
    if (size < 0) return false;
    if (starts.empty()) starts.push_back(0);
    parts.push_back(f);
    starts.push_back(starts.back() + static_cast<uint64_t>(size));
    return true;
  }
  uint64_t size() const { return starts.empty() ? 0 : starts.back(); }
  uint64_t tell() const { return pos; }
  bool seek(uint64_t to) {
    if (to > size()) return false;
    pos = to;
    return true;
  }
  size_t read(void* out, size_t n) {
    size_t done = 0;
    uint8_t* target = static_cast<uint8_t*>(out);
    while (done < n && pos < size()) {
      size_t part = 0;
      while (part + 1 < parts.size() && pos >= starts[part + 1]) ++part;
      const uint64_t local = pos - starts[part];
      const size_t available = static_cast<size_t>(std::min<uint64_t>(n - done, starts[part + 1] - pos));
      if (std::fseek(parts[part], static_cast<long>(local), SEEK_SET) != 0) break;
      const size_t got = std::fread(target + done, 1, available, parts[part]);
      pos += got;
      done += got;
      if (got != available) break;
    }
    return done;
  }
  void close() {
    for (FILE* f : parts) std::fclose(f);
    parts.clear();
    starts.clear();
    pos = 0;
  }
};
template <class T> bool read(PackFile* f, T& v) {
  return f->read(&v, sizeof(v)) == sizeof(v);
}
bool bytes(PackFile* f, void* p, size_t n) {
  return n == 0 || f->read(p, n) == n;
}
uint32_t position(PackFile* f) {
  const uint64_t value = f->tell();
  return value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
}
std::string part_name(const std::string& path, int index) {
  // "ddlc.nzp.tns" -> "ddlc.nzp2.tns", "ddlc.nzp" -> "ddlc.nzp2"
  const size_t at = path.rfind(".nzp");
  if (at == std::string::npos) return {};
  return path.substr(0, at + 4) + std::to_string(index) + path.substr(at + 4);
}
#pragma pack(push, 1)
struct Header {
  char magic[4];
  uint16_t flags;
  uint32_t images, glyphs, events, strings;
};
struct ImageCore {
  uint16_t id, width, height, palette;
  uint8_t flags, reserved;
  uint32_t bytes;
  uint16_t animation_next, animation_ms;
};
struct ImageHead {
  ImageCore image;
  uint16_t composite_base;
  int16_t patch_x, patch_y;
  uint16_t patch_width, patch_height;
};
struct GlyphHead {
  uint32_t cp;
  uint8_t width, height, advance, reserved;
  uint16_t bytes;
};
struct EventDisk {
  uint8_t op, flags;
  uint16_t resource;
  int16_t x, y;
  TextRef speaker, text;
};
struct DdlcUiFooter {
  char magic[4];
  uint16_t version, title, notebook;
};
struct UiFooter {
  char magic[4];
  uint32_t credits_offset, credits_size;
  TextRef texts[ui_text_count];
};
#pragma pack(pop)
} // namespace

Pack::~Pack() {
  close_file();
}

void Pack::close_file() {
  if (file_) {
    auto* f = static_cast<PackFile*>(file_);
    f->close();
    delete f;
    file_ = nullptr;
  }
}

bool Pack::load(const char* path) {
  close_file();
  const char* slash1 = std::strrchr(path, '/');
  const char* slash2 = std::strrchr(path, '\\');
  const char* slash = slash1 > slash2 ? slash1 : slash2;
  directory_ = slash ? std::string(path, static_cast<size_t>(slash - path + 1)) : std::string{};
  auto* f = new PackFile;
  auto open_part = [](const std::string& name) {
    FILE* file = nullptr;
#ifdef _MSC_VER
    fopen_s(&file, name.c_str(), "rb");
#else
    file = std::fopen(name.c_str(), "rb");
#endif
    return file;
  };
  if (!f->add(open_part(path))) {
    delete f;
    error_ = "cannot open pack";
    return false;
  }
  for (int index = 2; index < 10; ++index) {
    const std::string name = part_name(path, index);
    FILE* part = name.empty() ? nullptr : open_part(name);
    if (!part) break;
    f->add(part);
  }
  return load_file(f);
}

#ifdef _WIN32
bool Pack::load(const wchar_t* path) {
  close_file();
  auto* f = new PackFile;
  FILE* first = nullptr;
  _wfopen_s(&first, path, L"rb");
  if (!f->add(first)) {
    delete f;
    error_ = "cannot open pack";
    return false;
  }
  std::wstring wide(path);
  const size_t at = wide.rfind(L".nzp");
  for (int index = 2; at != std::wstring::npos && index < 10; ++index) {
    const std::wstring name = wide.substr(0, at + 4) + std::to_wstring(index) + wide.substr(at + 4);
    FILE* part = nullptr;
    _wfopen_s(&part, name.c_str(), L"rb");
    if (!part) break;
    f->add(part);
  }
  const wchar_t *a = wcsrchr(path, L'/'), *b = wcsrchr(path, L'\\'), *slash = a > b ? a : b;
  const int chars = slash ? static_cast<int>(slash - path + 1) : 0;
  const int path_bytes =
      chars ? WideCharToMultiByte(CP_ACP, 0, path, chars, nullptr, 0, nullptr, nullptr) : 0;
  directory_.resize(path_bytes);
  if (path_bytes)
    WideCharToMultiByte(CP_ACP, 0, path, chars, directory_.data(), path_bytes, nullptr, nullptr);
  return load_file(f);
}
#endif

bool Pack::load_file(void* handle) {
  auto* f = static_cast<PackFile*>(handle);
  auto fail = [&](const char* message) {
    error_ = message;
    f->close();
    delete f;
    return false;
  };

  Header h{};
  if (!read(f, h) || std::memcmp(h.magic, "NZPK", 4))
    return fail("incompatible content pack");

  chinese_ = (h.flags & 1) != 0;
  loose_translation_ = (h.flags & 2) != 0;
  // English's complete font payload fits in 128 KiB. Chinese gameplay touches
  // about 450 KiB across an end-to-end run; a 768 KiB first block also leaves
  // room for the menu faces without another late heap allocation.
  glyph_block_capacity_ = (chinese_ ? 768U : 128U) * 1024U;
  images_.clear();
  glyphs_.clear();
  events_.clear();
  strings_.clear();
  credits_ = {};
  ddlc_title_id_ = ddlc_notebook_id_ = 0;
  poem_words_ = {};
  ui_texts_.fill({});
  images_.reserve(h.images);
  glyphs_.reserve(h.glyphs);
  events_.reserve(h.events);
  resident_image_bytes_ = 0;
  resident_glyph_bytes_ = 0;
  for (auto& block : glyph_blocks_)
    std::vector<uint8_t>().swap(block);
  glyph_block_count_ = glyph_block_used_ = 0;
  image_use_clock_ = 0;
  pinned_image_count_ = 0;

  auto add_image = [&](const ImageHead& d, uint32_t data_offset) {
    Image im;
    const ImageCore& image = d.image;
    im.id = image.id;
    im.width = image.width;
    im.height = image.height;
    im.animation_next = image.animation_next;
    im.animation_ms = image.animation_ms;
    im.composite_base = d.composite_base;
    im.patch_x = d.patch_x;
    im.patch_y = d.patch_y;
    im.patch_width = d.patch_width ? d.patch_width : image.width;
    im.patch_height = d.patch_height ? d.patch_height : image.height;
    im.alpha = (image.flags & 2) != 0;
    im.particle = (image.flags & 4) != 0;
    im.background = (image.flags & 8) != 0;
    im.base_layer = (image.flags & 16) != 0;
    im.screen_layer = (image.flags & 32) != 0;
    im.full_screen = (image.flags & 64) != 0;
    im.alias = (image.flags & 128) != 0;
    im.palette_entries = image.palette ? image.palette : 256;
    im.pixel_bytes = image.bytes;
    im.data_offset = data_offset;
    images_.push_back(std::move(im));
  };
  auto add_glyph = [&](const GlyphHead& d, uint32_t data_offset) {
    Glyph g;
    g.codepoint = d.cp;
    g.width = d.width;
    g.height = d.height;
    g.advance = d.advance;
    g.bearing_x = static_cast<int8_t>(d.reserved);
    g.bitmap_bytes = d.bytes;
    g.data_offset = data_offset;
    glyphs_.push_back(g);
  };
  auto add_event = [&](const EventDisk& d) {
    events_.push_back({static_cast<Op>(d.op), d.flags, d.resource, d.x, d.y, d.speaker, d.text});
  };

  std::vector<ImageHead> image_heads(h.images);
  std::vector<GlyphHead> glyph_heads(h.glyphs);
  std::vector<EventDisk> event_disks(h.events);
  if (!bytes(f, image_heads.data(), image_heads.size() * sizeof(ImageHead)))
    return fail("truncated image table");
  if (!bytes(f, glyph_heads.data(), glyph_heads.size() * sizeof(GlyphHead)))
    return fail("truncated glyph table");
  if (!bytes(f, event_disks.data(), event_disks.size() * sizeof(EventDisk)))
    return fail("truncated event table");
  for (size_t i = 0; i < event_disks.size(); ++i) {
    const auto& event = event_disks[i];
    switch (static_cast<Op>(event.op)) {
    case Op::choice:
      if (event.resource == 0 || event.resource > 10 || event.x < 0 || event.x >= story_state_slots ||
          i + event.resource >= event_disks.size())
        return fail("invalid story choice");
      for (size_t option = i + 1; option <= i + event.resource; ++option)
        if (static_cast<Op>(event_disks[option].op) != Op::choice_option ||
            event_disks[option].speaker.offset >= h.events)
          return fail("invalid story choice option");
      break;
    case Op::choice_option:
      if (event.speaker.offset >= h.events)
        return fail("invalid story choice target");
      break;
    case Op::jump:
      if (event.speaker.offset >= h.events)
        return fail("invalid story jump");
      break;
    case Op::set_state:
      if (event.resource >= story_state_slots)
        return fail("invalid story state slot");
      break;
    case Op::if_state:
      if (event.resource >= story_state_slots || event.speaker.offset >= h.events)
        return fail("invalid story condition");
      break;
    case Op::add_state:
      if (event.resource >= story_state_slots)
        return fail("invalid story state slot");
      break;
    case Op::if_state_gt:
      if (event.resource >= story_state_slots || event.x < 0 || event.x >= story_state_slots ||
          event.speaker.offset >= h.events)
        return fail("invalid story comparison");
      break;
    case Op::persistent_load:
    case Op::persistent_store:
      if (event.resource >= story_state_slots || event.x < 0 ||
          static_cast<size_t>(event.x) >= persistent_value_count)
        return fail("invalid persistent access");
      break;
    case Op::rand_state:
      if (event.resource >= story_state_slots || event.x < 1 || event.x > 16 ||
          (event.y >= 0 && event.y >= static_cast<int>(story_state_slots)))
        return fail("invalid random state");
      break;
    default:
      break;
    }
  }
  strings_.resize(h.strings);
  if (!bytes(f, strings_.data(), strings_.size()))
    return fail("truncated strings");

  uint64_t offset = position(f);
  if (offset == UINT32_MAX)
    return fail("invalid payload offset");
  for (const auto& d : image_heads) {
    if (offset > UINT32_MAX)
      return fail("pack payload is too large");
    add_image(d, static_cast<uint32_t>(offset));
    if (!(d.image.flags & 128)) {
      const size_t palette_entries = d.image.palette ? d.image.palette : 256;
      offset += palette_entries * sizeof(uint16_t) +
                static_cast<size_t>(d.image.bytes) * ((d.image.flags & 2) ? 2U : 1U);
    }
  }
  for (const auto& d : glyph_heads) {
    if (offset > UINT32_MAX)
      return fail("pack payload is too large");
    add_glyph(d, static_cast<uint32_t>(offset));
    offset += d.bytes;
  }
  for (const auto& d : event_disks)
    add_event(d);
  const uint64_t file_size = f->size();
  if (file_size < offset)
    return fail("truncated payload");
  if (h.flags & 4) {
    if (file_size < offset + sizeof(UiFooter) || !f->seek(offset))
      return fail("truncated UI metadata");
    UiFooter footer{};
    if (!read(f, footer) || std::memcmp(footer.magic, "NZUI", 4) != 0)
      return fail("invalid UI metadata");
    const uint64_t credits_end = static_cast<uint64_t>(footer.credits_offset) + footer.credits_size;
    if (credits_end > strings_.size())
      return fail("invalid UI text reference");
    credits_ = {footer.credits_offset, footer.credits_size};
    for (size_t i = 0; i < ui_text_count; ++i) {
      const uint64_t end = static_cast<uint64_t>(footer.texts[i].offset) + footer.texts[i].size;
      if (end > strings_.size())
        return fail("invalid UI text reference");
      ui_texts_[i] = footer.texts[i];
    }
  }

  if (h.flags & 8) {
    if (!(h.flags & 4) || file_size < offset + sizeof(UiFooter) + sizeof(DdlcUiFooter))
      return fail("truncated DDLC UI metadata");
    DdlcUiFooter footer{};
    if (!read(f, footer) || std::memcmp(footer.magic, "DDUI", 4) || footer.version != 3)
      return fail("Regenerate the DDLC pack with the current converter");
    const auto valid = [&](uint16_t id) {
      const Image* image = image_metadata(id);
      return image && image->width == 320 && image->height == 240 && !image->alpha && !image->alias;
    };
    if (!valid(footer.title) || !valid(footer.notebook))
      return fail("invalid DDLC UI images");
    ddlc_title_id_ = footer.title;
    ddlc_notebook_id_ = footer.notebook;
    uint16_t word_count = 0;
    if (!read(f, word_count) || word_count != poem_words_.size())
      return fail("invalid DDLC poem word count");
    for (auto& word : poem_words_) {
      uint8_t length = 0;
      if (!read(f, length) || !length || length > 63 ||
          !bytes(f, word.points.data(), word.points.size()))
        return fail("invalid DDLC poem word record");
      for (const uint8_t score : word.points)
        if (score < 1 || score > 3) return fail("invalid DDLC poem score");
      word.text.resize(length);
      if (!bytes(f, word.text.data(), length)) return fail("truncated DDLC poem word");
      for (const unsigned char c : word.text)
        if (c < 32 || c > 126) return fail("invalid DDLC poem word text");
    }
    // Version 3: start event, glitch alphabet, embedded character files.
    if (!read(f, start_event_) || start_event_ >= h.events)
      return fail("invalid DDLC start event");
    uint16_t alphabet_count = 0;
    if (!read(f, alphabet_count) || alphabet_count > 512)
      return fail("invalid DDLC glitch alphabet");
    glitch_alphabet_.resize(alphabet_count);
    for (auto& cp : glitch_alphabet_)
      if (!read(f, cp)) return fail("truncated DDLC glitch alphabet");
    for (auto& blob : character_files_) {
      uint32_t size = 0;
      if (!read(f, size) || size > (1U << 20)) return fail("invalid DDLC character file");
      const uint32_t at = position(f);
      if (at == UINT32_MAX || !f->seek(f->tell() + size))
        return fail("truncated DDLC character file");
      blob = {at, size};
    }
  }
  if (has_poems() && !ddlc_title_id_)
    return fail("DDLC pack needs regeneration with the current converter");
  file_ = f;
  error_ = "";
  return true;
}

bool Pack::read_character_file(size_t index, std::vector<uint8_t>& out) const {
  if (index >= character_files_.size() || !file_ || !character_files_[index].size)
    return false;
  auto* f = static_cast<PackFile*>(file_);
  const auto& blob = character_files_[index];
  out.resize(blob.size);
  return f->seek(blob.offset) && bytes(f, out.data(), out.size());
}

bool Pack::image_pinned(uint16_t id) const {
  return std::find(pinned_images_.begin(), pinned_images_.begin() + pinned_image_count_, id) !=
         pinned_images_.begin() + pinned_image_count_;
}

void Pack::trim_image_cache(size_t incoming, uint16_t exclude) const {
  while (resident_image_bytes_ + incoming > image_cache_budget_) {
    Image* victim = nullptr;
    for (auto& candidate : images_) {
      if (!candidate.loaded || candidate.id == exclude || image_pinned(candidate.id))
        continue;
      if (!victim || candidate.last_used < victim->last_used)
        victim = &candidate;
    }
    if (!victim)
      break;
    unload_image(*victim);
  }
}

void Pack::unload_image(Image& image) const {
  if (!image.loaded)
    return;
  const size_t released = image.palette.size() * sizeof(uint16_t) +
                          image.direct_pixels.size() * sizeof(uint16_t) + image.pixels.size() +
                          image.coverage.size();
  std::vector<uint16_t>().swap(image.palette);
  std::vector<uint16_t>().swap(image.direct_pixels);
  std::vector<uint8_t>().swap(image.pixels);
  std::vector<uint8_t>().swap(image.coverage);
  image.loaded = false;
  resident_image_bytes_ -= std::min(resident_image_bytes_, released);
}

bool Pack::load_image(Image& image) const {
  if (!file_)
    return false;
  if (image.composite_base) {
    const size_t output_pixels = static_cast<size_t>(image.width) * image.height;
    const size_t output_bytes = output_pixels * (sizeof(uint16_t) + sizeof(uint8_t));
    trim_image_cache(output_bytes, image.id);
    const Image* base = this->image(image.composite_base);
    if (!base) {
      error_ = "cannot load sprite base";
      return false;
    }
    if (base->width != image.width || base->height != image.height ||
        base->pixels.size() != output_pixels) {
      error_ = "invalid sprite base dimensions";
      return false;
    }

    auto* f = static_cast<PackFile*>(file_);
    if (!f->seek(image.data_offset)) {
      error_ = "cannot seek image delta";
      return false;
    }
    std::vector<uint16_t> patch_palette(image.palette_entries);
    std::vector<uint8_t> patch_pixels(image.pixel_bytes), patch_coverage(image.pixel_bytes);
    if (!bytes(f, patch_palette.data(), patch_palette.size() * sizeof(uint16_t)) ||
        !bytes(f, patch_pixels.data(), patch_pixels.size()) ||
        !bytes(f, patch_coverage.data(), patch_coverage.size())) {
      error_ = "truncated image delta";
      return false;
    }

    image.direct_pixels.assign(output_pixels, 0);
    image.coverage.assign(output_pixels, 0);
    for (size_t at = 0; at < output_pixels; ++at) {
      const uint8_t index = base->pixels[at];
      if (index < base->palette.size())
        image.direct_pixels[at] = base->palette[index];
      image.coverage[at] = base->alpha ? base->coverage[at] : 255;
    }
    for (uint16_t y = 0; y < image.patch_height; ++y) {
      const int target_y = image.patch_y + y;
      if (target_y < 0 || target_y >= image.height)
        continue;
      for (uint16_t x = 0; x < image.patch_width; ++x) {
        const int target_x = image.patch_x + x;
        const size_t source = static_cast<size_t>(y) * image.patch_width + x;
        if (target_x < 0 || target_x >= image.width || source >= patch_pixels.size())
          continue;
        const size_t target = static_cast<size_t>(target_y) * image.width + target_x;
        const uint8_t index = patch_pixels[source];
        image.direct_pixels[target] =
            index < patch_palette.size() ? patch_palette[index] : uint16_t{};
        image.coverage[target] = patch_coverage[source];
      }
    }
    image.loaded = true;
    image.last_used = ++image_use_clock_;
    resident_image_bytes_ += output_bytes;
    trim_image_cache(0, image.id);
    return true;
  }
  const size_t total = static_cast<size_t>(image.palette_entries) * sizeof(uint16_t) +
                       static_cast<size_t>(image.pixel_bytes) * (image.alpha ? 2U : 1U);
  trim_image_cache(total, image.id);
  auto* f = static_cast<PackFile*>(file_);
  if (!f->seek(image.data_offset)) {
    error_ = "cannot seek image";
    return false;
  }
  image.palette.resize(image.palette_entries);
  image.pixels.resize(image.pixel_bytes);
  if (image.alpha)
    image.coverage.resize(image.pixel_bytes);
  if (!bytes(f, image.palette.data(), image.palette.size() * sizeof(uint16_t)) ||
      !bytes(f, image.pixels.data(), image.pixels.size()) ||
      !bytes(f, image.coverage.data(), image.coverage.size())) {
    std::vector<uint16_t>().swap(image.palette);
    std::vector<uint8_t>().swap(image.pixels);
    std::vector<uint8_t>().swap(image.coverage);
    error_ = "truncated pixels";
    return false;
  }
  image.loaded = true;
  image.last_used = ++image_use_clock_;
  resident_image_bytes_ += total;
  return true;
}

const Image* Pack::image(uint16_t id) const {
  auto* metadata = const_cast<Image*>(image_metadata(id));
  if (metadata && metadata->alias)
    return image(metadata->composite_base);
  if (!metadata || (!metadata->loaded && !load_image(*metadata)))
    return nullptr;
  metadata->last_used = ++image_use_clock_;
  return metadata;
}

const Image* Pack::image_metadata(uint16_t id) const {
  if (id && id <= images_.size() && images_[id - 1].id == id)
    return &images_[id - 1];
  const auto i = std::find_if(images_.begin(), images_.end(),
                              [id](const auto& value) { return value.id == id; });
  return i == images_.end() ? nullptr : &*i;
}

bool Pack::load_glyph(Glyph& glyph) const {
  if (!file_ || glyph.data_offset > static_cast<uint32_t>(LONG_MAX))
    return false;
  if (!glyph_block_count_ || glyph_block_used_ + glyph.bitmap_bytes > glyph_block_capacity_) {
    if (glyph_block_count_ >= glyph_blocks_.size()) {
      error_ = "glyph arena exhausted";
      return false;
    }
    auto& block = glyph_blocks_[glyph_block_count_++];
    block.resize(glyph_block_capacity_);
    glyph_block_used_ = 0;
    resident_glyph_bytes_ += block.size();
  }
  auto& block = glyph_blocks_[glyph_block_count_ - 1];
  uint8_t* const target = block.data() + glyph_block_used_;
  auto* f = static_cast<PackFile*>(file_);
  if (!f->seek(glyph.data_offset)) {
    error_ = "cannot seek glyph";
    return false;
  }
  if (!bytes(f, target, glyph.bitmap_bytes)) {
    error_ = "truncated glyph bitmap";
    return false;
  }
  glyph.pixels = target;
  glyph_block_used_ += glyph.bitmap_bytes;
  return true;
}

const Glyph* Pack::glyph(uint32_t cp, uint8_t size) const {
  const uint32_t key = cp | (static_cast<uint32_t>(size) << 24);
  auto i = std::lower_bound(glyphs_.begin(), glyphs_.end(), key,
                            [](const auto& g, uint32_t c) { return g.codepoint < c; });
  if (i == glyphs_.end() || i->codepoint != key || (!i->pixels && !load_glyph(*i)))
    return nullptr;
  return &*i;
}

void Pack::pin_images(const uint16_t* ids, size_t count) const {
  pinned_image_count_ = std::min(count, pinned_images_.size());
  std::copy_n(ids, pinned_image_count_, pinned_images_.begin());
}

void Pack::release_unpinned_images() const {
  for (auto& image : images_)
    if (image.loaded && !image_pinned(image.id))
      unload_image(image);
}

void Pack::set_image_cache_budget(size_t bytes_count) const {
  image_cache_budget_ = bytes_count;
  trim_image_cache(0, UINT16_MAX);
}

std::string_view Pack::text(TextRef r) const {
  return r.offset <= strings_.size() && r.size <= strings_.size() - r.offset
             ? std::string_view(strings_.data() + r.offset, r.size)
             : std::string_view{};
}

std::string opening_video_path(const Pack& pack) {
#ifdef _WIN32
  return std::string(pack.directory()) + (pack.chinese() ? "nZATO-zh.nvp" : "nZATO.nvp");
#else
  return std::string(pack.directory()) + (pack.chinese() ? "nZATO-zh.nvp.tns" : "nZATO.nvp.tns");
#endif
}
} // namespace nzato
