# Putting your mod in fawwk menu

Good news first: it is small work. You define your settings in a few lines, and the menu draws them for you. No UI code, no ini parser, no key-capture code. You just read a value.

There are two ways in:

- **Lua mod (MoonLoader):** the SDK is inside `fawwkmenu.asi` (`lua/fawwk.lua` is the same file, for reading)
- **C++ / ASI mod:** use `include/fawwk.hpp` (it is lower down on this page)

Both talk to the same `fawwkmenu.asi`.

---

## The short version (Lua)

```lua
local fawwk = require 'fawwk'

local cfg = fawwk.mod {
  id = 'yourname.nitro',      -- lowercase letters, digits, . _ -   (3 to 64 characters)
  name = 'Nitro Boost',       -- what shows in the list on the left
  category = 'Vehicles',      -- the group it sits under
  version = '1.2',            -- optional
  author = 'you',             -- optional
  description = 'Free nitro.' -- optional
}

cfg:toggle('enabled', 'Enabled', true)
cfg:slider('power', 'Power', 1.5, 0.5, 5.0, { step = 0.1, fmt = '%.1fx' })
cfg:key('hotkey', 'Boost key', 0x4E)

function main()
  while true do
    wait(0)
    if cfg.v.enabled and cfg:pressed('hotkey') then
      -- do your thing
    end
  end
end
```

There is nothing to install: the Lua side is built into `fawwkmenu.asi`. Put this at the top of your script to get it:

```lua
local function fawwkSdk()
    local ok, sdk = pcall(function()
        local ffi = require 'ffi'
        pcall(ffi.cdef, 'void* GetModuleHandleA(const char*); void* GetProcAddress(void*, const char*);')
        local mod = ffi.C.GetModuleHandleA('fawwkmenu.asi')
        local addr = mod ~= nil and ffi.C.GetProcAddress(mod, 'fawwk_sdk') or nil
        if addr == nil then return nil end
        return assert(loadstring(ffi.string(ffi.cast('const char*(*)(void)', addr)()), '=fawwk sdk'))()
    end)
    if ok and sdk then package.loaded.fawwk = sdk return sdk end
    local ok2, file = pcall(require, 'fawwk')   -- a copy on disk still works, for old plugin versions
    return ok2 and file or nil
end
local fawwk = fawwkSdk()   -- nil when the menu is not installed
```

That is all the setup. Your settings show up in the menu by themselves, and `cfg.v.power` always holds the current value. Reading it is free, so use it as often as you like.

If the menu is not installed on the player's side, no wahala: `cfg.v.power` simply gives your default and your mod carries on as normal.

**About the id.** The id is your mod's identity. Saved values are tied to it, so choose it once and never change it. If you change it, players lose their saved settings.

---

## The controls

Every builder gives `cfg` back, so you can chain them. The last argument `o` is an optional table of extras (explained just below).

| call | what the player sees | value you get |
| --- | --- | --- |
| `cfg:toggle(id, label, default, o)` | on/off switch | `true` / `false` |
| `cfg:slider(id, label, default, min, max, o)` | slider with decimals | number |
| `cfg:int(id, label, default, min, max, o)` | slider for whole numbers | integer |
| `cfg:combo(id, label, defaultIndex, {'a','b'}, o)` | dropdown | index, starting from 1 |
| `cfg:meter(id, label, default, min, max, o)` | read-only bar that you fill with `cfg:set` | number (never saved) |
| `cfg:color(id, label, default, o)` | color picker | `0xAARRGGBB` |
| `cfg:key(id, label, default, o)` | key bind | virtual key code, 0 means none |
| `cfg:text(id, label, default, o)` | text box | string |
| `cfg:button(id, label, fn, o)` | button that runs `fn` when pressed | nothing |
| `cfg:header(text)` | section title | nothing |
| `cfg:separator()` | divider line | nothing |
| `cfg:note(text)` | small grey paragraph | nothing |

Extras you can put in `o` on any of them:

| key | what it does |
| --- | --- |
| `desc` | tooltip text (the player can also choose to see it under each setting) |
| `page` | which tab it goes on, default is `General` |
| `group` | a fold-able section inside the tab |
| `tag` | small grey word after the label, like `restart` |
| `show` | only show the setting when another one says so: `'enabled'`, `'!enabled'`, `'mode=2'` |

A few extras depend on the type:

- combos: `widget = 'radio'` draws the choices as radio buttons instead of a dropdown
- sliders and ints: `step`, and `fmt` (printf style, like `'%.1fx'` or `'%d%%'`). Add `widget = 'input'` if you want a typed box instead of a slider.
- colors: `alpha = true` gives the player an alpha channel. Without it, alpha is always `FF`.
- text: `maxlen` (default 64, maximum 250)
- buttons: `text` is the caption on the button (default is "Run")

If you prefer, you can skip the builders and write `cfg:add { type = 'bool', id = 'x', label = 'X', default = true }`. The type names `toggle`, `checkbox`, `slider`, `combo`, `dropdown`, `hotkey`, `string`, `label` and `divider` are accepted too.

### Pages and groups, without repeating yourself

