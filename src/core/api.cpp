// api.cpp
// the plain C exports other scripts and plugins call. every one takes and
// returns text, so lua (ffi), c++ and anything else can use them the same way.
// returned pointers stay valid until the next call from the same thread.
#include "api.hpp"

#include <exception>
#include <string>

#ifdef _WIN32
#define FAWWK_API extern "C" __declspec(dllexport)
#else
#define FAWWK_API extern "C" __attribute__((visibility("default")))
#endif

static fw::Store* g_store = nullptr;

void fw::setGlobalStore(Store* s) { g_store = s; }

static const char* reply(const std::string& s) {
  thread_local std::string buf;
  buf = s;
  return buf.c_str();
}

static std::string safe(const char* p) { return p ? std::string(p) : std::string(); }

#define NEED_STORE() if (!g_store) return reply("err: menu not ready")
#define GUARD(expr)                               \
  try {                                           \
    return reply(expr);                           \
  } catch (const std::exception& e) {             \
    return reply(std::string("err: ") + e.what()); \
  } catch (...) {                                 \
    return reply("err: unknown");                 \
  }

FAWWK_API const char* fawwk_hello() {
  NEED_STORE();
  GUARD(g_store->hello());
}

FAWWK_API const char* fawwk_register(const char* meta, const char* specs, const char* seeds) {
  NEED_STORE();
  GUARD(g_store->apiRegister(safe(meta), safe(specs), safe(seeds)));
}

FAWWK_API const char* fawwk_unregister(const char* id) {
  NEED_STORE();
  GUARD(g_store->apiUnregister(safe(id)));
}

FAWWK_API const char* fawwk_poll(const char* id, const char* last) {
  NEED_STORE();
  GUARD(g_store->apiPoll(safe(id), safe(last)));
}

FAWWK_API const char* fawwk_dump(const char* id) {
  NEED_STORE();
  GUARD(g_store->apiDump(safe(id)));
}

FAWWK_API const char* fawwk_set(const char* id, const char* sid, const char* val) {
  NEED_STORE();
  GUARD(g_store->apiSet(safe(id), safe(sid), safe(val)));
}

FAWWK_API const char* fawwk_save(const char* id) {
  NEED_STORE();
  GUARD(g_store->apiSave(safe(id)));
}

FAWWK_API const char* fawwk_show(const char* id) {
  NEED_STORE();
  GUARD(g_store->apiShow(safe(id)));
}
