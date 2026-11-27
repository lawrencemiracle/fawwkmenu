#include "store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace fw {

// ini ---------------------------------------------------------------------

static std::string esc(const std::string& v) {
  std::string o;
  for (char c : v) {
    if (c == '\\') o += "\\\\";
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else o += c;
  }
  return o;
}

static std::string unesc(const std::string& v) {
  std::string o;
  for (size_t i = 0; i < v.size(); i++) {
    if (v[i] == '\\' && i + 1 < v.size()) {
      char c = v[++i];
      o += (c == 'n') ? '\n' : (c == 'r') ? '\r' : c;
    } else {
      o += v[i];
    }
  }
  return o;
}

static std::map<std::string, std::string> parseIni(const std::string& text) {
  std::map<std::string, std::string> saved;
  std::string section;
  for (auto line : lines(text)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
      section = line.substr(1, line.size() - 2);
    } else if (section == "values" && !line.empty() && line[0] != ';') {
      size_t eq = line.find('=');
      if (eq != std::string::npos && eq > 0) saved[line.substr(0, eq)] = unesc(line.substr(eq + 1));
    }
  }
  return saved;
}

// -------------------------------------------------------------------------

Store::Store(Files files, std::string d, std::function<double()> n, std::string ep)
    : epoch(std::move(ep)), fs(std::move(files)), dir(std::move(d)), now(std::move(n)) {
  if (fs.mkdir && !dir.empty()) fs.mkdir(dir);
}

Mod* Store::find(const std::string& id) {
  auto it = mods.find(id);
  return it == mods.end() ? nullptr : it->second.get();
}

void Store::notify(Mod* m, const std::string& kind, const std::string& id, const std::string& source) {
  for (auto& fn : listeners) {
    try { fn(m, kind, id, source); } catch (...) {}
  }
}

void Store::touch(Mod& m) {
  m.dirty = true;
  dirtyAt = now();
}

void Store::emit(Mod& m, char kind, const std::string& id, const std::string& val, const std::string& source) {
  m.seq++;
  if (kind == 'v') {
    for (size_t i = m.ring.size(); i-- > 0;) {
      if (m.ring[i].kind == 'v' && m.ring[i].id == id) {
        m.ring.erase(m.ring.begin() + (long)i);
        break;
      }
    }
  }
  m.ring.push_back({m.seq, kind, id, val});
  while (m.ring.size() > 256) {
    m.droppedSeq = m.ring.front().seq;
    m.ring.pop_front();
  }
  notify(&m, std::string(1, kind), id, source);
}

