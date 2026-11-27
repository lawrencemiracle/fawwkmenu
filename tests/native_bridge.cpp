// native_bridge.cpp
// test rig: runs a lua script against the real c++ core. the lua side gets the
// same string exports a game script would see, plus a few "T" helpers that act
// like the menu ui (change a value, press a button, restart the host...).
//
// build (linux): see tests/run_native.sh
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>

#include "../src/core/api.hpp"
#include "../src/core/lang.hpp"
#include "../src/core/store.hpp"

extern "C" {
typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State*);
lua_State* luaL_newstate(void);
void luaL_openlibs(lua_State*);
int luaL_loadfilex(lua_State*, const char*, const char*);
int lua_pcallk(lua_State*, int, int, int, intptr_t, void*);
const char* lua_tolstring(lua_State*, int, size_t*);
double lua_tonumberx(lua_State*, int, int*);
int lua_toboolean(lua_State*, int);
const char* lua_pushstring(lua_State*, const char*);
void lua_pushboolean(lua_State*, int);
void lua_pushnumber(lua_State*, double);
void lua_pushcclosure(lua_State*, lua_CFunction, int);
void lua_setglobal(lua_State*, const char*);
void lua_createtable(lua_State*, int, int);
void lua_setfield(lua_State*, int, const char*);
const char* fawwk_hello();
const char* fawwk_register(const char*, const char*, const char*);
const char* fawwk_unregister(const char*);
const char* fawwk_poll(const char*, const char*);
const char* fawwk_dump(const char*);
const char* fawwk_set(const char*, const char*, const char*);
const char* fawwk_save(const char*);
const char* fawwk_show(const char*);
}

static std::map<std::string, std::string> disk;
static double clockNow = 0;
static int bootCount = 0;
static std::unique_ptr<fw::Store> store;

static void boot() {
  fw::Files f;
  f.read = [](const std::string& p, std::string& out) {
    auto it = disk.find(p);
    if (it == disk.end()) return false;
    out = it->second;
    return true;
  };
  f.write = [](const std::string& p, const std::string& d) { disk[p] = d; return true; };
  f.mkdir = [](const std::string&) {};
  fw::setGlobalStore(nullptr);
  store.reset();
  store = std::make_unique<fw::Store>(f, "cfg", [] { return clockNow; }, "boot" + std::to_string(++bootCount));
  fw::setGlobalStore(store.get());
}

static std::string arg(lua_State* L, int i) {
  const char* s = lua_tolstring(L, i, nullptr);
  return s ? s : "";
}
static int ret(lua_State* L, const std::string& s) { lua_pushstring(L, s.c_str()); return 1; }
static int retb(lua_State* L, bool b) { lua_pushboolean(L, b); return 1; }

#define EXPORT1(name, call) static int l_##name(lua_State* L) { return ret(L, call); }
EXPORT1(fawwk_hello, fawwk_hello())
EXPORT1(fawwk_register, fawwk_register(arg(L, 1).c_str(), arg(L, 2).c_str(), arg(L, 3).c_str()))
EXPORT1(fawwk_unregister, fawwk_unregister(arg(L, 1).c_str()))
EXPORT1(fawwk_poll, fawwk_poll(arg(L, 1).c_str(), arg(L, 2).c_str()))
EXPORT1(fawwk_dump, fawwk_dump(arg(L, 1).c_str()))
EXPORT1(fawwk_set, fawwk_set(arg(L, 1).c_str(), arg(L, 2).c_str(), arg(L, 3).c_str()))
EXPORT1(fawwk_save, fawwk_save(arg(L, 1).c_str()))
EXPORT1(fawwk_show, fawwk_show(arg(L, 1).c_str()))

