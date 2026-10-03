//
// PictureMask (core/model/picture_mask.h): the resume picture mask's arithmetic and compose size - pure, no fixture.
//
#include "doctest/doctest.h"

#include "core/model/picture_mask.h"

#include <cstdint>
#include <initializer_list>

TEST_CASE("PictureMask::multiplyAlpha: 255 leaves the alpha, 0 removes it, in between is the rounded product") {
    using PictureMask::multiplyAlpha;
    for (int a : {0, 1, 100, 254, 255}) {
        CHECK(multiplyAlpha(static_cast<uint8_t>(a), 255) == a);
        CHECK(multiplyAlpha(static_cast<uint8_t>(a), 0) == 0);
        CHECK(multiplyAlpha(255, static_cast<uint8_t>(a)) == a);
    }
    CHECK(multiplyAlpha(128, 128) == 64);
    CHECK(multiplyAlpha(255, 128) == 128);
    CHECK(multiplyAlpha(10, 10) == 0);
    CHECK(multiplyAlpha(200, 100) == 78); // 78.43
    // never above either factor
    for (int a = 0; a < 256; a += 5)
        for (int m = 0; m < 256; m += 5) {
            const int r = multiplyAlpha(static_cast<uint8_t>(a), static_cast<uint8_t>(m));
            CHECK(r <= a);
            CHECK(r <= m);
        }
}

TEST_CASE("PictureMask::composeSize: the window's logical size times ComposeScale") {
    const PictureMask::Size s = PictureMask::composeSize(68, 52);
    CHECK(s.w == 68 * PictureMask::ComposeScale);
    CHECK(s.h == 52 * PictureMask::ComposeScale);
    // the 2.7x slot copy is drawn from at least its own pixels
    CHECK(PictureMask::ComposeScale >= 3);
    CHECK(PictureMask::composeSize(0, 0).w == 0);
}
