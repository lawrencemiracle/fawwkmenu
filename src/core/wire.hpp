// wire.hpp
// scripts and the menu talk in flat strings. these are the helpers for that.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace fw {

using Map = std::map<std::string, std::string>;

std::string enc(const std::string& s);
std::string dec(const std::string& s);
std::string pack(const Map& m);
Map unpack(const std::string& s);
std::vector<std::string> lines(const std::string& s);
std::string numstr(double n);

}  // namespace fw
