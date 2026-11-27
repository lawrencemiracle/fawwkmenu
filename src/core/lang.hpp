// lang.hpp
// translations. the english text is the key, so a string with no translation simply shows as it is.
// mods bring their own translations in their specs (label_pt, desc_pt ...), the menu's own text comes
// from the tables in lang_builtin.cpp and from optional lang_<code>.txt files that can add to or replace them.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "schema.hpp"

namespace fw {

struct Language {
  const char* code;    // "pt"
  const char* native;  // "Português (Brasil)"
};

const std::vector<Language>& languages();            // what the menu offers, english first
void setLanguage(const std::string& code);           // unknown codes fall back to english
const std::string& language();                       // current code

// the menu's own text
std::string T(const std::string& english);
void addStrings(const std::string& code, const std::map<std::string, std::string>& strings);
// one file: "English text => translation" per line, # starts a comment. returns how many lines were used
int parseLangFile(const std::string& code, const std::string& text);
void builtinStrings();  // fills the tables, called once

// a mod's text in the current language: its own translation if it brought one, otherwise the original
std::string shown(const Spec& s, const char* field, const std::string& original);
std::string shownOption(const Spec& s, size_t i);
std::string shownMeta(const Map& tr, const char* field, const std::string& original);

}  // namespace fw
