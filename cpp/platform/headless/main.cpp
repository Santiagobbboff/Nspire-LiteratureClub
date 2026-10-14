// Headless Linux driver for automated playthrough tests. Reads one command per
// line from stdin and writes PPM frames on request; no original game content is
// embedded here.
#include "nzato/player.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: ddlc-headless <pack>\n");
    return 1;
  }
  nzato::Pack pack;
  if (!pack.load(argv[1])) {
    std::fprintf(stderr, "pack error: %s\n", pack.error());
    return 2;
  }
  nzato::Player player(pack, false);
  std::array<uint16_t, 320 * 240> pixels{};
  char line[512];
  while (std::fgets(line, sizeof(line), stdin)) {
    int value = 0;
    char path[400];
    if (std::sscanf(line, "tick %d", &value) == 1)
      player.tick(static_cast<uint32_t>(value));
    else if (std::sscanf(line, "char %d", &value) == 1) {
      if (player.name_entry_open()) player.input_character(static_cast<char>(value));
    } else if (std::sscanf(line, "shot %399s", path) == 1) {
      player.render(pixels.data());
      FILE* out = std::fopen(path, "wb");
      if (!out) continue;
      std::fprintf(out, "P6 320 240 255\n");
      for (uint16_t p : pixels) {
        unsigned char rgb[3] = {static_cast<unsigned char>(((p >> 11) & 31) * 255 / 31),
                                static_cast<unsigned char>(((p >> 5) & 63) * 255 / 63),
                                static_cast<unsigned char>((p & 31) * 255 / 31)};
        std::fwrite(rgb, 1, 3, out);
      }
      std::fclose(out);
    } else if (line[0] == 'e') player.input_confirm();
    else if (line[0] == 'x') player.input_escape();
    else if (line[0] == 'u') player.input_up();
    else if (line[0] == 'd') player.input_down();
    else if (line[0] == 'l') player.input_left();
    else if (line[0] == 'r') player.input_right();
    else if (line[0] == 'b') player.input_backspace();
    else if (line[0] == 'f') player.fast_forward();
    else if (line[0] == 'q') break;
    else if (line[0] == 's' && line[1] == 't') {
      // state: cursor, flags, current speaker/text
      std::string text(player.current_text());
      for (char& c : text) if (c == '\n') c = ' ';
      std::printf("cursor=%zu title=%d name=%d choice=%d poem=%d ended=%d quit=%d nav=%d ia=%d sk=%d fx=%d text=%s\n",
                  player.event_index(), player.title_open(), player.name_entry_open(),
                  player.navigation_open() && !player.title_open() && !player.poem_open() &&
                      !player.name_entry_open(),
                  player.poem_open(), player.ended(), player.quit_requested(),
                  player.navigation_open(), player.interactive_game(), player.skip_allowed(),
                  player.effect_active(), text.c_str());
      std::fflush(stdout);
    }
  }
  return 0;
}
