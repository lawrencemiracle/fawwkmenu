// fawwk.hpp
// part of fawwk menu, made by fawwktrustdigital (MIT licence).
// header-only client for fawwk menu, for asi / c++ mods.
// copy this one file into your project. no libs to link, it finds the menu at runtime.
//
//   static float power = 1.5f;
//   static bool enabled = true;
//   fawwk::Mod menu("you.mymod", "My Mod", "Gameplay");
//   menu.toggle("enabled", "Enabled", &enabled, true)
//       .slider("power", "Power", &power, 1.5f, 0.5f, 5.f, {.step = 0.1});   // c++20 for the {.x = ..} syntax
//   // every frame, from your own update:  menu.poll();
//
// your variables always hold the current value. without the menu installed they
// just keep their defaults.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fawwk {

struct Opts {
  const char* desc = "";   // tooltip
  const char* tag = "";    // small note after the label, like "restart"
  const char* show = "";   // only show when another setting says so: "enabled", "!enabled", "mode=2"
  const char* fmt = "";    // slider format, "%.1fx"
  const char* text = "";   // caption for buttons
  const char* page = nullptr;   // override the current page
  const char* group = nullptr;  // override the current group
  double step = 0;
  bool input = false;  // typed box instead of a slider
  bool radio = false;  // combo: radio buttons instead of a dropdown
  bool alpha = false;  // colors: let the user pick alpha
  int maxlen = 0;      // text: max length
  // translations, keys like "label_pt", "desc_pt", "text_pt", "options_pt" (options separated by |)
  std::map<std::string, std::string> tr;
};

namespace detail {

inline std::string enc(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string o;
  for (unsigned char c : s) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '.' || c == '_' || c == '~') {
      o += (char)c;
    } else {
      o += '%';
      o += hex[c >> 4];
      o += hex[c & 15];
    }
  }
  return o;
}

inline int hv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline std::string dec(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && hv(s[i + 1]) >= 0 && hv(s[i + 2]) >= 0) {
      o += (char)(hv(s[i + 1]) * 16 + hv(s[i + 2]));
      i += 2;
    } else {
      o += s[i];
    }
  }
  return o;
}

using Map = std::map<std::string, std::string>;

inline std::string pack(const Map& m) {
  std::string o;
  for (auto& kv : m) {
    if (!o.empty()) o += '&';
    o += enc(kv.first) + "=" + enc(kv.second);
  }
  return o;
}

inline Map unpack(const std::string& s) {
  Map m;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = s.find('&', i);
    if (j == std::string::npos) j = s.size();
    std::string pair = s.substr(i, j - i);
    size_t eq = pair.find('=');
    if (eq != std::string::npos) m[dec(pair.substr(0, eq))] = dec(pair.substr(eq + 1));
    i = j + 1;
  }
  return m;
}

// "A|B|C" -> each part percent-encoded, still joined with |
inline std::string encodeList(const std::string& list) {
  std::string out, part;
  for (size_t i = 0; i <= list.size(); i++) {
    if (i == list.size() || list[i] == '|') {
      out += (out.empty() ? "" : "|") + enc(part);
      part.clear();
    } else {
      part += list[i];
    }
  }
  return out;
}

inline std::vector<std::string> lines(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (true) {
    size_t j = s.find('\n', i);
    if (j == std::string::npos) { out.push_back(s.substr(i)); break; }
    out.push_back(s.substr(i, j - i));
    i = j + 1;
  }
  return out;
}

inline std::string num(double n) {
  char b[64];
  if (n == (double)(long long)n && n > -1e15 && n < 1e15) std::snprintf(b, sizeof b, "%lld", (long long)n);
  else std::snprintf(b, sizeof b, "%.10g", n);
  return b;
}

// the exports, looked up once
struct Api {
  using F0 = const char*(__cdecl*)();
  using F1 = const char*(__cdecl*)(const char*);
  using F2 = const char*(__cdecl*)(const char*, const char*);
  using F3 = const char*(__cdecl*)(const char*, const char*, const char*);
  F0 hello = nullptr;
  F3 reg = nullptr;
  F1 unreg = nullptr, dump = nullptr, show = nullptr, save = nullptr;
  F2 poll = nullptr;
  F3 set = nullptr;
  bool open = false;  // is the menu on screen right now
  std::string lang = "en";  // language picked in the menu

  bool ready() const { return hello && reg && poll; }

  static Api& get() {
    static Api a;
    return a;
  }

