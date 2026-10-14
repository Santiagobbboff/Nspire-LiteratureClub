#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nzato {
inline constexpr size_t story_state_slots = 16;
inline constexpr size_t poem_word_count = 228;
struct PoemWord {
  std::string text;
  std::array<uint8_t, 3> points{};
};
struct Image {
  uint16_t id{}, width{}, height{};
  uint16_t animation_next{}, animation_ms{};
  uint16_t composite_base{};
  int16_t patch_x{}, patch_y{};
  uint16_t patch_width{}, patch_height{};
  bool alpha{}, particle{}, background{}, base_layer{}, screen_layer{}, full_screen{}, alias{};
  std::vector<uint16_t> palette;
  std::vector<uint16_t> direct_pixels;
  std::vector<uint8_t> pixels, coverage;
  uint32_t data_offset{}, pixel_bytes{}, last_used{};
  uint16_t palette_entries{};
  bool loaded{};
};
struct Glyph {
  uint32_t codepoint{};
  uint8_t width{}, height{}, advance{};
  int8_t bearing_x{}; // Signed bitmap origin stored in the glyph header spare byte.
  const uint8_t* pixels{};
  uint32_t data_offset{};
  uint16_t bitmap_bytes{};
};
enum class Op : uint8_t {
  scene = 1,
  show = 2,
  hide = 3,
  say = 4,
  end = 5,
  title = 6,
  pause = 7,
  window = 8,
  overlay = 9,
  nvl = 10,
  transform = 11,
  persistent = 12,
  choice = 13,
  choice_option = 14,
  jump = 15,
  set_state = 16,
  if_state = 17,
  add_state = 18,
  if_state_gt = 19,
  move = 20,
  poem = 21,
  // Act 2 additions (DDUI footer version 3).
  system = 22,          // resource: 1 delete saves, 2 delete character x, 3 restore characters (mask x),
                        //           4 slot x <- character file y exists
  persistent_load = 23, // story slot `resource` <- persistent key `x`
  persistent_store = 24,// persistent key `x` <- story slot `resource`
  effect = 25,          // resource: 0 stop, 1 tear, 2 invert; x duration ms (0 = until stop)
  rand_state = 26       // story slot `resource` <- random [0, x), excluding bits set in slot `y`
};
// Persistent key indices shared with the pack compiler.
enum class PersistentKey : uint8_t {
  playthrough = 0,
  yuri_kill = 1,
  autoload = 2,
  special_poem_0 = 3,
  special_poem_1 = 4,
  special_poem_2 = 5,
  seen_eyes = 6,
  seen_sticker = 7,
  seen_ghost_menu = 8,
  deleted_saves = 9,
  seen_colors_poem = 10,
  monika_reload = 11,
  current_monikatopic = 12,
  monika_kill = 13,
  tried_skip = 14,
  monika_back = 15,
  topics_0 = 16, // four 16-bit masks of used Monika topics (1-56)
  topics_1 = 17,
  topics_2 = 18,
  topics_3 = 19,
  count = 32
};
inline constexpr size_t persistent_value_count = static_cast<size_t>(PersistentKey::count);
inline constexpr size_t character_file_count = 4;
inline constexpr const char* character_file_names[character_file_count] = {"monika", "natsuki",
                                                                            "sayori", "yuri"};
struct TextRef {
  uint32_t offset{}, size{};
};
enum class UiText : uint8_t {
  start,
  log,
  save,
  load,
  gallery,
  title,
  credits,
  controls,
  quit,
  return_to_game,
  yes,
  no,
  no_signal,
  page_pattern,
  auto_saves,
  quick_saves,
  confirm_quit,
  confirm_main_menu,
  confirm_overwrite,
  confirm_load,
  confirm_quick_save,
  count
};
inline constexpr size_t ui_text_count = static_cast<size_t>(UiText::count);
struct Event {
  Op op{};
  uint8_t flags{};
  uint16_t resource{};
  int16_t x{}, y{};
  TextRef speaker, text;
};
// Branch targets of jump/if_state/if_state_gt/choice_option are 32-bit event
// indices stored in the otherwise unused speaker reference.
inline size_t branch_target(const Event& event) {
  return static_cast<size_t>(event.speaker.offset);
}

