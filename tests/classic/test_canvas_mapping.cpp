//
// The Renderer's canvas math (canvas.h): wide outputs exactly as before, a 4:3 output through a frame target stretched
// to the output's pixel aspect, and the cover crop of a frame of the other canvas.
//
#include "doctest/doctest.h"

#include <ableem/ui/canvas.h>

using ableem::CanvasMapping;
using ableem::coverCrop;
using ableem::isFourByThreeOutput;
using ableem::mapCanvas;
using ableem::Rect;

namespace {
void checkRect(const Rect &r, int x, int y, int w, int h) {
    CHECK(r.x == x);
    CHECK(r.y == y);
    CHECK(r.w == w);
    CHECK(r.h == h);
}
} // namespace

TEST_CASE("isFourByThreeOutput: up to 1.5 wide is 4:3 (480p, 576p, 4:3 monitors); 16:10 and 16:9 are wide") {
    CHECK(isFourByThreeOutput(720, 480));
    CHECK(isFourByThreeOutput(720, 576));
    CHECK(isFourByThreeOutput(640, 480));
    CHECK(isFourByThreeOutput(1024, 768));
    CHECK_FALSE(isFourByThreeOutput(1280, 720));
    CHECK_FALSE(isFourByThreeOutput(1920, 1080));
    CHECK_FALSE(isFourByThreeOutput(1280, 800));
    CHECK_FALSE(isFourByThreeOutput(0, 480));
    CHECK_FALSE(isFourByThreeOutput(720, 0));
}

TEST_CASE("usesFrameTarget: only a wide canvas on a 4:3 output; a canvas of the output's shape is drawn straight") {
    CHECK(ableem::usesFrameTarget(720, 480, 1280, 720));
    CHECK(ableem::usesFrameTarget(1024, 768, 1280, 720));
    CHECK_FALSE(ableem::usesFrameTarget(1280, 720, 1280, 720));
    CHECK_FALSE(ableem::usesFrameTarget(1920, 1080, 1280, 720));
    CHECK_FALSE(ableem::usesFrameTarget(320, 240, 320, 240)); // a test's own window
    CHECK_FALSE(ableem::usesFrameTarget(720, 480, 640, 480));
    CHECK_FALSE(ableem::usesFrameTarget(720, 480, 0, 0));
    // what the straight path gives such a canvas: the old fit, scale 1
    const CanvasMapping m = ableem::fitCanvas(320, 240, 320, 240);
    CHECK(m.scale == 1.0f);
    checkRect(m.display, 0, 0, 320, 240);
}

TEST_CASE("mapCanvas: a wide output is the old fit-and-centre, square pixels, no frame target") {
    CanvasMapping m = mapCanvas(1280, 720, 1280, 720);
    CHECK_FALSE(m.fourByThree);
    CHECK(m.scale == 1.0f);
    checkRect(m.display, 0, 0, 1280, 720);
    m = mapCanvas(1920, 1080, 1280, 720);
    CHECK(m.scale == doctest::Approx(1.5));
    checkRect(m.display, 0, 0, 1920, 1080);
    CHECK(m.scaleX == m.scaleY);
    // a desktop of another shape: bars, as before (a 16:10 laptop)
    m = mapCanvas(1280, 800, 1280, 720);
    CHECK(m.scale == 1.0f);
    checkRect(m.display, 0, 40, 1280, 720);
    CHECK(m.frameW == 0);
}

TEST_CASE("mapCanvas: 720x480 shows the 640x480 canvas full screen, stretched 1.125x (pixel aspect 8:9)") {
    const CanvasMapping m = mapCanvas(720, 480, 640, 480);
    CHECK(m.fourByThree);
    CHECK(m.scale == 1.0f); // square frame pixels: text and covers drawn 1:1, then stretched once
    CHECK(m.frameW == 640);
    CHECK(m.frameH == 480);
    checkRect(m.display, 0, 0, 720, 480);
    CHECK(m.scaleX == doctest::Approx(1.125));
    CHECK(m.scaleY == doctest::Approx(1.0));
}

