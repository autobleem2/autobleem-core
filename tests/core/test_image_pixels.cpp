//
// readImagePixels (ableem/engine/image_pixels.h): a PNG / JPEG / BMP file as RGBA, no SDL.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"

#include <ableem/engine/image_pixels.h>

#include <fstream>
#include <sstream>
#include <string>

using std::string;

namespace {

string fixture(const string &name) {
    std::ifstream in(string(AB_TEST_DATA_DIR) + "/color-fixtures/" + name, std::ios::binary);
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

} // namespace

TEST_CASE("readImagePixels: a PNG comes out as 4 bytes a pixel, top row first") {
    TempDir tmp("image_pixels");
    tmp.writeFile("dark.png", fixture("dark.png"));

    ableem::ImagePixels pic;
    REQUIRE(ableem::readImagePixels(tmp.at("dark.png"), pic));
    CHECK(pic.width == 64);
    CHECK(pic.height == 36);
    REQUIRE(pic.rgba.size() == 64u * 36u * 4u);
    // the fixture's gradient starts at (14, 18, 36) on the top row and ends at (6, 8, 20) on the bottom one
    CHECK(int(pic.rgba[0]) == 14);
    CHECK(int(pic.rgba[1]) == 18);
    CHECK(int(pic.rgba[2]) == 36);
    CHECK(int(pic.rgba[3]) == 255);
    const size_t last = (35u * 64u + 0u) * 4u;
    CHECK(int(pic.rgba[last]) == 6);
    CHECK(int(pic.rgba[last + 1]) == 8);
    CHECK(int(pic.rgba[last + 2]) == 20);
    // the orange emblem (36..60, 6..26)
    const size_t emblem = (10u * 64u + 40u) * 4u;
    CHECK(int(pic.rgba[emblem]) == 226);
    CHECK(int(pic.rgba[emblem + 1]) == 112);
    CHECK(int(pic.rgba[emblem + 2]) == 30);
}

TEST_CASE("readImagePixels: a missing file, an empty one and a file that is not a picture are refused, untouched") {
    TempDir tmp("image_pixels");
    tmp.writeFile("empty.png", "");
    tmp.writeFile("text.png", "this is not a picture");
    tmp.writeFile("cut.png", fixture("dark.png").substr(0, 40)); // the header of a PNG, nothing after it

    ableem::ImagePixels pic;
    pic.width = 7;
    CHECK_FALSE(ableem::readImagePixels(tmp.at("missing.png"), pic));
    CHECK_FALSE(ableem::readImagePixels(tmp.at("empty.png"), pic));
    CHECK_FALSE(ableem::readImagePixels(tmp.at("text.png"), pic));
    CHECK_FALSE(ableem::readImagePixels(tmp.at("cut.png"), pic));
    CHECK(pic.width == 7);
    CHECK(pic.rgba.empty());
}

TEST_CASE("readImageSize: the size of a picture without decoding it; anything else is refused, untouched") {
    TempDir tmp("image_pixels");
    tmp.writeFile("dark.png", fixture("dark.png"));
    tmp.writeFile("strip.png", fixture("strip30x200.png"));
    tmp.writeFile("text.png", "this is not a picture");

    int w = 7;
    int h = 8;
    REQUIRE(ableem::readImageSize(tmp.at("dark.png"), w, h));
    CHECK(w == 64);
    CHECK(h == 36);
    REQUIRE(ableem::readImageSize(tmp.at("strip.png"), w, h));
    CHECK(w == 30);
    CHECK(h == 200);

    w = 7;
    h = 8;
    CHECK_FALSE(ableem::readImageSize(tmp.at("text.png"), w, h));
    CHECK_FALSE(ableem::readImageSize(tmp.at("missing.png"), w, h));
    CHECK(w == 7);
    CHECK(h == 8);
}
