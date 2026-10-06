//
// Platform::listableModes: which of a display's modes the launcher's Display row offers, and in what order.
//
#include "doctest/doctest.h"

#include <ableem/ui/platform.h>

#include <vector>

using ableem::DisplayMode;
using ableem::Platform;

namespace {
DisplayMode mode(int w, int h, int hz) {
    DisplayMode m;
    m.w = w;
    m.h = h;
    m.refreshRate = hz;
    return m;
}
} // namespace

TEST_CASE("Platform::listableModes: TV sizes first, then the others, each from the smallest") {
    const std::vector<DisplayMode> modes = Platform::listableModes(
        {mode(1920, 1080, 60), mode(1024, 768, 60), mode(1280, 720, 60), mode(3840, 2160, 60), mode(800, 600, 60),
         mode(1600, 900, 60), mode(1280, 1024, 60), mode(640, 480, 60)});
    REQUIRE(modes.size() == 8);
    const int expected[8][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {3840, 2160},
                                {640, 480},  {800, 600},  {1024, 768},  {1280, 1024}};
    for (size_t i = 0; i < modes.size(); i++) {
        CHECK(modes[i].w == expected[i][0]);
        CHECK(modes[i].h == expected[i][1]);
    }
}

TEST_CASE("Platform::listableModes: one per size, at the refresh rate nearest 60 Hz, never under 50 Hz") {
    const std::vector<DisplayMode> modes =
        Platform::listableModes({mode(3840, 2160, 30), mode(3840, 2160, 24), mode(1920, 1080, 50),
                                 mode(1920, 1080, 75), mode(1920, 1080, 59), mode(1280, 720, 0)});
    REQUIRE(modes.size() == 2); // 2160p only at 24/30 Hz: left out
    CHECK(modes[0].w == 1280);
    CHECK(modes[0].refreshRate == 0); // unknown counts as fine
    CHECK(modes[1].w == 1920);
    CHECK(modes[1].refreshRate == 59);
}

TEST_CASE("Platform::listableModes: of two rates as near 60 Hz, the higher") {
    const std::vector<DisplayMode> modes = Platform::listableModes({mode(1920, 1080, 55), mode(1920, 1080, 65)});
    REQUIRE(modes.size() == 1);
    CHECK(modes[0].refreshRate == 65);
    CHECK(Platform::listableModes({}).empty());
}

TEST_CASE("Platform::largestMode: the biggest listed size - what Auto picks - not the one running") {
    const std::vector<DisplayMode> modes = Platform::listableModes(
        {mode(800, 600, 60), mode(1920, 1080, 60), mode(1280, 720, 60), mode(1024, 768, 60), mode(640, 480, 60)});
    const ableem::Size best = Platform::largestMode(modes);
    CHECK(best.w == 1920);
    CHECK(best.h == 1080);
    // by area, not by the list's order (the VESA modes come after the TV ones)
    const ableem::Size vesa = Platform::largestMode(Platform::listableModes({mode(1280, 720, 60), mode(1600, 1200, 60)}));
    CHECK(vesa.w == 1600);
    CHECK(Platform::largestMode({}).w == 0);
}
