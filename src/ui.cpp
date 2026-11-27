#include "ui.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/lang.hpp"
#include "theme.hpp"

using fw::T;
using fw::shown;
using fw::shownMeta;
using fw::shownOption;

using fw::Mod;
using fw::Spec;
using fw::Type;
using fw::Value;

// keys ---------------------------------------------------------------------

const char* Menu::keyName(int vk, char* buf, size_t n) {
  if (vk == 0) { std::snprintf(buf, n, "%s", T("none").c_str()); return buf; }
  if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) { std::snprintf(buf, n, "%c", vk); return buf; }
  if (vk >= 0x70 && vk <= 0x87) { std::snprintf(buf, n, "F%d", vk - 0x6F); return buf; }
  if (vk >= 0x60 && vk <= 0x69) { std::snprintf(buf, n, "Num%d", vk - 0x60); return buf; }
  struct K { int vk; const char* name; };
  static const K table[] = {
      {1, "LMB"}, {2, "RMB"}, {4, "MMB"}, {5, "Mouse4"}, {6, "Mouse5"}, {8, "Backspace"}, {9, "Tab"},
      {13, "Enter"}, {16, "Shift"}, {17, "Ctrl"}, {18, "Alt"}, {19, "Pause"}, {20, "CapsLock"},
      {27, "Esc"}, {32, "Space"}, {33, "PageUp"}, {34, "PageDown"}, {35, "End"}, {36, "Home"},
      {37, "Left"}, {38, "Up"}, {39, "Right"}, {40, "Down"}, {44, "PrtSc"}, {45, "Insert"},
      {46, "Delete"}, {106, "Num*"}, {107, "Num+"}, {109, "Num-"}, {110, "Num."}, {111, "Num/"},
      {144, "NumLock"}, {145, "ScrollLock"}, {160, "LShift"}, {161, "RShift"}, {162, "LCtrl"},
      {163, "RCtrl"}, {164, "LAlt"}, {165, "RAlt"}, {186, ";"}, {187, "="}, {188, ","},
      {189, "-"}, {190, "."}, {191, "/"}, {192, "`"}, {219, "["}, {220, "\\"}, {221, "]"}, {222, "'"},
  };
  for (auto& k : table) {
    if (k.vk == vk) { std::snprintf(buf, n, "%s", k.name); return buf; }
  }
  std::snprintf(buf, n, "key %d", vk);
  return buf;
}

// small helpers --------------------------------------------------------------

static ImU32 mixU32(ImU32 a, ImU32 b, float t) {
  ImU32 out = 0;
  for (int sh = 0; sh < 32; sh += 8) {
    float x = (float)((a >> sh) & 255), y = (float)((b >> sh) & 255);
    out |= (ImU32)(x + (y - x) * t + 0.5f) << sh;
  }
  return out;
}

static std::string defaultFmt(const Spec& s) {
  if (!s.fmt.empty()) return s.fmt;
  if (s.type == Type::Int) return "%d";
  if (s.step >= 1) return "%.0f";
  if (s.step >= 0.1) return "%.1f";
  if (s.step >= 0.01 || s.step == 0) return "%.2f";
  return "%.3f";
}

static std::string lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

// what a mod's text looks like in the language the player chose. the menu's own text comes from the tables,
// a mod's from the translations it sent along, and anything missing stays as the mod wrote it
static std::string lab(const Mod& m, const Spec& s) { return m.internal ? T(s.label) : shown(s, "label", s.label); }
static std::string dsc(const Mod& m, const Spec& s) { return m.internal ? T(s.desc) : shown(s, "desc", s.desc); }
static std::string txt(const Mod& m, const Spec& s) { return m.internal ? T(s.text) : shown(s, "text", s.text); }
static std::string tagN(const Mod& m, const Spec& s) { return m.internal ? T(s.tag) : shown(s, "tag", s.tag); }
static std::string optN(const Mod& m, const Spec& s, size_t i) { return m.internal ? T(s.options[i]) : shownOption(s, i); }
static std::string grpN(const Mod& m, const Spec& s) { return m.internal ? T(s.group) : shown(s, "group", s.group); }
static std::string pgN(const Mod& m, const std::string& page) {
  if (m.internal) return T(page);
  for (const Spec& s : m.specs)
    if (s.page == page) return shown(s, "page", page);
  return page;
}
static std::string nameN(const Mod& m) { return m.internal ? T(m.name) : shownMeta(m.tr, "name", m.name); }
static std::string descN(const Mod& m) { return m.internal ? T(m.description) : shownMeta(m.tr, "description", m.description); }
static std::string catN(const Mod& m) { return m.internal ? T("System") : m.category; }

