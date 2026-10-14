#include "nzato/player.hpp"
#include <algorithm>

namespace nzato {
namespace {
int wrap_previous(int value, int count) {
  return value > 0 ? value - 1 : count - 1;
}

int wrap_next(int value, int count) {
  return value + 1 < count ? value + 1 : 0;
}

bool gallery_group_unlocked(uint64_t bits, int group) {
  static constexpr uint64_t masks[12] = {
      0x7ULL,    0x18ULL,    0x20ULL,       0x1c0ULL,      0x200ULL,       0xc00ULL,
      0x1000ULL, 0x1e000ULL, 0x0ffe0000ULL, 0x10000000ULL, 0x7e0000000ULL, 0x800000000ULL};
  const uint64_t mask = masks[std::clamp(group, 0, 11)];
  const bool requires_all = group == 1 || group == 7 || group == 8 || group == 10;
  return requires_all ? (bits & mask) == mask : (bits & mask) != 0;
}
} // namespace

void Player::toggle_history() {
  if (!video_active_ && !title_active_ && !choice_open_ && !ended_) {
    history_open_ = !history_open_;
    save_open_ = load_open_ = info_open_ = false;
    history_focus_ = 1;
    history_selection_ = 0;
    history_scroll_px_ = 0;
    screen_fade_left_ = 240;
    if (!history_open_)
      release_navigation_caches();
  }
}

void Player::open_save() {
  // Ren'Py's game menu remains accessible during timed pauses. `waiting_`
  // stops story advancement but must not consume Escape.
  if (!video_active_ && !title_active_ && !choice_open_ && !history_open_ && !save_open_ && !load_open_ &&
      !info_open_ && !confirm_open_ && !ended_) {
    load_save_index();
    save_open_ = true;
    history_open_ = load_open_ = info_open_ = false;
    file_return_title_ = false;
    file_page_ = 0;
    file_selection_ = 0;
    file_region_ = 0;
    history_focus_ = 1;
    history_selection_ = 1;
    screen_fade_left_ = 240;
  }
}
void Player::open_load() {
  if (startup_open()) return;
  if (credits_roll_active_) return;
  if (settings_open_) return;
  if (!video_active_ && !waiting_) {
    load_save_index();
    file_return_title_ = title_active_;
    title_active_ = false;
    load_open_ = true;
    history_open_ = save_open_ = info_open_ = false;
    file_page_ = 0;
    file_selection_ = 0;
    file_region_ = 0;
    history_focus_ = 1;
    history_selection_ = file_return_title_ ? 1 : 2;
    screen_fade_left_ = 240;
  }
}
void Player::toggle_ui() {
  if (interactive_game())
    manual_ui_hidden_ = !manual_ui_hidden_;
}
void Player::toggle_auto() {
  if (cutscene_locked_) return;
  if (interactive_game()) {
    auto_play_ = !auto_play_;
    auto_play_left_ = auto_play_ ? 900 : 0;
  }
}
void Player::open_confirm(int action) {
  confirm_open_ = true;
  confirm_action_ = action;
  confirm_selection_ = 1;
  screen_fade_left_ = 0;
}

void Player::open_quick_quit() {
  if (startup_open()) { quit_requested_ = true; return; }
  // The physical On key is an in-game emergency pause/quit shortcut. Timed
  // pauses are still part of gameplay, so unlike interactive_game() this does
  // not reject waiting_.
  if (!simple_title_ && !video_active_ && !confirm_open_ && !ended_) {
    open_confirm(2);
    confirm_selection_ = 0;
  }
}

int Player::sidebar_count() const {
  return file_return_title_ ? (ddlc_ui_ ? 5 : 6) : (ddlc_ui_ ? 7 : 8);
}
Player::NavAction Player::sidebar_action(int index) const {
  if (ddlc_ui_) {
    static constexpr NavAction title[] = {NavAction::start, NavAction::load, NavAction::controls,
                                          NavAction::quit, NavAction::return_to_game};
    static constexpr NavAction game[] = {NavAction::log, NavAction::save, NavAction::load,
                                         NavAction::title, NavAction::controls, NavAction::quit,
                                         NavAction::return_to_game};
    return file_return_title_ ? title[std::clamp(index,0,4)] : game[std::clamp(index,0,6)];
  }
  static constexpr NavAction title_items[] = {NavAction::start,   NavAction::load,
                                              NavAction::credits, NavAction::controls,
                                              NavAction::quit,    NavAction::return_to_game};
  static constexpr NavAction game_items[] = {
      NavAction::log,     NavAction::save,     NavAction::load, NavAction::title,
      NavAction::credits, NavAction::controls, NavAction::quit, NavAction::return_to_game};
  return file_return_title_ ? title_items[std::clamp(index, 0, 5)]
                            : game_items[std::clamp(index, 0, 7)];
}
bool Player::activate_sidebar() {
  switch (sidebar_action(history_selection_)) {
  case NavAction::start:
    start_new_game();
    screen_fade_left_ = 240;
    break;
  case NavAction::log:
    history_open_ = true;
    save_open_ = load_open_ = info_open_ = false;
    history_focus_ = 1;
    break;
  case NavAction::save:
    load_save_index();
    save_open_ = true;
    load_open_ = history_open_ = info_open_ = false;
    file_page_ = file_selection_ = file_region_ = 0;
    history_focus_ = 1;
    break;
  case NavAction::load:
    load_save_index();
    load_open_ = true;
    save_open_ = history_open_ = info_open_ = false;
    file_page_ = file_selection_ = file_region_ = 0;
    history_focus_ = 1;
    break;
  case NavAction::gallery:
    gallery_open_ = true;
    gallery_view_ = false;
    title_active_ = history_open_ = save_open_ = load_open_ = info_open_ = false;
    break;
  case NavAction::title:
    open_confirm(1);
    break;
  case NavAction::credits:
    info_open_ = true;
    info_page_ = 1;
    history_open_ = save_open_ = load_open_ = false;
    info_scroll_px_ = 0;
    history_focus_ = 1;
    break;
  case NavAction::controls:
    info_open_ = true;
    info_page_ = 2;
    history_open_ = save_open_ = load_open_ = false;
    info_scroll_px_ = 0;
    history_focus_ = 1;
    break;
  case NavAction::quit:
    open_confirm(2);
    break;
  case NavAction::return_to_game:
    return input_escape();
  }
  screen_fade_left_ = confirm_open_ ? 0 : 240;
  return true;
}

void Player::input_up(bool fresh_press) {
  if (startup_open()) return;
  if (settings_open_) {
    settings_selection_ = wrap_previous(settings_selection_, 4);
    return;
  }
  if (name_entry_open_)
    return;
  if (confirm_open_)
    return;
  if (poem_open_ && !save_open_ && !load_open_ && !history_open_ && !info_open_) {
    poem_scroll_px_ = std::max(0, poem_scroll_px_ - 4);
    return;
  }
  if (choice_open_) {
    if (choice_count_) {
      if (choice_first_ && word_menu_page(choice_first_ - 1))
        choice_selection_ = static_cast<uint8_t>((choice_selection_ / 5) * 5 + (choice_selection_ % 5 + 4) % 5);
      else choice_selection_ = static_cast<uint8_t>(choice_selection_ ? choice_selection_ - 1 : choice_count_ - 1);
    }
  } else if (gallery_open_) {
    if (!gallery_view_) {
      if (gallery_selection_ == 12)
        gallery_selection_ = 11;
      else if (gallery_selection_ >= 4)
        gallery_selection_ -= 4;
    }
  } else if (save_open_ || load_open_) {
    if (!history_focus_)
      history_selection_ = wrap_previous(history_selection_, sidebar_count());
    else if (file_region_) {
      file_region_ = 0;
      file_selection_ = std::max(3, file_selection_);
    } else if (file_selection_ >= 3)
      file_selection_ -= 3;
  } else if (title_active_)
    title_selection_ = wrap_previous(title_selection_, simple_title_ ? 4 : 6);
  else if (info_open_) {
    if (history_focus_)
      info_scroll_px_ = std::max(0, info_scroll_px_ - 2);
    else
      history_selection_ = wrap_previous(history_selection_, sidebar_count());
  } else if (history_open_) {
    if (history_focus_)
      history_scroll_px_ += 2;
    else
      history_selection_ = wrap_previous(history_selection_, sidebar_count());
  } else if (dialogue_scroll_max_ > 0 && dialogue_scroll_px_ > 0) {
    dialogue_scroll_follow_ = false;
    dialogue_scroll_px_ = std::max(0, dialogue_scroll_px_ - 2);
  } else if (nvl_scroll_max_ > 0 && nvl_scroll_px_ > 0) {
    nvl_scroll_follow_ = false;
    nvl_scroll_px_ = std::max(0, nvl_scroll_px_ - 2);
  } else if (fresh_press)
    toggle_history();
}
void Player::input_down() {
  if (startup_open()) return;
  if (settings_open_) {
    settings_selection_ = wrap_next(settings_selection_, 4);
    return;
  }
  if (name_entry_open_)
    return;
  if (confirm_open_)
    return;
  if (poem_open_ && !save_open_ && !load_open_ && !history_open_ && !info_open_) {
    poem_scroll_px_ = std::min(poem_scroll_max_, poem_scroll_px_ + 4);
    return;
  }
  if (choice_open_) {
    if (choice_count_)
      choice_selection_ = static_cast<uint8_t>(choice_first_ && word_menu_page(choice_first_ - 1)
          ? (choice_selection_ / 5) * 5 + (choice_selection_ % 5 + 1) % 5
          : (choice_selection_ + 1) % choice_count_);
  } else if (gallery_open_) {
    if (!gallery_view_) {
      if (gallery_selection_ >= 8)
        gallery_selection_ = 12;
      else
        gallery_selection_ += 4;
    }
  } else if (save_open_ || load_open_) {
    if (!history_focus_)
      history_selection_ = wrap_next(history_selection_, sidebar_count());
    else if (!file_region_ && file_selection_ < 3)
      file_selection_ += 3;
    else
      file_region_ = 1;
  } else if (title_active_)
    title_selection_ = wrap_next(title_selection_, simple_title_ ? 4 : 6);
  else if (info_open_) {
    if (history_focus_)
      info_scroll_px_ += 2;
    else
      history_selection_ = wrap_next(history_selection_, sidebar_count());
  } else if (history_open_) {
    if (history_focus_)
      history_scroll_px_ = std::max(0, history_scroll_px_ - 2);
    else
      history_selection_ = wrap_next(history_selection_, sidebar_count());
  } else if (dialogue_scroll_max_ > 0) {
    dialogue_scroll_follow_ = false;
    dialogue_scroll_px_ = std::min(dialogue_scroll_max_, dialogue_scroll_px_ + 2);
  } else if (nvl_scroll_max_ > 0) {
    nvl_scroll_follow_ = false;
    nvl_scroll_px_ = std::min(nvl_scroll_max_, nvl_scroll_px_ + 2);
  }
}
void Player::input_left() {
  if (startup_open()) return;
  if (name_entry_open_)
    return;
  if (poem_open_ && !confirm_open_ && !save_open_ && !load_open_ && !history_open_ && !info_open_) {
    poem_scroll_px_ = std::max(0, poem_scroll_px_ - 176);
    return;
  }
  if (confirm_open_)
    confirm_selection_ = 0;
  else if (choice_open_ && choice_first_ && word_menu_page(choice_first_ - 1)) {
    if (choice_selection_ >= 5) choice_selection_ -= 5;
  } else if (gallery_open_) {
    if (gallery_view_) {
      gallery_variant_ = std::max(0, gallery_variant_ - 1);
      screen_fade_left_ = 180;
    } else if (gallery_selection_ % 4)
      --gallery_selection_;
  } else if (save_open_ || load_open_) {
    if (!history_focus_)
      return;
    if (file_region_)
      file_page_ = (file_page_ + file_pages - 1) % file_pages;
    else if (file_selection_ % 3)
      file_selection_--;
    else
      history_focus_ = 0;
  } else if (history_open_ || info_open_)
    history_focus_ = 0;
}
void Player::input_right() {
  if (startup_open()) return;
  if (name_entry_open_)
    return;
  if (poem_open_ && !confirm_open_ && !save_open_ && !load_open_ && !history_open_ && !info_open_) {
    poem_scroll_px_ = std::min(poem_scroll_max_, poem_scroll_px_ + 176);
    return;
  }
  if (confirm_open_)
    confirm_selection_ = 1;
  else if (choice_open_ && choice_first_ && word_menu_page(choice_first_ - 1)) {
    if (choice_selection_ < 5) choice_selection_ += 5;
  } else if (gallery_open_) {
    if (gallery_view_) {
      ++gallery_variant_;
      screen_fade_left_ = 180;
    } else if (gallery_selection_ % 4 < 3)
      ++gallery_selection_;
  } else if (save_open_ || load_open_) {
    if (!history_focus_) {
      history_focus_ = 1;
      return;
    }
    if (file_region_)
      file_page_ = (file_page_ + 1) % file_pages;
    else if (file_selection_ % 3 < 2)
      file_selection_++;
  } else if (history_open_ || info_open_)
    history_focus_ = 1;
}
bool Player::input_confirm() {
  if (startup_open()) {
    if (startup_phase_ == StartupPhase::notice) {
      notice_seen_ = true;
      save_persistent();
      startup_phase_ = StartupPhase::fade_to_white;
      startup_elapsed_ms_ = 0;
    }
    return true;
  }
  if (settings_open_) {
    if (settings_selection_ == 3) {
      settings_open_ = false;
      screen_fade_left_ = 240;
    } else {
      background_mode_ = static_cast<BackgroundMode>(settings_selection_);
      stage_cache_valid_ = movement_base_cache_valid_ = transition_cache_valid_ = false;
      dialogue_ui_cache_valid_ = false;
      save_persistent();
    }
    return true;
  }
  if (poem_open_ && !confirm_open_ && !save_open_ && !load_open_ && !history_open_ && !info_open_)
    return advance();
  if (name_entry_open_) {
    if (player_name_.empty())
      player_name_ = "MC";
    name_entry_open_ = false;
    start_new_game();
    screen_fade_left_ = 240;
    return true;
  }
  if (choice_open_) {
    if (!choice_count_ || choice_first_ + choice_selection_ >= pack_.events().size())
      return true;
    const Event& option = pack_.events()[choice_first_ + choice_selection_];
    if (option.op != Op::choice_option)
      return true;
    const size_t state_slot = choice_first_ ? choice_first_ - 1 : pack_.events().size();
    if (state_slot >= pack_.events().size())
      return true;
    const Event& menu = pack_.events()[state_slot];
    if (menu.op != Op::choice || menu.x < 0 || menu.x >= static_cast<int>(story_state_.size()))
      return true;
    story_state_[menu.x] = static_cast<int16_t>(option.resource);
    const uint32_t menu_event = static_cast<uint32_t>(choice_first_ - 1);
    bool recorded = false;
    for (uint8_t i = 0; i < choice_history_count_; ++i)
      if (choice_history_[i].menu_event == menu_event) {
        choice_history_[i].option = choice_selection_;
        recorded = true;
        break;
      }
    if (!recorded && choice_history_count_ < choice_history_.size())
      choice_history_[choice_history_count_++] = {menu_event, choice_selection_};
    const size_t target = branch_target(option);
    if (target >= pack_.events().size())
      return true;
    choice_open_ = false;
    choice_count_ = 0;
    if (!apply_random_word(choice_selection_, target)) cursor_ = target;
    window_visible_ = false;
    text_.clear();
    speaker_.clear();
    reveal_bytes_ = 0;
    return advance();
  }
  if (confirm_open_) {
    const int action = confirm_action_, slot = pending_slot_;
    const bool yes = confirm_selection_ == 0;
    confirm_open_ = false;
    pending_slot_ = -1;
    if (!yes) {
      if (!save_open_ && !load_open_ && (action == 4 || action == 5))
        release_save_cache();
      return true;
    }
    if (action == 1) {
      history_open_ = save_open_ = load_open_ = info_open_ = false;
      title_active_ = true;
      history_selection_ = 0;
      screen_fade_left_ = 240;
      release_navigation_caches();
    } else if (action == 2)
      quit_requested_ = true;
    else if (action == 3)
      save_slot(slot);
    else if (action == 4)
      load_slot(slot);
    else if (action == 5) {
      // Quick-save has its own confirmation. If all quick slots were already
      // occupied, follow it with the normal overwrite confirmation as a
      // separate step instead of silently replacing data.
      if (slot >= 0 && slot < static_cast<int>(saves_.size()) && saves_[slot].valid) {
        pending_slot_ = slot;
        open_confirm(3);
      } else
        save_slot(slot);
    }
    return !quit_requested_;
  }
  // Confirm/advance is also the intuitive way to interrupt automatic play.
  // Consume this press so it cannot both stop auto and advance the statement.
  if (auto_play_ && skip_allowed()) {
    auto_play_ = false;
    auto_play_left_ = 0;
    return true;
  }
  if (gallery_open_) {
    if (!gallery_view_ && gallery_selection_ == 12) {
      gallery_open_ = false;
      title_active_ = true;
      file_return_title_ = false;
      screen_fade_left_ = 240;
      return true;
    }
    if (!gallery_view_ && gallery_group_unlocked(gallery_unlocks_, gallery_selection_)) {
      gallery_view_ = true;
      gallery_variant_ = 0;
      screen_fade_left_ = 200;
    } else if (gallery_view_) {
      ++gallery_variant_;
      screen_fade_left_ = 180;
    }
    return true;
  }
  if ((save_open_ || load_open_) && !history_focus_)
    return activate_sidebar();
  if ((save_open_ || load_open_) && file_region_) {
    file_region_ = 0;
    file_selection_ = std::max(3, file_selection_);
    return true;
  }
  const int slot = file_page_ * slots_per_page + file_selection_;
  if (save_open_) {
    if (saves_[slot].valid) {
      pending_slot_ = slot;
      open_confirm(3);
    } else
      save_slot(slot);
    return true;
  }
  if (load_open_) {
    if (saves_[slot].valid) {
      // Ren'Py FileLoad(confirm=True) asks whenever loading from an active
      // game, irrespective of whether the selected save is ahead of or behind
      // the current statement. Only the main-menu load screen skips it.
      if (!file_return_title_) {
        pending_slot_ = slot;
        open_confirm(4);
      } else
        load_slot(slot);
    }
    return true;
  }
  if (title_active_) {
    if (simple_title_) {
      if (title_selection_ == 1) {
        open_load();
      } else if (title_selection_ == 2) {
        settings_open_ = true;
        settings_selection_ = static_cast<int>(background_mode_);
        screen_fade_left_ = 240;
      } else if (title_selection_ == 3) {
        open_confirm(2);
      } else {
        player_name_.clear();
        name_entry_open_ = true;
        title_active_ = false;
      }
      return true;
    }
    if (title_selection_ == 1) {
      open_load();
      return true;
    }
    if (title_selection_ == 2) {
      file_return_title_ = true;
      title_active_ = false;
      gallery_open_ = true;
      gallery_view_ = false;
      gallery_selection_ = 0;
      screen_fade_left_ = 240;
      return true;
    }
    if (title_selection_ == 3 || title_selection_ == 4) {
      file_return_title_ = true;
      title_active_ = false;
      info_open_ = true;
      info_page_ = title_selection_ == 3 ? 1 : 2;
      // The title-side file/info sidebar intentionally omits Gallery, so its
      // Credits and Controls indices are one less than the main title menu.
      history_selection_ = title_selection_ - 1;
      history_focus_ = 1;
      screen_fade_left_ = 240;
      return true;
    }
    if (title_selection_ == 5) {
      open_confirm(2);
      return true;
    }
    start_new_game();
    screen_fade_left_ = 240;
    return true;
  }
  if (info_open_ && !history_focus_)
    return activate_sidebar();
  if (history_open_ && history_focus_ == 0)
    return activate_sidebar();
  return advance();
}
bool Player::input_escape() {
  if (startup_open()) {
    if (startup_phase_ == StartupPhase::notice) quit_requested_ = true;
    return true;
  }
  if (credits_roll_active_) return true;
  if (settings_open_) {
    settings_open_ = false;
    screen_fade_left_ = 240;
    return true;
  }
  // Escape is the platform-level "back" key, but while the opening movie is
  // active it has the same skip semantics as confirm/advance.  Handling it
  // before the menu cases also closes the decoder immediately instead of
  // leaving the key to open a screen that is unavailable during video.
  if (video_active_)
    return advance();
  if (name_entry_open_) {
    name_entry_open_ = false;
    title_active_ = true;
    title_selection_ = 0;
    return true;
  }
  if (choice_open_)
    return true;
  if (confirm_open_) {
    if (!save_open_ && !load_open_ && (confirm_action_ == 4 || confirm_action_ == 5))
      release_save_cache();
    confirm_open_ = false;
    return true;
  }
  if (simple_title_ && title_active_)
    return true;
  if (gallery_open_) {
    if (gallery_view_) {
      gallery_view_ = false;
      screen_fade_left_ = 200;
    } else {
      gallery_open_ = false;
      title_active_ = true;
      file_return_title_ = false;
      screen_fade_left_ = 240;
    }
    return true;
  }
  if (save_open_ || load_open_ || info_open_) {
    save_open_ = load_open_ = info_open_ = false;
    if (file_return_title_) {
      title_active_ = true;
      file_return_title_ = false;
    }
    screen_fade_left_ = 240;
    release_navigation_caches();
    return true;
  }
  if (history_open_) {
    history_open_ = false;
    if (file_return_title_) {
      title_active_ = true;
      file_return_title_ = false;
    }
    screen_fade_left_ = 240;
    release_navigation_caches();
    return true;
  }
  if (title_active_)
    return true;
  open_save();
  return true;
}

bool Player::input_character(char value) {
  if (!name_entry_open_ || player_name_.size() >= 16 || value < 32 || value > 126)
    return false;
  player_name_.push_back(value);
  return true;
}

bool Player::input_backspace() {
  if (!name_entry_open_ || player_name_.empty())
    return false;
  player_name_.pop_back();
  return true;
}

bool Player::debug_unlock_gallery() {
  constexpr uint64_t all_gallery_flags = (uint64_t{1} << 36) - 1;
  const bool changed = gallery_unlocks_ != all_gallery_flags;
  gallery_unlocks_ = all_gallery_flags;
  save_persistent();
  return changed;
}

} // namespace nzato
