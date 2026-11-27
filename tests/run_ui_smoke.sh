#!/bin/sh
# needs a checkout of imgui (v1.91.5): IMGUI=/path/to/imgui tests/run_ui_smoke.sh
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
: "${IMGUI:?set IMGUI to an imgui checkout}"
out=${TMPDIR:-/tmp}/fawwk_ui_smoke
clang++ -std=c++17 -g -I"$here/src" -I"$here/tests" -I"$IMGUI" \
  "$here/tests/ui_smoke.cpp" "$here/src/ui.cpp" "$here/src/theme.cpp" "$here/tests/demo.cpp" \
  "$here"/src/core/*.cpp "$IMGUI"/imgui.cpp "$IMGUI"/imgui_draw.cpp "$IMGUI"/imgui_tables.cpp "$IMGUI"/imgui_widgets.cpp \
  -o "$out"
"$out"
