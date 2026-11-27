// main.cpp
// fawwk menu, made by fawwktrustdigital.
// the plugin shell: finds the game's d3d9 device, draws the menu on top of the
// frame, listens for the menu key and owns the files on disk.
//
// everything that can go wrong here writes a line to fawwkmenu.log next to the
// .asi, so if the menu doesn't show up that file says why.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"
#include "core/api.hpp"
#include "core/lang.hpp"
#include "core/store.hpp"
#include "imgui.h"
#include "theme.hpp"
#include "icons.hpp"
#include "ui.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

const char* MENU_ID = "fawwk.menu";
const char* VERSION = "1.0.0";

HMODULE g_module = nullptr;
std::string g_dir;      // settings live here, see pickDataDir()
std::string g_base;     // the folder the plugin keeps its things in, the settings folder is inside it
std::atomic<bool> g_logOn{true};  // the "write the log" setting
std::string g_logPath;  // the log sits next to the plugin file, see pickLogPath()

std::unique_ptr<fw::Store> g_store;
std::unique_ptr<Menu> g_menu;

HWND g_hwnd = nullptr;
WNDPROC g_oldProc = nullptr;
bool g_unicodeWindow = false;

bool g_open = false;
bool g_imguiReady = false;
bool g_disabled = false;   // something threw, stay out of the way
bool g_themeDirty = true;

using EndScene_t = HRESULT(WINAPI*)(IDirect3DDevice9*);
using Reset_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using Present_t = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

// every vtable we patch gets its own record of the original functions. the game makes more
// than one d3d9 device while starting, and calling one device's function with another
// device's pointer is a guaranteed crash, so the originals must never be shared
struct Hooked {
  void** table;
  EndScene_t es;
  Reset_t rs;
  Present_t pr;
};
constexpr int MAX_HOOKED = 8;
Hooked g_hooked[MAX_HOOKED] = {};
volatile long g_hookedCount = 0;

Hooked* hookedForTable(void** vt) {
  long n = g_hookedCount;
  for (long i = 0; i < n; i++)
    if (g_hooked[i].table == vt) return &g_hooked[i];
  return nullptr;
}