// "undid 1 change" / "undid 3 changes", in the current language
static std::string count(int n, const char* one, const char* many) {
  char buf[96];
  std::snprintf(buf, sizeof buf, T(n == 1 ? one : many).c_str(), n);
  return buf;
}

void Menu::setStatus(const std::string& text) {
  status = text;
  statusAt = env.now();
}

void Menu::clearSearch() { search[0] = '\0'; }

std::string Menu::fit(const std::string& t, float w) {
  if (ImGui::CalcTextSize(t.c_str()).x <= w) return t;
  std::string out = t;
  while (out.size() > 1 && ImGui::CalcTextSize((out + "..").c_str()).x > w) out.pop_back();
  return out + "..";
}

void Menu::muted(const std::string& t) {
  ImGui::PushStyleColor(ImGuiCol_Text, theme::cur.mutedVec);
  ImGui::TextUnformatted(t.c_str());
  ImGui::PopStyleColor();
}

void Menu::mutedWrapped(const std::string& t, float wrapAt) {
  ImGui::PushStyleColor(ImGuiCol_Text, theme::cur.mutedVec);
  ImGui::PushTextWrapPos(wrapAt);
  ImGui::TextUnformatted(t.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

void Menu::accentText(const std::string& t) {
  ImGui::PushStyleColor(ImGuiCol_Text, theme::cur.accentVec);
  ImGui::TextUnformatted(t.c_str());
  ImGui::PopStyleColor();
}

void Menu::tip(const std::string& t) {
  if (!t.empty() && ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24);
    ImGui::TextUnformatted(t.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

// little pill switch, nicer than the stock checkbox
bool Menu::sw(const char* id, bool on) {
  float W = S(40), H = S(21), fh = ImGui::GetFrameHeight();
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::InvisibleButton(id, ImVec2(W, fh));
  ImGuiID key = ImGui::GetItemID();
  float target = on ? 1.f : 0.f;
  auto it = anim.find(key);
  float a = it == anim.end() ? target : it->second;
  a += (target - a) * std::min(1.f, ImGui::GetIO().DeltaTime * 16.f);
  anim[key] = a;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float y0 = p.y + (fh - H) / 2;
  dl->AddRectFilled(ImVec2(p.x, y0), ImVec2(p.x + W, y0 + H), mixU32(theme::cur.off, theme::cur.accent, a), H / 2);
  dl->AddCircleFilled(ImVec2(p.x + H / 2 + (W - H) * a, y0 + H / 2), H / 2 - S(3), theme::cur.knob, 16);
  return clicked;
}

bool Menu::confirmButton(const std::string& id, const char* label) {
  bool armed = confirmId == id && env.now() - confirmAt < 3;
  if (armed) {
    ImVec4 c = theme::cur.accentVec;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.x, c.y, c.z, 0.55f));
  }
  std::string cap = (armed ? T("sure?") : std::string(label)) + "##" + id;
  bool fired = false;
  if (ImGui::Button(cap.c_str())) {
    if (armed) {
      confirmId.clear();
      fired = true;
    } else {
      confirmId = id;
      confirmAt = env.now();
    }
  }
  if (armed) ImGui::PopStyleColor();
  return fired;
}

// one control; returns true and fills out when the user changed it
bool Menu::control(Mod& m, size_t idx, float w, Value& out) {
  const Spec& s = m.specs[idx];
  const Value& v = m.values[idx];
  switch (s.type) {
    case Type::Bool:
      if (sw("##sw", v.n != 0)) { out.n = v.n != 0 ? 0 : 1; return true; }
      break;

    case Type::Int: {
      int x = (int)v.n;
      if (s.live) {
        char txt[48];
        std::snprintf(txt, sizeof txt, defaultFmt(s).c_str(), x);
        float frac = s.max > s.min ? (float)((v.n - s.min) / (s.max - s.min)) : 0.f;
        ImGui::ProgressBar(std::min(1.f, std::max(0.f, frac)), ImVec2(w, 0), txt);
        break;
      }
      ImGui::SetNextItemWidth(w);
      bool ch;
      if (s.inputWidget) ch = ImGui::InputInt("##in", &x, (int)s.step, (int)s.step * 10);
      else ch = ImGui::SliderInt("##sl", &x, (int)s.min, (int)s.max, defaultFmt(s).c_str());
      if (ch) { out.n = x; return true; }
      break;
    }

    case Type::Float: {
      float x = (float)v.n;
      if (s.live) {
        char txt[48];
        std::snprintf(txt, sizeof txt, defaultFmt(s).c_str(), (double)x);
        float frac = s.max > s.min ? (float)((v.n - s.min) / (s.max - s.min)) : 0.f;
        ImGui::ProgressBar(std::min(1.f, std::max(0.f, frac)), ImVec2(w, 0), txt);
        break;
      }
      ImGui::SetNextItemWidth(w);
      bool ch;
      if (s.inputWidget) {
        float st = s.step > 0 ? (float)s.step : 0.1f;
        ch = ImGui::InputFloat("##in", &x, st, st * 10, defaultFmt(s).c_str());
      } else {
        ch = ImGui::SliderFloat("##sl", &x, (float)s.min, (float)s.max, defaultFmt(s).c_str());
      }
      if (ch) { out.n = x; return true; }
      break;
    }

    case Type::Enum: {
      int cur = (int)v.n - 1;
      if (s.radio) {
        float right = ImGui::GetCursorPosX() + w;
        for (int i = 0; i < (int)s.options.size(); i++) {
          std::string name = optN(m, s, i);
          float need = ImGui::CalcTextSize(name.c_str()).x + ImGui::GetFrameHeight() + S(14);
          if (i > 0) {
            if (ImGui::GetCursorPosX() + need <= right) ImGui::SameLine(0, S(10));
            else ImGui::SetCursorPosX(right - w);
          }
          if (ImGui::RadioButton((name + "##r" + std::to_string(i)).c_str(), cur == i)) { out.n = i + 1; return true; }
        }
        break;
      }
      std::vector<std::string> names;
      for (size_t i = 0; i < s.options.size(); i++) names.push_back(optN(m, s, i));
      std::vector<const char*> items;
      for (auto& o : names) items.push_back(o.c_str());
      ImGui::SetNextItemWidth(w);
      if (ImGui::Combo("##cb", &cur, items.data(), (int)items.size())) { out.n = cur + 1; return true; }
      break;
    }

    case Type::Color: {
      unsigned c = (unsigned)v.n;
      float col[4] = {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, ((c >> 24) & 255) / 255.f};
      ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs | (s.alpha ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_NoAlpha);
      bool ch = ImGui::ColorEdit4("##col", col, flags);
      ImGui::SameLine();
      char hex[16];
      std::snprintf(hex, sizeof hex, "#%02X%02X%02X", (int)(col[0] * 255 + 0.5f), (int)(col[1] * 255 + 0.5f), (int)(col[2] * 255 + 0.5f));
      muted(hex);
      if (ch) {
        auto b = [](float x) { return (unsigned)(x * 255.f + 0.5f); };
        out.n = (double)((b(col[3]) << 24) | (b(col[0]) << 16) | (b(col[1]) << 8) | b(col[2]));
        return true;
      }
      break;
    }

    case Type::Key: {
      bool listening = listen.active && listen.mod == m.id && listen.setting == s.id;
      char nb[32];
      std::string label = listening ? T("press a key...") : std::string(keyName((int)v.n, nb, sizeof nb));
      label += "##key";
      if (ImGui::Button(label.c_str(), ImVec2(std::min(w, S(130)), 0))) {
        listen.active = true;
        listen.mod = m.id;
        listen.setting = s.id;
        listen.since = env.now();
      }
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        listen.active = false;
        out.n = 0;
        return true;
      }
      tip(T("click, then press a key. right click clears it"));
      break;
    }

    case Type::Text: {
      char buf[256];
      std::snprintf(buf, sizeof buf, "%s", v.s.c_str());
      ImGui::SetNextItemWidth(w);
      bool ch = ImGui::InputText("##tx", buf, sizeof buf);
      if (ImGui::IsItemActive()) textActive = true;
      if (ch) { out.s = buf; return true; }
      break;
    }

    case Type::Button: {
      std::string cap = (s.text.empty() ? T("Run") : txt(m, s)) + "##btn";
      if (ImGui::Button(cap.c_str(), ImVec2(std::min(w, S(110)), 0))) env.store->press(m, s.id, "ui");
      break;
    }

    default:
      break;
  }
  return false;
}

// a list the mod fills in: one line per entry, title and a muted detail, with a button per action at the end.
// rows come as text: lines separated by \n, title and detail separated by a tab
void Menu::listRow(Mod& m, size_t idx) {
  const Spec& s = m.specs[idx];
  std::vector<std::pair<std::string, std::string>> rows;
  {
    const std::string& t = m.values[idx].s;
    for (size_t i = 0; i < t.size();) {
      size_t j = t.find('\n', i);
      if (j == std::string::npos) j = t.size();
      std::string line = t.substr(i, j - i);
      size_t tab = line.find('\t');
      if (!line.empty()) rows.push_back({line.substr(0, tab), tab == std::string::npos ? "" : line.substr(tab + 1)});
      i = j + 1;
    }
  }

  ImGui::PushID(m.id.c_str());
  ImGui::PushID(s.id.c_str());
  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  accentText(lab(m, s));
  if (!rows.empty()) {
    ImGui::SameLine();
    muted("(" + std::to_string(rows.size()) + ")");
  }
  if (!s.desc.empty()) mutedWrapped(dsc(m, s), 0);

  float rowH = ImGui::GetFrameHeight() + S(8);
  float shown = (float)std::max<size_t>(std::min<size_t>(rows.size(), 8), 1);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::cur.hover);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(6));
  if (ImGui::BeginChild("##list", ImVec2(0, shown * rowH + S(10)), ImGuiChildFlags_Borders)) {
    if (rows.empty()) {
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
      muted(s.text.empty() ? T("nothing in here yet") : txt(m, s));
    }
    for (size_t i = 0; i < rows.size(); i++) {
      ImGui::PushID((int)i);
      ImVec2 p = ImGui::GetCursorScreenPos();
      float w = ImGui::GetContentRegionAvail().x;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + rowH)) && ImGui::IsWindowHovered())
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), theme::cur.accentSoft, S(4));

      // buttons first, from the right, so the text knows how much room it has
      float x = p.x + w - S(6);
      int pressed = -1;
      for (int a = (int)s.options.size() - 1; a >= 0; a--) {
        std::string cap = optN(m, s, a);
        float bw = ImGui::CalcTextSize(cap.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        x -= bw;
        ImGui::SetCursorScreenPos(ImVec2(x, p.y + (rowH - ImGui::GetFrameHeight()) / 2));
        if (ImGui::Button((cap + "##a" + std::to_string(a)).c_str())) pressed = a;
        x -= S(6);
      }
      float room = x - p.x - S(14);
      float ty = p.y + (rowH - ImGui::GetTextLineHeight()) / 2;
      std::string title = fit(rows[i].first, std::max(S(60), room * (rows[i].second.empty() ? 1.f : 0.55f)));
      dl->AddText(ImVec2(p.x + S(8), ty), theme::cur.text, title.c_str());
      if (!rows[i].second.empty()) {
        float tx = p.x + S(8) + ImGui::CalcTextSize(title.c_str()).x + S(12);
        std::string d = fit(rows[i].second, std::max(S(40), p.x + room - tx));
        dl->AddText(ImVec2(tx, ty), theme::cur.muted, d.c_str());
      }
      ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
      ImGui::Dummy(ImVec2(w, 0));
      if (pressed >= 0) env.store->pressRow(m, s.id, (int)i + 1, pressed + 1, "ui");
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::PopID();
  ImGui::PopID();
}