// ---- T helpers: what the ui side would do
static int t_set(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  std::string out;
  bool ok = m && store->setWire(*m, arg(L, 2), arg(L, 3), "ui", out);
  return retb(L, ok);
}
static int t_press(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  return retb(L, m && store->press(*m, arg(L, 2), "ui"));
}
static int t_reset(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  if (!m) return retb(L, false);
  auto it = m->byId.find(arg(L, 2));
  return retb(L, it != m->byId.end() && store->reset(*m, it->second, "ui"));
}
static int t_revert(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  if (!m) { lua_pushnumber(L, -1); return 1; }
  lua_pushnumber(L, store->revert(*m, "ui"));
  return 1;
}
static int t_unsaved(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  return retb(L, m && store->unsaved(*m));
}
static int t_autosave(lua_State* L) { store->autosave = lua_toboolean(L, 1) != 0; return 0; }
static int t_open(lua_State* L) { store->open = lua_toboolean(L, 1) != 0; return 0; }
static int t_flush(lua_State*) { store->flush(true); return 0; }
static int t_tick(lua_State*) { store->tick(); return 0; }
static int t_restart(lua_State*) { boot(); return 0; }
static int t_clock(lua_State* L) { clockNow = lua_tonumberx(L, 1, nullptr); return 0; }
static int t_value(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  if (!m) { lua_pushboolean(L, 0); return 1; }
  auto it = m->byId.find(arg(L, 2));
  if (it == m->byId.end()) { lua_pushboolean(L, 0); return 1; }
  return ret(L, fw::tostr(m->specs[it->second], m->values[it->second]));
}
static int t_file(lua_State* L) {
  auto it = disk.find("cfg/" + arg(L, 1) + ".ini");
  if (it == disk.end()) { lua_pushboolean(L, 0); return 1; }
  return ret(L, it->second);
}
static int t_hasmod(lua_State* L) { return retb(L, store->find(arg(L, 1)) != nullptr); }
static int t_pages(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  return ret(L, m && !m->pages.empty() ? m->pages[0] : "");
}
static int t_name(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  return ret(L, m ? m->name : "");
}
static int t_ringcount(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  int n = 0;
  if (m) for (auto& e : m->ring) if (e.id == arg(L, 2)) n++;
  lua_pushnumber(L, n);
  return 1;
}
static int t_visible(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  if (!m) return retb(L, false);
  auto it = m->byId.find(arg(L, 2));
  return retb(L, it != m->byId.end() && store->isVisible(*m, m->specs[it->second]));
}
static int t_pressRow(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  bool ok = m && store->pressRow(*m, arg(L, 2), (int)lua_tonumberx(L, 3, nullptr), (int)lua_tonumberx(L, 4, nullptr), "test");
  return retb(L, ok);
}
// language: switch it the way the menu does, and read what a spec shows
static int t_lang(lua_State* L) {
  fw::builtinStrings();
  fw::setLanguage(arg(L, 1));
  store->language = fw::language();
  lua_pushstring(L, fw::language().c_str());
  return 1;
}
static int t_shown(lua_State* L) {  // mod, setting, field -> text in the current language
  fw::Mod* m = store->find(arg(L, 1));
  if (!m) return 0;
  auto it = m->byId.find(arg(L, 2));
  if (it == m->byId.end()) return 0;
  const fw::Spec& sp = m->specs[it->second];
  std::string f = arg(L, 3), out;
  if (f == "label") out = fw::shown(sp, "label", sp.label);
  else if (f == "desc") out = fw::shown(sp, "desc", sp.desc);
  else if (f == "page") out = fw::shown(sp, "page", sp.page);
  else if (f == "group") out = fw::shown(sp, "group", sp.group);
  else if (f.rfind("option", 0) == 0) out = fw::shownOption(sp, (size_t)std::atoi(f.c_str() + 6));
  lua_pushstring(L, out.c_str());
  return 1;
}
static int t_T(lua_State* L) { lua_pushstring(L, fw::T(arg(L, 1)).c_str()); return 1; }
static int t_modname(lua_State* L) {
  fw::Mod* m = store->find(arg(L, 1));
  if (!m) return 0;
  lua_pushstring(L, fw::shownMeta(m->tr, "name", m->name).c_str());
  return 1;
}
static int t_langfile(lua_State* L) { lua_pushnumber(L, fw::parseLangFile(arg(L, 1), arg(L, 2))); return 1; }
static int t_disk_set(lua_State* L) { disk[arg(L, 1)] = arg(L, 2); return 0; }

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: native_bridge script.lua [root]\n"); return 2; }
  boot();
  lua_State* L = luaL_newstate();
  luaL_openlibs(L);
  lua_pushstring(L, argc > 2 ? argv[2] : ".");
  lua_setglobal(L, "ROOT");

  lua_createtable(L, 0, 8);
#define HOSTFN(n) lua_pushcclosure(L, l_##n, 0); lua_setfield(L, -2, #n);
  HOSTFN(fawwk_hello) HOSTFN(fawwk_register) HOSTFN(fawwk_unregister) HOSTFN(fawwk_poll)
  HOSTFN(fawwk_dump) HOSTFN(fawwk_set) HOSTFN(fawwk_save) HOSTFN(fawwk_show)
  lua_setglobal(L, "HOST");

  lua_createtable(L, 0, 16);
#define TFN(n) lua_pushcclosure(L, t_##n, 0); lua_setfield(L, -2, #n);
  TFN(revert) TFN(unsaved) TFN(autosave) TFN(set) TFN(press) TFN(reset) TFN(open) TFN(flush) TFN(tick) TFN(restart) TFN(clock) TFN(value)
  TFN(file) TFN(hasmod) TFN(pages) TFN(name) TFN(ringcount) TFN(visible) TFN(disk_set) TFN(pressRow) TFN(lang) TFN(shown) TFN(T) TFN(modname) TFN(langfile)
  lua_setglobal(L, "T");

  if (luaL_loadfilex(L, argv[1], nullptr) || lua_pcallk(L, 0, 0, 0, 0, nullptr)) {
    std::printf("LUA ERROR: %s\n", lua_tolstring(L, -1, nullptr));
    return 1;
  }
  return 0;
}
