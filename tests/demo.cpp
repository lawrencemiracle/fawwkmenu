#include "demo.hpp"

#include <cmath>
#include <vector>

namespace demo {

const char* ID = "fawwk.demo";

static fw::Spec spec(fw::Map m) {
  fw::Spec s;
  std::string err;
  fw::parseSpec(m, s, err);
  return s;
}

void add(fw::Store& store) {
  std::vector<fw::Spec> specs = {
      spec({{"type", "bool"}, {"id", "enabled"}, {"label", "Enabled"}, {"default", "1"}, {"desc", "A plain on/off switch."}}),
      spec({{"type", "float"}, {"id", "power"}, {"label", "Power"}, {"default", "1.5"}, {"min", "0"}, {"max", "5"}, {"step", "0.1"},
            {"desc", "A slider with decimals."}}),
      spec({{"type", "int"}, {"id", "count"}, {"label", "Count"}, {"default", "3"}, {"min", "1"}, {"max", "10"}}),
      spec({{"type", "enum"}, {"id", "mode"}, {"label", "Mode"}, {"default", "1"}, {"options", "Calm|Normal|Wild"}}),
      spec({{"type", "enum"}, {"id", "style"}, {"label", "Style"}, {"default", "2"}, {"options", "Quiet|Loud|Very loud"}, {"widget", "radio"},
            {"desc", "The same kind of choice as Mode, drawn as radio buttons."}}),
      spec({{"type", "float"}, {"id", "level"}, {"label", "Live level"}, {"default", "0"}, {"min", "0"}, {"max", "100"}, {"widget", "meter"}, {"fmt", "%.0f%%"},
            {"desc", "A read-only bar. The mod fills it in, you only look."}}),
      spec({{"type", "color"}, {"id", "tint"}, {"label", "Tint"}, {"default", "4281559039"}}),
      spec({{"type", "key"}, {"id", "hotkey"}, {"label", "Hotkey"}, {"default", "78"}, {"desc", "Click, then press a key."}}),
      spec({{"type", "text"}, {"id", "name"}, {"label", "Name"}, {"default", "fawwk"}}),
      spec({{"type", "list"}, {"id", "stash"}, {"label", "Stash"}, {"options", "Use|Drop"}, {"text", "Nothing stashed. Suspiciously clean."},
            {"desc", "A list the mod fills in. Every row gets the buttons you give it."}}),
      spec({{"type", "button"}, {"id", "boom"}, {"label", "Do the thing"}, {"text", "Run"}}),
      spec({{"type", "header"}, {"label", "Advanced"}, {"page", "Advanced"}}),
      spec({{"type", "bool"}, {"id", "adv"}, {"label", "Show hidden slider"}, {"default", "0"}, {"page", "Advanced"}}),
      spec({{"type", "float"}, {"id", "hidden"}, {"label", "Hidden slider"}, {"default", "1"}, {"min", "0"}, {"max", "2"}, {"show", "adv"},
            {"page", "Advanced"}, {"desc", "Only visible while the switch above is on."}}),
      spec({{"type", "note"}, {"label", "A demo mod built into the menu. It does nothing, which is more than some mods manage. Turn it off in fawwk options > About."}, {"page", "Advanced"}}),
  };
  fw::Map meta = {{"id", ID}, {"name", "Demo mod"}, {"category", "Demo"}, {"version", "1.0"}, {"author", "fawwk"},
                  {"description", "One of every kind of setting, just to try things out."}};
  std::string err;
  store.registerMod(meta, std::move(specs), {}, false, err);
  store.setText(ID, "stash", "Pistol\t9mm, 12 rounds\nSpray can\thalf full\nSandwich\tabout a week old", "demo");
}

void remove(fw::Store& store) { store.unregisterMod(ID); }

void tick(fw::Store& store, double seconds) {
  store.setNum(ID, "level", 50.0 + 50.0 * std::sin(seconds * 1.3), "demo");
}

}  // namespace demo
