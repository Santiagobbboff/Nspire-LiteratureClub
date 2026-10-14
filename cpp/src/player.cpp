#include "nzato/player.hpp"
#include "nzato/raster.hpp"
#include "nzato/ui_metrics.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nzato {
namespace {
constexpr uint8_t transition_mask = 0x03;
constexpr uint8_t centered_no_wrap_flag = 0x10;
constexpr uint8_t extend_flag = 0x80;
constexpr uint8_t nvl_say_flag = 0x40;
constexpr uint32_t transition_unit_ms = 50;

uint32_t ease1024(uint32_t elapsed, uint32_t total) {
  if (!total)
    return 1024;
  const uint32_t t = std::min(elapsed, total) * 1024U / total;
  return t * t * (3072U - 2U * t) >> 20U;
}

bool reveal_next(std::string_view text, size_t& at, uint32_t& cps, uint32_t& wait, bool& click_wait,
                 bool& auto_advance) {
  while (at < text.size() && text[at] == '{') {
    const size_t end = text.find('}', at + 1);
    if (end == std::string_view::npos)
      break;
    const auto tag = text.substr(at + 1, end - at - 1);
    at = end + 1;
    if (tag == "nw") {
      auto_advance = true;
      continue;
    }
    if (tag == "w" || tag == "p") {
      click_wait = true;
      return false;
    }
    if (tag.rfind("w=", 0) == 0 || tag.rfind("p=", 0) == 0) {
      double seconds = 0;
      for (char c : tag.substr(2)) {
        if (c >= '0' && c <= '9')
          seconds = seconds * 10 + (c - '0');
        else if (c == '.') {
          const auto dot = tag.find('.');
          double place = .1;
          for (char d : tag.substr(dot + 1)) {
            if (d < '0' || d > '9')
              break;
            seconds += (d - '0') * place;
            place *= .1;
          }
          break;
        }
      }
      wait = std::max<uint32_t>(1, static_cast<uint32_t>(seconds * 1000));
      return false;
    }
    if (tag.rfind("cps=", 0) == 0) {
      uint32_t value = 0;
      for (char c : tag.substr(4))
        if (c >= '0' && c <= '9')
          value = value * 10 + (c - '0');
      cps = value ? value : 100000;
      continue;
    }
    if (tag == "/cps")
      cps = 0;
  }
  if (at < text.size())
    utf8_next(text, at);
  return true;
}
} // namespace

const uint16_t* Player::presentation_framebuffer() const {
  return video_active_ && !screen_fade_left_ ? video_.framebuffer() : nullptr;
}

void Player::capture_video_frame() {
  const uint16_t* frame = video_.framebuffer();
  if (!frame)
    return;
  std::copy_n(frame, static_cast<size_t>(screen_w) * screen_h, last_frame_.begin());
  last_frame_valid_ = true;
  rendered_screen_fade_left_ = 0;
}

Player::Player(const Pack& p, bool play_intro, uint16_t* video_framebuffer)
    : pack_(p), ddlc_ui_(p.has_poems()), play_intro_(play_intro && !p.has_poems()), video_framebuffer_(video_framebuffer),
      last_frame_(screen_w * screen_h), stage_cache_(screen_w * screen_h),
      movement_base_cache_(screen_w * screen_h), transition_old_cache_(screen_w * screen_h),
      transition_new_cache_(screen_w * screen_h), dialogue_ui_cache_(screen_w * screen_h),
      game_thumbnail_(80 * 60) {
  reset();
  load_persistent();
  if (ddlc_ui_ && !notice_seen_) startup_phase_ = StartupPhase::notice;
  // DDLC's persistent.autoload: Act 2's weekend and the Act 3 handoff resume
  // the story on launch instead of showing the main menu.
  if (ddlc_ui_ && notice_seen_ && pack_.start_event() &&
      persistent_value(PersistentKey::autoload) != 0) {
    start_new_game();
    screen_fade_left_ = 240;
  }
}

uint32_t Player::transition_ms(uint8_t flags) {
  if (!(flags & transition_mask))
    return 0;
  const uint32_t units = flags >> 2;
  return units ? units * transition_unit_ms : 500;
}

void Player::reset() {
  startup_phase_ = StartupPhase::none;
  startup_elapsed_ms_ = 0;
  cutscene_locked_ = false;
  credits_roll_active_ = false;
  credits_roll_ms_ = 0;
  last_frame_valid_ = framebuffer_valid_ = false;
  rendered_screen_fade_left_ = 0;
  stage_cache_valid_ = false;
  movement_base_cache_valid_ = false;
  transition_cache_valid_ = false;
  menu_chrome_cache_valid_ = title_base_cache_valid_ = false;
  dialogue_ui_cache_valid_ = false;
  dialogue_ui_cache_signature_ = 0;
  release_navigation_caches();
  release_title_caches();
  language_ = pack_.chinese() ? Language::chinese : Language::english;
  cursor_ = 0;
  story_state_.fill(-1);
  effect_kind_ = 0;
  effect_left_ = effect_elapsed_ = 0;
  ended_ = false;
  background_ = nullptr;
  background_resource_ = 0;
  background_animation_left_ = 0;
  old_background_ = nullptr;
  background_y_ = old_background_y_ = 0;
  previous_background_ = nullptr;
  background_opacity_ = 255;
  background_transition_left_ = 0;
  face_ = nullptr;
  poem_paper_ = nullptr;
  poem_open_ = false;
  poem_text_.clear();
  poem_scroll_px_ = poem_scroll_max_ = 0;
  video_frame_ = title_background_ = title_menu_overlay_ = history_background_ = history_overlay_ =
      nullptr;
  video_left_ = 0;
  video_active_ = title_active_ = gallery_open_ = gallery_view_ = history_open_ = save_open_ =
      load_open_ = info_open_ = confirm_open_ = choice_open_ = waiting_ = inline_wait_ =
          quit_requested_ = false;
  simple_title_ = name_entry_open_ = false;
  settings_open_ = false;
  settings_selection_ = 0;
  window_visible_ = false;
  manual_ui_hidden_ = pause_ui_hidden_ = centered_ = centered_fading_out_ = centered_no_wrap_ =
      transition_gate_ = replaying_ = false;
  centered_fade_ = true;
  nvl_mode_ = false;
  nvl_text_.clear();
  title_selection_ = history_selection_ = history_scroll_px_ = info_scroll_px_ = info_page_ =
      file_selection_ = file_region_ = confirm_selection_ = confirm_action_ = 0;
  history_focus_ = 1;
  file_page_ = 0;
  pending_slot_ = -1;
  file_return_title_ = false;
  choice_first_ = 0;
  choice_count_ = 0;
  choice_selection_ = 0;
  gallery_selection_ = gallery_variant_ = 0;
  window_opacity_ = window_from_opacity_ = window_target_opacity_ = 0;
  window_transition_left_ = window_transition_total_ = 0;
  overlay_text_.clear();
  overlay_positioned_ = false;
  overlay_x_ = overlay_y_ = 0;
  overlay_opacity_ = overlay_target_opacity_ = 0;
  overlay_transition_left_ = overlay_transition_total_ = 0;
  centered_opacity_ = 255;
  centered_transition_left_ = particle_accumulator_ = animation_ms_ = 0;
  particle_rng_ = 0x7a7a25u;
  const auto random_particle = [this]() {
    particle_rng_ ^= particle_rng_ << 13;
    particle_rng_ ^= particle_rng_ >> 17;
    particle_rng_ ^= particle_rng_ << 5;
    return particle_rng_;
  };
  for (size_t i = 0; i < particles_.size(); i++) {
    const uint32_t random = random_particle();
    particles_[i] = {static_cast<int>(random % 320), -8 - static_cast<int>((random >> 9) % 300),
                     1 + static_cast<int>((random >> 18) & 1),
                     static_cast<int>((random >> 19) & 1)};
  }
  layers_ = {};
  snow_up_[0] = snow_up_[1] = false;
  old_layers_ = {};
  stage_transition_left_ = stage_transition_total_ = 0;
  stage_transition_kind_ = 0;
  stage_transition_direction_ = 0;
  shake_left_ = 0;
  speaker_ = {};
  text_ = {};
  reveal_bytes_ = 0;
  dialogue_scroll_px_ = dialogue_scroll_max_ = 0;
  dialogue_scroll_follow_ = true;
  nvl_scroll_px_ = nvl_scroll_max_ = 0;
  nvl_scroll_follow_ = true;
  reveal_accumulator_ = 0;
  inline_wait_left_ = 0;
  active_cps_ = 0;
  auto_advance_text_ = auto_play_ = skip_indicator_ = false;
  auto_play_left_ = 0;
  history_.clear();
  history_.reserve(64);
  history_revision_ = 0;
  history_layout_revision_ = UINT32_MAX;
  history_content_height_ = credits_content_height_ = 0;
  save_index_loaded_ = false;
  if (play_intro_) {
    // Avoid cold flash reads during the two 100 ms post-OP cards and the
    // first title frame. These resources remain comfortably inside the image
    // cache even on 32 MiB models.
    std::array<uint16_t, 16> intro_ids{};
    size_t intro_count = 0;
    for (const auto& event : pack_.events()) {
      if ((event.op == Op::scene || event.op == Op::show) && event.resource &&
          intro_count < intro_ids.size())
        intro_ids[intro_count++] = event.resource;
      if (event.op == Op::title) {
        const uint16_t title_ids[] = {event.resource,
                                      static_cast<uint16_t>(event.y),
                                      static_cast<uint16_t>(event.y + 1),
                                      static_cast<uint16_t>(event.y + 2),
                                      static_cast<uint16_t>(event.y + 3),
                                      static_cast<uint16_t>(event.speaker.size)};
        for (const uint16_t id : title_ids)
          if (id && intro_count < intro_ids.size())
            intro_ids[intro_count++] = id;
        break;
      }
    }
    pack_.pin_images(intro_ids.data(), intro_count);
    for (size_t index = 0; index < intro_count; ++index)
      pack_.image(intro_ids[index]);
    const auto path = opening_video_path(pack_);
    video_active_ = video_.open(path.c_str(), video_framebuffer_);
    if (video_active_) {
      video_left_ = video_.frame_ms();
      return;
    }
  }
  advance();
}

