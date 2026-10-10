#define WIN32_LEAN_AND_MEAN
#include "nzato/launcher_config.hpp"
#include "nzato/pack.hpp"
#include "nzato/player.hpp"
#include <array>
#include <cstdio>
#include <windows.h>
#include <shellapi.h>

static nzato::Pack pack;
static nzato::Player* player = nullptr;
static std::array<uint16_t, nzato::screen_w * nzato::screen_h> fb{};
static std::array<uint32_t, nzato::screen_w * nzato::screen_h> dib{};
static unsigned skip_elapsed = 0;
static ULONGLONG last_tick_ms = 0;
static bool skip_toggled = false;

static bool file_exists(const char* path) {
  FILE* file = nullptr;
  fopen_s(&file, path, "rb");
  if (!file)
    return false;
  fclose(file);
  return true;
}

static void redraw(HWND w) {
  const uint16_t* source = player->presentation_framebuffer();
  if (!source) {
    player->render(fb.data());
    source = fb.data();
  }
  for (size_t i = 0; i < fb.size(); i++) {
    uint16_t c = source[i];
    dib[i] = ((c & 0xF800) << 8) | ((c & 0x07E0) << 5) | ((c & 0x001F) << 3);
  }
  InvalidateRect(w, nullptr, FALSE);
}
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM k, LPARAM l) {
  if (m == WM_KEYDOWN) {
    if (GetForegroundWindow() != w)
      return 0;
    if (k == VK_ESCAPE) {
      player->input_escape();
      redraw(w);
      return 0;
    }
    if (k == VK_UP || k == '8') {
      player->input_up((l & (1LL << 30)) == 0);
      redraw(w);
      return 0;
    }
    if (k == VK_DOWN || k == '2') {
      player->input_down();
      redraw(w);
      return 0;
    }
    if (k == VK_LEFT || k == '4') {
      player->input_left();
      redraw(w);
      return 0;
    }
    if (k == VK_RIGHT || k == '6') {
      if (player->navigation_open())
        player->input_right();
      redraw(w);
      return 0;
    }
    if (k == VK_RETURN || k == '5') {
      player->input_confirm();
      if (player->quit_requested())
        DestroyWindow(w);
      else
        redraw(w);
      return 0;
    }
    if (k == VK_SPACE) {
      player->input_confirm();
      redraw(w);
      return 0;
    }
    if (k == VK_TAB && (l & (1LL << 30)) == 0 && player->skip_allowed()) {
      skip_toggled = !skip_toggled;
      redraw(w);
      return 0;
    }
    if (k == VK_PAUSE && (l & (1LL << 30)) == 0) {
      player->open_quick_quit();
      redraw(w);
      return 0;
    }
    if (k == 'H') {
      player->toggle_ui();
      redraw(w);
      return 0;
    }
    if (k == 'A') {
      player->toggle_auto();
      redraw(w);
      return 0;
    }
    if (nzato::launcher_debug && (k == '7' || k == '9')) {
      player->debug_jump(k == '7' ? -1 : 1);
      redraw(w);
      return 0;
    }
    if (nzato::launcher_debug && k == '0') {
      player->debug_unlock_gallery();
      redraw(w);
      return 0;
    }
  }
  if (m == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = nzato::screen_w;
    bi.bmiHeader.biHeight = -nzato::screen_h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0, 0, nzato::screen_w, nzato::screen_h, 0, 0, 0, nzato::screen_h,
                      dib.data(), &bi, DIB_RGB_COLORS);
    EndPaint(w, &ps);
    return 0;
  }
  if (m == WM_TIMER) {
    const ULONGLONG now = GetTickCount64();
    const unsigned elapsed =
        last_tick_ms ? static_cast<unsigned>(std::min<ULONGLONG>(250, now - last_tick_ms)) : 16;
    last_tick_ms = now;
    bool changed = player->tick(elapsed);
    if (!player->skip_allowed())
      skip_toggled = false;
    const bool skip_pressed = GetForegroundWindow() == w && player->skip_allowed() &&
                              ((GetKeyState(VK_CONTROL) & 0x8000) || skip_toggled);
    changed |= player->set_skip_indicator(skip_pressed);
    if (skip_pressed) {
      skip_elapsed += elapsed;
      if (skip_elapsed >= 32) {
        player->fast_forward();
        skip_elapsed = 0;
        changed = true;
      }
    } else
      skip_elapsed = 0;
    if (changed)
      redraw(w);
    return 0;
  }
  if (m == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(w, m, k, l);
}
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int) {
  SetProcessDPIAware();
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  wchar_t fallback[MAX_PATH]{};
  const wchar_t* path = nullptr;
  if (argc >= 2)
    path = argv[1];
  else {
    GetModuleFileNameW(nullptr, fallback, MAX_PATH);
    wchar_t* slash = wcsrchr(fallback, L'\\');
    const wchar_t* package = nzato::launcher_chinese ? L"ddlc-zh.nzp" : L"ddlc.nzp";
    if (slash)
      wcscpy_s(slash + 1, static_cast<size_t>(MAX_PATH - (slash + 1 - fallback)), package);
    path = fallback;
  }
  const DWORD attributes = GetFileAttributesW(path);
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    MessageBoxA(nullptr, "The required ddlc content pack was not found.", "ddlc content missing",
                MB_ICONERROR);
    if (argv)
      LocalFree(argv);
    return 1;
  }
  bool loaded = pack.load(path);
  if (argv)
    LocalFree(argv);
  if (!loaded) {
    MessageBoxA(nullptr, pack.error(), "ddlc pack error", MB_ICONERROR);
    return 1;
  }
  const bool op_available = file_exists(opening_video_path(pack).c_str());
  nzato::Player p(pack, op_available);
  player = &p;
  WNDCLASSA wc{};
  wc.lpfnWndProc = proc;
  wc.hInstance = h;
  wc.lpszClassName = "ddlc";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassA(&wc);
  constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  RECT wr{0, 0, nzato::screen_w, nzato::screen_h};
  AdjustWindowRect(&wr, style, FALSE);
  HWND w = CreateWindowA("ddlc",
                         "ddlc - Esc: save/back, Up: log, Ctrl: skip, H: hide UI, S/O: save/load",
                         style, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left,
                         wr.bottom - wr.top, nullptr, nullptr, h, nullptr);
  SetTimer(w, 1, 16, nullptr);
  ShowWindow(w, SW_SHOW);
  redraw(w);
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}
