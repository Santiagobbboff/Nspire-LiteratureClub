#include "nzato/player.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace nzato {
namespace {
#pragma pack(push, 1)
struct SaveChoice {
  uint32_t menu_event;
  uint8_t option;
};
struct SaveHeadV2 {
  char magic[4];
  uint32_t cursor;
  int16_t story_state[8];
  char player_name[17];
  uint8_t choice_count;
};
struct SaveHead {
  char magic[4];
  uint32_t cursor;
  int16_t story_state[story_state_slots];
  char player_name[17];
  uint8_t choice_count;
};
#pragma pack(pop)
struct PersistentHead {
  char magic[4];
  uint64_t gallery;
};

void append_text(char*& out, char* end, const char* text) {
  while (*text && out < end)
    *out++ = *text++;
}

void append_text(char*& out, char* end, std::string_view text) {
  for (const char value : text) {
    if (out >= end)
      break;
    *out++ = value;
  }
}

void append_number(char*& out, char* end, size_t value) {
  char digits[3 * sizeof(size_t)]{};
  size_t count = 0;
  do {
    digits[count++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value && count < sizeof(digits));
  while (count && out < end)
    *out++ = digits[--count];
}

void save_name(char* output, size_t size, bool chinese, bool ddlc, int slot,
               std::string_view directory = {}) {
  if (!size)
    return;
  char* out = output;
  char* const end = output + size - 1;
  append_text(out, end, directory);
  append_text(out, end, ddlc ? "ddlc-" : (chinese ? "nZATO-zh-" : "nZATO-"));
  append_number(out, end, static_cast<size_t>(std::max(0, slot)));
#ifdef _WIN32
  append_text(out, end, ".sav");
#else
  append_text(out, end, ".sav.tns");
#endif
  *out = '\0';
}

void persistent_name(char* output, size_t size, bool chinese, bool ddlc,
                     std::string_view directory) {
  if (!size)
    return;
  char* out = output;
  char* const end = output + size - 1;
  append_text(out, end, directory);
#ifdef _WIN32
  append_text(out, end, ddlc ? "ddlc.sav" : (chinese ? "nZATO-zh.sav" : "nZATO.sav"));
#else
  append_text(out, end, ddlc ? "ddlc.sav.tns" : (chinese ? "nZATO-zh.sav.tns" : "nZATO.sav.tns"));
#endif
  *out = '\0';
}
} // namespace

void Player::load_persistent() {
  char name[512]{};
  FILE* file = nullptr;
  persistent_name(name, sizeof(name), pack_.chinese(), ddlc_ui_, pack_.directory());
#ifdef _MSC_VER
  fopen_s(&file, name, "rb");
#else
  file = std::fopen(name, "rb");
#endif
  if (!file)
    return;
  PersistentHead head{};
  if (std::fread(&head, sizeof(head), 1, file) == 1 &&
      std::memcmp(head.magic, ddlc_ui_ ? "DDGP" : "NZGP", 4) == 0) {
    gallery_unlocks_ = head.gallery;
    uint8_t mode = 0;
    if (ddlc_ui_ && std::fread(&mode, 1, 1, file) == 1 && mode <= 2)
      background_mode_ = static_cast<BackgroundMode>(mode);
    uint8_t seen = 0;
    if (ddlc_ui_ && std::fread(&seen, 1, 1, file) == 1 && seen == 1)
      notice_seen_ = true;
  }
  std::fclose(file);
}

void Player::save_persistent() const {
  char name[512]{};
  persistent_name(name, sizeof(name), pack_.chinese(), ddlc_ui_, pack_.directory());
  FILE* file = nullptr;
#ifdef _MSC_VER
  fopen_s(&file, name, "wb");
#else
  file = std::fopen(name, "wb");
#endif
  if (!file)
    return;
  PersistentHead head{};
  std::memcpy(head.magic, ddlc_ui_ ? "DDGP" : "NZGP", 4);
  head.gallery = gallery_unlocks_;
  std::fwrite(&head, sizeof(head), 1, file);
  if (ddlc_ui_) {
    const uint8_t mode = static_cast<uint8_t>(background_mode_);
    std::fwrite(&mode, 1, 1, file);
    const uint8_t seen = notice_seen_ ? 1 : 0;
    std::fwrite(&seen, 1, 1, file);
  }
  std::fclose(file);
}

void Player::load_save_index() {
  if (save_index_loaded_)
    return;
  save_index_loaded_ = true;
  for (int slot = 0; slot < static_cast<int>(saves_.size()); slot++) {
    char name[512];
    FILE* file = nullptr;
    save_name(name, sizeof(name), pack_.chinese(), ddlc_ui_, slot, pack_.directory());
#ifdef _MSC_VER
    fopen_s(&file, name, "rb");
#else
    file = std::fopen(name, "rb");
#endif
    if (!file)
      continue;
    auto& save = saves_[slot];
    char magic[4]{};
    SaveHead header{};
    bool header_ok = std::fread(magic, sizeof(magic), 1, file) == 1;
    uint32_t loaded_seed = 0;
    const bool randomized = ddlc_ui_ && !std::memcmp(magic, "DDS", 4);
    if (header_ok && (randomized || (!pack_.has_poems() && !std::memcmp(magic, "NZS3", 4)))) {
      std::memcpy(header.magic, magic, sizeof(magic));
      header_ok = std::fread(reinterpret_cast<char*>(&header) + sizeof(magic),
                             sizeof(header) - sizeof(magic), 1, file) == 1;
    } else if (header_ok && !pack_.has_poems() && !std::memcmp(magic, "NZS2", 4)) {
      SaveHeadV2 old_header{};
      std::memcpy(old_header.magic, magic, sizeof(magic));
      header_ok = std::fread(reinterpret_cast<char*>(&old_header) + sizeof(magic),
                             sizeof(old_header) - sizeof(magic), 1, file) == 1;
      if (header_ok) {
        std::memcpy(header.magic, old_header.magic, sizeof(magic));
        header.cursor = old_header.cursor;
        std::fill(std::begin(header.story_state), std::end(header.story_state), -1);
        std::copy(std::begin(old_header.story_state), std::end(old_header.story_state),
                  std::begin(header.story_state));
        std::memcpy(header.player_name, old_header.player_name, sizeof(header.player_name));
        header.choice_count = old_header.choice_count;
      }
    } else {
      header_ok = false;
    }
    if (header_ok && randomized) header_ok = std::fread(&loaded_seed, sizeof(loaded_seed), 1, file) == 1;
    const bool compatible = header_ok && header.choice_count <= save.choices.size();
    std::array<SaveChoice, story_choice_capacity> choices{};
    const bool choices_ok = compatible &&
                            std::fread(choices.data(), sizeof(SaveChoice), header.choice_count,
                                       file) == header.choice_count;
    save.thumbnail.resize(80 * 60);
    save.valid = choices_ok &&
                 std::fread(save.thumbnail.data(), 2, save.thumbnail.size(), file) ==
                     save.thumbnail.size();
    save.cursor = save.valid ? header.cursor : 0;
    if (save.valid) {
      std::copy(std::begin(header.story_state), std::end(header.story_state),
                save.story_state.begin());
      save.poem_seed = loaded_seed;
      save.player_name = header.player_name;
      save.choice_count = header.choice_count;
      for (size_t i = 0; i < save.choice_count; ++i)
        save.choices[i] = {choices[i].menu_event, choices[i].option};
    }
    std::fclose(file);
  }
}

bool Player::save_slot(int slot) {
  if (slot < 0 || slot >= static_cast<int>(saves_.size()) || title_active_)
    return false;
  auto& save = saves_[slot];
  save.thumbnail.resize(80 * 60);
  if (game_thumbnail_.size() == 80 * 60)
    std::copy(game_thumbnail_.begin(), game_thumbnail_.end(), save.thumbnail.begin());
  char name[512];
  save_name(name, sizeof(name), pack_.chinese(), ddlc_ui_, slot, pack_.directory());
  FILE* file = nullptr;
#ifdef _MSC_VER
  fopen_s(&file, name, "wb");
#else
  file = std::fopen(name, "wb");
#endif
  if (!file)
    return false;
  SaveHead header{};
  // DDLC's own identifier is "DDS" plus a terminating zero byte. Its file
  // names and header are independent of nZATO; no legacy saves are imported.
  std::memcpy(header.magic, ddlc_ui_ ? "DDS" : "NZS3", 4);
  header.cursor = static_cast<uint32_t>(cursor_);
  std::copy(story_state_.begin(), story_state_.end(), std::begin(header.story_state));
  std::strncpy(header.player_name, player_name_.c_str(), sizeof(header.player_name) - 1);
  header.choice_count = choice_history_count_;
  std::array<SaveChoice, story_choice_capacity> choices{};
  for (size_t i = 0; i < header.choice_count; ++i)
    choices[i] = {choice_history_[i].menu_event, choice_history_[i].option};
  const bool ok = std::fwrite(&header, sizeof(header), 1, file) == 1 &&
                  (!ddlc_ui_ || std::fwrite(&poem_seed_, sizeof(poem_seed_), 1, file) == 1) &&
                  std::fwrite(choices.data(), sizeof(SaveChoice), header.choice_count, file) ==
                      header.choice_count &&
                  std::fwrite(save.thumbnail.data(), 2, save.thumbnail.size(), file) ==
                      save.thumbnail.size();
  std::fclose(file);
  if (ok) {
    save.valid = true;
    save.cursor = cursor_;
    save.story_state = story_state_;
    save.choices = choice_history_;
    save.choice_count = choice_history_count_;
    save.poem_seed = poem_seed_;
    save.player_name = player_name_;
  }
  if (!save_open_ && !load_open_)
    release_save_cache();
  return ok;
}

void Player::start_new_game() {
  cutscene_locked_ = false;
  credits_roll_active_ = false;
  credits_roll_ms_ = 0;
  static uint32_t game_serial = 0;
  poem_seed_ = static_cast<uint32_t>(std::time(nullptr)) ^ static_cast<uint32_t>(std::clock()) ^
               (++game_serial * 0x9e3779b9u);
  if (!poem_seed_) poem_seed_ = 1;
  word_board_base_ = 0xffffffffu;
  settings_open_ = false;
  poem_open_ = false;
  poem_paper_ = nullptr;
  poem_text_.clear();
  poem_scroll_px_ = poem_scroll_max_ = 0;
  release_navigation_caches();
  release_title_caches();
  video_.close();
  ended_ = gallery_open_ = gallery_view_ = history_open_ = save_open_ = load_open_ = info_open_ =
      confirm_open_ = choice_open_ = waiting_ = inline_wait_ = manual_ui_hidden_ = pause_ui_hidden_ = centered_ =
          centered_fading_out_ = centered_no_wrap_ = false;
  story_state_.fill(-1);
  choice_history_.fill({});
  choice_history_count_ = 0;
  choice_first_ = 0;
  choice_count_ = 0;
  choice_selection_ = 0;
  background_ = old_background_ = previous_background_ = face_ = nullptr;
  background_resource_ = 0;
  background_animation_left_ = 0;
  background_y_ = old_background_y_ = 0;
  layers_ = {};
  old_layers_ = {};
  snow_images_[0] = snow_images_[1] = nullptr;
  snow_up_[0] = snow_up_[1] = false;
  stage_transition_left_ = stage_transition_total_ = window_transition_left_ =
      centered_transition_left_ = overlay_transition_left_ = 0;
  window_visible_ = false;
  window_opacity_ = window_target_opacity_ = 0;
  overlay_text_.clear();
  overlay_positioned_ = false;
  overlay_x_ = overlay_y_ = 0;
  overlay_opacity_ = 0;
  speaker_.clear();
  text_.clear();
  nvl_mode_ = false;
  nvl_text_.clear();
  history_.clear();
  ++history_revision_;
  reveal_bytes_ = 0;
  file_return_title_ = false;
  cursor_ = 0;
  while (cursor_ < pack_.events().size() && pack_.events()[cursor_].op != Op::title)
    cursor_++;
  if (cursor_ < pack_.events().size())
    cursor_++;
  title_active_ = video_active_ = false;
  advance_automatic();
  particle_accumulator_ = 0;
  particle_rng_ = 0x7a7a25u;
  for (size_t i = 0; i < particles_.size(); i++) {
    particle_rng_ ^= particle_rng_ << 13;
    particle_rng_ ^= particle_rng_ >> 17;
    particle_rng_ ^= particle_rng_ << 5;
    particles_[i] = {static_cast<int>(particle_rng_ % 320),
                     -8 - static_cast<int>((particle_rng_ >> 9) % 300),
                     1 + static_cast<int>((particle_rng_ >> 18) & 1),
                     static_cast<int>((particle_rng_ >> 19) & 1)};
  }
}

bool Player::load_slot(int slot) {
  if (slot < 0 || slot >= static_cast<int>(saves_.size()) || !saves_[slot].valid)
    return false;
  return restore_to(std::min(saves_[slot].cursor, pack_.events().size()), slot);
}

bool Player::debug_jump(int direction) {
  if (!direction || video_active_ || title_active_ || ended_ || navigation_open())
    return false;
  constexpr int statements_per_jump = 50;
  int remaining = statements_per_jump;
  size_t target = cursor_;
  if (direction > 0) {
    for (size_t index = cursor_; index < pack_.events().size(); ++index) {
      const auto& event = pack_.events()[index];
      if (event.op == Op::say && !(event.flags & 0x80)) {
        target = index + 1;
        if (--remaining == 0)
          break;
      }
    }
  } else {
    for (size_t index = cursor_; index-- > 0;) {
      const auto& event = pack_.events()[index];
      if (event.op == Op::say && !(event.flags & 0x80) && --remaining == 0) {
        target = index + 1;
        break;
      }
      target = index;
    }
  }
  if (target == cursor_)
    return false;
  if (direction > 0) {
    // The current state is already the correct starting point for a forward
    // jump. Replaying from event zero after every press made repeated 9-key
    // jumps O(n^2) and thrashed the image cache on 32 MiB calculators.
    finish_transitions();
    replaying_ = true;
    while (cursor_ < target) {
      const auto& event = pack_.events()[cursor_++];
      if (event.op == Op::title || event.op == Op::pause)
        continue;
      apply(event);
      finish_transitions();
      reveal_bytes_ = text_.size();
      waiting_ = inline_wait_ = false;
    }
    replaying_ = false;
    materialize_active_images();
    shake_left_ = 0;
    shake_horizontal_ = false;
    pause_ui_hidden_ = false;
    screen_fade_left_ = 240;
    return true;
  }
  return restore_to(target);
}

bool Player::restore_to(size_t target, int save_slot) {
  target = std::min(target, pack_.events().size());
  const auto saved_story_state = save_slot >= 0 ? saves_[save_slot].story_state : story_state_;
  const auto saved_choices = save_slot >= 0 ? saves_[save_slot].choices : choice_history_;
  const uint8_t saved_choice_count =
      save_slot >= 0 ? saves_[save_slot].choice_count : choice_history_count_;
  const std::string saved_player_name =
      save_slot >= 0 ? saves_[save_slot].player_name : player_name_;
  const uint32_t saved_seed = save_slot >= 0 ? saves_[save_slot].poem_seed : poem_seed_;
  start_new_game();
  poem_seed_ = saved_seed;
  word_board_base_ = 0xffffffffu;
  player_name_ = saved_player_name;
  choice_history_ = saved_choices;
  choice_history_count_ = std::min<uint8_t>(saved_choice_count, choice_history_.size());
  story_state_.fill(-1);
  background_ = nullptr;
  background_resource_ = 0;
  background_animation_left_ = 0;
  background_y_ = 0;
  layers_ = {};
  history_.clear();
  ++history_revision_;
  cursor_ = 0;
  while (cursor_ < pack_.events().size() && pack_.events()[cursor_].op != Op::title)
    cursor_++;
  if (cursor_ < pack_.events().size())
    cursor_++;
  replaying_ = true;
  size_t replay_budget = pack_.events().size() * 4;
  while (cursor_ != target) {
    if (cursor_ >= pack_.events().size() || replay_budget-- == 0) {
      replaying_ = false;
      return false;
    }
    const auto& event = pack_.events()[cursor_++];
    if (event.op == Op::title || event.op == Op::pause)
      continue;
    apply(event);
    finish_transitions();
    reveal_bytes_ = text_.size();
    waiting_ = inline_wait_ = false;
  }
  replaying_ = false;
  if (save_slot >= 0)
    story_state_ = saved_story_state;
  // Replayed events only need stable image metadata. Resolve aliases and load
  // the final active scene once, after replay, instead of seeking/decompressing
  // every image the story has passed through.
  materialize_active_images();
  // apply() sees hpunch/vpunch events while reconstructing a save.
  // Their transient animation state must not leak into the post-load fade.
  shake_left_ = 0;
  shake_horizontal_ = false;
  title_active_ = video_active_ = history_open_ = save_open_ = load_open_ = false;
  ended_ = false;
  pause_ui_hidden_ = false;
  screen_fade_left_ = 240;
  return true;
}
} // namespace nzato