void Player::toggle_language() {
  // A pack is deliberately monolingual; switching means launching the other NZP.
}
bool Player::resource_visible(uint16_t id) const {
  for (const auto& layer : layers_)
    if (layer.image && layer.resource == id)
      return true;
  return background_ && background_resource_ == id;
}

bool Player::resource_shaking(uint16_t id) const {
  for (const auto& layer : layers_)
    if (layer.image && layer.resource == id)
      return layer.shaking;
  return false;
}

void Player::pin_active_images(const uint16_t* extra, size_t extra_count) const {
  std::array<uint16_t, 40> ids{};
  size_t count = 0;
  auto add = [&](const Image* image) {
    if (image && count < ids.size())
      ids[count++] = image->id;
  };
  if (pack_.ddlc_title_id() && count < ids.size()) ids[count++] = pack_.ddlc_title_id();
  if (pack_.ddlc_notebook_id() && count < ids.size()) ids[count++] = pack_.ddlc_notebook_id();
  add(background_);
  add(old_background_);
  add(previous_background_);
  add(face_);
  add(poem_paper_);
  add(title_background_);
  add(title_menu_overlay_);
  add(history_background_);
  add(history_overlay_);
  for (const auto* image : snow_images_)
    add(image);
  for (const auto* image : title_snow_images_)
    add(image);
  for (const auto& layer : layers_)
    add(layer.image);
  for (const auto& layer : old_layers_)
    add(layer.image);
  for (size_t i = 0; i < extra_count && count < ids.size(); ++i)
    ids[count++] = extra[i];
  pack_.pin_images(ids.data(), count);
  // advance() can apply several visual events before the next render.  A
  // later resource load in that batch may therefore have evicted an image
  // which has already become active, because the previous pin set still
  // described the preceding frame.  Pin the complete new set first, then
  // make every active resource resident again.  Image objects themselves are
  // stable, so the pointers held by Layer remain valid after the reload.
  for (size_t i = 0; i < count; ++i)
    pack_.image(ids[i]);
}

void Player::materialize_active_images() {
  // During save/debug replay, apply() deliberately stores metadata pointers so
  // thousands of preceding images are not decompressed. An alias metadata
  // object has no pixels of its own: Pack::image(alias) returns the composite
  // base object instead. Reassign every live pointer to that returned object;
  // merely warming the cache leaves the renderer pointing at the empty alias.
  auto load = [this](const Image*& image) {
    if (image)
      image = pack_.image(image->id);
  };
  load(background_);
  load(face_);
  load(poem_paper_);
  for (auto& layer : layers_)
    load(layer.image);
  for (auto& image : snow_images_)
    load(image);
  pin_active_images();
}

void Player::finish_transitions() {
  background_opacity_ = 255;
  background_transition_left_ = 0;
  previous_background_ = nullptr;
  stage_transition_left_ = 0;
  old_background_ = nullptr;
  old_background_y_ = 0;
  old_layers_ = {};
  window_transition_left_ = 0;
  window_opacity_ = window_target_opacity_;
  overlay_transition_left_ = 0;
  overlay_opacity_ = overlay_target_opacity_;
  if (!overlay_opacity_)
    overlay_text_.clear();
  for (auto& layer : layers_)
    if (layer.image) {
      layer.transition_left = 0;
      layer.opacity = layer.target_opacity;
      if (layer.move_total)
        layer.x = layer.move_to_x;
      if (layer.remove_when_done)
        layer = {};
    }
  size_t write = 0;
  for (size_t read = 0; read < layers_.size(); read++)
    if (layers_[read].image)
      layers_[write++] = layers_[read];
  while (write < layers_.size())
    layers_[write++] = {};
}

void Player::release_navigation_caches() {
  std::vector<uint16_t>().swap(scroll_text_cache_);
  std::vector<uint16_t>().swap(menu_chrome_cache_);
  scroll_cache_revision_ = UINT32_MAX;
  scroll_cache_height_ = scroll_cache_origin_ = scroll_cache_content_height_ = 0;
  scroll_cache_kind_ = 0;
  menu_chrome_cache_valid_ = false;
  release_save_cache();
}

void Player::release_save_cache() {
  for (auto& save : saves_) {
    save.valid = false;
    save.cursor = 0;
    std::vector<uint16_t>().swap(save.thumbnail);
  }
  save_index_loaded_ = false;
}

void Player::release_title_caches() {
  std::vector<uint16_t>().swap(title_base_cache_);
  std::vector<uint16_t>().swap(title_static_cache_);
  title_base_cache_valid_ = false;
}

void Player::finish_story() {
  // The title and gallery share a Player with the story. Leaving the final
  // scene resident kept its background, sprite deltas and transition frames
  // pinned alongside every gallery thumbnail. On 32 MiB models that also
  // prevented the image cache from returning fragmented story allocations.
  finish_transitions();
  background_ = old_background_ = previous_background_ = face_ = nullptr;
  background_resource_ = 0;
  background_animation_left_ = 0;
  background_y_ = old_background_y_ = 0;
  layers_ = {};
  old_layers_ = {};
  snow_images_[0] = snow_images_[1] = nullptr;
  snow_up_[0] = snow_up_[1] = false;
  stage_transition_left_ = stage_transition_total_ = 0;
  background_transition_left_ = 0;
  centered_transition_left_ = 0;
  shake_left_ = 0;
  transition_gate_ = false;
  auto_play_ = skip_indicator_ = false;
  auto_play_left_ = 0;
  stage_cache_valid_ = movement_base_cache_valid_ = transition_cache_valid_ = false;
  history_.clear();
  ++history_revision_;
  std::string().swap(nvl_text_);
  release_navigation_caches();
  pin_active_images();
  pack_.release_unpinned_images();
}

