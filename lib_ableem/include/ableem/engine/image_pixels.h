// lib_ableem - engine: a PNG / JPEG / BMP file as plain RGBA pixels, with no SDL and no renderer. What the theme
// converter reads a 1.0 theme's background with (to derive its colours); the UI loads its textures through SDL.
#pragma once

#include <string>
#include <vector>

namespace ableem {

//******************
// ImagePixels
//******************
struct ImagePixels {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba; // width * height * 4, rows packed, top row first
};

// Decodes the file at `path` (PNG, JPEG or BMP; a grey or palette picture comes out as RGBA too). False (`out`
// untouched) when the file cannot be read or is not a picture of those kinds. Never throws.
bool readImagePixels(const std::string &path, ImagePixels &out);

// The size of the picture in the file at `path` without decoding it (the same kinds of file). False (w and h
// untouched) when it cannot be read or is not a picture of those kinds. Never throws.
bool readImageSize(const std::string &path, int &w, int &h);

} // namespace ableem