Mod* Store::registerMod(const Map& meta, std::vector<Spec> specs, const Map& seeds, bool internal, std::string& err) {
  auto mi = meta.find("id");
  std::string id = mi == meta.end() ? "" : mi->second;
  bool idGood = id.size() >= 3 && id.size() <= 64;
  for (unsigned char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) idGood = false;
  }
  if (!idGood) { err = "bad mod id"; return nullptr; }

  std::map<std::string, size_t> byId;
  std::vector<std::string> pages;
  std::set<std::string> seenPage;
  for (size_t i = 0; i < specs.size(); i++) {
    if (specs[i].id.empty()) specs[i].id = "~" + std::to_string(i + 1);
    if (byId.count(specs[i].id)) { err = "duplicate setting id \"" + specs[i].id + "\""; return nullptr; }
    byId[specs[i].id] = i;
    if (seenPage.insert(specs[i].page).second) pages.push_back(specs[i].page);
  }

  auto get = [&](const char* k) { auto it = meta.find(k); return it == meta.end() ? std::string() : it->second; };

  Mod* old = find(id);
  Mod* m = old;
  if (!m) {
    mods[id] = std::make_unique<Mod>();
    m = mods[id].get();
    m->id = id;
  }
  m->name = get("name").empty() ? id : get("name");
  m->category = get("category").empty() ? "General" : get("category");
  m->version = get("version");
  m->author = get("author");
  m->description = get("description");
  m->icon = get("icon");
  m->tr.clear();
  for (const auto& kv : meta) {
    size_t us = kv.first.rfind('_');
    if (us != std::string::npos && us + 3 == kv.first.size() && !kv.second.empty()) {
      std::string base = kv.first.substr(0, us);
      if (base == "name" || base == "description") m->tr[kv.first] = kv.second;
    }
  }
  m->internal = internal;
  m->lastPoll = now();

  if (!old) {
    std::string text;
    if (fs.read && fs.read(path(id), text)) {
      m->saved = parseIni(text);
      m->disk = m->saved;
      m->fileExists = true;
    }
  }

  // keep what we had for settings that still exist
  std::map<std::string, Value> prev;
  if (old) {
    for (size_t i = 0; i < old->specs.size(); i++) prev[old->specs[i].id] = old->values[i];
  }

  std::vector<Value> values(specs.size());
  for (size_t i = 0; i < specs.size(); i++) {
    const Spec& s = specs[i];
    if (!s.valued()) continue;
    Value v;
    bool have = false;
    std::string e;
    auto p = prev.find(s.id);
    if (p != prev.end()) {
      have = (s.type == Type::Text) ? coerceStr(s, p->second.s, v, e) : coerceNum(s, p->second.n, v);
    }
    auto sv = m->saved.find(s.id);
    if (!have && sv != m->saved.end()) have = coerceStr(s, sv->second, v, e);
    auto sd = seeds.find(s.id);
    if (!have && !m->fileExists && sd != seeds.end() && coerceStr(s, sd->second, v, e)) {
      have = true;
      if (!same(s, v, s.def)) {
        m->saved[s.id] = tostr(s, v);
        m->dirty = true;
        dirtyAt = now();
      }
    }
    values[i] = have ? v : s.def;
  }

  m->specs = std::move(specs);
  m->values = std::move(values);
  m->byId = std::move(byId);
  m->pages = std::move(pages);

  // first sight of a mod with imported values: write the file right away so
  // the import only ever happens once
  if (!m->fileExists && !seeds.empty()) save(*m);

  notify(m, "registered", "", "sdk");
  return m;
}

void Store::unregisterMod(const std::string& id) {
  Mod* m = find(id);
  if (!m) return;
  if (m->dirty && autosave) save(*m);
  notify(m, "unregistered", "", "sdk");
  if (!m->internal) mods.erase(id);
}

const Value* Store::value(Mod& m, const std::string& sid) {
  auto it = m.byId.find(sid);
  return it == m.byId.end() ? nullptr : &m.values[it->second];
}

double Store::num(const std::string& modId, const std::string& sid, double fallback) {
  std::lock_guard<std::recursive_mutex> g(lock);
  Mod* m = find(modId);
  if (!m) return fallback;
  const Value* v = value(*m, sid);
  return v ? v->n : fallback;
}

std::string Store::str(const std::string& modId, const std::string& sid) {
  std::lock_guard<std::recursive_mutex> g(lock);
  Mod* m = find(modId);
  const Value* v = m ? value(*m, sid) : nullptr;
  return v ? v->s : std::string();
}

bool Store::setValue(Mod& m, size_t idx, const Value& in, const std::string& source) {
  if (idx >= m.specs.size() || !m.specs[idx].valued()) return false;
  const Spec& s = m.specs[idx];
  Value v;
  std::string e;
  if (s.type == Type::Text || s.type == Type::List) {
    if (!coerceStr(s, in.s, v, e)) return false;
  } else if (!coerceNum(s, in.n, v)) {
    return false;
  }
  if (s.type == Type::List ? m.values[idx].s == v.s : s.live ? m.values[idx].n == v.n : same(s, m.values[idx], v)) return true;
  m.values[idx] = v;
  if (!s.live) {  // meters are shown, not stored
    if (same(s, v, s.def)) m.saved.erase(s.id);
    else m.saved[s.id] = tostr(s, v);
    touch(m);
  }
  emit(m, 'v', s.id, tostr(s, v), source);
  return true;
}

bool Store::setWire(Mod& m, const std::string& sid, const std::string& raw, const std::string& source, std::string& out) {
  auto it = m.byId.find(sid);
  if (it == m.byId.end() || !m.specs[it->second].valued()) { out = "unknown setting"; return false; }
  const Spec& s = m.specs[it->second];
  Value v;
  if (!coerceStr(s, raw, v, out)) return false;
  setValue(m, it->second, v, source);
  out = tostr(s, m.values[it->second]);
  return true;
}

