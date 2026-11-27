// sdk.cpp
// the Lua SDK (lua/fawwk.lua) travels inside this plugin. the build turns the file into sdk_lua.inc,
// and scripts fetch the text with fawwk_sdk(), so they always get the version that matches the plugin.
static const char kSdk[] = {
#include "sdk_lua.inc"
};

extern "C" __declspec(dllexport) const char* fawwk_sdk() { return kSdk; }
