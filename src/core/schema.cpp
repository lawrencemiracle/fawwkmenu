#include "schema.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fw {

static double clampd(double n, double lo, double hi) { return n < lo ? lo : (n > hi ? hi : n); }

static std::string lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

static std::string get(const Map& m, const char* k, const char* fallback = "") {
  auto it = m.find(k);
  return it == m.end() ? std::string(fallback) : it->second;
}

static bool parseNum(const std::string& in, double& out) {
  if (in.empty()) return false;
  char* end = nullptr;
  double d = std::strtod(in.c_str(), &end);
  if (end == in.c_str() || *end != '\0') return false;
  if (!std::isfinite(d)) return false;
  out = d;
  return true;
}

static bool idOk(const std::string& id) {
  if (id.empty() || id.size() > 64) return false;
  for (unsigned char c : id) {
    if (!(std::isalnum(c) || c == '_' || c == '.' || c == '-')) return false;
  }
  return true;
}

bool coerceNum(const Spec& s, double n, Value& out) {
  if (!std::isfinite(n)) return false;
  switch (s.type) {
    case Type::Bool:
      out.n = n != 0 ? 1 : 0;
      return true;
    case Type::Int: {
      n = clampd(std::floor(n + 0.5), s.min, s.max);
      if (s.step > 1) n = clampd(s.min + std::floor((n - s.min) / s.step + 0.5) * s.step, s.min, s.max);
      out.n = n;
      return true;
    }
    case Type::Float: {
      n = clampd(n, s.min, s.max);
      if (s.step > 0) n = clampd(s.min + std::floor((n - s.min) / s.step + 0.5) * s.step, s.min, s.max);
      char buf[48];
      std::snprintf(buf, sizeof buf, "%.10g", n);
      out.n = std::strtod(buf, nullptr);
      return true;
    }
    case Type::Enum:
      out.n = clampd(std::floor(n + 0.5), 1, (double)s.options.size());
      return true;
    case Type::Key:
      out.n = clampd(std::floor(n + 0.5), 0, 255);
      return true;
    case Type::Color: {
      n = clampd(std::floor(n), 0, 4294967295.0);
      if (!s.alpha) n = std::fmod(n, 16777216.0) + 4278190080.0;
      out.n = n;
      return true;
    }
    default:
      return false;
  }
}

bool coerceStr(const Spec& s, const std::string& in, Value& out, std::string& err) {
  if (s.type == Type::List) {
    out.s = in.size() > 32768 ? in.substr(0, 32768) : in;
    return true;
  }
  if (s.type == Type::Text) {
    out.s = in.size() > (size_t)s.maxlen ? in.substr(0, s.maxlen) : in;
    return true;
  }
  if (s.type == Type::Bool) {
    std::string l = lower(in);
    if (l == "1" || l == "true" || l == "on" || l == "yes") { out.n = 1; return true; }
    if (l == "0" || l == "false" || l == "off" || l == "no" || l.empty()) { out.n = 0; return true; }
    err = "expected true/false";
    return false;
  }
  double d;
  if (!parseNum(in, d)) { err = "expected a number"; return false; }
  if (!coerceNum(s, d, out)) { err = "this setting has no value"; return false; }
  return true;
}

std::string tostr(const Spec& s, const Value& v) {
  if (s.type == Type::Bool) return v.n != 0 ? "1" : "0";
  if (s.type == Type::Text || s.type == Type::List) return v.s;
  return numstr(v.n);
}

bool same(const Spec& s, const Value& a, const Value& b) {
  if (s.type == Type::Text || s.type == Type::List) return a.s == b.s;
  if (s.type == Type::Float) return std::fabs(a.n - b.n) < 1e-6;
  return a.n == b.n;
}

