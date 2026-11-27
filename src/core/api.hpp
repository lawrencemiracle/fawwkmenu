// api.hpp
#pragma once
#include "store.hpp"

namespace fw {
// the exports below talk to whatever store is set here
void setGlobalStore(Store* s);
}
