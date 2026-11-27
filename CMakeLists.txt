cmake_minimum_required(VERSION 3.20)
project(fawwkmenu LANGUAGES CXX)

# GTA SA is 32 bit, so this has to be built as Win32 / x86:
#   cmake -A Win32 -S . -B build
#   cmake --build build --config Release
# the result is build/Release/fawwkmenu.asi

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static runtime so players don't need any redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(FAWWK_BUILD_CORE_ONLY "build just the portable core (used by the tests)" OFF)

set(CORE_SRC src/core/wire.cpp src/core/schema.cpp src/core/store.cpp src/core/api.cpp src/core/lang.cpp src/core/lang_builtin.cpp)

if(FAWWK_BUILD_CORE_ONLY OR NOT WIN32)
  add_library(fawwkcore STATIC ${CORE_SRC})
  return()
endif()

include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.5
  GIT_SHALLOW TRUE
)
# single header image reader, only used for the optional mod icons
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui stb)

# the Lua SDK is carried inside the plugin (see fawwk_sdk in main.cpp), so mods do not need a copy of it
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lua/fawwk.lua)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/lua/fawwk.lua SDK_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," SDK_BYTES "${SDK_HEX}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/sdk_lua.inc "${SDK_BYTES}0x00")

add_library(fawwkmenu SHARED
  ${CORE_SRC}
  src/main.cpp src/ui.cpp src/theme.cpp src/sdk.cpp src/icons.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)

target_include_directories(fawwkmenu PRIVATE src ${imgui_SOURCE_DIR} ${stb_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_compile_definitions(fawwkmenu PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN IMGUI_DISABLE_DEMO_WINDOWS)
target_link_libraries(fawwkmenu PRIVATE d3d9 dwmapi)

if(MSVC)
  target_compile_options(fawwkmenu PRIVATE /EHsc /utf-8 /W3)
endif()

# the game loads .asi files, it's just a dll with another name
set_target_properties(fawwkmenu PROPERTIES PREFIX "" SUFFIX ".asi")