bool parseSpec(const Map& raw, Spec& s, std::string& err) {
  std::string t = lower(get(raw, "type", "bool"));
  static const std::pair<const char*, Type> names[] = {
      {"bool", Type::Bool}, {"toggle", Type::Bool}, {"checkbox", Type::Bool}, {"boolean", Type::Bool},
      {"int", Type::Int}, {"float", Type::Float}, {"slider", Type::Float}, {"number", Type::Float},
      {"enum", Type::Enum}, {"combo", Type::Enum}, {"dropdown", Type::Enum}, {"select", Type::Enum},
      {"color", Type::Color}, {"key", Type::Key}, {"hotkey", Type::Key},
      {"text", Type::Text}, {"string", Type::Text}, {"input", Type::Text},
      {"button", Type::Button},
      {"header", Type::Header}, {"heading", Type::Header}, {"section", Type::Header},
      {"separator", Type::Separator}, {"divider", Type::Separator},
      {"list", Type::List},
      {"note", Type::Note}, {"label", Type::Note}, {"info", Type::Note}, {"description", Type::Note},
  };
  bool found = false;
  for (auto& n : names) {
    if (t == n.first) { s.type = n.second; found = true; break; }
  }
  if (!found) { err = "unknown type \"" + t + "\""; return false; }

  s.id = get(raw, "id");
  if ((s.valued() || s.type == Type::Button) && !idOk(s.id)) {
    err = "bad setting id \"" + s.id + "\" (letters, digits, _ . - only, 64 max)";
    return false;
  }
  s.label = get(raw, "label", s.id.c_str());
  s.page = get(raw, "page", "General");
  if (s.page.empty()) s.page = "General";
  s.group = get(raw, "group");
  s.desc = get(raw, "desc");
  s.tag = get(raw, "tag");
  s.show = get(raw, "show");
  s.text = get(raw, "text");

  // translations that came with the spec: label_pt, desc_pt, group_pt, page_pt, text_pt, tag_pt, options_pt
  for (const auto& kv : raw) {
    const std::string& k = kv.first;
    size_t us = k.rfind('_');
    if (us == std::string::npos || us + 3 != k.size() || kv.second.empty()) continue;
    std::string base = k.substr(0, us), code = k.substr(us + 1);
    if (base == "label" || base == "desc" || base == "group" || base == "page" || base == "text" || base == "tag") {
      s.tr[k] = kv.second;
    } else if (base == "options") {
      std::vector<std::string>& v = s.optionsTr[code];
      const std::string& o = kv.second;
      for (size_t i = 0; i < o.size();) {
        size_t j = o.find('|', i);
        if (j == std::string::npos) j = o.size();
        v.push_back(dec(o.substr(i, j - i)));
        i = j + 1;
      }
    }
  }

  if (s.type == Type::Int || s.type == Type::Float) {
    bool isInt = s.type == Type::Int;
    double v;
    s.min = parseNum(get(raw, "min"), v) ? v : 0;
    s.max = parseNum(get(raw, "max"), v) ? v : (isInt ? 100 : 1);
    if (s.max < s.min) std::swap(s.min, s.max);
    s.step = parseNum(get(raw, "step"), v) ? v : (isInt ? 1 : 0);
    if (s.step < 0) s.step = 0;
    if (isInt && s.step < 1) s.step = 1;
    s.fmt = get(raw, "fmt");
    std::string w = get(raw, "widget");
    s.inputWidget = w == "input";
    s.live = w == "meter";
  } else if (s.type == Type::Enum) {
    std::string o = get(raw, "options");
    size_t i = 0;
    while (i < o.size()) {
      size_t j = o.find('|', i);
      if (j == std::string::npos) j = o.size();
      if (j > i) s.options.push_back(dec(o.substr(i, j - i)));
      i = j + 1;
    }
    s.radio = get(raw, "widget") == "radio";
    if (s.options.empty()) { err = "combo \"" + s.id + "\" needs at least one option"; return false; }
  } else if (s.type == Type::List) {
    // the options are the captions of the buttons at the end of every row
    std::string o = get(raw, "options");
    for (size_t i = 0; i < o.size();) {
      size_t j = o.find('|', i);
      if (j == std::string::npos) j = o.size();
      if (j > i && s.options.size() < 4) s.options.push_back(dec(o.substr(i, j - i)));
      i = j + 1;
    }
    s.live = true;  // filled by the mod, never stored
  } else if (s.type == Type::Color) {
  } else if (s.type == Type::Text) {
    double v;
    s.maxlen = parseNum(get(raw, "maxlen"), v) ? (int)clampd(std::floor(v), 1, 250) : 64;
  }

  if (s.valued()) {
    std::string e;
    Value d;
    auto it = raw.find("default");
    if (it != raw.end() && coerceStr(s, it->second, d, e)) {
      s.def = d;
    } else {
      switch (s.type) {
        case Type::Bool: s.def.n = 0; break;
        case Type::Int: case Type::Float: s.def.n = clampd(0, s.min, s.max); break;
        case Type::Enum: s.def.n = 1; break;
        case Type::Color: s.def.n = 4294967295.0; break;
        case Type::Key: s.def.n = 0; break;
        default: s.def.s.clear(); break;
      }
    }
  }
  return true;
}

}  // namespace fw
