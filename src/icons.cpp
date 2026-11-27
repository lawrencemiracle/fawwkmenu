// icons.cpp
// reads a mod's icon file (png, jpg, bmp or tga) into plain RGBA pixels. turning those into a
// texture is the host's job, it owns the d3d device.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "icons.hpp"

#include <fstream>
#include <iterator>

#include "stb_image.h"

bool decodeIcon(const std::string& path, std::vector<unsigned char>& rgba, int& w, int& h, std::string& err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    err = "file not found";
    return false;
  }
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.empty() || bytes.size() > 2 * 1024 * 1024) {
    err = bytes.empty() ? "file is empty" : "file is bigger than 2 MB";
    return false;
  }
  int comp = 0;
  unsigned char* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
  if (!px) {
    err = "not a png, jpg, bmp or tga image";
    return false;
  }
  if (w > 512 || h > 512 || w < 1 || h < 1) {
    stbi_image_free(px);
    err = "image is larger than 512 x 512";
    return false;
  }
  rgba.assign(px, px + (size_t)w * h * 4);
  stbi_image_free(px);
  return true;
}
