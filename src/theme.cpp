#include "theme.hpp"

#include <cmath>

namespace theme {

Colors cur;

const std::vector<Preset>& presets() {
  static const std::vector<Preset> p = {
      {"Rose", {0.078f, 0.078f, 0.094f, 1}, {0.90f, 0.90f, 0.93f, 1}, {0.94f, 0.54f, 0.65f, 1}, false},
      {"Midnight", {0.055f, 0.062f, 0.090f, 1}, {0.90f, 0.92f, 0.97f, 1}, {0.40f, 0.52f, 1.00f, 1}, false},
      {"Graphite", {0.095f, 0.095f, 0.100f, 1}, {0.92f, 0.92f, 0.92f, 1}, {1.00f, 0.62f, 0.20f, 1}, false},
      {"Ember", {0.085f, 0.060f, 0.062f, 1}, {0.96f, 0.90f, 0.88f, 1}, {0.95f, 0.30f, 0.28f, 1}, false},
      {"Mint", {0.050f, 0.075f, 0.072f, 1}, {0.88f, 0.96f, 0.93f, 1}, {0.25f, 0.85f, 0.62f, 1}, false},
      {"Daylight", {0.955f, 0.958f, 0.965f, 1}, {0.12f, 0.13f, 0.16f, 1}, {0.20f, 0.40f, 0.95f, 1}, true},
  };
  return p;
}

std::vector<std::string> names() {
  std::vector<std::string> n;
  for (auto& p : presets()) n.push_back(p.name);
  return n;
}

static ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.f);
}

static ImVec4 withA(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }

unsigned int accentOf(int index) {
  auto& ps = presets();
  if (index < 1 || index > (int)ps.size()) index = 1;
  const ImVec4& a = ps[index - 1].accent;
  auto b = [](float x) { return (unsigned int)std::floor(x * 255.f + 0.5f); };
  return 0xFF000000u + (b(a.x) << 16) + (b(a.y) << 8) + b(a.z);
}

static ImVec4 fromArgb(unsigned int c) {
  return ImVec4(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1.f);
}

