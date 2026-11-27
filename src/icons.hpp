// icons.hpp
#pragma once
#include <string>
#include <vector>

// fills rgba with w * h * 4 bytes. on failure returns false and says why in err
bool decodeIcon(const std::string& path, std::vector<unsigned char>& rgba, int& w, int& h, std::string& err);