void Menu::row(Mod& m, size_t idx, bool showPage) {
  const Spec& s = m.specs[idx];
  if (s.type == Type::List) {
    listRow(m, idx);
    return;
  }
  if (s.type == Type::Separator) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    return;
  }
  if (s.type == Type::Header) {
    ImGui::Spacing();
    accentText(lab(m, s));
    return;
  }
  if (s.type == Type::Note) {
    mutedWrapped(lab(m, s), 0);
    return;
  }

  ImGui::PushID(m.id.c_str());
  ImGui::PushID(s.id.c_str());
  float x0 = ImGui::GetCursorPosX();
  float availW = ImGui::GetContentRegionAvail().x;
  float ctrlX = x0 + std::max(S(140), availW * 0.46f);
  float ctrlW = std::max(S(90), x0 + availW - ctrlX - S(4));
  float fh = ImGui::GetFrameHeight();
  float dotW = S(12);

  {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + availW, p0.y + fh);
    p0.y -= S(2);
    p1.y += S(2);
    if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(p0, p1))
      ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, theme::cur.hover, S(5));
  }

  bool changed = s.valued() && !s.live && !fw::same(s, m.values[idx], s.def);
  if (changed) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##rst", ImVec2(dotW, fh))) {
      env.store->reset(m, idx, "ui");
      setStatus(T("reset") + " " + lab(m, s));
    }
    tip(T("changed from default. click to reset"));
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + dotW / 2, p.y + fh / 2), S(3), theme::cur.accent, 12);
  } else {
    ImGui::Dummy(ImVec2(dotW, fh));
  }
  ImGui::SameLine(0, S(2));
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(lab(m, s).c_str());
  tip(s.desc.empty() ? "id: " + s.id : dsc(m, s) + "\n\nid: " + s.id);
  if (!s.tag.empty()) {
    ImGui::SameLine();
    muted("(" + tagN(m, s) + ")");
  }
  if (showPage && s.page != "General") {
    ImGui::SameLine();
    muted("[" + pgN(m, s.page) + "]");
  }

  ImGui::SameLine(ctrlX);
  Value nv;
  if (control(m, idx, ctrlW, nv)) env.store->setValue(m, idx, nv, "ui");

  if (env.inlineDesc() && !s.desc.empty()) {
    ImGui::SetCursorPosX(x0 + dotW + S(2));
    mutedWrapped(dsc(m, s), ctrlX - S(10));
  }
  ImGui::PopID();
  ImGui::PopID();
}

