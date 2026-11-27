// schema.hpp
// what a setting is, how values get cleaned up, and how specs travel as text.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "wire.hpp"

namespace fw {

enum class Type { Bool, Int, Float, Enum, Color, Key, Text, Button, Header, Separator, Note, List };

// numbers (and bools as 0/1) live in n, text lives in s
struct Value {
  double n = 0;
  std::string s;
};

struct Spec {
  Type type = Type::Bool;
  std::string id, label, page = "General", group, desc, tag, show, text;
  double min = 0, max = 1, step = 0;
  std::string fmt;
  bool inputWidget = false;
  bool radio = false;  // combo drawn as radio buttons
  bool live = false;   // meter: shown only, filled in by the mod, never stored
  std::vector<std::string> options;
  Map tr;                                                 // the mod's own translations: label_pt, desc_pt, page_pt ...
  std::map<std::string, std::vector<std::string>> optionsTr;  // options_pt ...
  bool alpha = false;
  int maxlen = 64;
  Value def;

  bool valued() const {
    return type != Type::Button && type != Type::Header && type != Type::Separator && type != Type::Note;  // a list carries text
  }
};

bool parseSpec(const Map& raw, Spec& out, std::string& err);
bool coerceNum(const Spec& s, double in, Value& out);
bool coerceStr(const Spec& s, const std::string& in, Value& out, std::string& err);
std::string tostr(const Spec& s, const Value& v);
bool same(const Spec& s, const Value& a, const Value& b);

}  // namespace fw
