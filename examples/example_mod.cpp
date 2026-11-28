// example_mod.cpp
// part of fawwk menu, made by fawwktrustdigital (MIT licence).
// the smallest useful asi that talks to fawwk menu. build it as a normal 32 bit
// dll renamed to .asi, with fawwk.hpp on the include path.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "fawwk.hpp"

static bool g_enabled = true;
static float g_power = 1.5f;
static int g_mode = 1;           // 0 = calm, 1 = normal, 2 = wild
static uint32_t g_tint = 0xFF3399FF;
static int g_hotkey = 'N';

static fawwk::Mod g_menu("yourname.example", "C++ Example", "Demo");

static void setup() {
  g_menu.version("1.0").author("you").description("What an asi mod looks like in the menu.");
  g_menu.translate("pt", "Exemplo em C++", "Como um mod asi aparece no menu.");   // optional, see Translations in the guide
  g_menu.page("General")
      .toggle("enabled", "Enabled", &g_enabled, true, {.tr = {{"label_pt", "Ligado"}}})
      .slider("power", "Power", &g_power, 1.5f, 0.5f, 5.f, {.fmt = "%.1fx", .step = 0.1})
      .combo("mode", "Mode", &g_mode, 1, {"Calm", "Normal", "Wild"})
      .key("hotkey", "Hotkey", &g_hotkey, 'N')
      .color("tint", "Tint", &g_tint, 0xFF3399FF)
      .button("beep", "Play a beep", [] { MessageBeep(MB_OK); }, {.text = "Beep"});
}

// run this from whatever you already call every frame (a game hook, a timer thread...)
static void onFrame() {
  g_menu.poll();
  if (!g_enabled || fawwk::Mod::menuOpen()) return;
  if (g_hotkey && (GetAsyncKeyState(g_hotkey) & 1)) {
    // do something with g_power, g_mode, g_tint
  }
}

static DWORD WINAPI loop(LPVOID) {
  setup();
  while (true) {
    onFrame();
    Sleep(16);
  }
}

BOOL APIENTRY DllMain(HMODULE m, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(m);
    CloseHandle(CreateThread(nullptr, 0, loop, nullptr, 0, nullptr));
  }
  return TRUE;
}