void apply(const Options& o) {
  auto& ps = presets();
  int idx = (o.preset >= 1 && o.preset <= (int)ps.size()) ? o.preset : 1;
  const Preset& p = ps[idx - 1];
  ImVec4 acc = fromArgb(o.accent ? o.accent : accentOf(idx));
  ImVec4 bg = p.bg, fg = p.fg;
  ImVec4 black(0, 0, 0, 1), white(1, 1, 1, 1);

  ImVec4 panel = p.light ? mix(bg, black, 0.03f) : mix(bg, black, 0.22f);
  ImVec4 frame = mix(bg, fg, p.light ? 0.07f : 0.08f);
  ImVec4 frameHi = mix(bg, fg, p.light ? 0.12f : 0.14f);
  ImVec4 frameAc = mix(bg, fg, p.light ? 0.17f : 0.20f);
  ImVec4 border = mix(bg, fg, 0.10f);
  ImVec4 muted = mix(bg, fg, 0.52f);
  ImVec4 accHi = mix(acc, white, 0.18f);

  cur.accent = ImGui::ColorConvertFloat4ToU32(acc);
  cur.accentSoft = ImGui::ColorConvertFloat4ToU32(withA(acc, 0.22f));
  cur.text = ImGui::ColorConvertFloat4ToU32(fg);
  cur.muted = ImGui::ColorConvertFloat4ToU32(muted);
  cur.frame = ImGui::ColorConvertFloat4ToU32(frame);
  cur.frameHi = ImGui::ColorConvertFloat4ToU32(frameHi);
  cur.border = ImGui::ColorConvertFloat4ToU32(border);
  cur.knob = ImGui::ColorConvertFloat4ToU32(p.light ? white : fg);
  cur.off = ImGui::ColorConvertFloat4ToU32(mix(bg, fg, 0.22f));
  cur.panel = ImGui::ColorConvertFloat4ToU32(withA(panel, 0.65f));
  cur.hover = ImGui::ColorConvertFloat4ToU32(withA(fg, 0.06f));
  cur.accentVec = withA(acc, 1.f);
  cur.mutedVec = withA(muted, 1.f);
  cur.textVec = withA(fg, 1.f);

  ImGuiStyle& st = ImGui::GetStyle();
  ImVec4* C = st.Colors;
  C[ImGuiCol_Text] = fg;
  C[ImGuiCol_TextDisabled] = muted;
  C[ImGuiCol_WindowBg] = withA(bg, o.opacity);
  C[ImGuiCol_ChildBg] = withA(bg, 0.f);  // panels set their own
  C[ImGuiCol_PopupBg] = withA(mix(bg, fg, 0.04f), 0.98f);
  C[ImGuiCol_Border] = withA(border, 0.7f);
  C[ImGuiCol_BorderShadow] = withA(black, 0.f);
  C[ImGuiCol_FrameBg] = frame;
  C[ImGuiCol_FrameBgHovered] = frameHi;
  C[ImGuiCol_FrameBgActive] = frameAc;
  C[ImGuiCol_TitleBg] = panel;
  C[ImGuiCol_TitleBgActive] = panel;
  C[ImGuiCol_TitleBgCollapsed] = panel;
  C[ImGuiCol_ScrollbarBg] = withA(bg, 0.f);
  C[ImGuiCol_ScrollbarGrab] = frameAc;
  C[ImGuiCol_ScrollbarGrabHovered] = mix(frameAc, fg, 0.15f);
  C[ImGuiCol_ScrollbarGrabActive] = mix(acc, bg, 0.55f);
  C[ImGuiCol_CheckMark] = acc;
  C[ImGuiCol_SliderGrab] = acc;
  C[ImGuiCol_SliderGrabActive] = accHi;
  C[ImGuiCol_Button] = frame;
  C[ImGuiCol_ButtonHovered] = frameHi;
  C[ImGuiCol_ButtonActive] = frameAc;
  C[ImGuiCol_Header] = withA(acc, 0.22f);
  C[ImGuiCol_HeaderHovered] = withA(acc, 0.34f);
  C[ImGuiCol_HeaderActive] = withA(acc, 0.46f);
  C[ImGuiCol_Separator] = border;
  C[ImGuiCol_SeparatorHovered] = withA(acc, 0.6f);
  C[ImGuiCol_SeparatorActive] = acc;
  C[ImGuiCol_ResizeGrip] = withA(acc, 0.15f);
  C[ImGuiCol_ResizeGripHovered] = withA(acc, 0.5f);
  C[ImGuiCol_ResizeGripActive] = withA(acc, 0.8f);
  C[ImGuiCol_Tab] = frame;
  C[ImGuiCol_TabHovered] = withA(acc, 0.5f);
  C[ImGuiCol_TabSelected] = withA(acc, 0.35f);
  C[ImGuiCol_TextSelectedBg] = withA(acc, 0.35f);

  float s = o.scale;
  float r = o.rounding * s;
  st.WindowRounding = r + 2 * s;
  st.ChildRounding = r;
  st.FrameRounding = r > s ? r - s : 0;
  st.PopupRounding = r;
  st.GrabRounding = r;
  st.ScrollbarRounding = r;
  st.TabRounding = r;
  st.WindowBorderSize = 1;
  st.ChildBorderSize = 1;
  st.FrameBorderSize = 0;
  st.WindowPadding = ImVec2(14 * s, 12 * s);
  st.FramePadding = ImVec2(10 * s, 5 * s);
  st.ItemSpacing = ImVec2(8 * s, 8 * s);
  st.ItemInnerSpacing = ImVec2(6 * s, 4 * s);
  st.ScrollbarSize = 9 * s;
  st.GrabMinSize = 10 * s;
}

}  // namespace theme