void Player::apply(const Event& event) {
  // Replaying a save must retain a poem only if it is the final event.
  if (replaying_ && event.op != Op::poem) {
    poem_open_ = false;
    poem_paper_ = nullptr;
    poem_text_.clear();
  }
  if (!replaying_)
    pin_active_images();
  const auto image_for_event = [this](uint16_t id) {
    return replaying_ ? pack_.image_metadata(id) : pack_.image(id);
  };
  const uint32_t duration = transition_ms(event.flags);
  // Transition duration occupies bits 2..7 on visual events.  hpunch/vpunch
  // reuse two of those bits only on dialogue events; interpreting them for
  // every opcode made ordinary fades look like a one-pixel screen shake.
  const bool dialogue_punch = event.op == Op::say && (event.flags & 0x28);
  const bool visual_punch =
      (event.op == Op::scene || event.op == Op::show || event.op == Op::hide) &&
      !(event.flags & transition_mask) && (event.flags & 0x28);
  if (dialogue_punch || visual_punch) {
    shake_left_ = 240;
    shake_horizontal_ = (event.flags & 0x08) != 0;
  }
  const Image* transition_image =
      (event.op == Op::show || event.op == Op::hide) ? image_for_event(event.resource) : nullptr;
  // Ren'Py applies easein/easeout to a shown displayable, even when that
  // displayable happens to fill the screen. Moving the complete old/new stage
  // made school/sch_street behave like a push transition and exposed the next
  // background too early.
  const bool sprite_move = duration && (event.flags & transition_mask) == 3 && transition_image &&
                           !transition_image->particle;
  if (duration && (event.op == Op::scene || event.op == Op::show || event.op == Op::hide) &&
      !sprite_move && !stage_transition_left_) {
    old_background_ = background_;
    old_background_y_ = background_y_;
    old_layers_ = layers_;
    stage_transition_left_ = stage_transition_total_ = duration;
    stage_transition_kind_ = event.flags & transition_mask;
    const bool entering = event.op == Op::scene || event.op == Op::show;
    if (event.speaker.size == 1)
      stage_transition_direction_ = entering ? 1 : -1;
    else if (event.speaker.size == 2)
      stage_transition_direction_ = entering ? -1 : 1;
    else
      stage_transition_direction_ = -1;
    transition_cache_valid_ = false;
  }
  switch (event.op) {
  case Op::poem:
    poem_open_ = true;
    poem_paper_ = image_for_event(event.resource);
    poem_text_ = std::string(pack_.text(event.text));
    poem_font_size_ = event.x;
    poem_line_height_ = event.y;
    poem_scroll_px_ = 0;
    poem_scroll_max_ = std::max(0, text_height(pack_, poem_text_, 238,
                                              poem_line_height_, poem_font_size_) - 198);
    auto_play_ = false;
    skip_indicator_ = false;
    break;
  case Op::choice: {
    choice_count_ = std::min<uint16_t>(event.resource, 10);
    choice_selection_ = 0;
    choice_first_ = cursor_;
    prepare_word_board(choice_first_ - 1);
    if (replaying_) {
      const uint32_t menu_event = cursor_ ? static_cast<uint32_t>(cursor_ - 1) : 0;
      for (uint8_t i = 0; i < choice_history_count_; ++i) {
        if (choice_history_[i].menu_event != menu_event ||
            choice_history_[i].option >= choice_count_)
          continue;
        const Event& option = pack_.events()[choice_first_ + choice_history_[i].option];
        if (option.op == Op::choice_option && event.x >= 0 &&
            event.x < static_cast<int>(story_state_.size())) {
          story_state_[event.x] = static_cast<int16_t>(option.resource);
          if (!apply_random_word(choice_history_[i].option, branch_target(option)))
            cursor_ = branch_target(option);
          choice_open_ = false;
          return;
        }
      }
      choice_open_ = false;
      cursor_ += choice_count_;
      return;
    }
    cursor_ += choice_count_;
    const int slot = event.x;
    if (slot < 0 || slot >= static_cast<int>(story_state_.size()) || !choice_count_) {
      choice_open_ = false;
      break;
    }
    choice_open_ = true;
    speaker_.clear();
    text_.assign(pack_.text(event.text));
    reveal_bytes_ = text_.size();
    window_visible_ = true;
    window_opacity_ = window_target_opacity_ = 255;
    break;
  }
  case Op::choice_option:
    break;
  case Op::jump:
    cursor_ = branch_target(event);
    break;
  case Op::set_state:
    if (event.resource < story_state_.size())
      story_state_[event.resource] = event.x;
    break;
  case Op::add_state:
    if (event.resource < story_state_.size())
      story_state_[event.resource] = static_cast<int16_t>(story_state_[event.resource] + event.x);
    break;
  case Op::if_state:
    if (event.resource < story_state_.size() && story_state_[event.resource] == event.x)
      cursor_ = branch_target(event);
    break;
  case Op::if_state_gt:
    if (event.resource < story_state_.size() && event.x >= 0 &&
        event.x < static_cast<int>(story_state_.size()) &&
        story_state_[event.resource] > story_state_[event.x])
      cursor_ = branch_target(event);
    break;
  case Op::persistent_load:
    if (event.resource < story_state_.size() && event.x >= 0 &&
        static_cast<size_t>(event.x) < persistent_values_.size())
      story_state_[event.resource] = persistent_values_[static_cast<size_t>(event.x)];
    break;
  case Op::persistent_store:
    if (!replaying_ && event.resource < story_state_.size() && event.x >= 0 &&
        static_cast<size_t>(event.x) < persistent_values_.size()) {
      persistent_values_[static_cast<size_t>(event.x)] = story_state_[event.resource];
      save_persistent();
    }
    break;
  case Op::system:
    if (replaying_)
      break;
    if (event.resource == 1)
      delete_all_saves();
    else if (event.resource == 2)
      delete_character_file(static_cast<size_t>(event.x));
    else if (event.resource == 3)
      write_character_files();
    break;
  case Op::effect:
    if (replaying_)
      break;
    effect_kind_ = static_cast<uint8_t>(event.resource);
    effect_left_ = event.x > 0 ? static_cast<uint32_t>(event.x) : 0;
    effect_elapsed_ = 0;
    effect_param_ = event.y;
    effect_seed_ = (poem_seed_ ^ static_cast<uint32_t>(cursor_ * 0x9e3779b9u)) | 1U;
    stage_cache_valid_ = false;
    break;
  case Op::rand_state: {
    if (event.resource >= story_state_.size() || event.x < 1)
      break;
    // Deterministic per story position so save replay reproduces the draw.
    uint32_t rng = (poem_seed_ ^ static_cast<uint32_t>(cursor_ * 0x9e3779b9u)) | 1U;
    for (int i = 0; i < 4; ++i) {
      rng ^= rng << 13;
      rng ^= rng >> 17;
      rng ^= rng << 5;
    }
    const int count = event.x;
    int16_t* mask = event.y >= 0 && static_cast<size_t>(event.y) < story_state_.size()
                        ? &story_state_[static_cast<size_t>(event.y)]
                        : nullptr;
    int candidates[16];
    int available = 0;
    for (int value = 0; value < count && value < 16; ++value)
      if (!mask || !(*mask & (1 << value)))
        candidates[available++] = value;
    if (!available)
      break;
    const int chosen = candidates[rng % static_cast<uint32_t>(available)];
    story_state_[event.resource] = static_cast<int16_t>(chosen);
    if (mask)
      *mask = static_cast<int16_t>(*mask | (1 << chosen));
    break;
  }
  case Op::scene:
    previous_background_ = nullptr;
    background_ = image_for_event(event.resource);
    background_resource_ = event.resource;
    background_animation_left_ = background_ ? background_->animation_ms : 0;
    background_y_ = event.y;
    background_opacity_ = 255;
    background_transition_left_ = 0;
    layers_ = {};
    break;
  case Op::move:
    // Reposition only an existing atom. Candidate expressions compiled from
    // alternate branches must never be created by a bare Ren'Py tag move.
    for (auto& layer : layers_)
      if (layer.image && layer.resource == event.resource) {
        layer.x = event.x;
        layer.y = event.y;
        stage_cache_valid_ = movement_base_cache_valid_ = transition_cache_valid_ = false;
        break;
      }
    break;
  case Op::show: {
    const Image* image = image_for_event(event.resource);
    const bool shaking = event.y <= -10000;
    const int event_y = shaking ? -20000 - event.y : event.y;
    if (image && image->particle) {
      const bool upward = event.x <= -30001 && event.x >= -30003;
      const int slot = upward ? (-event.x - 30001) % 2 : image->id % 2;
      snow_images_[slot] = image;
      snow_up_[slot] = upward;
      if (upward)
        for (size_t i = 0; i < particles_.size(); ++i) {
          particles_[i].y = 242 + static_cast<int>((i * 23) % 180);
          particles_[i].x =
              i & 1 ? 250 + static_cast<int>((i * 17) % 70) : static_cast<int>((i * 19) % 70);
        }
      return;
    }
    // `scene` owns the replaceable background slot. A large image introduced
    // with `show` is still an ordinary Ren'Py tag and may cover another large
    // image until a matching `hide` reveals it again (is_neutral/is_bored).
    for (auto& layer : layers_) {
      // A resource id is the Ren'Py image tag in the compiled linear stream.
      // Two images may share a diced body while deliberately remaining
      // separate layers (show neutral over smirk, then hide neutral). Only an
      // exact tag match is an in-place expression/position update.
      if (layer.image && layer.resource == event.resource) {
        const int old_x = layer.x;
        const bool old_below_particles = layer.below_particles;
        layer = {image, event.x, event_y, 255, 255, 0, false};
        layer.shaking = shaking;
        layer.below_particles = old_below_particles;
        layer.resource = event.resource;
        layer.animation_left = image ? image->animation_ms : 0;
        if (sprite_move) {
          layer.move_from_x = old_x;
          layer.move_to_x = event.x;
          layer.move_total = layer.transition_left = duration;
          layer.x = layer.move_from_x;
        }
        return;
      }
    }
    size_t used = 0;
    while (used < layers_.size() && layers_[used].image)
      ++used;
    size_t insert = used;
    const uint16_t behind = static_cast<uint16_t>(event.speaker.offset);
    bool below_particles = false;
    if (behind)
      if (const Image* behind_image = pack_.image_metadata(behind))
        below_particles = behind_image->particle;
    if (behind)
      for (size_t i = 0; i < used; ++i)
        if (layers_[i].resource == behind) {
          insert = i;
          below_particles |= layers_[i].below_particles;
          break;
        }
    if (used < layers_.size()) {
      for (size_t i = used; i > insert; --i)
        layers_[i] = layers_[i - 1];
      auto& layer = layers_[insert];
      layer = {image, event.x, event_y, 255, 255, 0, false};
      layer.shaking = shaking;
      layer.below_particles = below_particles;
      layer.resource = event.resource;
      layer.animation_left = image ? image->animation_ms : 0;
      if (sprite_move) {
        layer.move_from_x = event.speaker.size == 1 ? -static_cast<int>(image->width) : screen_w;
        layer.move_to_x = event.x;
        layer.move_total = layer.transition_left = duration;
        layer.x = layer.move_from_x;
      }
    }
    break;
  }
  case Op::hide:
    if (const Image* im = image_for_event(event.resource); im && im->particle) {
      for (auto& snow : snow_images_)
        if (snow == im) {
          snow_up_[&snow - snow_images_] = false;
          snow = nullptr;
        }
      break;
    }
    for (size_t i = 0; i < layers_.size(); i++)
      if (layers_[i].image && layers_[i].resource == event.resource) {
        if (sprite_move) {
          auto& layer = layers_[i];
          layer.move_from_x = layer.x;
          layer.move_to_x =
              event.speaker.size == 2 ? screen_w : -static_cast<int>(layer.image->width);
          layer.move_total = layer.transition_left = duration;
          layer.remove_when_done = true;
        } else {
          for (size_t j = i + 1; j < layers_.size(); j++)
            layers_[j - 1] = layers_[j];
          layers_.back() = {};
        }
        break;
      }
    break;
  case Op::say: {
    if (event.x & 2) {
      cutscene_locked_ = true;
      skip_indicator_ = auto_play_ = false;
    }
    const bool extending = (event.flags & extend_flag) != 0;
    const bool continuing_centered = extending && centered_ && event.x == 1;
    centered_ = event.x == 1;
    if (!centered_) {
      centered_no_wrap_ = false;
      const bool event_nvl = (event.flags & nvl_say_flag) != 0;
      if (event_nvl != nvl_mode_) {
        nvl_mode_ = event_nvl;
        nvl_text_.clear();
      }
    }
    if (extending) {
      finish_transitions();
      reveal_bytes_ = text_.size();
      text_ += pack_.text(event.text);
      if (nvl_mode_ && !centered_)
        nvl_text_ = text_;
      if (event.resource)
        face_ = image_for_event(event.resource);
      if (!history_.empty()) {
        history_.back().text += pack_.text(event.text);
        ++history_revision_;
      }
    } else {
      face_ = event.resource ? image_for_event(event.resource) : nullptr;
      speaker_ = pack_.text(event.speaker);
      text_ = pack_.text(event.text);
      auto replace_player = [this](std::string& value) {
        constexpr std::string_view marker = "{player}";
        size_t at = 0;
        while ((at = value.find(marker, at)) != std::string::npos) {
          value.replace(at, marker.size(), player_name_);
          at += player_name_.size();
        }
      };
      replace_player(speaker_);
      replace_player(text_);
      if (!pack_.glitch_alphabet().empty()) {
        const uint32_t seed = poem_seed_ ^ static_cast<uint32_t>(cursor_ * 0x9e3779b9u);
        if (speaker_.find("{glitch=") != std::string::npos)
          speaker_ = expand_glitch(speaker_, seed ^ 0x5bd1e995u);
        if (text_.find("{glitch=") != std::string::npos)
          text_ = expand_glitch(text_, seed);
      }
      if (nvl_mode_ && !centered_) {
        if (!nvl_text_.empty())
          nvl_text_ += "\n\n";
        reveal_bytes_ = nvl_text_.size();
        nvl_text_ += text_;
        text_ = nvl_text_;
      }
      if (!nvl_mode_)
        reveal_bytes_ = 0;
      if (history_.size() == 64) {
        // Reuse the oldest entry's string allocations instead of repeatedly
        // freeing and reallocating variable-sized dialogue on newlib's small
        // heap during a full playthrough.
        std::rotate(history_.begin(), history_.begin() + 1, history_.end());
        history_.back().speaker = speaker_;
        history_.back().text = text_;
      } else {
        history_.push_back({speaker_, text_});
      }
      ++history_revision_;
    }
    font_size_ = event.y ? event.y : ui_metrics::prose_size(pack_.chinese());
    if (!centered_ && !nvl_mode_) {
      constexpr int dialogue_viewport = 59;
      const int width = ui_metrics::dialogue_width(ddlc_ui_, face_ != nullptr);
      const int body_line_height = ui_metrics::line_height(font_size_);
      dialogue_scroll_max_ = std::max(
          0, text_height(pack_, text_, width, body_line_height, font_size_) - dialogue_viewport);
      dialogue_scroll_follow_ = true;
      if (!extending)
        dialogue_scroll_px_ = 0;
      nvl_scroll_px_ = nvl_scroll_max_ = 0;
      nvl_scroll_follow_ = true;
    } else if (nvl_mode_ && !centered_) {
      constexpr int nvl_viewport = 216, nvl_paragraph_gap = 7;
      const int body_line_height = ui_metrics::line_height(font_size_);
      dialogue_scroll_px_ = dialogue_scroll_max_ = 0;
      dialogue_scroll_follow_ = true;
      nvl_scroll_max_ = std::max(0, text_height_compact(pack_, text_, 272, body_line_height,
                                                        font_size_, nvl_paragraph_gap) -
                                        nvl_viewport);
      nvl_scroll_follow_ = true;
    } else {
      dialogue_scroll_px_ = dialogue_scroll_max_ = 0;
      dialogue_scroll_follow_ = true;
      nvl_scroll_px_ = nvl_scroll_max_ = 0;
      nvl_scroll_follow_ = true;
    }
    pause_ui_hidden_ = false;
    if (centered_) {
      // Ren'Py's `extend` adds to the existing centered statement.  Restarting
      // its reveal and dissolve here replayed every previously displayed line
      // (most visibly in the repair-shop timetable).  Keep both the reveal
      // cursor and the open {cps} state while only the newly appended bytes run.
      if (!continuing_centered) {
        centered_no_wrap_ = (event.flags & centered_no_wrap_flag) != 0;
        reveal_bytes_ = 0;
        centered_fade_ = transition_ms(event.flags) != 0;
        centered_opacity_ = centered_fade_ ? 0 : 255;
        centered_transition_left_ = centered_fade_ ? 350 : 0;
        centered_fading_out_ = false;
      }
    } else if (!nvl_mode_) {
      window_visible_ = true;
      if (!window_transition_left_)
        window_opacity_ = window_target_opacity_ = 255;
    }
    inline_wait_ = false;
    inline_wait_left_ = 0;
    if (!extending)
      active_cps_ = 0;
    auto_advance_text_ = false;
    auto_play_left_ = 900;
    reveal_accumulator_ = 0;
    break;
  }
  case Op::end:
    if (event.flags & 2) {
      finish_story();
      credits_roll_active_ = true;
      credits_roll_ms_ = 0;
      ended_ = true;
      title_active_ = false;
      gallery_open_ = gallery_view_ = history_open_ = save_open_ = load_open_ = false;
      info_open_ = confirm_open_ = choice_open_ = waiting_ = false;
      auto_play_ = skip_indicator_ = false;
      text_.clear();
      speaker_.clear();
      window_visible_ = false;
      video_active_ = false;
      video_.close();
      break;
    }
    // A top-level Ren'Py `return` finishes the label and returns to the main
    // menu; it does not terminate the executable. Keep the already decoded
    // title resources resident and reset only transient game presentation.
    finish_story();
    ended_ = false;
    title_active_ = true;
    gallery_open_ = gallery_view_ = false;
    title_selection_ = 0;
    history_open_ = save_open_ = load_open_ = info_open_ = confirm_open_ = waiting_ = false;
    centered_ = nvl_mode_ = false;
    overlay_text_.clear();
    text_.clear();
    speaker_.clear();
    dialogue_scroll_px_ = dialogue_scroll_max_ = 0;
    dialogue_scroll_follow_ = true;
    nvl_scroll_px_ = nvl_scroll_max_ = 0;
    nvl_scroll_follow_ = true;
    window_visible_ = false;
    screen_fade_left_ = 240;
    break;
  case Op::title:
    video_active_ = false;
    video_.close();
    title_active_ = true;
    simple_title_ = (event.flags & 0x01) != 0;
    name_entry_open_ = false;
    video_frame_ = nullptr;
    {
      const uint16_t title_ids[] = {event.resource,
                                    static_cast<uint16_t>(event.y),
                                    static_cast<uint16_t>(event.y + 1),
                                    static_cast<uint16_t>(event.y + 2),
                                    static_cast<uint16_t>(event.y + 3),
                                    static_cast<uint16_t>(event.y + 4)};
      pin_active_images(title_ids, std::size(title_ids));
      title_background_ = pack_.image(event.resource);
      gallery_base_ = static_cast<uint16_t>(event.speaker.offset);
      history_background_ = pack_.image(static_cast<uint16_t>(event.y));
      history_overlay_ = pack_.image(static_cast<uint16_t>(event.y + 1));
      title_snow_images_[0] = pack_.image(static_cast<uint16_t>(event.y + 2));
      title_snow_images_[1] = pack_.image(static_cast<uint16_t>(event.y + 3));
      title_menu_overlay_ = pack_.image(static_cast<uint16_t>(event.speaker.size));
      menu_chrome_cache_valid_ = title_base_cache_valid_ = false;
    }
    break;
  case Op::persistent: {
    if (event.resource < 64) {
      const uint64_t bit = uint64_t{1} << event.resource;
      if (!(gallery_unlocks_ & bit)) {
        gallery_unlocks_ |= bit;
        if (!replaying_)
          save_persistent();
      }
    }
    break;
  }
  case Op::pause:
    waiting_ = true;
    wait_left_ = event.x > 0 ? static_cast<uint32_t>(event.x) : event.x < 0 ? wait_for_input : 1;
    if (event.flags & 0x40) {
      speaker_.clear();
      text_.clear();
      reveal_bytes_ = 0;
      face_ = nullptr;
      dialogue_scroll_px_ = dialogue_scroll_max_ = 0;
      dialogue_scroll_follow_ = true;
      nvl_scroll_px_ = nvl_scroll_max_ = 0;
      nvl_scroll_follow_ = true;
    }
    break;
  case Op::window: {
    window_visible_ = event.x != 0;
    const uint8_t target = window_visible_ ? 255 : 0;
    if (duration) {
      window_from_opacity_ = window_opacity_;
      window_target_opacity_ = target;
      window_transition_left_ = window_transition_total_ = duration;
    } else
      window_opacity_ = window_target_opacity_ = target;
    if (window_visible_) {
      speaker_.clear();
      text_.clear();
      reveal_bytes_ = 0;
    }
    break;
  }
  case Op::overlay: {
    const std::string incoming(pack_.text(event.text));
    if (!incoming.empty() && event.x == 1) {
      speaker_.clear();
      text_.clear();
      reveal_bytes_ = 0;
      face_ = nullptr;
      window_visible_ = false;
      window_opacity_ = window_target_opacity_ = 0;
      centered_ = false;
      dialogue_scroll_px_ = dialogue_scroll_max_ = 0;
      dialogue_scroll_follow_ = true;
      nvl_scroll_px_ = nvl_scroll_max_ = 0;
      nvl_scroll_follow_ = true;
    }
    overlay_target_opacity_ = incoming.empty() ? 0 : 255;
    if (!incoming.empty()) {
      overlay_text_ = incoming;
      overlay_positioned_ = event.x < 0;
      overlay_x_ = overlay_positioned_ ? -event.x - 1 : 0;
      overlay_y_ = event.y;
    }
    if (duration) {
      overlay_transition_left_ = overlay_transition_total_ = duration;
      if (overlay_target_opacity_)
        overlay_opacity_ = 0;
    } else {
      overlay_opacity_ = overlay_target_opacity_;
      if (!overlay_opacity_)
        overlay_text_.clear();
    }
    break;
  }
  case Op::nvl:
    if (event.x == 1) {
      nvl_mode_ = true;
      nvl_scroll_px_ = nvl_scroll_max_ = 0;
      nvl_scroll_follow_ = true;
      window_visible_ = false;
      window_opacity_ = window_target_opacity_ = 0;
    } else if (event.x == 2) {
      nvl_mode_ = false;
      nvl_text_.clear();
      text_.clear();
      reveal_bytes_ = 0;
      nvl_scroll_px_ = nvl_scroll_max_ = 0;
      nvl_scroll_follow_ = true;
    } else {
      nvl_text_.clear();
      text_.clear();
      reveal_bytes_ = 0;
      nvl_scroll_px_ = nvl_scroll_max_ = 0;
      nvl_scroll_follow_ = true;
    }
    break;
  case Op::transform: {
    const uint16_t options = static_cast<uint16_t>(event.y);
    for (auto& layer : layers_)
      if (layer.image && layer.resource == event.resource) {
        const int target_scale = options & 0x0fffU;
        layer.pivot_x = static_cast<uint16_t>(event.x) & 0xffU;
        layer.pivot_y = static_cast<uint16_t>(event.x) >> 8U;
        if (options & 0x1000U)
          layer.opacity = layer.target_opacity = event.flags;
        if (options & 0x2000U)
          layer.blend_mode = 1;
        else if (options & 0x4000U)
          layer.blend_mode = 2;
        if (options & 0x8000U)
          layer.xflip = true;
        if (target_scale) {
          const uint32_t transform_duration = event.speaker.offset;
          layer.scale_from = layer.scale;
          layer.scale_to = target_scale;
          layer.scale_total = layer.scale_left = transform_duration;
          if (!transform_duration)
            layer.scale = target_scale;
        }
        // A transform can legally accompany a dissolve on the same Show.
        // Rebuild the transition's new side at the initial ATL scale instead
        // of retaining a cache captured before the transform was applied.
        transition_cache_valid_ = false;
        stage_cache_valid_ = false;
        break;
      }
    break;
  }
  }
}