// rows of one page, with collapsible groups
void Menu::settings(Mod& m, const std::string& page) {
  std::string current;
  bool started = false, skip = false;
  for (size_t i = 0; i < m.specs.size(); i++) {
    const Spec& s = m.specs[i];
    if (s.page != page || !env.store->isVisible(m, s)) continue;
    if (!started || s.group != current) {
      started = true;
      current = s.group;
      skip = false;
      if (!current.empty()) {
        ImGui::Spacing();
        std::string label = grpN(m, s);
        for (size_t p = label.find('/'); p != std::string::npos; p = label.find('/', p + 3)) label.replace(p, 1, " / ");
        label += "##grp" + m.id + page;
        skip = !ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
      }
    }
    if (!skip) row(m, i, false);
  }
}

// text drawn twice, half a pixel apart, reads as semi bold without needing a second font
static void boldText(ImDrawList* dl, ImVec2 p, ImU32 col, const char* t) {
  dl->AddText(p, col, t);
  dl->AddText(ImVec2(p.x + 0.7f, p.y), col, t);
}

void Menu::modRow(Mod& m) {
  float h = S(26);
  float w = ImGui::GetContentRegionAvail().x;
  ImVec2 p = ImGui::GetCursorScreenPos();
  std::string sid = "##mod" + m.id;
  bool picked = sel == m.id;
  if (ImGui::InvisibleButton(sid.c_str(), ImVec2(w, h))) {
    sel = m.id;
    clearSearch();
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (picked) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::cur.accentSoft, S(4));
  else if (ImGui::IsItemHovered()) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::cur.hover, S(4));

  ImU32 col = picked ? theme::cur.accent : (env.store->isActive(m) ? theme::cur.text : theme::cur.muted);
  float ty = p.y + (h - ImGui::GetTextLineHeight()) / 2;
  float tx = p.x + S(22);
  ImTextureID tex = (!m.icon.empty() && env.icon) ? env.icon(m.icon) : (ImTextureID)0;
  if (tex) {
    float is = S(16);
    dl->AddImage(tex, ImVec2(p.x + S(6), p.y + (h - is) / 2), ImVec2(p.x + S(6) + is, p.y + (h + is) / 2));
    tx = p.x + S(28);
  }
  std::string name = fit(nameN(m), w - (tx - p.x) - S(18));
  if (picked) boldText(dl, ImVec2(tx, ty), col, name.c_str());
  else dl->AddText(ImVec2(tx, ty), col, name.c_str());
  if (env.store->unsaved(m)) dl->AddCircleFilled(ImVec2(p.x + w - S(10), p.y + h / 2), S(3), theme::cur.accent, 10);
  ImGui::Dummy(ImVec2(0, S(1)));
}

