#!/bin/sh
# builds the core + lua bridge and runs the sdk test against it (linux, needs clang/g++ and liblua5.4.so)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=${TMPDIR:-/tmp}/fawwk_native_test
mkdir -p "$out"
LUA=${LUA_SO:-/usr/lib/x86_64-linux-gnu/liblua5.4.so.0}
${CXX:-clang++} -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -o "$out/native_bridge" \
  "$here/tests/native_bridge.cpp" "$here"/src/core/*.cpp "$LUA" -lpthread
"$out/native_bridge" "$here/tests/native_sdk_test.lua" "$here"