bool Store::setText(const std::string& modId, const std::string& sid, const std::string& text, const std::string& source) {
  std::lock_guard<std::recursive_mutex> g(lock);
  Mod* m = find(modId);
  if (!m) return false;
  auto it = m->byId.find(sid);
  if (it == m->byId.end()) return false;
  Value v;
  v.s = text;
  return setValue(*m, it->second, v, source);
}

bool Store::setNum(const std::string& modId, const std::string& sid, double n, const std::string& source) {
  std::lock_guard<std::recursive_mutex> g(lock);
  Mod* m = find(modId);
  if (!m) return false;
  auto it = m->byId.find(sid);
  if (it == m->byId.end()) return false;
  Value v;
  v.n = n;
  return setValue(*m, it->second, v, source);
}

bool Store::press(Mod& m, const std::string& sid, const std::string& source) {
  auto it = m.byId.find(sid);
  if (it == m.byId.end() || m.specs[it->second].type != Type::Button) return false;
  emit(m, 'b', sid, "", source);
  return true;
}

bool Store::pressRow(Mod& m, const std::string& sid, int row, int action, const std::string& source) {
  auto it = m.byId.find(sid);
  if (it == m.byId.end() || m.specs[it->second].type != Type::List) return false;
  emit(m, 'b', sid, std::to_string(row) + "," + std::to_string(action), source);
  return true;
}

bool Store::reset(Mod& m, size_t idx, const std::string& source) {
  if (idx >= m.specs.size() || !m.specs[idx].valued()) return false;
  return setValue(m, idx, m.specs[idx].def, source);
}

int Store::resetMod(Mod& m, const std::string& page, const std::string& source) {
  int n = 0;
  for (size_t i = 0; i < m.specs.size(); i++) {
    const Spec& s = m.specs[i];
    if (s.valued() && !s.live && (page.empty() || s.page == page) && !same(s, m.values[i], s.def)) {
      setValue(m, i, s.def, source);
      n++;
    }
  }
  return n;
}

int Store::modifiedCount(const Mod& m) const {
  int n = 0;
  for (size_t i = 0; i < m.specs.size(); i++) {
    if (m.specs[i].valued() && !m.specs[i].live && !same(m.specs[i], m.values[i], m.specs[i].def)) n++;
  }
  return n;
}

bool Store::isActive(const Mod& m) const { return m.internal || (now() - m.lastPoll) < 3; }

// "enabled", "!enabled" or "mode=2"
bool Store::isVisible(const Mod& m, const Spec& s) const {
  const std::string& c = s.show;
  if (c.empty()) return true;
  size_t p = 0;
  bool neg = false;
  if (c[0] == '!') { neg = true; p = 1; }
  size_t eq = c.find('=', p);
  std::string id = c.substr(p, eq == std::string::npos ? std::string::npos : eq - p);
  auto it = m.byId.find(id);
  if (it == m.byId.end()) return true;
  const Spec& o = m.specs[it->second];
  const Value& v = m.values[it->second];
  bool r;
  if (eq != std::string::npos && eq + 1 < c.size()) {
    r = tostr(o, v) == c.substr(eq + 1);
  } else {
    r = o.type == Type::Text ? !v.s.empty() : v.n != 0;
  }
  return neg ? !r : r;
}

std::vector<Mod*> Store::list() {
  std::vector<Mod*> out;
  for (auto& kv : mods) out.push_back(kv.second.get());
  auto low = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
  std::sort(out.begin(), out.end(), [&](Mod* a, Mod* b) {
    if (a->internal != b->internal) return !a->internal;
    std::string ca = low(a->category), cb = low(b->category);
    if (ca != cb) return ca < cb;
    return low(a->name) < low(b->name);
  });
  return out;
}

void Store::counts(int& nm, int& ns) {
  nm = ns = 0;
  for (auto& kv : mods) {
    if (kv.second->internal) continue;
    nm++;
    for (auto& s : kv.second->specs) {
      if (s.valued() || s.type == Type::Button) ns++;
    }
  }
}

int Store::revert(Mod& m, const std::string& source) {
  int n = 0;
  for (size_t i = 0; i < m.specs.size(); i++) {
    const Spec& s = m.specs[i];
    if (!s.valued() || s.live) continue;
    Value v = s.def;
    auto d = m.disk.find(s.id);
    Value t;
    std::string e;
    if (d != m.disk.end() && coerceStr(s, d->second, t, e)) v = t;
    if (!same(s, m.values[i], v)) {
      setValue(m, i, v, source);
      n++;
    }
  }
  m.saved = m.disk;
  m.dirty = false;
  return n;
}