  bool resolve() {
    if (ready()) return true;
    HMODULE m = GetModuleHandleA("fawwkmenu.asi");
    if (!m) return false;
    hello = (F0)GetProcAddress(m, "fawwk_hello");
    reg = (F3)GetProcAddress(m, "fawwk_register");
    unreg = (F1)GetProcAddress(m, "fawwk_unregister");
    poll = (F2)GetProcAddress(m, "fawwk_poll");
    dump = (F1)GetProcAddress(m, "fawwk_dump");
    set = (F3)GetProcAddress(m, "fawwk_set");
    show = (F1)GetProcAddress(m, "fawwk_show");
    save = (F1)GetProcAddress(m, "fawwk_save");
    return ready();
  }
};

}  // namespace detail

class Mod {
 public:
  Mod(std::string id, std::string name, std::string category = "General")
      : id_(std::move(id)), name_(std::move(name)), category_(std::move(category)) {}
  Mod(const Mod&) = delete;
  Mod& operator=(const Mod&) = delete;
  ~Mod() {
    auto& api = detail::Api::get();
    if (online_ && api.unreg) api.unreg(id_.c_str());
  }

  Mod& version(std::string v) { version_ = std::move(v); return *this; }
  Mod& author(std::string v) { author_ = std::move(v); return *this; }
  // optional picture shown next to the name: a png, jpg, bmp or tga up to 512 x 512. a relative path is
  // taken from the game folder
  Mod& icon(std::string path) { icon_ = std::move(path); return *this; }
  Mod& description(std::string v) { description_ = std::move(v); return *this; }

  // the second argument is optional: page("Keys", {{"pt", "Teclas"}, {"es", "Teclas"}})
  Mod& page(std::string name, std::map<std::string, std::string> tr = {}) {
    page_ = std::move(name); group_.clear(); page_tr_ = std::move(tr); group_tr_.clear(); return *this;
  }
  Mod& group(std::string name, std::map<std::string, std::string> tr = {}) {
    group_ = std::move(name); group_tr_ = std::move(tr); return *this;
  }
  // name and description in other languages: translate("pt", "Nome", "Descrição")
  Mod& translate(const std::string& code, const std::string& name, const std::string& desc = "") {
    meta_tr_["name_" + code] = name;
    if (!desc.empty()) meta_tr_["description_" + code] = desc;
    return *this;
  }
  // the language the player picked in the menu: "en", "pt", "es", "ru"
  static std::string language() { return detail::Api::get().lang; }

  Mod& toggle(const char* id, const char* label, bool* var, bool def, Opts o = {}) {
    *var = def;
    detail::Map m = base("bool", id, label, o);
    m["default"] = def ? "1" : "0";
    bind(id, std::move(m), [var](const std::string& v) { *var = v == "1"; });
    return *this;
  }

  Mod& slider(const char* id, const char* label, float* var, float def, float min, float max, Opts o = {}) {
    *var = def;
    detail::Map m = base("float", id, label, o);
    m["default"] = detail::num(def);
    m["min"] = detail::num(min);
    m["max"] = detail::num(max);
    m["step"] = detail::num(o.step);
    bind(id, std::move(m), [var](const std::string& v) { *var = (float)std::atof(v.c_str()); });
    return *this;
  }

  // a bar the player can only look at. push values into it with set()
  Mod& meter(const char* id, const char* label, float min, float max, Opts o = {}) {
    detail::Map m = base("float", id, label, o);
    m["widget"] = "meter";
    m["default"] = detail::num(min);
    m["min"] = detail::num(min);
    m["max"] = detail::num(max);
    bind(id, std::move(m), [](const std::string&) {});
    return *this;
  }

  void set(const char* id, double value) {
    auto& api = detail::Api::get();
    if (!api.set || !online_) return;
    auto last = lastSet_.find(id);
    if (last != lastSet_.end() && last->second == value) return;
    lastSet_[id] = value;
    api.set(id_.c_str(), id, detail::num(value).c_str());
  }

  Mod& integer(const char* id, const char* label, int* var, int def, int min, int max, Opts o = {}) {
    *var = def;
    detail::Map m = base("int", id, label, o);
    m["default"] = detail::num(def);
    m["min"] = detail::num(min);
    m["max"] = detail::num(max);
    if (o.step > 0) m["step"] = detail::num(o.step);
    bind(id, std::move(m), [var](const std::string& v) { *var = (int)std::atof(v.c_str()); });
    return *this;
  }

  // your variable is a plain 0-based index
  Mod& combo(const char* id, const char* label, int* var, int def, const std::vector<std::string>& options, Opts o = {}) {
    *var = def;
    detail::Map m = base("enum", id, label, o);
    m["default"] = detail::num(def + 1);
    std::string joined;
    for (auto& s : options) joined += (joined.empty() ? "" : "|") + detail::enc(s);
    m["options"] = joined;
    bind(id, std::move(m), [var](const std::string& v) { *var = (int)std::atof(v.c_str()) - 1; });
    return *this;
  }

