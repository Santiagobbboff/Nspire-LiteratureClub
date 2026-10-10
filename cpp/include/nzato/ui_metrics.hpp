#pragma once

#include <cstdint>

namespace nzato::ui_metrics {

// All UI typography is derived from these language baselines. Page code uses
// semantic roles rather than embedding per-language pixel sizes.
constexpr int base_size(bool /*chinese*/) {
  return 11;
}
constexpr int menu_size(bool chinese) {
  // The Latin menu face is unusually wide and heavy at 11 px on a 320 px
  // display. Keep prose large while returning only navigation labels to the
  // original compact scale.
  return chinese ? 11 : 10;
}
constexpr int small_size(bool chinese) {
  return base_size(chinese) - 1;
}
constexpr int file_slot_size(bool chinese) {
  return chinese ? base_size(chinese) : small_size(chinese);
}
constexpr int credits_size(bool chinese) {
  return chinese ? small_size(chinese) : base_size(chinese);
}
constexpr int controls_size(bool chinese) {
  return chinese ? base_size(chinese) : base_size(chinese) + 1;
}
constexpr int prose_size(bool chinese) {
  // The Jinghua glyphs have a smaller apparent ink area than the Latin face
  // at the same nominal pixel size. Compensate once at the semantic baseline
  // so dialogue, NVL and centered story text remain visually comparable.
  (void)chinese;
  return 12;
}
constexpr int dialogue_name_size(bool chinese) {
  return prose_size(chinese);
}
constexpr int modal_size(bool chinese) {
  return base_size(chinese) + 1;
}
constexpr int page_title_size = 12;
constexpr int keycap_size = 14;
constexpr int keycap_fallback_size = keycap_size - 3;
constexpr int line_height(int font_size) {
  return font_size + 2;
}
constexpr int dialogue_left(bool ddlc) { return ddlc ? 18 : 50; }
constexpr int dialogue_top(bool ddlc) { return ddlc ? 172 : 176; }
constexpr int dialogue_bottom(bool ddlc) { return ddlc ? 231 : 235; }
constexpr int dialogue_width(bool ddlc, bool face) { return ddlc ? 284 : face ? 144 : 221; }

constexpr uint16_t white = 0xFFFF;
constexpr uint16_t orange = 0xDDEF;
constexpr uint16_t dim = 0x8410;

} // namespace nzato::ui_metrics
