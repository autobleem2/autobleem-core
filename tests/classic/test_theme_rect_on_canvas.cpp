//
// TextRenderer::onCanvas: the theme's classic rects (made for 1280x720) on another canvas - the classic screens'
// 800x600 on a 4:3 (CRT) output. 1280x720 is the identity, so the wide outputs draw as they always did.
//
#include "doctest/doctest.h"

#include "gui/text_renderer.h"

using ableem::Rect;

TEST_CASE("onCanvas leaves a rect alone on the 1280x720 canvas") {
    const Rect panel(30, 10, 1220, 615);
    const Rect same = TextRenderer::onCanvas(panel, 1280, 720);
    CHECK(same.x == 30);
    CHECK(same.y == 10);
    CHECK(same.w == 1220);
    CHECK(same.h == 615);
}

TEST_CASE("onCanvas scales each axis to the 800x600 canvas, the panel stays inside it") {
    const Rect panel(30, 10, 1220, 615);
    const Rect fitted = TextRenderer::onCanvas(panel, 800, 600);
    CHECK(fitted.x == 19);             // 30 * 0.625
    CHECK(fitted.y == 8);              // 10 * 0.8333
    CHECK(fitted.x + fitted.w == 781); // 1250 * 0.625
    CHECK(fitted.w > 0);
    CHECK(fitted.x + fitted.w <= 800);
    CHECK(fitted.y + fitted.h <= 600);
}

TEST_CASE("onCanvas keeps the width of a rect that touches both edges") {
    const Rect full(0, 0, 1280, 720);
    const Rect fitted = TextRenderer::onCanvas(full, 640, 480);
    CHECK(fitted.x == 0);
    CHECK(fitted.y == 0);
    CHECK(fitted.w == 640);
    CHECK(fitted.h == 480);
}