void Player::advance_automatic() {
  while (cursor_ < pack_.events().size()) {
    const auto& event = pack_.events()[cursor_++];
    const bool visual = event.op == Op::scene || event.op == Op::show || event.op == Op::hide ||
                        event.op == Op::move ||
                        event.op == Op::window || event.op == Op::overlay || event.op == Op::nvl ||
                        event.op == Op::transform;
    bool layer_transition = false;
    for (const auto& layer : layers_)
      layer_transition |= layer.transition_left != 0;
    const bool active_transition = stage_transition_left_ || window_transition_left_ ||
                                   overlay_transition_left_ || layer_transition;
    const Image* next_image = event.op == Op::show ? pack_.image_metadata(event.resource) : nullptr;
    const bool concurrent_sprite_move = event.op == Op::show &&
                                        (event.flags & transition_mask) == 3 && next_image &&
                                        !next_image->background && !next_image->particle;
    if (active_transition && (!visual || (event.op != Op::transform && transition_ms(event.flags) &&
                                          !concurrent_sprite_move))) {
      cursor_--;
      transition_gate_ = true;
      return;
    }
    apply(event);
    if (event.op == Op::title || event.op == Op::say || event.op == Op::pause ||
        event.op == Op::choice || event.op == Op::poem ||
        event.op == Op::end)
      return;
  }
}