  // 0xAARRGGBB
  Mod& color(const char* id, const char* label, uint32_t* var, uint32_t def, Opts o = {}) {
    *var = def;
    detail::Map m = base("color", id, label, o);
    m["default"] = detail::num((double)def);
    m["alpha"] = o.alpha ? "1" : "0";
    bind(id, std::move(m), [var](const std::string& v) { *var = (uint32_t)std::strtoull(v.c_str(), nullptr, 10); });
    return *this;
  }

  // windows virtual key code, 0 means unbound
  Mod& key(const char* id, const char* label, int* var, int def, Opts o = {}) {
    *var = def;
    detail::Map m = base("key", id, label, o);
    m["default"] = detail::num(def);
    bind(id, std::move(m), [var](const std::string& v) { *var = (int)std::atof(v.c_str()); });
    return *this;
  }

  Mod& text(const char* id, const char* label, std::string* var, const std::string& def, Opts o = {}) {
    *var = def;
    detail::Map m = base("text", id, label, o);
    m["default"] = def;
    if (o.maxlen > 0) m["maxlen"] = detail::num(o.maxlen);
    bind(id, std::move(m), [var](const std::string& v) { *var = v; });
    return *this;
  }

  Mod& button(const char* id, const char* label, std::function<void()> fn, Opts o = {}) {
    detail::Map m = base("button", id, label, o);
    specs_.push_back(std::move(m));
    buttons_[id] = std::move(fn);
    dirty_ = true;
    return *this;
  }

  // a list the player can look through, with a button per action on every row (4 at most).
  // fill it with rows(), and onRow gets the 1-based row and action
  Mod& list(const char* id, const char* label, const std::vector<std::string>& actions, std::function<void(int row, int action)> onRow, Opts o = {}) {
    detail::Map m = base("list", id, label, o);
    std::string joined;
    for (auto& a : actions) joined += (joined.empty() ? "" : "|") + detail::enc(a);
    m["options"] = joined;
    specs_.push_back(std::move(m));
    rowHandlers_[id] = std::move(onRow);
    dirty_ = true;
    return *this;
  }

  // title and detail of every row
  void rows(const char* id, const std::vector<std::pair<std::string, std::string>>& items) {
    auto clean = [](std::string s) {
      for (char& c : s) if ((unsigned char)c < 32) c = ' ';
      return s;
    };
    std::string text;
    for (auto& it : items) {
      if (!text.empty()) text += '\n';
      text += clean(it.first);
      if (!it.second.empty()) text += '\t' + clean(it.second);
    }
    auto& api = detail::Api::get();
    if (!api.set || !online_) return;
    auto last = lastText_.find(id);
    if (last != lastText_.end() && last->second == text) return;
    lastText_[id] = text;
    api.set(id_.c_str(), id, text.c_str());
  }

  Mod& header(const char* text, Opts o = {}) { return layout("header", text, o); }
  Mod& separator(Opts o = {}) { return layout("separator", "", o); }
  Mod& note(const char* text, Opts o = {}) { return layout("note", text, o); }

  // call once per frame
  void poll() {
    auto& api = detail::Api::get();
    if (!api.resolve()) return;
    if (!online_ || dirty_) {
      if (GetTickCount64() < nextTry_) return;
      if (!registerNow()) { nextTry_ = GetTickCount64() + 1500; return; }
    }
    std::string r = api.poll(id_.c_str(), std::to_string(seq_).c_str());
    if (r == "unknown") { online_ = false; dirty_ = true; return; }
    auto ls = detail::lines(r);
    unsigned long epoch = 0;
    int open = 0, resync = 0;
    long head = 0;
    if (std::sscanf(ls[0].c_str(), "%*[^,],%d,%ld,%d", &open, &head, &resync) != 3) return;
    (void)epoch;
    api.open = open != 0;
    {
      size_t c = 0;  // the language is the fifth field of the header: epoch,open,seq,resync,lang
      for (int n = 0; n < 4 && c != std::string::npos; n++) c = ls[0].find(',', n == 0 ? 0 : c + 1);
      if (c != std::string::npos && c + 1 < ls[0].size()) api.lang = ls[0].substr(c + 1);
    }
    if (resync) {
      std::string d = api.dump(id_.c_str());
      auto dl = detail::lines(d);
      if (dl.size() >= 2) {
        seq_ = std::atol(dl[0].c_str());
        applyValues(dl[1]);
      }
      return;
    }
    for (size_t i = 1; i < ls.size(); i++) {
      // seq|kind|id|value
      const std::string& l = ls[i];
      size_t a = l.find('|'), b = a == std::string::npos ? a : l.find('|', a + 1), c = b == std::string::npos ? b : l.find('|', b + 1);
      if (c == std::string::npos) continue;
      char kind = l[a + 1];
      std::string id = l.substr(b + 1, c - b - 1);
      if (kind == 'v') {
        auto it = setters_.find(id);
        if (it != setters_.end()) it->second(detail::dec(l.substr(c + 1)));
      } else if (kind == 'b' && rowHandlers_.count(id)) {
        int row = 0, action = 0;
        if (std::sscanf(detail::dec(l.substr(c + 1)).c_str(), "%d,%d", &row, &action) == 2 && rowHandlers_[id]) rowHandlers_[id](row, action);
      } else if (kind == 'b') {
        auto it = buttons_.find(id);
        if (it != buttons_.end() && it->second) it->second();
      }
    }
    seq_ = head;
  }