void Menu::sidebar(float h) {
  ImGui::BeginChild("##side", ImVec2(S(220), h), ImGuiChildFlags_None);
  auto mods = env.store->list();
  if (mods.empty()) muted("no mods here yet. quiet, isn't it");
  std::string lastCat;
  bool shut = false;
  for (Mod* m : mods) {
    std::string cat = catN(*m);
    if (cat != lastCat) {
      lastCat = cat;
      float w = ImGui::GetContentRegionAvail().x;
      ImVec2 p = ImGui::GetCursorScreenPos();
      float rh = S(28);
      bool& isShut = closed[cat];
      if (ImGui::InvisibleButton(("##cat" + cat).c_str(), ImVec2(w, rh))) isShut = !isShut;
      shut = isShut;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (ImGui::IsItemHovered()) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rh), theme::cur.hover, S(4));
      // little arrow: points right when shut, down when open
      float cx = p.x + S(10), cy = p.y + rh / 2, r = S(4);
      if (shut) dl->AddTriangleFilled(ImVec2(cx - r * 0.6f, cy - r), ImVec2(cx - r * 0.6f, cy + r), ImVec2(cx + r, cy), theme::cur.accent);
      else dl->AddTriangleFilled(ImVec2(cx - r, cy - r * 0.6f), ImVec2(cx + r, cy - r * 0.6f), ImVec2(cx, cy + r), theme::cur.accent);
      boldText(dl, ImVec2(p.x + S(22), p.y + (rh - ImGui::GetTextLineHeight()) / 2), theme::cur.accent, fit(cat, w - S(30)).c_str());
    }
    if (!shut) modRow(*m);
  }
  ImGui::EndChild();
  // thin divider between the list and the page
  ImVec2 a = ImGui::GetItemRectMax();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x + S(4), a.y - h), ImVec2(a.x + S(4), a.y), theme::cur.border);
}

