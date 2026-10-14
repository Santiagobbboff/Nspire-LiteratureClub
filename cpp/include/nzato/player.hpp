#pragma once
#include "nzato/pack.hpp"
#include "nzato/video.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nzato {
constexpr int screen_w = 320, screen_h = 240;
// Three poem rounds plus poem sharing and story menus exceed 64 selections.
// NZS3 stores a variable-length list with an 8-bit count, so 128 keeps its format.
constexpr size_t story_choice_capacity = 128;
enum class Language { english, chinese };
class Player {
public:
  enum class BackgroundMode : uint8_t { original, stretch, crop };
  BackgroundMode background_mode() const { return background_mode_; }
  bool settings_open() const { return settings_open_; }
  bool startup_open() const { return startup_phase_ != StartupPhase::none; }
  explicit Player(const Pack& pack, bool play_intro = true, uint16_t* video_framebuffer = nullptr);
  void reset();
  bool advance();
  bool fast_forward();
  bool set_skip_indicator(bool active) {
    const bool changed = skip_indicator_ != active;
    skip_indicator_ = active;
    return changed;
  }
  bool tick(uint32_t elapsed_ms);
  bool video_playing() const {
    return video_active_;
  }
  bool credits_open() const {
    return info_open_ && info_page_ == 1;
  }
  void toggle_language();
  void toggle_history();
  void open_save();
  void open_load();
  void open_quick_quit();
  bool debug_jump(int direction);
  bool debug_unlock_gallery();
  bool shaking() const {
    return shake_left_ != 0;
  }
  std::string_view current_text() const {
    return text_;
  }
  std::string_view nvl_text() const {
    return nvl_text_;
  }
  bool text_fully_revealed() const {
    return reveal_bytes_ >= text_.size();
  }
  void toggle_ui();
  void toggle_auto();
  void input_up(bool fresh_press = true);
  void input_down();
  void input_left();
  void input_right();
  bool input_confirm();
  bool input_escape();
  void render(uint16_t* framebuffer) const;
  // A complete frame that backends can present without another composition.
  // nullptr means render() is required, including during transitions.
  const uint16_t* presentation_framebuffer() const;
  bool ended() const {
    return ended_ && !credits_roll_active_;
  }
  bool history_open() const {
    return history_open_;
  }
  bool title_open() const {
    return title_active_;
  }
  bool name_entry_open() const {
    return name_entry_open_;
  }
  bool poem_open() const { return poem_open_; }
  int poem_scroll() const { return poem_scroll_px_; }
  int poem_scroll_max() const { return poem_scroll_max_; }
  bool input_character(char value);
  bool input_backspace();
  bool navigation_open() const {
    return poem_open_ || title_active_ || name_entry_open_ || gallery_open_ || history_open_ || save_open_ || load_open_ ||
           info_open_ || confirm_open_ || choice_open_;
  }
  bool screen_transition_first_frame_pending() const {
    return screen_fade_left_ > rendered_screen_fade_left_;
  }
  bool file_open() const {
    return save_open_ || load_open_;
  }
  int file_page() const {
    return file_page_;
  }
  int file_selection() const {
    return file_selection_;
  }
  int gallery_selection() const {
    return gallery_selection_;
  }
  bool skip_allowed() const {
    return !startup_open() && !cutscene_locked_ && !poem_open_ && !video_active_ && !title_active_ && !name_entry_open_ && !gallery_open_ && !history_open_ && !save_open_ &&
           !load_open_ && !info_open_ && !confirm_open_ && !choice_open_ && !ended_;
  }
  bool interactive_game() const {
    return !poem_open_ && !video_active_ && !title_active_ && !name_entry_open_ && !gallery_open_ && !history_open_ && !save_open_ &&
           !load_open_ && !info_open_ && !confirm_open_ && !choice_open_ && !waiting_;
  }
  bool quit_requested() const {
    return quit_requested_;
  }
  bool rolling_credits() const { return credits_roll_active_; }
  bool confirm_open() const {
    return confirm_open_;
  }
  bool auto_playing() const {
    return auto_play_;
  }
  Language language() const {
    return language_;
  }
  size_t event_index() const {
    return cursor_;
  }
  bool resource_visible(uint16_t id) const;
  bool resource_shaking(uint16_t id) const;
  bool pixel_scroll_active() const {
    return poem_open_ || ((history_open_ || info_open_) && history_focus_ != 0) || dialogue_scroll_max_ > 0 ||
           nvl_scroll_max_ > 0;
  }
  bool overlay_visible() const {
    return !overlay_text_.empty() && overlay_opacity_ != 0;
  }
  int16_t persistent_value(PersistentKey key) const {
    return persistent_values_[static_cast<size_t>(key)];
  }
  bool effect_active() const { return effect_kind_ != 0; }

private:
  enum class StartupPhase : uint8_t { none, notice, fade_to_white, fade_to_menu };
  StartupPhase startup_phase_{StartupPhase::none};
  bool notice_seen_{};
  uint32_t startup_elapsed_ms_{};
  static constexpr uint32_t startup_fade_ms = 1200;
  bool cutscene_locked_{};
  // Act 2 persistent values (DDGP "DDP3" section) and screen effects.
  std::array<int16_t, persistent_value_count> persistent_values_{};
  uint8_t effect_kind_{};
  uint32_t effect_left_{}, effect_elapsed_{}, effect_seed_{};
  int effect_param_{};
  static constexpr uint32_t wait_for_input = 0xffffffffu;
  std::string expand_glitch(std::string_view text, uint32_t seed) const;
  void screen_effects(uint16_t* dst) const;
  void delete_all_saves();
  void write_character_files() const;
  void delete_character_file(size_t index) const;
  bool credits_roll_active_{};
  uint32_t credits_roll_ms_{};
  static constexpr uint32_t credits_delay_ms = 5000;
  static constexpr uint32_t credits_scroll_ms = 30000;
  static constexpr uint32_t credits_hold_ms = 5000;
  void capture_video_frame();
  enum class NavAction : uint8_t {
    start,
    log,
    save,
    load,
    gallery,
    title,
    credits,
    controls,
    quit,
    return_to_game
  };
  struct Layer {
    const Image* image{};
    int x{}, y{}, opacity{255}, target_opacity{255};
    uint32_t transition_left{};
    bool remove_when_done{};
    int move_from_x{}, move_to_x{};
    uint32_t move_total{};
    bool shaking{};
    bool below_particles{};
    uint16_t resource{};
    uint32_t animation_left{};
    int scale{1024}, scale_from{1024}, scale_to{1024};
    uint32_t scale_left{}, scale_total{};
    int pivot_x{}, pivot_y{};
    uint8_t blend_mode{};
    bool xflip{};
  };
  struct HistoryEntry {
    std::string speaker, text;
  };
  const Pack& pack_;
  const bool ddlc_ui_;
  BackgroundMode background_mode_{BackgroundMode::original};
  bool settings_open_{};
  int settings_selection_{};
  bool play_intro_{true};
  uint16_t* video_framebuffer_{};
  Language language_ = Language::english;
  size_t cursor_{};
  bool ended_{};
  const Image* background_{};
  const Image* old_background_{};
  int background_y_{}, old_background_y_{};
  const Image* previous_background_{};
  uint16_t background_resource_{};
  uint32_t background_animation_left_{};
  int background_opacity_{255};
  uint32_t background_transition_left_{};
  const Image* face_{};
  const Image* poem_paper_{};
  bool poem_open_{};
  std::string poem_text_;
  int poem_scroll_px_{}, poem_scroll_max_{}, poem_font_size_{14}, poem_line_height_{19};
  const Image* video_frame_{};
  const Image* title_background_{};
  const Image* title_menu_overlay_{};
  const Image* history_background_{};
  const Image* history_overlay_{};
  const Image* snow_images_[2]{};
  bool snow_up_[2]{};
  const Image* title_snow_images_[2]{};
  struct Particle {
    int x{}, y{}, speed{}, kind{};
  };
  std::array<Particle, 16> particles_{};
  uint32_t particle_rng_{0x7a7a25u};
  mutable std::vector<uint16_t> last_frame_;
  mutable std::vector<uint16_t> stage_cache_;
  mutable std::vector<uint16_t> movement_base_cache_;
  mutable std::vector<uint16_t> transition_old_cache_;
  mutable std::vector<uint16_t> transition_new_cache_;
  mutable std::vector<uint16_t> menu_chrome_cache_;
  mutable std::vector<uint16_t> title_base_cache_;
  mutable std::vector<uint16_t> title_static_cache_;
  mutable std::vector<uint16_t> dialogue_ui_cache_;
  mutable std::vector<uint16_t> scroll_text_cache_;
  mutable std::vector<uint16_t> game_thumbnail_;
  mutable bool last_frame_valid_{};
  mutable bool framebuffer_valid_{};
  mutable uint32_t rendered_screen_fade_left_{};
  mutable bool stage_cache_valid_{};
  mutable bool movement_base_cache_valid_{};
  mutable bool transition_cache_valid_{};
  mutable bool menu_chrome_cache_valid_{};
  mutable bool title_base_cache_valid_{};
  mutable bool dialogue_ui_cache_valid_{};
  mutable uint64_t stage_cache_signature_{};
  mutable uint64_t movement_base_cache_signature_{};
  mutable uint64_t dialogue_ui_cache_signature_{};
  NvpVideo video_{};
  uint32_t video_left_{};
  uint32_t wait_left_{};
  uint32_t screen_fade_left_{};
  bool video_active_{}, title_active_{}, simple_title_{}, name_entry_open_{}, gallery_open_{}, gallery_view_{}, history_open_{},
      save_open_{}, load_open_{}, info_open_{}, confirm_open_{}, waiting_{}, inline_wait_{};
  bool window_visible_{true}, manual_ui_hidden_{}, pause_ui_hidden_{}, centered_{},
      centered_fading_out_{}, centered_fade_{true}, centered_no_wrap_{}, transition_gate_{},
      replaying_{}, quit_requested_{};
  bool nvl_mode_{};
  std::string nvl_text_;
  int title_selection_{}, history_selection_{}, history_scroll_px_{}, info_page_{},
      history_focus_{1}, file_selection_{}, file_page_{2}, file_region_{}, confirm_selection_{},
      confirm_action_{}, pending_slot_{-1};
  mutable int info_scroll_px_{};
  bool file_return_title_{};
  uint32_t poem_seed_{};
  uint32_t word_board_base_{0xffffffffu};
  std::array<uint16_t, poem_word_count> word_board_{};
  int word_menu_page(size_t menu_event) const;
  void prepare_word_board(size_t menu_event);
  std::string_view word_choice_text(uint8_t option) const;
  bool apply_random_word(uint8_t option, size_t target);
  bool choice_open_{};
  size_t choice_first_{};
  uint16_t choice_count_{};
  uint8_t choice_selection_{};
  std::array<int16_t, story_state_slots> story_state_{};
  uint16_t gallery_base_{};
  int gallery_selection_{}, gallery_variant_{};
  uint64_t gallery_unlocks_{};
  std::array<Layer, 12> layers_{};
  std::array<Layer, 12> old_layers_{};
  uint32_t stage_transition_left_{}, stage_transition_total_{};
  uint32_t shake_left_{};
  bool shake_horizontal_{};
  uint8_t stage_transition_kind_{};
  int8_t stage_transition_direction_{};
  uint32_t window_transition_left_{}, window_transition_total_{}, centered_transition_left_{},
      particle_accumulator_{}, animation_ms_{};
  uint8_t window_opacity_{255}, window_from_opacity_{255}, window_target_opacity_{255},
      centered_opacity_{255};
  std::string speaker_, text_;
  std::string player_name_ = "MC";
  std::string overlay_text_;
  bool overlay_positioned_{};
  int overlay_x_{}, overlay_y_{};
  uint8_t overlay_opacity_{}, overlay_target_opacity_{};
  uint32_t overlay_transition_left_{}, overlay_transition_total_{};
  std::vector<HistoryEntry> history_;
  uint32_t history_revision_{};
  mutable uint32_t history_layout_revision_{UINT32_MAX};
  mutable std::array<int, 64> history_heights_{};
  mutable int history_content_height_{};
  mutable int credits_content_height_{};
  mutable uint32_t scroll_cache_revision_{UINT32_MAX};
  mutable int scroll_cache_height_{}, scroll_cache_origin_{}, scroll_cache_content_height_{};
  mutable uint8_t scroll_cache_kind_{};
  size_t reveal_bytes_{};
  size_t wait_tag_end_{};
  int font_size_{11}; // Initialized from ui_metrics::prose_size after loading.
  mutable int dialogue_scroll_px_{};
  int dialogue_scroll_max_{};
  mutable bool dialogue_scroll_follow_{true};
  mutable int nvl_scroll_px_{};
  int nvl_scroll_max_{};
  mutable bool nvl_scroll_follow_{true};
  uint32_t reveal_accumulator_{}, inline_wait_left_{}, active_cps_{};
  bool auto_advance_text_{}, auto_play_{}, skip_indicator_{};
  uint32_t auto_play_left_{};
  struct SaveRecord {
    bool valid{};
    uint32_t poem_seed{};
    size_t cursor{};
    std::vector<uint16_t> thumbnail;
    std::array<int16_t, story_state_slots> story_state{};
    struct Choice { uint32_t menu_event{}; uint8_t option{}; };
    std::array<Choice, story_choice_capacity> choices{};
    uint8_t choice_count{};
    std::string player_name;
  };
  static constexpr int file_pages = 4, slots_per_page = 6;
  std::array<SaveRecord, file_pages * slots_per_page> saves_{};
  using ChoiceRecord = SaveRecord::Choice;
  std::array<ChoiceRecord, story_choice_capacity> choice_history_{};
  uint8_t choice_history_count_{};
  bool save_index_loaded_{};
  void apply(const Event& event);
  void advance_automatic();
  void finish_transitions();
  void finish_story();
  void release_navigation_caches();
  void release_save_cache();
  void release_title_caches();
  void start_new_game();
  bool save_slot(int slot);
  bool load_slot(int slot);
  bool restore_to(size_t target, int save_slot = -1);
  void load_save_index();
  void load_persistent();
  void save_persistent() const;
  void open_confirm(int action);
  int sidebar_count() const;
  NavAction sidebar_action(int index) const;
  bool activate_sidebar();
  std::string_view nav_text(NavAction action) const;
  void ensure_title_cache() const;
  void render_menu_chrome(uint16_t* framebuffer) const;
  void render_ddlc_title(uint16_t* framebuffer) const;
  void render_menu_text(uint16_t* dst, std::string_view text, int x, int y, int width,
                        int size, bool sidebar = false, int top = 0, int bottom = 240) const;
  void render_menu_surface_text(uint16_t* dst, int height, std::string_view text, int x, int y,
                                int width, int leading, int size, int top, int bottom) const;
  void render_sidebar(uint16_t* framebuffer, NavAction active) const;
  void render_info_page(uint16_t* framebuffer) const;
  void render_scrollbar(uint16_t* framebuffer, int top, int bottom, int content_height,
                        int scroll_from_top, bool focused) const;
  void composite_scroll_cache(uint16_t* framebuffer, int source_top, int top, int bottom, int left,
                              int right) const;
  static uint32_t transition_ms(uint8_t flags);
  void pin_active_images(const uint16_t* extra = nullptr, size_t extra_count = 0) const;
  void materialize_active_images();
};
} // namespace nzato