bool Player::tick(uint32_t elapsed_ms) {
  if (startup_open()) {
    if (startup_phase_ == StartupPhase::notice) return false;
    startup_elapsed_ms_ += std::min(elapsed_ms, startup_fade_ms - startup_elapsed_ms_);
    if (startup_elapsed_ms_ == startup_fade_ms) {
      startup_phase_ = startup_phase_ == StartupPhase::fade_to_white
                           ? StartupPhase::fade_to_menu : StartupPhase::none;
      startup_elapsed_ms_ = 0;
    }
    return true;
  }
  if (credits_roll_active_) {
    const uint32_t end = credits_delay_ms + credits_scroll_ms + credits_hold_ms;
    credits_roll_ms_ += std::min(elapsed_ms, end - credits_roll_ms_);
    if (credits_roll_ms_ >= end) quit_requested_ = true;
    return true;
  }
  pin_active_images();
  bool changed = false;
  animation_ms_ += elapsed_ms;
  auto animate_image = [&](const Image*& image, uint32_t& left) {
    if (!image || !image->animation_next || !image->animation_ms)
      return false;
    if (elapsed_ms >= left) {
      // Never skip an image-sequence frame merely because flash I/O or a
      // costly composite made the host miss its nominal deadline. Horror ATL
      // sequences use 30/100 ms frames that must each reach the LCD at least
      // once; slowing the sequence under load is preferable to dropping it.
      image = pack_.image(image->animation_next);
      left = image && image->animation_ms ? image->animation_ms : 0;
      return true;
    }
    left -= std::min(elapsed_ms, left - 1);
    return false;
  };
  changed |= animate_image(background_, background_animation_left_);
  if (changed && stage_transition_left_)
    transition_cache_valid_ = false;
  pin_active_images();
  for (auto& layer : layers_)
    if (animate_image(layer.image, layer.animation_left)) {
      changed = true;
      if (stage_transition_left_)
        transition_cache_valid_ = false;
      pin_active_images();
    }
  if (screen_fade_left_) {
    screen_fade_left_ -= std::min(elapsed_ms, screen_fade_left_);
    changed = true;
  }
  if (video_active_) {
    uint32_t left = elapsed_ms;
    int decoded = 0;
    bool video_changed = false;
    while (video_active_ && left >= video_left_ && decoded < 6) {
      left -= video_left_;
      const bool output = left < video_.frame_ms() || decoded == 5;
      if (!video_.next(output)) {
        capture_video_frame();
        video_active_ = false;
        video_.close();
        advance_automatic();
        video_changed = true;
      } else {
        video_left_ = video_.frame_ms();
        video_changed |= output;
      }
      decoded++;
    }
    if (video_active_)
      video_left_ -= std::min(left, video_left_ - 1);
    return video_changed;
  }
  if (title_active_ || snow_images_[0] || snow_images_[1]) {
    particle_accumulator_ += elapsed_ms;
    while (particle_accumulator_ >= 40) {
      particle_accumulator_ -= 40;
      for (auto& p : particles_) {
        const bool upward = snow_up_[p.kind & 1];
        p.y += upward ? -p.speed : p.speed;
        if ((!upward && p.y >= 240) || (upward && p.y < -14)) {
          particle_rng_ ^= particle_rng_ << 13;
          particle_rng_ ^= particle_rng_ >> 17;
          particle_rng_ ^= particle_rng_ << 5;
          p.y = upward ? 242 + static_cast<int>((particle_rng_ >> 9) % 42)
                       : -5 - static_cast<int>((particle_rng_ >> 9) % 42);
          p.x = upward ? ((particle_rng_ & 1) ? 250 + static_cast<int>((particle_rng_ >> 1) % 70)
                                              : static_cast<int>((particle_rng_ >> 1) % 70))
                       : static_cast<int>(particle_rng_ % 320);
          p.speed = 1 + static_cast<int>((particle_rng_ >> 18) & 1);
          p.kind = static_cast<int>((particle_rng_ >> 19) & 1);
        }
      }
      changed = true;
      if (stage_transition_left_)
        transition_cache_valid_ = false;
    }
  }
  if (poem_open_ || title_active_ || gallery_open_ || history_open_ || save_open_ || load_open_ || info_open_ ||
      choice_open_)
    return changed;
  if (window_transition_left_) {
    const uint32_t step = std::min(elapsed_ms, window_transition_left_);
    window_transition_left_ -= step;
    const uint32_t done = window_transition_total_ - window_transition_left_;
    window_opacity_ = static_cast<uint8_t>(
        window_from_opacity_ +
        (static_cast<int>(window_target_opacity_) - window_from_opacity_) * static_cast<int>(done) /
            static_cast<int>(std::max<uint32_t>(1, window_transition_total_)));
    changed = true;
  }
  if (overlay_transition_left_) {
    const uint32_t step = std::min(elapsed_ms, overlay_transition_left_);
    overlay_transition_left_ -= step;
    const uint32_t done = overlay_transition_total_ - overlay_transition_left_;
    overlay_opacity_ = static_cast<uint8_t>(
        overlay_target_opacity_
            ? done * 255 / std::max<uint32_t>(1, overlay_transition_total_)
            : 255 - done * 255 / std::max<uint32_t>(1, overlay_transition_total_));
    if (!overlay_transition_left_ && !overlay_target_opacity_)
      overlay_text_.clear();
    changed = true;
  }
  if (centered_transition_left_) {
    const uint32_t step = std::min(elapsed_ms, centered_transition_left_);
    centered_transition_left_ -= step;
    const uint32_t done = 350 - centered_transition_left_;
    centered_opacity_ =
        static_cast<uint8_t>(centered_fading_out_ ? 255 - done * 255 / 350 : done * 255 / 350);
    changed = true;
    if (!centered_transition_left_ && centered_fading_out_) {
      centered_ = centered_fading_out_ = false;
      text_.clear();
      advance_automatic();
    }
  }
  if (background_transition_left_) {
    const uint32_t step = std::min(elapsed_ms, background_transition_left_);
    background_opacity_ += (255 - background_opacity_) * static_cast<int>(step) /
                           static_cast<int>(background_transition_left_);
    background_transition_left_ -= step;
    if (!background_transition_left_) {
      background_opacity_ = 255;
      previous_background_ = nullptr;
    }
    changed = true;
  }
  if (stage_transition_left_) {
    stage_transition_left_ -= std::min(elapsed_ms, stage_transition_left_);
    if (!stage_transition_left_) {
      old_background_ = nullptr;
      old_background_y_ = 0;
      old_layers_ = {};
      transition_cache_valid_ = false;
    }
    changed = true;
  }
  if (shake_left_) {
    shake_left_ -= std::min(elapsed_ms, shake_left_);
    changed = true;
  }
  for (auto& layer : layers_) {
    if (!layer.image)
      continue;
    if (layer.scale_left) {
      layer.scale_left -= std::min(elapsed_ms, layer.scale_left);
      const uint32_t done = layer.scale_total - layer.scale_left;
      const int eased = static_cast<int>(ease1024(done, layer.scale_total));
      layer.scale = layer.scale_from + (layer.scale_to - layer.scale_from) * eased / 1024;
      if (!layer.scale_left)
        layer.scale = layer.scale_to;
      if (stage_transition_left_)
        transition_cache_valid_ = false;
      changed = true;
    }
    if (!layer.transition_left)
      continue;
    const uint32_t step = std::min(elapsed_ms, layer.transition_left);
    layer.opacity += (layer.target_opacity - layer.opacity) * static_cast<int>(step) /
                     static_cast<int>(layer.transition_left);
    layer.transition_left -= step;
    if (layer.move_total) {
      const uint32_t done = layer.move_total - layer.transition_left;
      const int eased = static_cast<int>(ease1024(done, layer.move_total));
      layer.x = layer.move_from_x + (layer.move_to_x - layer.move_from_x) * eased / 1024;
    }
    if (!layer.transition_left) {
      layer.opacity = layer.target_opacity;
      if (layer.remove_when_done)
        layer = {};
    }
    changed = true;
  }
  size_t write = 0;
  for (size_t read = 0; read < layers_.size(); read++)
    if (layers_[read].image)
      layers_[write++] = layers_[read];
  while (write < layers_.size())
    layers_[write++] = {};
  bool layer_transition = false;
  for (const auto& layer : layers_)
    layer_transition |= layer.transition_left != 0;
  if (transition_gate_ && !stage_transition_left_ && !window_transition_left_ &&
      !overlay_transition_left_ && !layer_transition) {
    transition_gate_ = false;
    advance_automatic();
    return true;
  }
  if (effect_kind_) {
    effect_elapsed_ += elapsed_ms;
    if (effect_left_) {
      if (elapsed_ms >= effect_left_) {
        effect_left_ = 0;
        effect_kind_ = 0;
      } else
        effect_left_ -= elapsed_ms;
    }
    changed = true;
  }
  if (waiting_) {
    if (wait_left_ != wait_for_input && elapsed_ms >= wait_left_) {
      waiting_ = false;
      wait_left_ = 0;
      advance_automatic();
      return true;
    } else if (wait_left_ != wait_for_input)
      wait_left_ -= elapsed_ms;
    return changed;
  }
  if (inline_wait_ && inline_wait_left_) {
    if (elapsed_ms >= inline_wait_left_) {
      inline_wait_ = false;
      inline_wait_left_ = 0;
    } else {
      inline_wait_left_ -= elapsed_ms;
      return changed;
    }
  }
  if (reveal_bytes_ < text_.size() && !inline_wait_) {
    const size_t old_reveal_bytes = reveal_bytes_;
    const uint32_t cps = active_cps_ ? active_cps_ : (language_ == Language::english ? 55 : 35);
    reveal_accumulator_ += elapsed_ms * cps;
    while (reveal_accumulator_ >= 1000 && reveal_bytes_ < text_.size()) {
      reveal_accumulator_ -= 1000;
      uint32_t wait = 0;
      bool click = false;
      if (!reveal_next(text_, reveal_bytes_, active_cps_, wait, click, auto_advance_text_)) {
        inline_wait_ = true;
        inline_wait_left_ = wait;
        break;
      }
    }
    changed |= reveal_bytes_ != old_reveal_bytes;
  }
  if (reveal_bytes_ >= text_.size() && auto_advance_text_ && !inline_wait_) {
    auto_advance_text_ = false;
    advance_automatic();
    return true;
  }
  if (auto_play_ && reveal_bytes_ >= text_.size() && !inline_wait_ && !waiting_ &&
      !centered_fading_out_) {
    if (elapsed_ms >= auto_play_left_) {
      auto_play_left_ = 900;
      advance();
      return true;
    }
    auto_play_left_ -= elapsed_ms;
  }
  return changed || snow_images_[0] || snow_images_[1];
}

