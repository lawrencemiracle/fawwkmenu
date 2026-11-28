# fawwk menu

Made by **fawwktrustdigital**.

One in-game menu for all your GTA San Andreas mods, as a plain `.asi`. Mods register their settings with it instead of shipping their own UI and ini file.

GTA SA 1.0 US (32 bit). Needs an ASI loader. Nothing else: no MoonLoader, mimgui or CLEO needed for the menu itself. Lua mods that use it still run on MoonLoader like always.

Press **F9** or **F10** in game. The language (English, Portuguese, Spanish, Russian) is picked in *fawwk options*; mods that bring translations follow it. Rebind both under *fawwk options*.

## What you get

- One window: sidebar of mods by category, tabs per mod, collapsible groups, search across every setting of every mod.
- A dot marks anything changed from its default, click it to reset just that one.
- Changes apply live so people can try things. They only reach the disk when you press Save (autosave is an option, off by default). Revert and Defaults buttons per mod.
- Settings can hide themselves based on other settings.
- Switch, slider, number box, dropdown, color (with optional alpha), key bind, text, button, plus headers, dividers, notes.
- Five themes, accent color, UI scale, opacity, rounding.
- While the menu is open the game can pause, run in slow motion, or keep playing, and the picture behind it can be clear, dimmed or blurred.
- With "Keep clicks and keys away from the game" on (default), the game and other mods see no mouse or keyboard input while the menu is open, so nothing moves and no other menu opens.
- Mods keep working on their defaults if the menu isn't installed.

## Install

Download `fawwkmenu.asi` from the releases page (or build it, below) and put it in its own folder in modloader, `modloader\fawwkmenu\`. It also works from the game folder (next to `gta_sa.exe`) or `scripts`.

Settings go in a `settings` folder where the plugin is:

- in `modloader\fawwkmenu\`: `modloader\fawwkmenu\settings\<mod>.ini`
- anywhere else: `fawwkmenu\settings` next to the plugin, so nothing ends up loose among your other files
- if that can't be written to (Program Files, a read-only copy): `fawwkmenu\settings` in the game folder, then AppData

Settings from older versions are copied over the first time. `fawwkmenu.log` is written next to the `.asi`; you can switch it off under *fawwk options*, General.

## Build it yourself

**GitHub Actions:** fork or upload the repo, open the Actions tab, run `build`, download the `fawwkmenu` artifact. It holds `fawwkmenu.asi`, `include/fawwk.hpp` and `lua/fawwk.lua`.

**Locally:** Visual Studio 2022 with the C++ desktop workload and CMake, then

```
cmake -A Win32 -S . -B build
cmake --build build --config Release
```

The output is `build/Release/fawwkmenu.asi`. It has to be a Win32 build, a 64 bit one won't load in the game. CMake downloads Dear ImGui and stb on the first configure, so you need internet that once.

## Using it from your mods

Lua (MoonLoader): nothing to copy, the Lua side is inside `fawwkmenu.asi`. Load it with the few lines from the integration guide, then

```lua
local fawwk = require 'fawwk'
local cfg = fawwk.mod { id = 'you.mymod', name = 'My Mod', category = 'Gameplay' }
cfg:toggle('enabled', 'Enabled', true)
cfg:slider('power', 'Power', 1.5, 0.5, 5, { step = 0.1 })
-- anywhere: cfg.v.power
```

C++: copy `include/fawwk.hpp` and bind variables to settings. Details and every option are in [docs/INTEGRATION.md](docs/INTEGRATION.md). `examples/` has a small Lua mod and a small C++ mod that show every control, so copy from those and delete the rest.

## Status

Version 1.0, GTA SA 1.0 US only. The settings core and the menu layout have automated tests (`tests/run_native.sh`, `tests/run_ui_smoke.sh`). The Windows side (the D3D9 hook, drawing, input handling, pause and slow motion) can't be tested that way and has been tried by hand in game, so bug reports are welcome. Please attach `fawwkmenu.log`.

## Layout

```
src/core/      registry, saving, text protocol, c exports (portable c++)
src/           ui, theme, d3d9 hook and plugin shell (windows only)
include/       fawwk.hpp for c++ mods
lua/           fawwk.lua for lua mods
examples/      a lua mod and a c++ mod
tests/         core tests (linux/mac, need clang or g++ and lua 5.4)
```


## Menu doesn't open?

Look at `fawwkmenu.log` next to the `.asi`. It is rewritten on every start and says which step failed:

- no log file at all: the game never loaded the .asi (check you have an ASI loader and the file is in the game folder or `scripts`)
- `patched game device ... ok` then `alive check` lines with `EndScene calls=0`: something else (overlay, d3d9 wrapper) replaced the device
- `menu frames` going up but nothing on screen: send the log, it's a drawing problem

F9 and F10 open it by default, both can be changed in the menu's settings. Esc closes it.

## Game crashes while starting?

Nothing in this plugin writes to disk before d3d9 is up, and nothing touches other mods until the first time you open the menu; before that it only registers settings and puts one hook on the graphics device. If the game still crashes on startup with certain mod combinations, `fawwkmenu.log` shows how far startup got, and any access violation inside the plugin is written there with its address. The load order of the .asi files (modloader priority) can matter: this plugin works fine loaded late.

## License

MIT, see [LICENSE](LICENSE). Copyright (c) 2026 fawwktrustdigital. Dear ImGui and stb are used under their own licenses, see [THIRD_PARTY.txt](THIRD_PARTY.txt).