Set the page or group once and everything after it follows:

```lua
cfg:page('Tuning')
cfg:group('Engine')
cfg:slider('torque', 'Torque', 1, 0.5, 3)
cfg:slider('rev', 'Rev limit', 1, 0.5, 3)
cfg:group('Gearbox')
cfg:combo('gears', 'Gears', 2, { '4', '5', '6' })
cfg:page('Look')          -- new tab, and the group starts fresh
cfg:color('flame', 'Flame', 0xFFFF8800)
```

---

## Reading values and reacting to changes

```lua
cfg.v.power              -- the current value
cfg:get('power')         -- same thing
cfg:option('mode')       -- the text of the selected dropdown entry
cfg:rgba('tint')         -- r, g, b, a from 0 to 255, ready for renderDrawBox
cfg.v.power = 2          -- set it from your own code (same as cfg:set('power', 2))
cfg:reset('power')       -- back to the default

cfg:on('power', function(new, old) ... end)   -- when one setting changes
cfg:on('*', function(id, new, old) ... end)   -- when anything changes
cfg:onReady(function() ... end)               -- the first time the menu connects
```

Your callbacks run on your own script's thread. Nothing of yours runs inside the menu, so even if your callback has a bug, it cannot bring the menu down.

For hotkeys, use `cfg:pressed('hotkey')` and `cfg:down('hotkey')`. They return false while the menu is open, so when a player is typing in a text box your mod does not fire by mistake.

Two more small ones:

- `fawwk.isOpen()` tells you if the menu is open. `fawwk.onToggle(function(open) end)` tells you the moment it opens or closes.
- `cfg:open()` opens the menu straight on your mod's page. Good for a "settings" key or a chat command.

---

## You already have an ini? Keep it

Do not throw away your ini loader. After you define the settings, hand the old values over one time:

```lua
cfg:seed({ power = myIni.power, enabled = myIni.enabled })
```

The menu uses the seed only the first time it ever sees your mod (when there is no saved file for it yet). It writes the values down, and from then on its own saved values win. So players keep the settings they already had, and your ini stops mattering after that first time.

Call `seed` right after you define the settings, before your script's first `wait`.

---

## How saving works (players will ask you this)

- Changes apply **immediately**, so a player can flip a switch and see what happens.
- They are written to disk only when the player presses **Store** (or **Store everything**), unless the player turned on **Autosave** in the menu's own options. Autosave is off by default.
- **Undo edits** takes the mod back to what was last stored. **Stock values** goes back to your defaults.
- Only values that differ from your defaults are written. So when you change a default in a new version, players who never touched that setting get the new default automatically.
- Settings live in a `settings` folder where `fawwkmenu.asi` is: `settings\<your id>.ini` inside its own folder when that is inside modloader, otherwise `fawwkmenu\settings` next to it. If that cannot be written to, `fawwkmenu\settings` in the game folder is used, then AppData. Players can edit the file by hand too.

---

## Good to know

- The menu cleans every value to your min, max and step, so you can trust that `cfg.v.count` is a whole number inside your range.
- Setting ids use letters, digits and `_ . -`. Headers, separators and notes do not need ids.
- The dropdown value is an index starting from 1, like everything else in Lua.
- If your script crashes, its entry turns grey in the list and the player's values are kept.
- You can add controls later while the game is running. The sdk sends the new layout again.
- The sdk starts its own small background thread to talk to the menu. If the MoonLoader console prints `no lua_thread available`, do the polling yourself: write `fawwk.mod { ..., manual = true }` and call `fawwk.poll()` once inside your main loop, after `wait(0)`. This is what Outfitter does.
- If the menu plugin is not ready yet, the sdk just tries again with a growing delay (up to 15 seconds). You do not need to handle anything.

---

## C++ / ASI mods

Copy `include/fawwk.hpp` into your project. There is no library to link; it finds `fawwkmenu.asi` at runtime. Your own variables are tied to the settings and always hold the current value:

```cpp
#include "fawwk.hpp"

static bool  g_enabled = true;
static float g_power   = 1.5f;
static fawwk::Mod g_menu("yourname.mymod", "My Mod", "Gameplay");

void setup() {
  g_menu.toggle("enabled", "Enabled", &g_enabled, true)
        .slider("power", "Power", &g_power, 1.5f, 0.5f, 5.f, {.step = 0.1});
}

void everyFrame() {
  g_menu.poll();           // keeps the variables up to date, call once per frame
  if (g_enabled) { /* use g_power */ }
}
```

`{.step = 0.1}` is C++20 syntax. If you are on C++17, make an `fawwk::Opts o; o.step = 0.1;` and pass `o`.

What you can call: `toggle`, `slider`, `meter` (fill it with `set(id, value)`), `integer`, `combo` (the variable is 0-based), `color` (`0xAARRGGBB`), `key`, `text`, `button`, `header`, `separator`, `note`, plus `page()`, `group()`, `version()`, `author()` and `description()`. The extras live in `fawwk::Opts` and match the Lua ones. `fawwk::Mod::menuOpen()` tells you whether the menu is showing, and `show()` opens it on your page. There is a full example in `examples/example_mod.cpp`.

