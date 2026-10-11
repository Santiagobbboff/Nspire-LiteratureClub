#include "nzato/player.hpp"
#include <array>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
// Windows helper embedded in the single-file Python preview. All original
// game content stays in the external pack; stdout carries only RGB frames.
int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 1;
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  nzato::Pack pack;
  if (!pack.load(argv[1])) { std::fprintf(stderr, "%s\n", pack.error()); return 2; }
  nzato::Player player(pack, false);
  std::array<uint16_t, 320*240> pixels{};
  std::array<unsigned char, 320*240*3> rgb{};
  char cmd[64];
  while (std::fgets(cmd, sizeof(cmd), stdin)) {
    int value=0;
    if (std::sscanf(cmd, "tick %d", &value)==1) player.tick(value);
    else if (std::sscanf(cmd, "char %d", &value)==1) {
      if (player.name_entry_open()) player.input_character(static_cast<char>(value));
      else if (value=='h' || value=='H') player.toggle_ui();
      else if (value=='s' || value=='S') player.open_save();
      else if (value=='o' || value=='O') player.open_load();
    }
    else if (cmd[0]=='e') player.input_confirm();
    else if (cmd[0]=='x') player.input_escape();
    else if (cmd[0]=='u') player.input_up();
    else if (cmd[0]=='d') player.input_down();
    else if (cmd[0]=='l') player.input_left();
    else if (cmd[0]=='r') player.input_right();
    else if (cmd[0]=='b') player.input_backspace();
    else if (cmd[0]=='f') player.fast_forward();
    else if (cmd[0]=='q') break;
    else if (cmd[0]=='p') {
      if (player.quit_requested()) { std::fputc(1, stdout); std::fflush(stdout); break; }
      player.render(pixels.data());
      for (size_t i=0; i<pixels.size(); ++i) {
        auto p=pixels[i];
        rgb[i*3]=((p>>11)&31)*255/31;
        rgb[i*3+1]=((p>>5)&63)*255/63;
        rgb[i*3+2]=(p&31)*255/31;
      }
      std::fputc(0, stdout);
      std::fwrite(rgb.data(), 1, rgb.size(), stdout); std::fflush(stdout);
    }
  }
  return 0;
}
