#include "ableem/engine/image_pixels.h"

#include <fstream>
#include <iterator>

// stb_image (public domain, third_party/stb), the decoders this needs only; the file is read here, not by stdio
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

using namespace std;

namespace ableem {

namespace {

// the whole file, empty when it cannot be read
vector<unsigned char> readFileBytes(const string &path) {
    ifstream in(path, ifstream::binary);
    if (!in.is_open())
        return vector<unsigned char>();
    return vector<unsigned char>((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
}

} // namespace

bool readImageSize(const string &path, int &w, int &h) {
    const vector<unsigned char> bytes = readFileBytes(path);
    int width = 0;
    int height = 0;
    int n = 0;
    if (bytes.empty() || !stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &n))
        return false;
    w = width;
    h = height;
    return true;
}

bool readImagePixels(const string &path, ImagePixels &out) {
    const vector<unsigned char> bytes = readFileBytes(path);
    if (bytes.empty())
        return false;

    int w = 0;
    int h = 0;
    int n = 0;
    unsigned char *pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!pixels)
        return false;
    out.width = w;
    out.height = h;
    out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(pixels);
    return true;
}

} // namespace ableem