Hooked* hookedFor(IDirect3DDevice9* dev) {
  void** vt = nullptr;
  __try {
    vt = *reinterpret_cast<void***>(dev);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
  return hookedForTable(vt);
}
volatile long g_endSceneCalls = 0;
volatile long g_presentCalls = 0;
volatile long g_frames = 0;   // times onFrame ran

// log --------------------------------------------------------------------------

std::mutex g_logLock;

void logf(const char* fmt, ...) {
  if (!g_logOn) return;
  std::lock_guard<std::mutex> g(g_logLock);
  FILE* f = std::fopen(g_logPath.c_str(), "a");
  if (!f) return;
  std::time_t t = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%H:%M:%S", std::localtime(&t));
  std::fprintf(f, "[%s] ", stamp);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(f, fmt, ap);
  va_end(ap);
  std::fputc('\n', f);
  std::fclose(f);
}

// if the game ever crashes inside this plugin, the log gets the address (an access violation can also
// be one that is caught and handled, like the probing of game memory, so one line alone doesn't prove much)
LONG CALLBACK crashNote(EXCEPTION_POINTERS* ep) {
  static volatile long count = 0;
  if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || count >= 8) return EXCEPTION_CONTINUE_SEARCH;
  MEMORY_BASIC_INFORMATION mbi = {};
  if (!VirtualQuery(ep->ExceptionRecord->ExceptionAddress, &mbi, sizeof mbi) || mbi.AllocationBase != (void*)g_module) return EXCEPTION_CONTINUE_SEARCH;
  count++;
  if (g_logOn) if (FILE* f = std::fopen(g_logPath.c_str(), "a")) {  // no lock: we may be inside logf
    std::fprintf(f, "access violation inside fawwkmenu.asi at +0x%lx (address %p)\n",
                 (unsigned long)((char*)ep->ExceptionRecord->ExceptionAddress - (char*)g_module), (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    std::fclose(f);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

std::string g_probeNote;  // why a folder was refused, written to the log once it exists

// can a file be made in this folder (and the folders above it)? whatever had to be created for
// the test is removed again when the folder turns out to be unusable, nothing is left behind
bool canWriteIn(const std::string& dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path top;  // the highest folder that does not exist yet
  for (fs::path p = dir; !p.empty() && !fs::exists(p, ec); p = p.parent_path()) {
    top = p;
    if (p == p.parent_path()) break;
  }
  fs::create_directories(dir, ec);
  if (ec) {
    g_probeNote += "  cannot create " + dir + " (" + ec.message() + ")\n";
    if (!top.empty()) fs::remove_all(top, ec);
    return false;
  }
  // a plain fopen, the same call the log uses. a locked, delete-on-close handle was refused
  // inside modloader folders even though ordinary writes there work fine
  std::string probe = dir + "\\fawwk_write_test.tmp";
  FILE* f = std::fopen(probe.c_str(), "wb");
  if (!f) {
    g_probeNote += "  cannot write in " + dir + " (errno " + std::to_string(errno) + ")\n";
    if (!top.empty()) fs::remove_all(top, ec);
    return false;
  }
  std::fclose(f);
  std::remove(probe.c_str());
  return true;
}

// where settings go: a "settings" folder where the plugin is.
//  - plugin in a folder of its own inside modloader (modloader\fawwkmenu\fawwkmenu.asi):
//    modloader\fawwkmenu\settings
//  - plugin anywhere else (game folder, scripts, modloader itself): fawwkmenu\settings next to it,
//    so a folder like the game's own is never filled with our files
// when that can't be written to (Program Files, a read-only copy) the game folder's own
// fawwkmenu\settings is tried, then the user's AppData, then temp
void pickDataDir() {
  auto parent = [](const std::string& p) { return p.substr(0, p.find_last_of("\\/")); };
  char me[MAX_PATH] = {}, exe[MAX_PATH] = {}, env[MAX_PATH] = {};
  GetModuleFileNameA(g_module, me, MAX_PATH);
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string beside = parent(me), game = parent(exe);
  std::string lowered = beside;
  for (char& c : lowered) c = (char)std::tolower((unsigned char)c);

  bool inModloader = (lowered + "\\").find("\\modloader\\") != std::string::npos;
  bool ownFolder = inModloader && lowered.size() >= 10 && lowered.compare(lowered.size() - 10, 10, "\\modloader") != 0;
  g_base = ownFolder ? beside : beside + "\\fawwkmenu";

  std::vector<std::string> options;
  options.push_back(g_base + "\\settings");
  options.push_back(game + "\\fawwkmenu\\settings");
  if (GetEnvironmentVariableA("LOCALAPPDATA", env, MAX_PATH)) options.push_back(std::string(env) + "\\fawwkmenu\\gta_sa\\settings");
  if (GetTempPathA(MAX_PATH, env)) options.push_back(std::string(env) + "fawwkmenu\\settings");
  for (const std::string& o : options) {
    if (canWriteIn(o)) {
      g_dir = o;
      return;
    }
  }
  g_dir = options.front();
}

// settings from older versions are copied over once (never over a file that is already there).
// older layouts: files loose next to the plugin (fawwk_<id>.ini), a fawwkmenu folder with or without
// a settings folder, fawwk files in the game folder, and the AppData folder
void migrateOldSettings() {
  char env[MAX_PATH] = {}, exe[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string game = exe;
  game = game.substr(0, game.find_last_of("\\/"));
  struct Src { std::string dir; bool fawwkOnly; };
  std::vector<Src> sources = {{g_base, true}, {g_base + "\\fawwkmenu\\settings", false}, {g_base + "\\fawwkmenu", false}, {game, true}};
  if (GetEnvironmentVariableA("LOCALAPPDATA", env, MAX_PATH)) {
    std::string a = std::string(env) + "\\fawwkmenu\\gta_sa";
    sources.push_back({a, false});
    sources.push_back({a + "\\settings", false});
  }
  for (const Src& src : sources) {
    if (_stricmp(src.dir.c_str(), g_dir.c_str()) == 0) continue;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((src.dir + "\\*.ini").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) continue;
    do {
      std::string name = fd.cFileName;
      if (src.fawwkOnly && name.rfind("fawwk", 0) != 0) continue;  // the game folder holds plenty of other ini files
      std::string id = name.substr(0, name.size() - 4);
      if (id.rfind("fawwk_", 0) == 0) id = id.substr(6);
      std::string to = g_dir + "\\" + id + ".ini";
      if (CopyFileA((src.dir + "\\" + name).c_str(), to.c_str(), TRUE)) logf("copied old settings file %s from %s", name.c_str(), src.dir.c_str());
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
}

// the log goes next to the plugin file. if that folder can't be written, the game's own
// fawwkmenu folder is tried, then the temp folder
void pickLogPath() {
  char me[MAX_PATH] = {};
  GetModuleFileNameA(g_module, me, MAX_PATH);
  std::string beside = me;
  beside = beside.substr(0, beside.find_last_of("\\/")) + "\\fawwkmenu.log";
  char tmp[MAX_PATH] = {};
  GetTempPathA(MAX_PATH, tmp);
  const std::string options[3] = {beside, g_dir + "\\fawwkmenu.log", std::string(tmp) + "fawwkmenu.log"};
  for (const std::string& o : options) {
    if (FILE* f = std::fopen(o.c_str(), "w")) {  // fresh log every start
      std::fclose(f);
      g_logPath = o;
      return;
    }
  }
}

// settings of the menu itself ------------------------------------------------------

double cfgNum(const char* id, double fb = 0) { return g_store ? g_store->num(MENU_ID, id, fb) : fb; }
bool cfgBool(const char* id, bool fb = false) { return cfgNum(id, fb ? 1 : 0) != 0; }

fw::Spec mk(fw::Map m) {
  fw::Spec s;
  std::string err;
  if (!fw::parseSpec(m, s, err)) logf("internal setting %s is bad: %s", m["id"].c_str(), err.c_str());
  return s;
}

fw::Files makeFiles();

// the language picked in the menu. the menu's own text and every mod that polls get it
void applyLanguage() {
  int i = (int)cfgNum("language", 1) - 1;
  const auto& all = fw::languages();
  std::string code = i >= 0 && i < (int)all.size() ? all[i].code : "en";
  fw::setLanguage(code);
  g_store->language = fw::language();
}

// lang_<code>.txt next to the settings adds to or replaces the built-in text: one "English => translation" per line
void loadLangFiles() {
  fw::builtinStrings();
  auto files = makeFiles();
  for (const fw::Language& l : fw::languages()) {
    std::string text;
    if (files.read(g_dir + "\\lang_" + l.code + ".txt", text)) {
      int n = fw::parseLangFile(l.code, text);
      logf("language file lang_%s.txt: %d line%s used", l.code, n, n == 1 ? "" : "s");
    }
  }
}

void registerOwnSettings() {
  std::string themeNames;
  for (auto& n : theme::names()) themeNames += (themeNames.empty() ? "" : "|") + fw::enc(n);

  std::string languageNames;
  for (const fw::Language& l : fw::languages()) languageNames += (languageNames.empty() ? "" : "|") + fw::enc(l.native);

  std::vector<fw::Spec> specs = {
      mk({{"type", "enum"}, {"id", "language"}, {"label", "Language"}, {"default", "1"}, {"options", languageNames}, {"page", "General"},
          {"desc", "Mods that bring translations follow this. The rest stay in their own language."}}),
      mk({{"type", "key"}, {"id", "openkey"}, {"label", "Open menu key"}, {"default", "120"}, {"page", "General"}, {"desc", "F9 by default. Click, then press a key. Right click clears it."}}),
      mk({{"type", "key"}, {"id", "openkey2"}, {"label", "Second open key"}, {"default", "121"}, {"page", "General"}, {"desc", "F10 by default, so there is a spare if the first one misbehaves. Right click clears it."}}),
      mk({{"type", "enum"}, {"id", "gamemode"}, {"label", "While the menu is open"}, {"default", "1"}, {"options", "Pause the game|Slow motion|Keep playing"}, {"page", "General"},
          {"desc", "Keep playing leaves the game running. The mouse can still move the camera in that mode."}}),
      mk({{"type", "float"}, {"id", "slowspeed"}, {"label", "Slow motion speed"}, {"default", "0.1"}, {"min", "0.02"}, {"max", "0.5"}, {"step", "0.01"},
          {"fmt", "%.2fx"}, {"page", "General"}}),
      mk({{"type", "enum"}, {"id", "backdrop"}, {"label", "Background"}, {"default", "3"}, {"options", "Clear|Dim|Blur"}, {"page", "General"},
          {"desc", "What happens to the game picture behind the menu."}}),
      mk({{"type", "int"}, {"id", "blurlevel"}, {"label", "Blur strength"}, {"default", "3"}, {"min", "1"}, {"max", "4"}, {"page", "General"}}),
      mk({{"type", "float"}, {"id", "dim"}, {"label", "Background darkness"}, {"default", "0.3"}, {"min", "0"}, {"max", "0.9"}, {"step", "0.01"}, {"page", "General"}}),
      mk({{"type", "bool"}, {"id", "blockinput"}, {"label", "Keep clicks and keys away from the game"}, {"default", "1"}, {"page", "General"}}),
      mk({{"type", "bool"}, {"id", "autosave"}, {"label", "Autosave"}, {"default", "0"}, {"page", "General"},
          {"desc", "Off: changes work right away but only reach the disk when you press Store. On: saved a moment after you stop touching things."}}),
      mk({{"type", "bool"}, {"id", "log"}, {"label", "Write fawwkmenu.log"}, {"default", "1"}, {"page", "General"},
          {"desc", "Turn off and the log stops and the file is removed. Turn it on again if something goes wrong and send the file to the author."}}),
      mk({{"type", "bool"}, {"id", "inlinedesc"}, {"label", "Show descriptions under settings"}, {"default", "0"}, {"page", "General"}}),
      mk({{"type", "enum"}, {"id", "theme"}, {"label", "Theme"}, {"default", "1"}, {"options", themeNames}, {"page", "Look"}}),
      mk({{"type", "color"}, {"id", "accent"}, {"label", "Accent color"}, {"default", std::to_string(theme::accentOf(1))}, {"page", "Look"}}),
      mk({{"type", "float"}, {"id", "winsize"}, {"label", "Window size"}, {"default", "0.7"}, {"min", "0.4"}, {"max", "1"}, {"step", "0.01"},
          {"fmt", "%.2f"}, {"page", "Look"}, {"desc", "How much of the screen the menu takes. You can also drag its corner."}}),
      mk({{"type", "float"}, {"id", "scale"}, {"label", "UI scale"}, {"default", "1"}, {"min", "0.8"}, {"max", "1.6"}, {"step", "0.05"},
          {"fmt", "%.2fx"}, {"page", "Look"}}),
      mk({{"type", "float"}, {"id", "opacity"}, {"label", "Window opacity"}, {"default", "0.97"}, {"min", "0.5"}, {"max", "1"}, {"step", "0.01"}, {"page", "Look"}}),
      mk({{"type", "int"}, {"id", "rounding"}, {"label", "Corner rounding"}, {"default", "6"}, {"min", "0"}, {"max", "14"}, {"page", "Look"}}),
      mk({{"type", "note"}, {"label", std::string("fawwk menu ") + VERSION + ", made by fawwktrustdigital."}, {"page", "About"}}),
      mk({{"type", "note"}, {"label", "Mods that use fawwk menu appear in the list on the left on their own."}, {"page", "About"}}),
      mk({{"type", "button"}, {"id", "saveall"}, {"label", "Write all settings to disk now"}, {"text", "Save all"}, {"page", "About"}}),
      mk({{"type", "button"}, {"id", "resetlook"}, {"label", "Put the look back to default"}, {"text", "Reset look"}, {"page", "About"}}),
  };
  fw::Map meta = {{"id", MENU_ID}, {"name", "fawwk options"}, {"category", "System"}, {"version", VERSION},
                  {"author", "fawwktrustdigital"}, {"description", "Look, keys and behavior of the menu itself."}};
  std::string err;
  if (!g_store->registerMod(meta, std::move(specs), {}, true, err)) logf("could not register menu settings: %s", err.c_str());
}

// pausing ----------------------------------------------------------------------------

constexpr uintptr_t ADDR_USER_PAUSE = 0xB7CB49;
constexpr uintptr_t ADDR_TIME_SCALE = 0xB7CB64;
bool g_pausedByUs = false;
bool g_slowedByUs = false;
float g_scaleBefore = 1.f;
int g_gameChecked = 0;  // 0 not yet, 1 ok, -1 not the game version we know

bool readU32(uintptr_t a, unsigned& out) {
  __try {
    out = *(volatile unsigned*)a;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool writeU8(uintptr_t a, unsigned char v) {
  __try {
    *(volatile unsigned char*)a = v;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool readF32(uintptr_t a, float& out) {
  __try {
    out = *(volatile float*)a;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool writeF32(uintptr_t a, float v) {
  __try {
    *(volatile float*)a = v;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool gameKnown() {
  if (g_gameChecked == 0) {
    unsigned v = 0;
    // 1.0 US (also the usual compact exe) has this value here
    g_gameChecked = (readU32(0x82457C, v) && (v & 0xFFFF) == 0x94BF) ? 1 : -1;
    logf("game version check: %s", g_gameChecked == 1 ? "1.0 US, pause and slow motion available" : "unknown exe, pause and slow motion disabled");
  }
  return g_gameChecked == 1;
}

// the game's own pause / main menu. while it is up ours has to stay out of the way
bool gameMenuUp() {
  if (!gameKnown()) return false;
  unsigned v = 0;
  if (!readU32(0xBA67A4, v)) return false;  // CMenuManager::m_bMenuActive, the low byte of this dword
  return (v & 0xFF) != 0;
}

// pause / slow motion while the menu is open, undone as soon as it closes
void applyGameMode() {
  int mode = (int)cfgNum("gamemode", 1);  // 1 pause, 2 slow, 3 keep playing
  if (g_menu && g_menu->popup) mode = 3;  // a small window never touches the game
  bool on = g_open && gameKnown();
  bool wantPause = on && mode == 1;
  bool wantSlow = on && mode == 2;

  if (wantPause && !g_pausedByUs) {
    if (writeU8(ADDR_USER_PAUSE, 1)) g_pausedByUs = true;
  } else if (!wantPause && g_pausedByUs) {
    writeU8(ADDR_USER_PAUSE, 0);
    g_pausedByUs = false;
  }

  if (wantSlow) {
    float now = 1.f;
    if (!g_slowedByUs && readF32(ADDR_TIME_SCALE, now) && now > 0.f && now <= 10.f) {
      g_scaleBefore = now;
      g_slowedByUs = true;
    }
    if (g_slowedByUs) writeF32(ADDR_TIME_SCALE, (float)cfgNum("slowspeed", 0.1));
  } else if (g_slowedByUs) {
    writeF32(ADDR_TIME_SCALE, g_scaleBefore);
    g_slowedByUs = false;
  }
}

void undoGameMode() {
  if (g_pausedByUs) { writeU8(ADDR_USER_PAUSE, 0); g_pausedByUs = false; }
  if (g_slowedByUs) { writeF32(ADDR_TIME_SCALE, g_scaleBefore); g_slowedByUs = false; }
}

// window proc --------------------------------------------------------------------------

LRESULT CALLBACK hkWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  // windows treats F10 as a menu key and would hand the window to its (nonexistent) menu bar,
  // which stalls the game. when F10 opens our menu, that part of the press is not passed on
  if ((m == WM_SYSKEYDOWN || m == WM_SYSKEYUP) && w == VK_F10 && g_imguiReady &&
      (cfgNum("openkey", VK_F9) == VK_F10 || cfgNum("openkey2", VK_F10) == VK_F10))
    return 0;
  if (g_open && g_imguiReady && !g_disabled) {
    ImGui_ImplWin32_WndProcHandler(h, m, w, l);
    if (cfgBool("blockinput", true)) {
      switch (m) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
          if (g_menu && g_menu->popup && !g_menu->textActive) break;  // a small window only takes the keyboard while typing
          return 0;
        default:
          break;
      }
    }
  }
  return g_unicodeWindow ? CallWindowProcW(g_oldProc, h, m, w, l) : CallWindowProcA(g_oldProc, h, m, w, l);
}

void installWndProc() {
  if (!g_hwnd || g_oldProc) return;
  g_unicodeWindow = IsWindowUnicode(g_hwnd) != FALSE;
  LONG_PTR prev = g_unicodeWindow ? SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc)
                                  : SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
  g_oldProc = (WNDPROC)prev;
  logf("window proc hooked (%s), hwnd=%p", g_unicodeWindow ? "unicode" : "ansi", (void*)g_hwnd);
}

// imgui ------------------------------------------------------------------------------------

IDirect3DDevice9* g_imguiDev = nullptr;

void ensureImgui(IDirect3DDevice9* dev) {
  if (g_imguiReady) return;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.MouseDrawCursor = true;

  char windir[MAX_PATH] = "C:\\Windows";
  GetEnvironmentVariableA("WINDIR", windir, sizeof windir);
  // basic latin and accents, cyrillic, and latin extended-a (polish, turkish, romanian letters)
  static ImVector<ImWchar> ranges;
  {
    ImFontGlyphRangesBuilder b;
    b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    static const ImWchar extra[] = {0x0100, 0x017F, 0};
    b.AddRanges(extra);
    b.BuildRanges(&ranges);
  }
  ImFont* font = nullptr;
  for (const char* name : {"segoeui.ttf", "tahoma.ttf", "arial.ttf"}) {
    std::string p = std::string(windir) + "\\Fonts\\" + name;
    if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
      font = io.Fonts->AddFontFromFileTTF(p.c_str(), 17.f, nullptr, ranges.Data);
      if (font) { logf("font: %s", name); break; }
    }
  }
  if (!font) { io.Fonts->AddFontDefault(); logf("font: imgui default"); }

  ImGui_ImplWin32_Init(g_hwnd);
  ImGui_ImplDX9_Init(dev);
  g_imguiDev = dev;
  g_imguiReady = true;
  g_themeDirty = true;
  logf("imgui ready");
}

void applyTheme() {
  theme::Options o;
  o.preset = (int)cfgNum("theme", 1);
  o.accent = (unsigned)cfgNum("accent", 0);
  o.opacity = (float)cfgNum("opacity", 0.97);
  o.rounding = (float)cfgNum("rounding", 6);
  o.scale = (float)cfgNum("scale", 1);
  theme::apply(o);
  g_themeDirty = false;
}

// keys -------------------------------------------------------------------------------------

bool g_prev[256] = {};
double g_openedAt = 0;

// while the menu is open other code in the game gets told that no key is down. the menu itself
// has to keep seeing the real keyboard, so it goes through the untouched function
volatile bool g_hideInput = false;  // the mouse is hidden from the game
volatile bool g_hideKeys = false;   // the keyboard too (a small window leaves it alone unless typing)
ULONGLONG g_graceUntil = 0;  // keys stay hidden from the game for a moment after closing (Esc would open its pause menu)
SHORT(WINAPI* g_realAsync)(int) = nullptr;
SHORT(WINAPI* g_realState)(int) = nullptr;

// a tap shorter than one frame can start and end between two polls (a slow frame, a freeze). the low
// bit of the key state says the key went down since the last call, so such a tap is not lost
bool keyTapped(int vk) {
  SHORT st = g_realAsync ? g_realAsync(vk) : GetAsyncKeyState(vk);
  return (st & 0x8000) == 0 && (st & 1) != 0;
}

bool keyDown(int vk) { return ((g_realAsync ? g_realAsync(vk) : GetAsyncKeyState(vk)) & 0x8000) != 0; }

double nowSeconds() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return duration<double>(steady_clock::now() - start).count();
}

bool hookDirectInput();
int hookKeyboardReaders();

// done the first time the menu opens, long after the game has started: nothing in here touches
// other mods while they are still loading
bool g_inputHooksDone = false;

void installInputHooks() {
  if (g_inputHooksDone) return;
  g_inputHooksDone = true;
  logf("direct input hook: %s", hookDirectInput() ? "ok" : "FAILED (mouse and keys may still reach the game)");
  logf("keyboard readers hooked in %d module(s)", hookKeyboardReaders());
}

void setOpen(bool v) {
  if (g_open == v) return;
  g_open = v;
  g_store->open = v;
  logf("menu %s", v ? "opened" : "closed");
  if (v) {
    g_openedAt = nowSeconds();
    installInputHooks();
  } else {
    g_graceUntil = GetTickCount64() + 400;
    g_menu->listen.active = false;
    g_menu->popup = false;
    if (g_store->autosave) g_store->flush(true);
  }
}

// returns true while a key capture is running
bool captureKey() {
  if (!g_menu->listen.active) return false;
  for (int vk = 3; vk < 255; vk++) {
    bool down = keyDown(vk);
    if (down && !g_prev[vk]) {
      auto& l = g_menu->listen;
      if (vk != VK_ESCAPE) {
        int v = (vk == VK_BACK || vk == VK_DELETE) ? 0 : vk;
        g_store->setNum(l.mod, l.setting, v, "ui");
      }
      l.active = false;
      break;
    }
  }
  return true;
}

bool patchSlot(void** vtable, int index, void* hook, void** original);

// keeping input away from the game -------------------------------------------------------------
// the game reads the mouse and keyboard through DirectInput, and other mods poll the keyboard
// with GetAsyncKeyState / GetKeyState. both get an "nothing pressed" answer while the menu is open.

SHORT WINAPI hkAsyncKey(int vk) { return g_hideKeys ? 0 : g_realAsync(vk); }
SHORT WINAPI hkKeyState(int vk) { return g_hideKeys ? 0 : g_realState(vk); }

bool patchImports(HMODULE mod) {
  auto base = reinterpret_cast<unsigned char*>(mod);
  auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
  auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return false;
  bool any = false;
  for (auto d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; d++) {
    auto thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
    for (; thunk->u1.Function; thunk++) {
      auto fn = reinterpret_cast<void**>(&thunk->u1.Function);
      void* want = nullptr;
      if (*fn == (void*)g_realAsync) want = (void*)hkAsyncKey;
      else if (*fn == (void*)g_realState) want = (void*)hkKeyState;
      if (!want) continue;
      DWORD old = 0;
      if (VirtualProtect(fn, sizeof(void*), PAGE_READWRITE, &old)) {
        *fn = want;
        VirtualProtect(fn, sizeof(void*), old, &old);
        any = true;
      }
    }
  }
  return any;
}

// a module can be unloaded or half loaded while we look at it, so the walk is guarded
bool patchImportsSafe(HMODULE mod) {
  __try {
    return patchImports(mod);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// every loaded module except ourselves and windows' own; run again later to catch mods that load after us
int hookKeyboardReaders() {
  char sys[MAX_PATH] = {};
  GetWindowsDirectoryA(sys, sizeof sys);
  size_t sysLen = std::strlen(sys);
  if (!g_realAsync || !g_realState) return 0;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
  if (snap == INVALID_HANDLE_VALUE) return 0;
  MODULEENTRY32 e = {};
  e.dwSize = sizeof e;
  int n = 0;
  for (BOOL ok = Module32First(snap, &e); ok; ok = Module32Next(snap, &e)) {
    if (e.hModule == g_module) continue;
    if (sysLen && _strnicmp(e.szExePath, sys, sysLen) == 0) continue;
    if (patchImportsSafe(e.hModule)) n++;
  }
  CloseHandle(snap);
  return n;
}

using DevState_t = HRESULT(WINAPI*)(void*, DWORD, void*);
DevState_t g_oDevState = nullptr;

HRESULT WINAPI hkDevState(void* self, DWORD size, void* data) {
  HRESULT hr = g_oDevState(self, size, data);
  // 16 / 20 bytes: mouse state, 256: keyboard state
  if (SUCCEEDED(hr) && data && ((size == 256 && g_hideKeys) || ((size == 16 || size == 20) && g_hideInput))) std::memset(data, 0, size);
  return hr;
}

// all DirectInput devices of one kind share a vtable, so a throwaway mouse is enough to find it
bool hookDirectInput() {
  HMODULE dll = LoadLibraryA("dinput8.dll");
  if (!dll) return false;
  using Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, const GUID&, void**, void*);
  auto create = reinterpret_cast<Create_t>(GetProcAddress(dll, "DirectInput8Create"));
  if (!create) return false;
  const GUID iid = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};  // IDirectInput8A
  const GUID mouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};  // system mouse
  void* di = nullptr;
  if (FAILED(create(GetModuleHandleA(nullptr), 0x0800, iid, &di, nullptr)) || !di) return false;
  using CreateDevice_t = HRESULT(WINAPI*)(void*, const GUID&, void**, void*);
  using Release_t = ULONG(WINAPI*)(void*);
  void** diVt = *reinterpret_cast<void***>(di);
  void* dev = nullptr;
  bool ok = false;
  if (SUCCEEDED(reinterpret_cast<CreateDevice_t>(diVt[3])(di, mouse, &dev, nullptr)) && dev) {
    void** vt = *reinterpret_cast<void***>(dev);
    ok = patchSlot(vt, 9, (void*)hkDevState, (void**)&g_oDevState);  // GetDeviceState
    reinterpret_cast<Release_t>(vt[2])(dev);
  }
  reinterpret_cast<Release_t>(diVt[2])(di);
  return ok;
}

// blurred copy of the frame, made by shrinking the back buffer a few times -------------------

struct Blur {
  static constexpr int LEVELS = 4;  // 1/2, 1/4, 1/8, 1/16 of the screen
  IDirect3DTexture9* tex[LEVELS] = {};
  IDirect3DSurface9* surf[LEVELS] = {};
  UINT w = 0, h = 0;
  bool unsupported = false;

  void release() {
    for (int i = 0; i < LEVELS; i++) {
      if (surf[i]) { surf[i]->Release(); surf[i] = nullptr; }
      if (tex[i]) { tex[i]->Release(); tex[i] = nullptr; }
    }
    w = h = 0;
  }

  bool make(IDirect3DDevice9* dev, const D3DSURFACE_DESC& bb) {
    release();
    for (D3DFORMAT fmt : {bb.Format, D3DFMT_A8R8G8B8}) {
      bool all = true;
      for (int i = 0; i < LEVELS && all; i++) {
        UINT tw = std::max<UINT>(8, bb.Width >> (i + 1)), th = std::max<UINT>(8, bb.Height >> (i + 1));
        all = SUCCEEDED(dev->CreateTexture(tw, th, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &tex[i], nullptr)) &&
              SUCCEEDED(tex[i]->GetSurfaceLevel(0, &surf[i]));
      }
      if (all) { w = bb.Width; h = bb.Height; return true; }
      release();
    }
    return false;
  }

  // returns the texture for the strength asked for (1..4), or null if blur isn't possible here
  IDirect3DTexture9* grab(IDirect3DDevice9* dev, IDirect3DSurface9* backBuffer, int strength) {
    if (unsupported) return nullptr;
    D3DSURFACE_DESC d = {};
    if (FAILED(backBuffer->GetDesc(&d))) return nullptr;
    if (!tex[0] || d.Width != w || d.Height != h) {
      if (!make(dev, d)) {
        unsupported = true;
        logf("blur not possible on this device, using plain dimming");
        return nullptr;
      }
    }
    if (FAILED(dev->StretchRect(backBuffer, nullptr, surf[0], nullptr, D3DTEXF_LINEAR))) return nullptr;
    for (int i = 1; i < LEVELS; i++) {
      if (FAILED(dev->StretchRect(surf[i - 1], nullptr, surf[i], nullptr, D3DTEXF_LINEAR))) return nullptr;
    }
    return tex[std::max(1, std::min(LEVELS, strength)) - 1];
  }
} g_blur;

// mod icons: one texture per file, made the first time it is drawn. a file that can't be read is
// reported once and then skipped. managed textures survive a device reset, they are only dropped
// when the device itself changes
struct IconCache {
  std::map<std::string, IDirect3DTexture9*> tex;
  std::set<std::string> failed;
  void release() {
    for (auto& kv : tex)
      if (kv.second) kv.second->Release();
    tex.clear();
  }
  ImTextureID get(IDirect3DDevice9* dev, const std::string& rawPath) {
    auto it = tex.find(rawPath);
    if (it != tex.end()) return (ImTextureID)it->second;
    if (!dev || failed.count(rawPath)) return (ImTextureID)0;
    std::string path = rawPath;
    bool absolute = path.size() > 1 && (path[1] == ':' || path[0] == '\\' || path[0] == '/');
    if (!absolute) {
      char exe[MAX_PATH] = {};
      GetModuleFileNameA(nullptr, exe, MAX_PATH);
      std::string game = exe;
      path = game.substr(0, game.find_last_of("\\/")) + "\\" + path;
    }
    std::vector<unsigned char> px;
    int w = 0, h = 0;
    std::string err;
    IDirect3DTexture9* t = nullptr;
    if (!decodeIcon(path, px, w, h, err)) {
      logf("icon %s: %s", path.c_str(), err.c_str());
    } else if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr)) || !t) {
      logf("icon %s: could not make a texture", path.c_str());
      t = nullptr;
    } else {
      D3DLOCKED_RECT lr;
      if (SUCCEEDED(t->LockRect(0, &lr, nullptr, 0))) {
        for (int y = 0; y < h; y++) {
          unsigned char* row = (unsigned char*)lr.pBits + (size_t)y * lr.Pitch;
          const unsigned char* src = &px[(size_t)y * w * 4];
          for (int x = 0; x < w; x++) {  // rgba in, bgra out
            row[x * 4 + 0] = src[x * 4 + 2];
            row[x * 4 + 1] = src[x * 4 + 1];
            row[x * 4 + 2] = src[x * 4 + 0];
            row[x * 4 + 3] = src[x * 4 + 3];
          }
        }
        t->UnlockRect(0);
        logf("icon %s loaded (%d x %d)", path.c_str(), w, h);
      } else {
        t->Release();
        t = nullptr;
      }
    }
    if (!t) failed.insert(rawPath);
    else tex[rawPath] = t;
    return (ImTextureID)t;
  }
} g_icons;

// the frame ---------------------------------------------------------------------------------

void backdrop(IDirect3DDevice9* dev, IDirect3DSurface9* backBuffer) {
  int kind = (int)cfgNum("backdrop", 3);  // 1 clear, 2 dim, 3 blur
  if (kind == 1) return;
  ImDrawList* bg = ImGui::GetBackgroundDrawList();
  ImVec2 size = ImGui::GetIO().DisplaySize;
  if (kind == 3) {
    if (IDirect3DTexture9* t = g_blur.grab(dev, backBuffer, (int)cfgNum("blurlevel", 3)))
      bg->AddImage((ImTextureID)t, ImVec2(0, 0), size);
  }
  int a = (int)(std::max(0.0, std::min(0.9, cfgNum("dim", 0.3))) * 255);
  if (a > 0) bg->AddRectFilled(ImVec2(0, 0), size, IM_COL32(0, 0, 0, a));
}

bool readPtr(uintptr_t a, void*& out);
constexpr uintptr_t ADDR_GAME_DEVICE = 0xC97C28;
volatile bool g_gameDeviceSeen = false;

void onFrame(IDirect3DDevice9* dev, bool inScene) {
  if (!g_store || !g_menu || g_disabled) return;

  // only the device the game is actually rendering with gets a menu
  void* current = nullptr;
  if (g_gameDeviceSeen && readPtr(ADDR_GAME_DEVICE, current) && current && current != (void*)dev) return;

  // when we are driven by EndScene (which the game calls many times a frame) do the work
  // at most about 100 times a second
  static DWORD lastTick = 0;
  if (inScene) {
    DWORD now = GetTickCount();
    if (now - lastTick < 10) return;
    lastTick = now;
  }
  g_frames++;

  static bool first = true;
  if (first) {
    first = false;
    D3DDEVICE_CREATION_PARAMETERS cp = {};
    if (SUCCEEDED(dev->GetCreationParameters(&cp))) g_hwnd = cp.hFocusWindow;
    if (!g_hwnd) g_hwnd = GetForegroundWindow();
    logf("first frame, game window = %p", (void*)g_hwnd);
    installWndProc();
  }

  std::lock_guard<std::recursive_mutex> guard(g_store->lock);
  g_store->tick();

  bool gameMenu = gameMenuUp();
  if (g_open && gameMenu) setOpen(false);

  bool focused = !g_hwnd || GetForegroundWindow() == g_hwnd;
  if (!focused) {
    static bool said = false;
    int k0 = (int)cfgNum("openkey", VK_F9);
    if (!said && k0 > 0 && k0 < 256 && keyDown(k0)) {
      said = true;
      logf("open key pressed but the game window is not the foreground window (game %p, foreground %p), so it is ignored", (void*)g_hwnd, (void*)GetForegroundWindow());
    }
  }
  if (focused) {
    bool listening = captureKey();
    int keys[2] = {(int)cfgNum("openkey", VK_F9), (int)cfgNum("openkey2", VK_F10)};
    for (int key : keys) {
      if (key <= 0 || key >= 256) continue;
      if ((keyDown(key) && !g_prev[key]) || (!g_prev[key] && keyTapped(key))) {
        if (gameMenu) logf("open key %d pressed, ignored: the game's own menu is up", key);
        else if (listening || (g_open && g_menu->textActive)) logf("open key %d pressed, ignored: a key capture or text box is active", key);
        else {
          logf("open key %d pressed, menu %s", key, g_open ? "closing" : "opening");
          setOpen(!g_open);
          break;
        }
      }
    }
    if (g_open && !listening && !g_menu->textActive && keyDown(VK_ESCAPE) && !g_prev[VK_ESCAPE]) setOpen(false);
    for (int vk = 1; vk < 255; vk++) g_prev[vk] = keyDown(vk);
  }

  if (!g_store->showRequest.empty()) {
    if (gameMenu) {
      g_store->showRequest.clear();  // asked for while the game's menu was up, it would pop up later out of nowhere
    } else if (!g_open) {
      g_menu->popup = g_store->showRequest.find('\n') != std::string::npos;  // known before the first frame, so no backdrop flashes
      setOpen(true);
    }
  }
  applyGameMode();
  {
    ULONGLONG t = GetTickCount64();
    if (t < g_graceUntil && keyDown(VK_ESCAPE)) g_graceUntil = t + 250;  // until the key is let go
    bool block = g_open && cfgBool("blockinput", true);
    bool smallWindow = g_open && g_menu->popup && !g_menu->textActive;  // keys stay with the game, so the hotkey that opened it can close it
    g_hideInput = block || t < g_graceUntil;
    g_hideKeys = (block && !smallWindow) || t < g_graceUntil;
  }
  if (!g_open) return;

  // draw into the real back buffer, whatever the game had selected
  IDirect3DSurface9 *rt = nullptr, *bb = nullptr;
  dev->GetRenderTarget(0, &rt);
  dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
  if (!bb) {
    if (rt) rt->Release();
    return;
  }
  bool switched = rt != bb;
  if (switched && inScene) {  // mid-frame post effect pass, not the final image
    if (rt) rt->Release();
    bb->Release();
    return;
  }
  if (switched && FAILED(dev->SetRenderTarget(0, bb))) {
    rt->Release();
    bb->Release();
    return;
  }
  bool began = !inScene && SUCCEEDED(dev->BeginScene());

  if (g_imguiReady && g_imguiDev != dev) {
    logf("render device changed, rebuilding imgui objects");
    g_blur.release();
    g_icons.release();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplDX9_Init(dev);
    g_imguiDev = dev;
  }
  ensureImgui(dev);
  if (g_themeDirty) applyTheme();
  ImGui::GetIO().FontGlobalScale = (float)cfgNum("scale", 1);

  ImGui_ImplDX9_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  if (!g_menu->popup) backdrop(dev, bb);
  g_menu->draw();
  ImGui::EndFrame();
  ImGui::Render();
  ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

  if (began) {
    if (Hooked* h = hookedFor(dev)) h->es(dev);
    else dev->EndScene();
  }
  if (switched && rt) dev->SetRenderTarget(0, rt);
  if (rt) rt->Release();
  bb->Release();
}

void guardedFrame(IDirect3DDevice9* dev, bool inScene) {
  static bool inside = false;
  if (inside) return;
  inside = true;
  try {
    onFrame(dev, inScene);
  } catch (const std::exception& e) {
    logf("exception in frame, menu disabled: %s", e.what());
    g_disabled = true;
    g_open = false;
    g_hideInput = false;
    g_hideKeys = false;
    undoGameMode();
  } catch (...) {
    logf("unknown exception in frame, menu disabled");
    g_disabled = true;
    g_open = false;
    g_hideInput = false;
    g_hideKeys = false;
    undoGameMode();
  }
  inside = false;
}

// the menu is drawn from Present: that is the last thing that happens to the frame, after
// every post effect. EndScene only takes over if Present never shows up.
bool endSceneDrives() { return g_presentCalls == 0 && g_endSceneCalls > 120; }

HRESULT WINAPI hkEndScene(IDirect3DDevice9* dev) {
  g_endSceneCalls++;
  Hooked* h = hookedFor(dev);
  if (!h) return D3D_OK;
  if (endSceneDrives()) guardedFrame(dev, true);
  return h->es(dev);
}

HRESULT WINAPI hkPresent(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND w, const RGNDATA* c) {
  g_presentCalls++;
  Hooked* h = hookedFor(dev);
  if (!h) return D3D_OK;
  if (!endSceneDrives()) guardedFrame(dev, false);
  return h->pr(dev, a, b, w, c);
}

HRESULT WINAPI hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
  g_blur.release();
  if (g_imguiReady) ImGui_ImplDX9_InvalidateDeviceObjects();
  Hooked* h = hookedFor(dev);
  if (!h) return D3DERR_INVALIDCALL;
  HRESULT hr = h->rs(dev, pp);
  if (SUCCEEDED(hr) && g_imguiReady) ImGui_ImplDX9_CreateDeviceObjects();
  return hr;
}

// hooking ----------------------------------------------------------------------------------

// puts hook in a vtable slot and remembers what was there. if the slot is already ours
// (same table patched twice) nothing changes, so we never end up calling ourselves
bool patchSlot(void** vtable, int index, void* hook, void** original) {
  if (vtable[index] == hook) return true;
  DWORD old = 0;
  if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
  *original = vtable[index];
  vtable[index] = hook;
  VirtualProtect(&vtable[index], sizeof(void*), old, &old);
  return true;
}

bool readPtr(uintptr_t a, void*& out) {
  __try {
    out = *(void* volatile*)a;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

void* g_patchedDevice = nullptr;

// puts our hooks in a table. a table we already know keeps the originals it had; if somebody
// else overwrote one of our slots meanwhile, what they put there becomes the new original
// so we stay in front of them and they keep working
// a vtable that lives inside a loaded module stays put for the whole run. one on the heap belongs to
// a wrapper object that can be freed at any moment, and writing into freed memory corrupts the heap
bool inModuleImage(void* p) {
  MEMORY_BASIC_INFORMATION mbi = {};
  return VirtualQuery(p, &mbi, sizeof mbi) && mbi.Type == MEM_IMAGE;
}

bool patchTable(void** vt, bool allowHeap = false) {
  if (!allowHeap && !inModuleImage(vt)) return false;
  Hooked* h = hookedForTable(vt);
  if (!h) {
    long n = g_hookedCount;
    if (n >= MAX_HOOKED) return false;
    Hooked& e = g_hooked[n];
    e.table = vt;
    e.es = (EndScene_t)vt[42];
    e.rs = (Reset_t)vt[16];
    e.pr = (Present_t)vt[17];
    if (!e.es || !e.rs || !e.pr) { e.table = nullptr; return false; }
    g_hookedCount = n + 1;  // the record is complete before anyone can look it up
    h = &e;
  } else {
    if (vt[42] != (void*)hkEndScene) h->es = (EndScene_t)vt[42];
    if (vt[16] != (void*)hkReset) h->rs = (Reset_t)vt[16];
    if (vt[17] != (void*)hkPresent) h->pr = (Present_t)vt[17];
  }
  void* unused = nullptr;  // originals live in the Hooked record
  return patchSlot(vt, 42, (void*)hkEndScene, &unused) && patchSlot(vt, 16, (void*)hkReset, &unused) &&
         patchSlot(vt, 17, (void*)hkPresent, &unused);
}

bool patchDevice(IDirect3DDevice9* dev, const char* who, bool allowHeap = false) {
  void** vt = *reinterpret_cast<void***>(dev);
  void *es = vt[42], *rs = vt[16], *pr = vt[17];
  bool ok = patchTable(vt, allowHeap);
  if (ok) g_patchedDevice = dev;
  logf("patched %s device %p vtable %p (EndScene %p, Reset %p, Present %p) -> %s", who, (void*)dev, (void*)vt, es, rs, pr,
       ok ? "ok" : (allowHeap || inModuleImage(vt) ? "FAILED" : "skipped, temporary heap vtable"));
  return ok;
}

// the game keeps its device in a global; that is the one that actually gets drawn with
bool looksLikeDevice(void* p) {
  void* vt = nullptr;
  if (!p || !readPtr((uintptr_t)p, vt) || !vt) return false;
  void* fn = nullptr;
  if (!readPtr((uintptr_t)vt + 42 * sizeof(void*), fn) || !fn) return false;
  MEMORY_BASIC_INFORMATION mbi = {};
  if (!VirtualQuery(fn, &mbi, sizeof mbi)) return false;
  return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

bool gameDevice(void*& out) {
  void* p = nullptr;
  if (!readPtr(ADDR_GAME_DEVICE, p) || !p || !looksLikeDevice(p)) return false;
  out = p;
  g_gameDeviceSeen = true;
  return true;
}

bool hookGameDevice() {
  void* lastHeap = nullptr;
  int heapTicks = 0;
  for (int i = 0; i < 480; i++) {  // up to ~2 minutes
    void* p = nullptr;
    if (gameDevice(p)) {
      void** vt = *reinterpret_cast<void***>(p);
      if (inModuleImage(vt)) return patchDevice(static_cast<IDirect3DDevice9*>(p), "game");
      // the game makes a short-lived wrapper device while it starts. only if the same one is
      // still there after a while is it the real thing
      heapTicks = p == lastHeap ? heapTicks + 1 : 0;
      lastHeap = p;
      if (heapTicks == 0) logf("game device %p has a heap vtable, waiting to see if it stays", p);
      if (heapTicks >= 80) return patchDevice(static_cast<IDirect3DDevice9*>(p), "game", true);
    }
    Sleep(250);
  }
  logf("game device pointer never appeared");
  return false;
}

// fallback for other exes: a throwaway device shares the vtable with the real one
bool hookDummyDevice() {
  IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
  if (!d3d) { logf("Direct3DCreate9 failed"); return false; }

  HWND tmp = CreateWindowExA(0, "STATIC", "fawwk", WS_OVERLAPPED, 0, 0, 8, 8, nullptr, nullptr, g_module, nullptr);
  D3DPRESENT_PARAMETERS pp = {};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.BackBufferFormat = D3DFMT_UNKNOWN;
  pp.hDeviceWindow = tmp;

  IDirect3DDevice9* dev = nullptr;
  HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, tmp, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES, &pp, &dev);
  if (FAILED(hr) || !dev) {
    logf("dummy device failed (0x%08lx)", (unsigned long)hr);
    if (tmp) DestroyWindow(tmp);
    d3d->Release();
    return false;
  }
  bool ok = patchDevice(dev, "dummy");
  dev->Release();
  d3d->Release();
  if (tmp) DestroyWindow(tmp);
  return ok;
}

bool installHooks() {
  if (hookGameDevice()) return true;
  logf("trying a throwaway device instead");
  return hookDummyDevice();
}

// other mods that touch the same device can replace the game's device or overwrite our
// slots. this keeps an eye on it for as long as the game runs
void watchdog() {
  long lastCalls = -1;
  int quiet = 0;
  for (int sec = 1;; sec++) {
    Sleep(1000);

    void* p = nullptr;
    static void* skipped = nullptr;
    if (gameDevice(p) && p != g_patchedDevice && p != skipped) {
      logf("the game's device changed (%p -> %p)", g_patchedDevice, p);
      if (!patchDevice(static_cast<IDirect3DDevice9*>(p), "game")) skipped = p;
    }

    long calls = g_endSceneCalls + g_presentCalls;
    quiet = calls == lastCalls ? quiet + 1 : 0;
    lastCalls = calls;
    if (quiet >= 3) {
      // nothing is calling us. if someone overwrote a slot, put ours back in front of theirs
      for (long i = 0; i < g_hookedCount; i++) {
        void** t = g_hooked[i].table;
        bool ours = t[42] == (void*)hkEndScene && t[17] == (void*)hkPresent && t[16] == (void*)hkReset;
        if (!ours) {
          logf("hooks in table %p were replaced by something else, putting them back in front", (void*)t);
          patchTable(t);
        }
      }
      quiet = 0;
    }

    if (sec % 3 == 0 && g_inputHooksDone) hookKeyboardReaders();  // mods that loaded after us

    if (sec <= 30 && sec % 5 == 0) logf("alive check: EndScene calls=%ld, Present calls=%ld, menu frames=%ld", g_endSceneCalls, g_presentCalls, g_frames);
    if (sec == 30 && g_endSceneCalls == 0 && g_presentCalls == 0)
      logf("the game never called the hooked functions, so the menu cannot appear (another overlay or d3d9 wrapper may be replacing the device)");
  }
}

// disk -------------------------------------------------------------------------------------

fw::Files makeFiles() {
  namespace fs = std::filesystem;
  fw::Files f;
  f.read = [](const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
  };
  f.write = [](const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    {
      std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      if (!out) return false;
      out << data;
      if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (!ec) return true;
    std::ofstream direct(path, std::ios::binary | std::ios::trunc);
    if (!direct) return false;
    direct << data;
    return (bool)direct;
  };
  f.mkdir = [](const std::string& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
  };
  return f;
}

// the log is on by default. turning it off writes one last line and removes the file, turning it on again starts it again
void applyLogSetting() {
  bool want = cfgBool("log", true);
  if (want == g_logOn) return;
  if (want) {
    g_logOn = true;
    logf("log turned on in the menu");
  } else {
    logf("log turned off in the menu");
    g_logOn = false;
    std::remove(g_logPath.c_str());
  }
}

DWORD WINAPI initThread(LPVOID) {
  try {
    // nothing touches the disk until the game has brought d3d9 up, which is well after modloader is done starting
    char path[MAX_PATH] = {};
    for (int i = 0; i < 480 && !GetModuleHandleA("d3d9.dll"); i++) Sleep(250);
    pickDataDir();
    pickLogPath();
    GetModuleFileNameA(g_module, path, MAX_PATH);
    logf("fawwk menu %s starting, plugin %s, settings in %s", VERSION, path, g_dir.c_str());
    if (!g_probeNote.empty()) logf("folders that were refused first:\n%s", g_probeNote.c_str());
    AddVectoredExceptionHandler(0, crashNote);

    migrateOldSettings();
    g_store = std::make_unique<fw::Store>(makeFiles(), g_dir, nowSeconds, std::to_string((long long)std::time(nullptr)));
    loadLangFiles();
    registerOwnSettings();
    applyLanguage();
    g_store->autosave = cfgBool("autosave", false);
    applyLogSetting();

    Menu::Env env;
    env.store = g_store.get();
    env.scale = [] { return (float)cfgNum("scale", 1); };
    env.winSize = [] { return (float)cfgNum("winsize", 0.7); };
    env.inlineDesc = [] { return cfgBool("inlinedesc", false); };
    env.close = [] { setOpen(false); };
    env.now = nowSeconds;
    env.alpha = [] { return (float)((nowSeconds() - g_openedAt) / 0.14); };
    env.icon = [](const std::string& path) { return g_icons.get(g_imguiDev, path); };
    g_menu = std::make_unique<Menu>(env);

    g_store->subscribe([](fw::Mod* m, const std::string& kind, const std::string& id, const std::string& source) {
      if (kind == "saved") { if (g_menu) g_menu->setStatus("saved"); return; }
      if (m && !m->internal && (kind == "registered" || kind == "unregistered"))
        logf("mod %s: %s (%s), %d settings", kind.c_str(), m->name.c_str(), m->id.c_str(), (int)m->specs.size());
      if (!m || m->id != MENU_ID) return;
      if (kind == "b") {
        if (id == "saveall") {
          int n = g_store->flush(true);
          g_menu->setStatus("wrote " + std::to_string(n) + (n == 1 ? " file" : " files"));
        } else if (id == "resetlook") {
          for (const char* sid : {"theme", "accent", "scale", "winsize", "opacity", "rounding"}) g_store->reset(*m, m->byId[sid], "internal");
        }
        return;
      }
      if (kind != "v") return;
      if (id == "theme" && source != "internal") g_store->setNum(MENU_ID, "accent", theme::accentOf((int)cfgNum("theme", 1)), "internal");
      if (id == "theme" || id == "accent" || id == "opacity" || id == "rounding" || id == "scale") g_themeDirty = true;
      if (id == "autosave") g_store->autosave = cfgBool("autosave", false);
      if (id == "log") applyLogSetting();
      if (id == "language") applyLanguage();
    });

    fw::setGlobalStore(g_store.get());
    logf("ready, scripts can register now");

    if (HMODULE u = GetModuleHandleA("user32.dll")) {
      g_realAsync = reinterpret_cast<decltype(g_realAsync)>(GetProcAddress(u, "GetAsyncKeyState"));
      g_realState = reinterpret_cast<decltype(g_realState)>(GetProcAddress(u, "GetKeyState"));
    }

    Sleep(1500);
    bool hooked = installHooks();
    if (!hooked) logf("giving up on drawing, settings still work for scripts");

    watchdog();
  } catch (const std::exception& e) {
    logf("init failed: %s", e.what());
  } catch (...) {
    logf("init failed: unknown error");
  }
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = module;
    DisableThreadLibraryCalls(module);
    HANDLE h = CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
  } else if (reason == DLL_PROCESS_DETACH && g_store) {
    if (g_store->lock.try_lock()) {
      if (g_store->autosave) g_store->flush(true);
      g_store->lock.unlock();
    }
    g_hideInput = false;
    g_hideKeys = false;
    undoGameMode();
  }
  return TRUE;
}
