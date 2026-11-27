// ui.hpp
// everything you see. one window: sidebar with the mods, a page per mod,
// and a search box that reaches into all of them.
#pragma once
#include <functional>
#include <map>
#include <string>

#include "core/store.hpp"
#include "imgui.h"

class Menu {
 public:
  struct Env {
    fw::Store* store = nullptr;
    std::function<float()> scale;
    std::function<float()> winSize;  // share of the screen width the window takes
    std::function<bool()> inlineDesc;
    std::function<void()> close;
    std::function<double()> now;
    std::function<float()> alpha;  // 0..1, used to fade the window in
    std::function<ImTextureID(const std::string&)> icon;  // texture for an icon file, null if there is none
  };

  explicit Menu(Env e) : env(std::move(e)) {}

  void draw();  // call once per frame, inside an imgui frame
  void setStatus(const std::string& text);
  void clearSearch();

  // key capture state, the host polls the keyboard while this is set
  struct Listen {
    bool active = false;
    std::string mod, setting;
    double since = 0;
  } listen;
  bool textActive = false;  // a text box has focus, so hotkeys should stay quiet
  std::string sel;
  // a mod can ask for just one of its pages in a small window of its own instead of the whole menu
  bool popup = false;
  std::string popupMod, popupPage;

  static const char* keyName(int vk, char* buf, size_t n);

 private:
  Env env;
  char search[64] = "";
  std::string status;
  double statusAt = -100;
  std::string confirmId;
  double confirmAt = 0;
  std::map<ImGuiID, float> anim;
  std::map<std::string, bool> closed;  // categories folded in the sidebar

  float S(float x) const { return x * env.scale(); }

  bool sw(const char* id, bool on);
  bool control(fw::Mod& m, size_t idx, float w, fw::Value& out);
  void row(fw::Mod& m, size_t idx, bool showPage);
  void listRow(fw::Mod& m, size_t idx);
  void settings(fw::Mod& m, const std::string& page);
  void modRow(fw::Mod& m);
  void sidebar(float h);
  void header(fw::Mod& m);
  void actions(fw::Mod& m);
  void modPage(fw::Mod& m);
  void searchResults(const std::string& q);
  void empty();
  void popupWindow(fw::Mod& m);
  void topBar();
  void footer();

  void muted(const std::string& t);
  void mutedWrapped(const std::string& t, float wrapAt);
  void accentText(const std::string& t);
  void tip(const std::string& t);
  bool confirmButton(const std::string& id, const char* label);
  std::string fit(const std::string& t, float w);
};
