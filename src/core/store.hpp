// store.hpp
// the data side of the menu: registered mods, their values, saving to disk
// and the string api scripts use. nothing in here draws anything.
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "schema.hpp"

namespace fw {

struct Event {
  long seq;
  char kind;  // 'v' value, 'b' button
  std::string id, val;
};

struct Mod {
  std::string id, name, category, version, author, description, icon;
  Map tr;  // name_pt, description_pt ...
  bool internal = false;
  std::vector<Spec> specs;
  std::vector<Value> values;  // same order as specs
  std::map<std::string, size_t> byId;
  std::vector<std::string> pages;
  std::map<std::string, std::string> saved;  // overrides as they are right now
  std::map<std::string, std::string> disk;   // overrides as last written to the ini
  std::deque<Event> ring;
  long seq = 0, droppedSeq = 0;
  bool dirty = false, fileExists = false;
  double lastPoll = 0;
};

struct Files {
  std::function<bool(const std::string& path, std::string& out)> read;
  std::function<bool(const std::string& path, const std::string& data)> write;
  std::function<void(const std::string& dir)> mkdir;
};

// kind: "v", "b", "registered", "unregistered", "saved"
using Listener = std::function<void(Mod* m, const std::string& kind, const std::string& id, const std::string& source)>;

class Store {
 public:
  Store(Files files, std::string dir, std::function<double()> now, std::string epoch);

  std::recursive_mutex lock;

  Mod* find(const std::string& id);
  Mod* registerMod(const Map& meta, std::vector<Spec> specs, const Map& seeds, bool internal, std::string& err);
  void unregisterMod(const std::string& id);

  // numeric/text access for the ui and the host itself
  const Value* value(Mod& m, const std::string& sid);
  double num(const std::string& modId, const std::string& sid, double fallback = 0);
  std::string str(const std::string& modId, const std::string& sid);
  bool setValue(Mod& m, size_t idx, const Value& v, const std::string& source);
  bool setWire(Mod& m, const std::string& sid, const std::string& raw, const std::string& source, std::string& out);
  bool setText(const std::string& modId, const std::string& sid, const std::string& text, const std::string& source);
  bool setNum(const std::string& modId, const std::string& sid, double n, const std::string& source);
  bool pressRow(Mod& m, const std::string& sid, int row, int action, const std::string& source);
  bool press(Mod& m, const std::string& sid, const std::string& source);
  bool reset(Mod& m, size_t idx, const std::string& source);
  int resetMod(Mod& m, const std::string& page, const std::string& source);  // empty page = all
  int modifiedCount(const Mod& m) const;
  bool unsaved(const Mod& m) const { return m.saved != m.disk; }
  int revert(Mod& m, const std::string& source);  // back to what the ini holds
  bool isActive(const Mod& m) const;
  bool isVisible(const Mod& m, const Spec& s) const;
  std::vector<Mod*> list();
  void counts(int& mods, int& settings);

  bool save(Mod& m);
  int flush(bool force);
  void tick();

  void subscribe(Listener fn) { listeners.push_back(std::move(fn)); }

  // the string api (what lives behind the exports)
  std::string hello();
  std::string apiRegister(const std::string& meta, const std::string& specs, const std::string& seeds);
  std::string apiUnregister(const std::string& id);
  std::string apiPoll(const std::string& id, const std::string& last);
  std::string apiDump(const std::string& id);
  std::string apiSet(const std::string& id, const std::string& sid, const std::string& val);
  std::string apiSave(const std::string& id);
  std::string apiShow(const std::string& id);

  bool open = false;
  bool autosave = true;
  double saveDelay = 0.8;
  std::string showRequest;
  std::string language = "en";  // current menu language, sent to mods with every poll
  std::string prefix;  // for settings files kept loose next to other files, "fawwk_" in front of every name but our own
  std::string lastError;
  std::string epoch;

 private:
  void emit(Mod& m, char kind, const std::string& id, const std::string& val, const std::string& source);
  void notify(Mod* m, const std::string& kind, const std::string& id, const std::string& source);
  void touch(Mod& m);
  std::string path(const std::string& id) const {
    return dir + "/" + (id.rfind("fawwk", 0) == 0 ? "" : prefix) + id + ".ini";
  }
  std::string wireValues(const Mod& m) const;

  Files fs;
  std::string dir;
  std::function<double()> now;
  std::map<std::string, std::unique_ptr<Mod>> mods;
  std::vector<Listener> listeners;
  double dirtyAt = -1;
};

}  // namespace fw