void Menu::header(Mod& m) {
  ImGui::SetWindowFontScale(1.25f);
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float shift = 0;
  if (ImTextureID tex = (!m.icon.empty() && env.icon) ? env.icon(m.icon) : (ImTextureID)0) {
    float is = ImGui::GetTextLineHeight();
    dl->AddImage(tex, p, ImVec2(p.x + is, p.y + is));
    shift = is + S(8);
  }
  std::string title = nameN(m);
  boldText(dl, ImVec2(p.x + shift, p.y), theme::cur.accent, title.c_str());
  ImVec2 ts = ImGui::CalcTextSize(title.c_str());
  ImGui::Dummy(ImVec2(ts.x + shift, ts.y));
  ImGui::SetWindowFontScale(1.0f);
  std::string meta;
  if (!m.version.empty()) meta += "v" + m.version;
  if (!m.author.empty()) meta += std::string(meta.empty() ? "" : "   ") + T("by") + " " + m.author;
  if (!meta.empty()) {
    ImGui::SameLine(0, S(10));
    muted(meta);
  }
  if (!m.description.empty()) mutedWrapped(descN(m), 0);
  if (!env.store->isActive(m)) {
    ImGui::Spacing();
    accentText(T("this mod isn't running right now. changes are kept and applied when it loads"));
  }
  ImGui::Spacing();
  actions(m);
}