bool Player::fast_forward() {
  if (!skip_allowed())
    return false;
  if (waiting_) {
    waiting_ = false;
    wait_left_ = 0;
    advance_automatic();
    return true;
  }
  if (inline_wait_) {
    inline_wait_ = false;
    return true;
  }
  if (transition_gate_ || stage_transition_left_ || window_transition_left_ ||
      overlay_transition_left_) {
    finish_transitions();
    transition_gate_ = false;
  }
  reveal_bytes_ = text_.size();
  reveal_accumulator_ = 0;
  return advance();
}

bool Player::advance() {
  if (startup_open()) return false;
  if (ended_)
    return false;
  if (choice_open_)
    return true;
  if (poem_open_) {
    poem_open_ = false;
    poem_paper_ = nullptr;
    poem_text_.clear();
    advance_automatic();
    return !ended_;
  }
  if (history_open_) {
    history_open_ = false;
    release_navigation_caches();
    return true;
  }
  if (video_active_) {
    capture_video_frame();
    video_active_ = false;
    video_.close();
    advance_automatic();
    return !ended_;
  }
  if (waiting_) {
    if (cutscene_locked_) return true;
    waiting_ = false;
    advance_automatic();
    return !ended_;
  }
  if (inline_wait_) {
    if (cutscene_locked_ && inline_wait_left_) return true;
    inline_wait_ = false;
    return true;
  }
  if (title_active_) {
    start_new_game();
    screen_fade_left_ = 240;
    return !ended_;
  }
  if (centered_ && !centered_fading_out_) {
    if (reveal_bytes_ < text_.size()) {
      reveal_bytes_ = text_.size();
      reveal_accumulator_ = 0;
      return true;
    }
    // A Ren'Py `extend` belongs to the currently displayed centered line.
    // Apply it in place instead of dismissing/fading and recreating the block.
    if (cursor_ < pack_.events().size()) {
      const auto& next = pack_.events()[cursor_];
      if (next.op == Op::say && (next.flags & extend_flag) && next.x == 1) {
        cursor_++;
        apply(next);
        return true;
      }
    }
    if (centered_fade_) {
      centered_fading_out_ = true;
      centered_transition_left_ = 350;
    } else {
      centered_ = false;
      text_.clear();
      advance_automatic();
    }
    return true;
  }
  if (reveal_bytes_ < text_.size()) {
    reveal_bytes_ = text_.size();
    reveal_accumulator_ = 0;
    return true;
  }
  while (cursor_ < pack_.events().size()) {
    const auto& event = pack_.events()[cursor_++];
    const bool visual = event.op == Op::scene || event.op == Op::show || event.op == Op::hide ||
                        event.op == Op::move ||
                        event.op == Op::window || event.op == Op::overlay || event.op == Op::nvl ||
                        event.op == Op::transform;
    bool layer_transition = false;
    for (const auto& layer : layers_)
      layer_transition |= layer.transition_left != 0;
    const bool active_transition = stage_transition_left_ || window_transition_left_ ||
                                   overlay_transition_left_ || layer_transition;
    if (active_transition &&
        (!visual || (event.op != Op::transform && transition_ms(event.flags)))) {
      cursor_--;
      transition_gate_ = true;
      return true;
    }
    apply(event);
    if (event.op == Op::say || event.op == Op::end || event.op == Op::title ||
        event.op == Op::pause || event.op == Op::choice || event.op == Op::poem)
      return !ended_;
  }
  ended_ = true;
  return false;
}

} // namespace nzato