  // read the values from the menu again and apply them (after the mod re-read its own ini, say)
  void resync() { dirty_ = true; }

  bool online() const { return online_; }
  static bool menuOpen() { return detail::Api::get().open; }
  void show() { auto& a = detail::Api::get(); if (a.show) a.show(id_.c_str()); }
  // one page in a small window of its own, not the whole menu
  void popup(const std::string& page) { auto& a = detail::Api::get(); if (a.show) a.show((id_ + "\n" + page).c_str()); }

 private:
  std::string id_, name_, category_, version_, author_, description_, icon_;
  std::string page_ = "General", group_;
  std::map<std::string, std::string> page_tr_, group_tr_, meta_tr_;
  std::vector<detail::Map> specs_;
  std::map<std::string, std::function<void(const std::string&)>> setters_;
  std::map<std::string, std::function<void()>> buttons_;
  std::map<std::string, double> lastSet_;
  std::map<std::string, std::string> lastText_;
  std::map<std::string, std::function<void(int, int)>> rowHandlers_;
  bool online_ = false, dirty_ = true;
  long seq_ = 0;
  ULONGLONG nextTry_ = 0;

  detail::Map base(const char* type, const char* id, const char* label, const Opts& o) {
    detail::Map m;
    m["type"] = type;
    m["id"] = id;
    m["label"] = label;
    m["page"] = o.page ? o.page : page_;
    m["group"] = o.group ? o.group : group_;
    if (*o.desc) m["desc"] = o.desc;
    if (*o.tag) m["tag"] = o.tag;
    if (*o.show) m["show"] = o.show;
    if (*o.fmt) m["fmt"] = o.fmt;
    if (*o.text) m["text"] = o.text;
    if (o.input) m["widget"] = "input";
    else if (o.radio) m["widget"] = "radio";
    for (const auto& kv : o.tr) m[kv.first] = kv.first.rfind("options_", 0) == 0 ? detail::encodeList(kv.second) : kv.second;
    if (page_tr_.size() && !o.page) for (const auto& kv : page_tr_) m["page_" + kv.first] = kv.second;
    if (group_tr_.size() && !o.group) for (const auto& kv : group_tr_) m["group_" + kv.first] = kv.second;
    return m;
  }

  Mod& layout(const char* type, const char* text, const Opts& o) {
    detail::Map m = base(type, "", text, o);
    m.erase("id");
    specs_.push_back(std::move(m));
    dirty_ = true;
    return *this;
  }

  void bind(const char* id, detail::Map spec, std::function<void(const std::string&)> setter) {
    specs_.push_back(std::move(spec));
    setters_[id] = std::move(setter);
    dirty_ = true;
  }

  void applyValues(const std::string& packed) {
    for (auto& kv : detail::unpack(packed)) {
      auto it = setters_.find(kv.first);
      if (it != setters_.end()) it->second(kv.second);
    }
  }

  bool registerNow() {
    auto& api = detail::Api::get();
    detail::Map meta = {{"id", id_}, {"name", name_}, {"category", category_},
                        {"version", version_}, {"author", author_}, {"description", description_}, {"icon", icon_}};
    for (const auto& kv : meta_tr_) meta[kv.first] = kv.second;
    std::string specText;
    for (auto& s : specs_) specText += (specText.empty() ? "" : "\n") + detail::pack(s);
    std::string r = api.reg(detail::pack(meta).c_str(), specText.c_str(), "");
    auto ls = detail::lines(r);
    if (ls.empty() || ls[0] != "ok") return false;
    seq_ = ls.size() > 1 ? std::atol(ls[1].c_str()) : 0;
    if (ls.size() > 2) applyValues(ls[2]);
    online_ = true;
    dirty_ = false;
    return true;
  }
};

}  // namespace fawwk