bool Store::save(Mod& m) {
  if (!fs.write) return false;
  std::string out = "; fawwk menu - settings for " + m.name + "\n" +
                    "; only values that differ from the defaults get stored. safe to edit by hand.\n" +
                    "[mod]\nid=" + m.id + "\nversion=" + esc(m.version) + "\n\n[values]\n";
  for (auto& kv : m.saved) out += kv.first + "=" + esc(kv.second) + "\n";
  bool ok = fs.write(path(m.id), out);
  if (ok) {
    m.disk = m.saved;
    m.dirty = false;
    m.fileExists = true;
  } else {
    lastError = "could not write " + path(m.id);
  }
  return ok;
}

int Store::flush(bool force) {
  if (!force && !autosave) return 0;
  int n = 0;
  for (auto& kv : mods) {
    if (kv.second->dirty && save(*kv.second)) n++;
  }
  dirtyAt = -1;
  return n;
}

void Store::tick() {
  if (dirtyAt >= 0 && autosave && now() - dirtyAt >= saveDelay) {
    if (flush(false) > 0) notify(nullptr, "saved", "", "store");
  }
}

// the string api ---------------------------------------------------------------

std::string Store::hello() { return "1|" + epoch; }

std::string Store::apiRegister(const std::string& metaStr, const std::string& specStr, const std::string& seedStr) {
  std::lock_guard<std::recursive_mutex> g(lock);
  Map meta = unpack(metaStr);
  std::vector<Spec> specs;
  for (auto& line : lines(specStr)) {
    if (line.empty()) continue;
    Spec s;
    std::string err;
    if (!parseSpec(unpack(line), s, err)) return "err: " + err;
    specs.push_back(std::move(s));
  }
  std::string err;
  Mod* m = registerMod(meta, std::move(specs), unpack(seedStr), false, err);
  if (!m) return "err: " + err;
  return "ok\n" + std::to_string(m->seq) + "\n" + wireValues(*m);
}

std::string Store::apiUnregister(const std::string& id) {
  std::lock_guard<std::recursive_mutex> g(lock);
  unregisterMod(id);
  return "ok";
}

std::string Store::apiPoll(const std::string& id, const std::string& lastStr) {
  std::lock_guard<std::recursive_mutex> g(lock);
  Mod* m = find(id);
  if (!m) return "unknown";
  long last = std::atol(lastStr.c_str());
  m->lastPoll = now();
  bool resync = last < m->droppedSeq || last > m->seq;
  std::string out = epoch + "," + (open ? "1" : "0") + "," + std::to_string(m->seq) + "," + (resync ? "1" : "0") + "," + language;
  if (!resync) {
    for (auto& e : m->ring) {
      if (e.seq > last) out += "\n" + std::to_string(e.seq) + "|" + std::string(1, e.kind) + "|" + e.id + "|" + enc(e.val);
    }
  }
  return out;
}

std::string Store::wireValues(const Mod& m) const {
  Map t;
  for (size_t i = 0; i < m.specs.size(); i++) {
    if (m.specs[i].valued()) t[m.specs[i].id] = tostr(m.specs[i], m.values[i]);
  }
  return pack(t);
}

std::string Store::apiDump(const std::string& id) {
  std::lock_guard<std::recursive_mutex> g(lock);
  Mod* m = find(id);
  if (!m) return "unknown";
  return std::to_string(m->seq) + "\n" + wireValues(*m);
}

std::string Store::apiSet(const std::string& id, const std::string& sid, const std::string& val) {
  std::lock_guard<std::recursive_mutex> g(lock);
  Mod* m = find(id);
  if (!m) return "err: unknown mod";
  std::string out;
  if (!setWire(*m, sid, val, "sdk", out)) return "err: " + out;
  return "ok\n" + out;
}

std::string Store::apiSave(const std::string& id) {
  std::lock_guard<std::recursive_mutex> g(lock);
  if (Mod* m = find(id)) save(*m);
  return "ok";
}

std::string Store::apiShow(const std::string& id) {
  std::lock_guard<std::recursive_mutex> g(lock);
  showRequest = id;
  return "ok";
}

}  // namespace fw