---

## Translations (optional)

The player picks a language in *fawwk options* (English, Portuguese (Brasil), Spanish, Russian). Your mod follows it
only if you bring translations. Anything you leave out stays exactly as you wrote it, so a mod with no translations
keeps working and just shows in its own language.

Lua: add `_pt`, `_es`, `_ru` next to any text.

```lua
local cfg = fawwk.mod {
  id = 'yourname.nitro', name = 'Nitro Boost', name_pt = 'Impulso de Nitro',
  description = 'Extra power.', description_pt = 'Potência extra.',
}
cfg:page('Keys', { pt = 'Teclas', es = 'Teclas', ru = 'Клавиши' })       -- page names
cfg:group('Tuning', { pt = 'Ajustes finos' })                             -- group names
cfg:toggle('on', 'Enabled', true, { label_pt = 'Ligado', desc = 'Turns it on', desc_pt = 'Liga' })
cfg:combo('mode', 'Mode', 1, { 'Slow', 'Fast' }, { options_pt = { 'Lento', 'Rápido' } })
cfg:button('go', 'Run', fn, { text = 'Go', text_pt = 'Ir' })
```

Fields that can be translated: `name`, `description` (the mod), and `label`, `desc`, `tag`, `text`, `options` (list
captions too) on settings. Page and group names take a table as shown. Your own in-game messages can follow the same
choice: `fawwk.language()` returns `'en'`, `'pt'`, `'es'` or `'ru'`.

C++: the same keys go into `Opts::tr`, and pages, groups and the mod have their own helpers.

```cpp
g_menu.translate("pt", "Impulso de Nitro", "Potência extra.");
g_menu.page("Keys", {{"pt", "Teclas"}});
g_menu.toggle("on", "Enabled", &g_on, true, {.tr = {{"label_pt", "Ligado"}, {"desc_pt", "Liga"}}});
g_menu.combo("mode", "Mode", &g_mode, 1, {"Slow", "Fast"}, {.tr = {{"options_pt", "Lento|Rápido"}}});
// fawwk::Mod::language() gives the code
```

### Fixing or adding menu text without rebuilding

The menu's own text lives in tables inside the plugin. A file called `lang_pt.txt` (or `lang_es.txt`, `lang_ru.txt`)
in the settings folder, next to the other `.ini` files, overrides single lines or adds missing ones. One line
each, the English text, `=>`, the translation. Lines starting with `#` are comments.

```
# lang_pt.txt
Store => Salvar
Stock values => Valores de fábrica
```

## How it works (for the curious)

`fawwkmenu.asi` owns all the data. It exports a few plain C functions that take text and give back text: `fawwk_register`, `fawwk_poll`, `fawwk_set` and a few more, all `__cdecl`. The Lua sdk calls them through ffi, and the C++ header through `GetProcAddress`.

Your settings are sent as text when you register. After that, each frame the sdk asks "anything new since event N?" and receives only the changes. This is why reading `cfg.v.x` or your bound variable costs nothing.

If the plugin restarts and forgets your mod, the next poll answers `unknown` and the sdk registers again by itself.

---

## When something is not working

- **The menu does not open on F9.** Open `fawwkmenu.log`. It sits right next to `fawwkmenu.asi` (if it is missing, the log was switched off under *fawwk options*, General: switch it on, restart and try again). The log lists each startup step: `starting`, `ready`, `patched game device`, `first frame`, `menu opened`. Wherever it stops is where the problem is. After 30 seconds it also says whether the game ever called the hooked functions. Send that file to the author.
- **My Lua mod does not show up.** Check three things. First, `fawwkmenu.asi` must be loaded (the SDK comes from it). Second, the plugin file must keep the name `fawwkmenu.asi`. Third, look in the log: every mod that registers gets a line like `mod registered: Nitro Boost`. If your line is not there, the menu never heard from your script, so check the MoonLoader log for lines starting with `[fawwk]`.
- **The game crashes at startup.** Look at the same log first. It records how far the startup went, and any crash inside the plugin is written there with an address.

## Showing one page in a small window

`cfg:open()` opens the whole menu on your mod. `cfg:popup('Page name')` (C++: `mod.popup("Page name")`)
shows only that page in a small window of its own, with a close button and a link to the full menu. The game is not paused, dimmed or blurred, and keys still reach it unless a text box has focus. Calling `popup` again with the same page closes the window, so the hotkey that opens it can close it too.
Good for a quick list the player opens with a hotkey, like a saved-cars list.

## Icons (optional)

A mod can have a small picture next to its name in the sidebar and above its page. Skip it and nothing changes.

```lua
local cfg = fawwk.mod { id = 'nitro', name = 'Nitro Boost', icon = 'nitro.png' }
```

C++: `mod.icon("path\\to\\nitro.png")`. The file can be a png, jpg, bmp or tga, up to 512 x 512 and 2 MB, and it can
have any name and sit in any folder. In Lua a relative path is taken from the script's own folder, in C++ from the
game folder. If the file is missing or not an image, the menu writes one line about it to `fawwkmenu.log` and
carries on without an icon.
