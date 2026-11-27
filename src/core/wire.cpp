#include "wire.hpp"

#include <cmath>
#include <cstdio>

namespace fw {

static bool plain(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         c == '-' || c == '.' || c == '_' || c == '~';
}

std::string enc(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if (plain(c)) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string dec(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() &&hexval(s[i + 1]) >= 0 && hexval(s[i + 2]) >= 0) {
      out += (char)(hexval(s[i + 1]) * 16 + hexval(s[i + 2]));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string pack(const Map& m) {
  std::string out;
  for (auto& kv : m) {  // map is already sorted by key
    if (!out.empty()) out += '&';
    out += enc(kv.first) + "=" + enc(kv.second);
  }
  return out;
}

Map unpack(const std::string& s) {
  Map m;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = s.find('&', i);
    if (j == std::string::npos) j = s.size();
    if (j > i) {
      std::string pair = s.substr(i, j - i);
      size_t eq = pair.find('=');
      if (eq != std::string::npos) m[dec(pair.substr(0, eq))] = dec(pair.substr(eq + 1));
    }
    i = j + 1;
  }
  return m;
}

std::vector<std::string> lines(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (true) {
    size_t j = s.find('\n', i);
    if (j == std::string::npos) {
      out.push_back(s.substr(i));
      break;
    }
    out.push_back(s.substr(i, j - i));
    i = j + 1;
  }
  return out;
}

std::string numstr(double n) {
  char buf[64];
  if (n == 0) return "0";
  if (n == std::floor(n) && std::fabs(n) < 1e15) {
    std::snprintf(buf, sizeof buf, "%.0f", n);
  } else {
    std::snprintf(buf, sizeof buf, "%.10g", n);
  }
  return buf;
}

}  // namespace fw