namespace nzato {
std::string Player::expand_glitch(std::string_view text, uint32_t seed) const {
  const auto& alphabet = pack_.glitch_alphabet();
  std::string out;
  out.reserve(text.size() + 64);
  uint32_t rng = seed | 1U;
  size_t at = 0;
  while (at < text.size()) {
    constexpr std::string_view marker = "{glitch=";
    const size_t open = text.find(marker, at);
    if (open == std::string_view::npos) {
      out.append(text.substr(at));
      break;
    }
    out.append(text.substr(at, open - at));
    const size_t close = text.find('}', open);
    if (close == std::string_view::npos) {
      out.append(text.substr(open));
      break;
    }
    int count = 0;
    for (size_t i = open + marker.size(); i < close; ++i)
      if (text[i] >= '0' && text[i] <= '9')
        count = count * 10 + (text[i] - '0');
    count = std::min(count, 200);
    for (int i = 0; i < count && !alphabet.empty(); ++i) {
      rng ^= rng << 13;
      rng ^= rng >> 17;
      rng ^= rng << 5;
      uint32_t cp = alphabet[rng % alphabet.size()];
      // UTF-8 encode (the alphabet is below U+0800).
      if (cp < 0x80)
        out.push_back(static_cast<char>(cp));
      else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
    }
    at = close + 1;
  }
  return out;
}

void Player::screen_effects(uint16_t* dst) const {
  if (!effect_kind_)
    return;
  if (effect_kind_ == 2) {
    for (int i = 0; i < screen_w * screen_h; ++i)
      dst[i] = static_cast<uint16_t>(~dst[i]);
    return;
  }
  if (effect_kind_ != 1)
    return;
  // Screen tear: the frame is cut into horizontal pieces which flicker
  // sideways on their own on/off periods (effects.rpy's Tear displayable).
  const int pieces = std::clamp(effect_param_ & 0xff, 1, 24);
  const int max_offset = std::clamp((effect_param_ >> 8) & 0xff, 1, 160);
  uint32_t rng = effect_seed_;
  auto next = [&rng]() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
  };
  int cuts[26];
  cuts[0] = 0;
  for (int i = 1; i <= pieces; ++i)
    cuts[i] = 10 + static_cast<int>(next() % (screen_h - 20));
  cuts[pieces + 1] = screen_h;
  std::sort(cuts, cuts + pieces + 2);
  static uint16_t row_copy[screen_w];
  for (int piece = 0; piece <= pieces; ++piece) {
    const uint32_t off_time = 200 + next() % 200, on_time = 200 + next() % 200;
    const uint32_t phase = (effect_elapsed_ + next() % 400) % (off_time + on_time);
    const int offset = static_cast<int>(next() % (2 * max_offset + 1)) - max_offset;
    if (phase <= off_time || !offset)
      continue;
    for (int y = cuts[piece]; y < cuts[piece + 1] && y < screen_h; ++y) {
      uint16_t* row = dst + y * screen_w;
      std::memcpy(row_copy, row, sizeof(row_copy));
      for (int x = 0; x < screen_w; ++x) {
        const int source = x - offset;
        row[x] = source >= 0 && source < screen_w ? row_copy[source] : 0;
      }
    }
  }
}