TEST_CASE("mapCanvas: on a 4:3 output the 16:9 canvas is letterboxed at its own shape, not 11 % squeezed") {
    const CanvasMapping m = mapCanvas(720, 480, 1280, 720);
    CHECK(m.fourByThree);
    CHECK(m.scale == 1.0f); // the same scale as the 4:3 canvas: fonts and targets fit both
    CHECK(m.frameW == 1280);
    CHECK(m.frameH == 720);
    checkRect(m.display, 0, 60, 720, 360); // a 16:9 picture on a 4:3 screen: three quarters of its height
    CHECK(m.scaleX == doctest::Approx(0.5625));
    CHECK(m.scaleY == doctest::Approx(0.5));
}

TEST_CASE("mapCanvas: 576p (PAL) and a square-pixel 4:3 monitor") {
    CanvasMapping m = mapCanvas(720, 576, 640, 480);
    CHECK(m.scale == doctest::Approx(1.2));
    CHECK(m.frameW == 768);
    CHECK(m.frameH == 576);
    checkRect(m.display, 0, 0, 720, 576);
    CHECK(m.scaleY / m.scaleX == doctest::Approx(16.0 / 15.0)); // the pixel aspect 16:15
    m = mapCanvas(1024, 768, 640, 480);
    checkRect(m.display, 0, 0, 1024, 768);
    CHECK(m.scaleX == doctest::Approx(m.scaleY)); // square pixels: no stretch
    m = mapCanvas(1024, 768, 1280, 720);
    checkRect(m.display, 0, 96, 1024, 576); // what the old fit gave a 4:3 monitor, now through the frame target
}

TEST_CASE("mapCanvas: no canvas gives the default mapping") {
    const CanvasMapping m = mapCanvas(720, 480, 0, 480);
    CHECK_FALSE(m.fourByThree);
    CHECK(m.scale == 1.0f);
    checkRect(m.display, 0, 0, 0, 0);
}

TEST_CASE("coverCrop: the same shape is the whole picture; another is its middle at the canvas's shape") {
    checkRect(coverCrop(640, 480, 640, 480), 0, 0, 640, 480);
    checkRect(coverCrop(1280, 720, 1280, 720), 0, 0, 1280, 720);
    checkRect(coverCrop(641, 480, 640, 480), 0, 0, 641, 480); // within 1 %
    // the 4:3 launcher's snapshot under a 16:9 menu: the middle band, full width
    checkRect(coverCrop(640, 480, 1280, 720), 0, 60, 640, 360);
    // a 16:9 frame under a 4:3 one: the middle, full height
    checkRect(coverCrop(1280, 720, 640, 480), 160, 0, 960, 720);
    checkRect(coverCrop(0, 480, 640, 480), 0, 0, 0, 480);
}

TEST_CASE("mapCanvas: the CRT safe area insets the 4:3 canvas by the same share on both axes (pixel aspect kept)") {
    // 0 % is the full screen, as above
    checkRect(mapCanvas(720, 480, 640, 480, 0).display, 0, 0, 720, 480);
    // 5 %: 36 px of the width and 24 of the height on each side
    CanvasMapping m = mapCanvas(720, 480, 640, 480, 5);
    CHECK(m.fourByThree);
    checkRect(m.display, 36, 24, 648, 432);
    CHECK(m.scale == 1.0f); // the frame is still drawn 1:1; only its placement shrinks
    CHECK(m.scaleX / m.scaleY == doctest::Approx(1.125)); // 8:9 pixel aspect unchanged
    // 10 %
    checkRect(mapCanvas(720, 480, 640, 480, 10).display, 72, 48, 576, 384);
    // 8 % rounds to whole pixels: 58 and 38
    checkRect(mapCanvas(720, 480, 640, 480, 8).display, 58, 38, 604, 404);
    // the 16:9 canvas is letterboxed inside the area
    checkRect(mapCanvas(720, 480, 1280, 720, 5).display, 36, 78, 648, 324);
    // a wide output has no margin
    checkRect(mapCanvas(1280, 720, 1280, 720, 10).display, 0, 0, 1280, 720);
    // out of range is clamped
    CHECK(ableem::clampSafeMargin(-3) == 0);
    CHECK(ableem::clampSafeMargin(99) == ableem::MaxSafeMargin);
    CHECK(ableem::clampSafeMargin(8) == 8);
}