void Menu::modPage(Mod& m) {
  header(m);
  ImGui::Separator();
  if (m.pages.size() <= 1) {
    std::string id = "##pg" + m.id;
    ImGui::BeginChild(id.c_str(), ImVec2(0, 0), 0);
    settings(m, m.pages.empty() ? "General" : m.pages[0]);
    ImGui::EndChild();
    return;
  }
  std::string bar = "##tabs" + m.id;
  if (ImGui::BeginTabBar(bar.c_str())) {
    for (auto& page : m.pages) {
      std::string tab = pgN(m, page) + "##t" + m.id;
      if (ImGui::BeginTabItem(tab.c_str())) {
        std::string id = "##pg" + m.id + page;
        ImGui::BeginChild(id.c_str(), ImVec2(0, 0), 0);
        ImGui::Spacing();
        settings(m, page);
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
}

void Menu::searchResults(const std::string& q) {
  std::string ql = lower(q);
  bool any = false;
  ImGui::BeginChild("##results", ImVec2(0, 0), 0);
  for (Mod* m : env.store->list()) {
    std::vector<size_t> hits;
    bool nameHit = lower(m->name + " " + nameN(*m) + " " + m->category).find(ql) != std::string::npos;
    for (size_t i = 0; i < m->specs.size(); i++) {
      const Spec& s = m->specs[i];
      if (!(s.valued() || s.type == Type::Button) || !env.store->isVisible(*m, s)) continue;
      std::string hay = lower(s.label + " " + lab(*m, s) + " " + s.desc + " " + dsc(*m, s) + " " + s.id + " " + s.group + " " + s.page);
      if (hay.find(ql) != std::string::npos) hits.push_back(i);
    }
    if (!nameHit && hits.empty()) continue;
    any = true;
    ImGui::Spacing();
    accentText(nameN(*m));
    ImGui::SameLine();
    std::string open = T("open") + "##o" + m->id;
    if (ImGui::SmallButton(open.c_str())) {
      sel = m->id;
      clearSearch();
    }
    for (size_t i : hits) row(*m, i, true);
  }
  if (!any) {
    ImGui::Spacing();
    muted(T("nothing matches") + " \"" + q + "\". " + T("it either does not exist or it is hiding"));
  }
  ImGui::EndChild();
}

void Menu::empty() {
  ImGui::Dummy(ImVec2(1, S(20)));
  ImGui::SetWindowFontScale(1.15f);
  ImGui::TextUnformatted(T("no mods connected yet").c_str());
  ImGui::SetWindowFontScale(1.0f);
  mutedWrapped(T("Mods show up here as soon as they register with fawwk menu. See the integration guide to add yours."), 0);
}

void Menu::topBar() {
  ImGui::SetWindowFontScale(1.15f);
  accentText("fawwk");
  ImGui::SameLine(0, S(5));
  muted("menu");
  ImGui::SetWindowFontScale(1.0f);
  ImGui::SameLine(S(150));
  float winW = ImGui::GetWindowWidth();
  ImGui::SetNextItemWidth(std::max(S(120), winW - S(150) - S(70)));
  ImGui::InputTextWithHint("##search", T("search every setting...").c_str(), search, sizeof search);
  if (ImGui::IsItemActive()) textActive = true;
  ImGui::SameLine(winW - S(48));
  if (ImGui::Button("X##close", ImVec2(S(32), 0)) && env.close) env.close();
  tip(T("close (or press your menu key)"));
}

// the per-mod actions sit under the page title
void Menu::actions(Mod& m) {
  if (ImGui::Button((T("Store") + "##a").c_str())) {
    env.store->save(m);
    setStatus(T("stored") + " " + nameN(m));
  }
  tip(T("write this mod's settings to disk"));
  ImGui::SameLine();
  if (ImGui::Button((T("Undo edits") + "##a").c_str())) {
    int n = env.store->revert(m, "ui");
    if (n == 0) setStatus(T("nothing to undo, for once"));
    else setStatus(count(n, "undid %d change", "undid %d changes"));
  }
  tip(T("go back to what was last stored"));
  ImGui::SameLine();
  if (confirmButton("stock" + m.id, T("Stock values").c_str())) {
    int n = env.store->resetMod(m, "", "ui");
    if (n == 0) setStatus(T("already stock. very disciplined"));
    else setStatus(count(n, "reset %d setting", "reset %d settings"));
  }
  tip(T("every setting of this mod back to its default"));
  ImGui::Spacing();
}

void Menu::footer() {
  char buf[96];
  int mods, sets;
  env.store->counts(mods, sets);
  bool anyUnsaved = false;
  for (Mod* x : env.store->list()) anyUnsaved = anyUnsaved || env.store->unsaved(*x);

  std::string text;
  bool hot = false;
  if (env.now() - statusAt < 2.5) text = status;
  else if (anyUnsaved) { text = env.store->autosave ? T("storing...") : T("changes not stored yet. they will not remember themselves"); hot = true; }
  else text = env.store->autosave ? T("autosave is on") : T("everything stored");
  ImGui::AlignTextToFramePadding();
  if (hot) accentText(text);
  else muted(text);

  std::string storeAll = T("Store everything");
  {
    char a[48];
    std::snprintf(a, sizeof a, T(mods == 1 ? "%d mod" : "%d mods").c_str(), mods);
    std::snprintf(buf, sizeof buf, "%s  |  ", a);
    char b[48];
    std::snprintf(b, sizeof b, T("%d settings").c_str(), sets);
    std::strncat(buf, b, sizeof buf - std::strlen(buf) - 1);
  }
  float bw = ImGui::CalcTextSize(storeAll.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
  float cw = ImGui::CalcTextSize(buf).x;
  ImGui::SameLine(ImGui::GetWindowWidth() - bw - cw - S(40));
  ImGui::AlignTextToFramePadding();
  muted(buf);
  ImGui::SameLine(ImGui::GetWindowWidth() - bw - S(16));
  if (ImGui::Button((storeAll + "##f").c_str())) {
    int n = env.store->flush(true);
    if (n == 0) setStatus(T("nothing to store. all calm"));
    else setStatus(count(n, "stored %d file", "stored %d files"));
  }
  tip(T("write every changed mod to disk"));
}

void Menu::popupWindow(Mod& m) {
  ImGuiIO& io = ImGui::GetIO();
  float dw = io.DisplaySize.x, dh = io.DisplaySize.y;
  float w = std::min(S(470), dw - 30), h = std::min(std::max(S(300), dh * 0.55f), dh - 30);
  ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImVec2(S(60), S(60)), ImGuiCond_Appearing);  // top left, the game stays in view
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, env.alpha ? std::max(0.f, std::min(1.f, env.alpha())) : 1.f);
  bool keep = true;
  std::string title = nameN(m) + "##popup" + m.id;
  if (ImGui::Begin(title.c_str(), &keep, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
    std::string page = popupPage;
    if (page.empty() || std::find(m.pages.begin(), m.pages.end(), page) == m.pages.end())
      page = m.pages.empty() ? "General" : m.pages[0];
    float foot = ImGui::GetFrameHeight() + S(12);
    ImGui::BeginChild("##popbody", ImVec2(0, std::max(S(60), ImGui::GetContentRegionAvail().y - foot)), ImGuiChildFlags_None);
    settings(m, page);
    ImGui::EndChild();
    ImGui::Separator();
    if (ImGui::SmallButton((T("All settings") + "##all").c_str())) {
      popup = false;
      sel = m.id;
    }
    ImGui::SameLine();
    muted(T("Esc closes this"));
  }
  ImGui::End();
  ImGui::PopStyleVar();
  if (!keep) env.close();
}

void Menu::draw() {
  std::lock_guard<std::recursive_mutex> guard(env.store->lock);
  textActive = false;
  if (listen.active && env.now() - listen.since > 8) listen.active = false;

  auto mods = env.store->list();
  if (sel.empty() || !env.store->find(sel)) sel = mods.empty() ? "" : mods[0]->id;
  if (!env.store->showRequest.empty()) {
    // "id" opens the whole menu on that mod, "id\npage" shows only that page in a small window
    std::string req = env.store->showRequest, page;
    size_t nl = req.find('\n');
    bool small = nl != std::string::npos;
    if (small) {
      page = req.substr(nl + 1);
      req.resize(nl);
    }
    if (small && popup && popupMod == req && popupPage == page) {
      // the button that opened the small window also closes it
      env.store->showRequest.clear();
      env.close();
      return;
    }
    if (env.store->find(req)) {
      sel = req;
      clearSearch();
      popup = small;
      popupMod = req;
      popupPage = page;
    }
    env.store->showRequest.clear();
  }
  if (popup) {
    Mod* pm = env.store->find(popupMod);
    if (pm) {
      popupWindow(*pm);
      return;
    }
    popup = false;
  }

  ImGuiIO& io = ImGui::GetIO();
  float dw = io.DisplaySize.x, dh = io.DisplaySize.y;
  float share = env.winSize ? std::max(0.35f, std::min(1.f, env.winSize())) : 0.7f;
  float w = std::min(std::max(dw * share, S(700)), dw - 30);
  float h = std::min(std::max(dh * share * 1.11f, S(440)), dh - 30);
  // the size is only forced once the slider is let go, so the window does not jump under the cursor
  // while it is being dragged. dragging the corner by hand still sticks
  static float appliedShare = -1;
  bool slid = appliedShare >= 0 && std::fabs(appliedShare - share) > 0.0005f && !ImGui::IsMouseDown(0);
  if (appliedShare < 0 || slid) appliedShare = share;
  ImGui::SetNextWindowSize(ImVec2(w, h), slid ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(S(700), dw), std::min(S(440), dh)), ImVec2(dw, dh));
  ImGui::SetNextWindowPos(ImVec2(dw / 2, dh / 2), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, env.alpha ? std::max(0.f, std::min(1.f, env.alpha())) : 1.f);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                           ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("fawwk menu##main", nullptr, flags)) {
    topBar();
    ImGui::Separator();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float footH = ImGui::GetFrameHeight() + S(10);
    float bodyH = std::max(S(100), avail.y - footH);
    sidebar(bodyH);
    ImGui::SameLine();

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(6));
    ImGui::BeginChild("##content", ImVec2(0, bodyH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (search[0] != '\0') {
      searchResults(search);
    } else if (!sel.empty() && env.store->find(sel)) {
      modPage(*env.store->find(sel));
    } else {
      empty();
    }
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, S(2)));
    footer();
  }
  ImGui::End();
  ImGui::PopStyleVar();
}
