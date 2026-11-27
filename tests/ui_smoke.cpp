// runs the real menu code for a few hundred frames against a real imgui,
// with no window and no gpu. imgui asserts on misuse (duplicate ids, unbalanced
// begin/end), so reaching the end means the layout code is at least well formed.
// build and run with tests/run_ui_smoke.sh
#include <cstdio>
#include <cstdlib>
#include <map>

#include "core/lang.hpp"
#include "core/store.hpp"
#include "demo.hpp"
#include "imgui.h"
#include "theme.hpp"
#include "ui.hpp"

static std::map<std::string, std::string> disk;

int main() {
  fw::Files files;
  files.read = [](const std::string& p, std::string& out) {
    auto it = disk.find(p);
    if (it == disk.end()) return false;
    out = it->second;
    return true;
  };
  files.write = [](const std::string& p, const std::string& d) { disk[p] = d; return true; };
  files.mkdir = [](const std::string&) {};

  double clock = 0;
  fw::Store store(files, "cfg", [&] { return clock; }, "1");
  demo::add(store);

  fw::Spec s;
  std::string err;
  fw::Map meta = {{"id", "fawwk.menu"}, {"name", "Menu settings"}, {"category", "System"}};
  std::vector<fw::Spec> specs;
  fw::parseSpec({{"type", "bool"}, {"id", "x"}, {"label", "X"}, {"default", "1"}, {"page", "General"}}, s, err);
  specs.push_back(s);
  store.registerMod(meta, std::move(specs), {}, true, err);

  // a long name and many settings, to exercise clipping and tabs
  for (int m = 0; m < 6; m++) {
    std::vector<fw::Spec> sp;
    for (int i = 0; i < 40; i++) {
      fw::parseSpec({{"type", i % 3 == 0 ? "bool" : "float"}, {"id", "s" + std::to_string(i)}, {"label", "Setting number " + std::to_string(i)},
                     {"default", "1"}, {"min", "0"}, {"max", "9"}, {"page", i < 20 ? "General" : "Second page"},
                     {"group", i % 10 == 0 ? "Group/Sub" : ""}},
                    s, err);
      sp.push_back(s);
    }
    store.registerMod({{"id", "bulk.mod" + std::to_string(m)}, {"name", "A rather long mod name that has to be cut " + std::to_string(m)},
                       {"category", m % 2 ? "Gameplay" : "Visuals"}, {"version", "1.2.3"}, {"author", "somebody"}},
                      std::move(sp), {}, false, err);
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(1366, 768);
  unsigned char* px;
  int w, h;
  io.Fonts->AddFontDefault();
  io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
  io.Fonts->SetTexID((ImTextureID)1);

  float scale = 1.f;
  Menu::Env env;
  env.store = &store;
  env.scale = [&] { return scale; };
  env.inlineDesc = [] { return false; };
  env.close = [] {};
  env.now = [&] { return clock; };
  env.alpha = [&] { return 1.f; };
  Menu menu(env);

  theme::Options o;
  int frames = 0, maxVerts = 0;
  for (int preset = 1; preset <= (int)theme::presets().size(); preset++) {
    for (float sc : {0.8f, 1.f, 1.6f}) {
      scale = sc;
      o.preset = preset;
      o.scale = sc;
      theme::apply(o);
      io.FontGlobalScale = sc;
      for (int i = 0; i < 24; i++) {
        clock += 0.016;
        io.DeltaTime = 0.016f;
        // wander the mouse over the window and click now and then
        io.AddMousePosEvent(60 + (i * 53) % 1200, 40 + (i * 37) % 680);
        io.AddMouseButtonEvent(0, i % 4 == 0);
        if (i == 10) menu.sel = "bulk.mod2";
        if (i == 14) menu.sel = demo::ID;
        ImGui::NewFrame();
        menu.draw();
        ImGui::Render();
        maxVerts = std::max(maxVerts, ImGui::GetDrawData()->TotalVtxCount);
        frames++;
      }
    }
  }
  // every language draws without trouble
  fw::builtinStrings();
  for (const char* code : {"pt", "es", "ru", "en"}) {
    fw::setLanguage(code);
    for (int i = 0; i < 4; i++) {
      clock += 0.016;
      ImGui::NewFrame();
      menu.draw();
      ImGui::Render();
      frames++;
    }
  }
  // a single page in a small window of its own
  store.showRequest = std::string(demo::ID) + "\nAdvanced";
  for (int i = 0; i < 5; i++) {
    clock += 0.016;
    ImGui::NewFrame();
    menu.draw();
    ImGui::Render();
    frames++;
  }
  if (!menu.popup) { std::printf("popup did not open\n"); return 1; }
  menu.popup = false;
  // search path
  menu.sel = demo::ID;
  io.AddInputCharactersUTF8("pow");
  for (int i = 0; i < 5; i++) {
    clock += 0.016;
    ImGui::NewFrame();
    menu.draw();
    ImGui::Render();
    frames++;
  }
  std::printf("ui smoke: %d frames ok, up to %d vertices in one frame\n", frames, maxVerts);
  ImGui::DestroyContext();
  return maxVerts > 100 ? 0 : 1;
}