class Pack {
public:
  bool has_poems() const {
    for (const auto& event : events_)
      if (event.op == Op::poem) return true;
    return false;
  }
  Pack() = default;
  ~Pack();
  Pack(const Pack&) = delete;
  Pack& operator=(const Pack&) = delete;
  bool load(const char* path);
#ifdef _WIN32
  bool load(const wchar_t* path);
#endif
  uint16_t ddlc_title_id() const { return ddlc_title_id_; }
  uint16_t ddlc_notebook_id() const { return ddlc_notebook_id_; }
  // First story event of a new game (the Act dispatch label), or 0 for the
  // legacy "first event after the title op" behaviour.
  uint32_t start_event() const { return start_event_; }
  const std::vector<uint32_t>& glitch_alphabet() const { return glitch_alphabet_; }
  // Original character files embedded by the converter; written beside the
  // pack as characters/<name>.chr.tns. Empty when the pack has none.
  bool read_character_file(size_t index, std::vector<uint8_t>& out) const;
  bool has_character_files() const { return character_files_[0].size != 0; }
  const std::array<PoemWord, poem_word_count>& poem_words() const { return poem_words_; }
  const Image* image(uint16_t id) const;
  const Image* image_metadata(uint16_t id) const;
  const Glyph* glyph(uint32_t cp, uint8_t size = 11) const;
  void pin_images(const uint16_t* ids, size_t count) const;
  void release_unpinned_images() const;
  void set_image_cache_budget(size_t bytes) const;
  size_t resident_image_bytes() const {
    return resident_image_bytes_;
  }
  size_t resident_glyph_bytes() const {
    return resident_glyph_bytes_;
  }
  std::string_view text(TextRef ref) const;
  std::string_view credits() const {
    return text(credits_);
  }
  std::string_view ui(UiText value) const {
    return text(ui_texts_[static_cast<size_t>(value)]);
  }
  const std::vector<Event>& events() const {
    return events_;
  }
  std::string_view directory() const {
    return directory_;
  }
  bool chinese() const {
    return chinese_;
  }
  bool loose_translation() const {
    return loose_translation_;
  }
  const char* error() const {
    return error_;
  }

private:
  bool load_file(void* file);
  void close_file();
  bool load_image(Image& image) const;
  void unload_image(Image& image) const;
  bool load_glyph(Glyph& glyph) const;
  void trim_image_cache(size_t incoming, uint16_t exclude) const;
  bool image_pinned(uint16_t id) const;
  mutable std::vector<Image> images_;
  mutable std::vector<Glyph> glyphs_;
  // Glyphs live for the lifetime of a pack. Allocating every bitmap as its own
  // vector fragmented the small newlib heap after thousands of Chinese glyphs
  // had been encountered during skipping. A few monotonic blocks keep their
  // addresses stable and turn that workload into only a handful of allocations.
  mutable std::array<std::vector<uint8_t>, 4> glyph_blocks_{};
  mutable size_t glyph_block_capacity_{}, glyph_block_count_{}, glyph_block_used_{};
  std::vector<Event> events_;
  std::vector<char> strings_;
  TextRef credits_{};
  std::array<TextRef, ui_text_count> ui_texts_{};
  void* file_{};
  std::string directory_;
  uint16_t ddlc_title_id_{}, ddlc_notebook_id_{};
  uint32_t start_event_{};
  std::vector<uint32_t> glitch_alphabet_;
  std::array<TextRef, character_file_count> character_files_{};
  std::array<PoemWord, poem_word_count> poem_words_{};
  bool chinese_{};
  bool loose_translation_{};
  mutable const char* error_ = "";
  mutable size_t resident_image_bytes_{};
  mutable size_t resident_glyph_bytes_{};
  mutable size_t image_cache_budget_{2U * 1024U * 1024U};
  mutable uint32_t image_use_clock_{};
  mutable std::array<uint16_t, 40> pinned_images_{};
  mutable size_t pinned_image_count_{};
};

// The opening movie is an optional sibling file selected by the runtime. It
// is not a field or resource in the content pack.
std::string opening_video_path(const Pack& pack);
} // namespace nzato
