//
// Image::pixel (texture.h): one pixel of a decoded picture, read back from a small hand-built PNG - inside
// the image (opaque and semi-transparent), a fully transparent pixel, and outside the image altogether.
// No SDL_Init/window needed - IMG_Load decodes straight from the file.
//
#include "doctest/doctest.h"
#include "support/temp_dir.h"

#include "ableem/ui/texture.h"

#include <ableem/engine/crc32.h>

#include <cstdint>
#include <string>

using ableem::Color;
using ableem::Crc32;
using ableem::Image;
using std::string;

namespace {

void put32be(string &s, uint32_t v) {
    s += static_cast<char>((v >> 24) & 0xFF);
    s += static_cast<char>((v >> 16) & 0xFF);
    s += static_cast<char>((v >> 8) & 0xFF);
    s += static_cast<char>(v & 0xFF);
}

void put16le(string &s, uint16_t v) {
    s += static_cast<char>(v & 0xFF);
    s += static_cast<char>((v >> 8) & 0xFF);
}

// a PNG chunk: 4-byte length, 4-byte type, data, 4-byte CRC-32 over type+data (the zip/PNG polynomial -
// the same one ableem::Crc32 computes)
void putChunk(string &png, const char *type, const string &data) {
    put32be(png, static_cast<uint32_t>(data.size()));
    string typeAndData = string(type, 4) + data;
    png += typeAndData;
    put32be(png, Crc32::ofBytes(typeAndData));
}

uint32_t adler32(const string &data) {
    uint32_t a = 1, b = 0;
    for (unsigned char c : data) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

// a 2x2 8-bit RGBA PNG, one uncompressed ("stored") deflate block - no zlib library needed to write it
string buildPng2x2(Color topLeft, Color topRight, Color bottomLeft, Color bottomRight) {
    string raw;
    raw += '\0'; // row 0 filter: None
    raw += static_cast<char>(topLeft.r);
    raw += static_cast<char>(topLeft.g);
    raw += static_cast<char>(topLeft.b);
    raw += static_cast<char>(topLeft.a);
    raw += static_cast<char>(topRight.r);
    raw += static_cast<char>(topRight.g);
    raw += static_cast<char>(topRight.b);
    raw += static_cast<char>(topRight.a);
    raw += '\0'; // row 1 filter: None
    raw += static_cast<char>(bottomLeft.r);
    raw += static_cast<char>(bottomLeft.g);
    raw += static_cast<char>(bottomLeft.b);
    raw += static_cast<char>(bottomLeft.a);
    raw += static_cast<char>(bottomRight.r);
    raw += static_cast<char>(bottomRight.g);
    raw += static_cast<char>(bottomRight.b);
    raw += static_cast<char>(bottomRight.a);

    string zlibStream;
    zlibStream += static_cast<char>(0x78);
    zlibStream += static_cast<char>(0x01);
    zlibStream += static_cast<char>(0x01); // BFINAL=1, BTYPE=00 (stored), byte-aligned
    put16le(zlibStream, static_cast<uint16_t>(raw.size()));
    put16le(zlibStream, static_cast<uint16_t>(~static_cast<uint16_t>(raw.size())));
    zlibStream += raw;
    put32be(zlibStream, adler32(raw));

    string png;
    const char sig[8] = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n'};
    png.append(sig, 8);

    string ihdr;
    put32be(ihdr, 2);             // width
    put32be(ihdr, 2);             // height
    ihdr += static_cast<char>(8); // bit depth
    ihdr += static_cast<char>(6); // color type: truecolor + alpha
    ihdr += static_cast<char>(0); // compression
    ihdr += static_cast<char>(0); // filter
    ihdr += static_cast<char>(0); // interlace
    putChunk(png, "IHDR", ihdr);
    putChunk(png, "IDAT", zlibStream);
    putChunk(png, "IEND", "");
    return png;
}

} // namespace

TEST_CASE("Image::pixel reads back a decoded PNG's corners, and transparent black outside it") {
    TempDir tmp("image_pixel");
    const Color red(255, 0, 0, 255);
    const Color halfBlue(0, 0, 255, 128);
    const Color transparent(0, 0, 0, 0);
    const Color green(0, 255, 0, 255);

    tmp.writeFile("t.png", buildPng2x2(red, halfBlue, transparent, green));

    Image image = Image::loadFile(tmp.at("t.png"));
    REQUIRE(image.valid());
    CHECK(image.size().w == 2);
    CHECK(image.size().h == 2);

    // opaque corner
    Color p = image.pixel(0, 0);
    CHECK(p.r == 255);
    CHECK(p.g == 0);
    CHECK(p.b == 0);
    CHECK(p.a == 255);

    // semi-transparent corner - alpha survives the round trip
    p = image.pixel(1, 0);
    CHECK(p.b == 255);
    CHECK(p.a == 128);

    // fully transparent pixel
    p = image.pixel(0, 1);
    CHECK(p.a == 0);

    p = image.pixel(1, 1);
    CHECK(p.g == 255);
    CHECK(p.a == 255);

    // outside the image: transparent black, not a crash
    p = image.pixel(5, 5);
    CHECK(p.r == 0);
    CHECK(p.g == 0);
    CHECK(p.b == 0);
    CHECK(p.a == 0);
    p = image.pixel(-1, 0);
    CHECK(p.a == 0);
}

TEST_CASE("Image::loadFile: an empty path or a missing file is invalid, not a crash") {
    CHECK_FALSE(Image::loadFile("").valid());
    CHECK_FALSE(Image::loadFile("/no/such/file/anywhere.png").valid());
}
