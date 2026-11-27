// lang.cpp
#include "lang.hpp"

namespace fw {

static std::string g_lang = "en";
static std::map<std::string, std::map<std::string, std::string>> g_tables;

const std::vector<Language>& languages() {
  static const std::vector<Language> all = {
      {"en", "English"}, {"pt", "Portugu\xC3\xAAs (Brasil)"}, {"es", "Espa\xC3\xB1ol"},
      {"ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9"},
  };
  return all;
}

void setLanguage(const std::string& code) {
  g_lang = "en";
  for (const Language& l : languages())
    if (code == l.code) g_lang = code;
}

const std::string& language() { return g_lang; }

std::string T(const std::string& english) {
  if (g_lang == "en") return english;
  auto t = g_tables.find(g_lang);
  if (t == g_tables.end()) return english;
  auto it = t->second.find(english);
  return it == t->second.end() ? english : it->second;
}

void addStrings(const std::string& code, const std::map<std::string, std::string>& strings) {
  for (const auto& kv : strings) g_tables[code][kv.first] = kv.second;
}

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

int parseLangFile(const std::string& code, const std::string& text) {
  int used = 0;
  size_t i = 0;
  while (i <= text.size()) {
    size_t j = text.find('\n', i);
    if (j == std::string::npos) j = text.size();
    std::string line = text.substr(i, j - i);
    i = j + 1;
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    size_t arrow = t.find("=>");
    if (arrow == std::string::npos) continue;
    std::string from = trim(t.substr(0, arrow)), to = trim(t.substr(arrow + 2));
    if (from.empty() || to.empty()) continue;
    g_tables[code][from] = to;
    used++;
  }
  return used;
}

static std::string pickField(const Map& tr, const std::string& key) {
  auto it = tr.find(key);
  return it == tr.end() ? std::string() : it->second;
}

std::string shown(const Spec& s, const char* field, const std::string& original) {
  if (g_lang != "en") {
    std::string v = pickField(s.tr, std::string(field) + "_" + g_lang);
    if (!v.empty()) return v;
  }
  return original;
}

std::string shownOption(const Spec& s, size_t i) {
  if (i >= s.options.size()) return std::string();
  if (g_lang != "en") {
    auto it = s.optionsTr.find(g_lang);
    if (it != s.optionsTr.end() && i < it->second.size() && !it->second[i].empty()) return it->second[i];
  }
  return s.options[i];
}

std::string shownMeta(const Map& tr, const char* field, const std::string& original) {
  if (g_lang != "en") {
    std::string v = pickField(tr, std::string(field) + "_" + g_lang);
    if (!v.empty()) return v;
  }
  return original;
}

}  // namespace fw
