// theme.hpp
// a preset is a background, a text color and an accent. every imgui color is
// mixed out of those three, so a new theme is one line.
#pragma once
#include <string>
#include <vector>

#include "imgui.h"

namespace theme {

struct Preset {
  const char* name;
  ImVec4 bg, fg, accent;
  bool light;
};

const std::vector<Preset>& presets();
std::vector<std::string> names();
unsigned int accentOf(int index);  // 1-based, returns 0xAARRGGBB

struct Options {
  int preset = 1;
  unsigned int accent = 0;
  float opacity = 0.97f;
  float rounding = 6.f;
  float scale = 1.f;
};

// colors the ui uses for its own drawing, filled in by apply()
struct Colors {
  ImU32 accent = 0, accentSoft = 0, text = 0, muted = 0, frame = 0, frameHi = 0, border = 0, knob = 0, off = 0, panel = 0, hover = 0;
  ImVec4 accentVec, mutedVec, textVec;
};
extern Colors cur;

void apply(const Options& o);

}  // namespace theme
