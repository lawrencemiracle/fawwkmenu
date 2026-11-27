// demo.hpp
// a fake mod with one of every kind of setting, so the menu can be tried out
// before any real mod uses it.
#pragma once
#include "core/store.hpp"

namespace demo {

extern const char* ID;
void add(fw::Store& store);
void remove(fw::Store& store);
void tick(fw::Store& store, double seconds);  // moves the demo meter

}  // namespace demo