int Player::word_menu_page(size_t menu_event) const {
  if (!ddlc_ui_ || menu_event >= pack_.events().size()) return 0;
  const auto& menu = pack_.events()[menu_event];
  if (menu.op != Op::choice || menu.x != 15 || menu.resource != 10) return 0;
  const std::string prompt(pack_.text(menu.text));
  int page = 0;
  return std::sscanf(prompt.c_str(), "Choose a word (%d/20).", &page) == 1 &&
                 page >= 1 && page <= 20 ? page : 0;
}
void Player::prepare_word_board(size_t menu_event) {
  if (!poem_seed_ || !word_menu_page(menu_event)) return;
  size_t base = menu_event;
  while (base && word_menu_page(base) != 1) --base;
  if (word_board_base_ == base) return;
  word_board_base_ = static_cast<uint32_t>(base);
  uint32_t rng = poem_seed_ ^ (static_cast<uint32_t>(base) * 0x9e3779b9u);
  if (!rng) rng = 0x6d2b79f5u;
  for (unsigned i=0; i<word_board_.size(); ++i) word_board_[i] = i;
  for (unsigned i=word_board_.size()-1; i>0; --i) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    std::swap(word_board_[i], word_board_[rng % (i+1)]);
  }
}
std::string_view Player::word_choice_text(uint8_t option) const {
  if (option >= choice_count_ || choice_first_ + option >= pack_.events().size()) return {};
  const int page = choice_first_ ? word_menu_page(choice_first_ - 1) : 0;
  if (page && poem_seed_)
    return pack_.poem_words()[word_board_[(page-1)*10+option]].text;
  return pack_.text(pack_.events()[choice_first_ + option].text);
}
bool Player::apply_random_word(uint8_t option, size_t target) {
  const int page = choice_first_ ? word_menu_page(choice_first_ - 1) : 0;
  if (!poem_seed_ || !page || option >= 10 || target+3 >= pack_.events().size()) return false;
  // Existing handlers are three score additions and a jump to the next board
  // (or the original winner/bonus logic). Replace only those score additions.
  for (unsigned i=0; i<3; ++i) {
    const auto& add = pack_.events()[target+i];
    if (add.op != Op::add_state || add.resource != 3+i) return false;
  }
  const auto& jump = pack_.events()[target+3];
  if (jump.op != Op::jump || branch_target(jump) >= pack_.events().size()) return false;
  const auto& word = pack_.poem_words()[word_board_[(page-1)*10+option]];
  for (unsigned i=0; i<3; ++i) story_state_[3+i] += word.points[i];
  cursor_ = branch_target(jump);
  return true;
}
} // namespace nzato
